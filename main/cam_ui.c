#include "cam_ui.h"
#include "camera.h"
#include "storage.h"
#include "ui.h"
#include "bridge.h"
#include "settings.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "cam_ui";

static SemaphoreHandle_t s_cam_lock;   /* serialises camera use: preview task / shoot / remote snap */
static bool s_peek;
static volatile bool s_preview_run;
static TaskHandle_t s_preview_task;

/* gallery */
static char (*s_names)[STORAGE_NAME_LEN];
static int s_count, s_index;
static uint16_t *s_shown;              /* decoded photo currently displayed */

void camui_init(void)
{
    s_cam_lock = xSemaphoreCreateMutex();
    char buf[8];
    if (settings_get_str("peek", buf, sizeof buf)) s_peek = strcmp(buf, "on") == 0;
    ui_set_peek_icon(s_peek);
}

/* ---- live view ------------------------------------------------------------- */

static void preview_task(void *arg)
{
    while (s_preview_run) {
        if (xSemaphoreTake(s_cam_lock, pdMS_TO_TICKS(200)) == pdTRUE) {
            const uint16_t *f = camera_ready() ? camera_preview() : NULL;
            xSemaphoreGive(s_cam_lock);
            if (f && ui_get_screen() == UI_SCREEN_CAMERA) ui_set_frame(f, CAM_PREVIEW_W, CAM_PREVIEW_H);
        }
        vTaskDelay(pdMS_TO_TICKS(40));
    }
    s_preview_task = NULL;
    vTaskDelete(NULL);
}

static void preview_start(void)
{
    if (s_preview_task) return;
    s_preview_run = true;
    xTaskCreatePinnedToCore(preview_task, "cam_preview", 6144, NULL, 2, &s_preview_task, 0);
}

static void preview_stop(void)
{
    s_preview_run = false;
    while (s_preview_task) vTaskDelay(pdMS_TO_TICKS(20));
}

static bool camera_on(void)
{
    xSemaphoreTake(s_cam_lock, portMAX_DELAY);
    esp_err_t err = camera_init();
    xSemaphoreGive(s_cam_lock);
    if (err != ESP_OK) {
        ui_toast("相机没起来，看串口日志", 2500);
        return false;
    }
    return true;
}

void camui_enter(void)
{
    storage_init();
    ui_set_screen(UI_SCREEN_CAMERA);
    if (!camera_on()) { ui_set_screen(UI_SCREEN_CHAT); return; }
    ui_set_cam_text(storage_dir() ? (storage_is_sd() ? "SD 卡" : "没插卡，存内部") : "没地方存照片");
    preview_start();
}

void camui_leave(void)
{
    preview_stop();
    xSemaphoreTake(s_cam_lock, portMAX_DELAY);
    camera_deinit();
    xSemaphoreGive(s_cam_lock);
    free(s_shown);
    s_shown = NULL;
    ui_set_screen(UI_SCREEN_CHAT);
}

void camui_shoot(void)
{
    if (!storage_dir()) { ui_set_cam_text("没地方存照片，请插卡"); return; }
    ui_set_cam_text("拍照中...");
    uint8_t *jpeg = NULL;
    size_t len = 0;
    xSemaphoreTake(s_cam_lock, portMAX_DELAY);
    esp_err_t err = camera_capture_jpeg(&jpeg, &len);
    xSemaphoreGive(s_cam_lock);
    if (err != ESP_OK) { ui_set_cam_text("拍照失败"); return; }
    char name[STORAGE_NAME_LEN];
    err = storage_save_jpeg(jpeg, len, name, sizeof name);
    free(jpeg);
    char msg[64];
    if (err == ESP_ERR_NO_MEM) snprintf(msg, sizeof msg, "内部只能存 %d 张，请插卡", STORAGE_FLASH_MAX);
    else if (err != ESP_OK) snprintf(msg, sizeof msg, "保存失败");
    else snprintf(msg, sizeof msg, "已保存 %s", name);
    ui_set_cam_text(msg);
}

/* ---- gallery -------------------------------------------------------------------- */

static void gallery_show(void)
{
    free(s_shown);
    s_shown = NULL;
    ui_set_frame(NULL, 0, 0);
    if (s_count == 0) {
        ui_set_gallery_pos(0, 0);
        ui_set_cam_text("");
        return;
    }
    if (s_index < 0) s_index = 0;
    if (s_index >= s_count) s_index = s_count - 1;
    ui_set_gallery_pos(s_index, s_count);
    ui_set_cam_text(s_names[s_index]);
    uint8_t *jpeg = NULL;
    size_t len = 0;
    if (storage_read(s_names[s_index], &jpeg, &len) != ESP_OK) { ui_set_cam_text("读不出来"); return; }
    int w, h;
    esp_err_t err = camera_decode_to_fit(jpeg, len, 480, 320 - 44, &s_shown, &w, &h);
    free(jpeg);
    if (err != ESP_OK) { ui_set_cam_text("解码失败"); return; }
    ui_set_frame(s_shown, w, h);
}

void camui_gallery_enter(void)
{
    preview_stop();
    storage_init();
    if (!s_names) s_names = heap_caps_malloc(STORAGE_MAX_FILES * STORAGE_NAME_LEN, MALLOC_CAP_SPIRAM);
    s_count = s_names ? storage_list(s_names) : 0;
    s_index = s_count - 1;
    ui_set_screen(UI_SCREEN_GALLERY);
    gallery_show();
}

void camui_gallery_step(int dir)
{
    if (s_count == 0) return;
    s_index += dir;
    if (s_index < 0) s_index = 0;
    if (s_index >= s_count) s_index = s_count - 1;
    gallery_show();
}

void camui_gallery_delete(void)
{
    if (s_count == 0) return;
    if (storage_delete(s_names[s_index]) == ESP_OK) {
        memmove(s_names[s_index], s_names[s_index + 1], (size_t)(s_count - s_index - 1) * STORAGE_NAME_LEN);
        s_count--;
        if (s_index >= s_count) s_index = s_count - 1;
        ui_toast("删掉了", 1000);
    } else {
        ui_toast("删除失败", 1500);
    }
    gallery_show();
}

void camui_gallery_send(void)
{
    if (s_count == 0) return;
    uint8_t *jpeg = NULL;
    size_t len = 0;
    if (storage_read(s_names[s_index], &jpeg, &len) != ESP_OK) { ui_toast("读不出来", 1500); return; }
    bridge_send_photo(jpeg, len);
    free(jpeg);
    ui_toast("寄给克...", 1500);
}

/* ---- remote snapshot -------------------------------------------------------------- */

bool camui_peek(void) { return s_peek; }

void camui_set_peek(bool on)
{
    s_peek = on;
    settings_set_str("peek", on ? "on" : "off");
    ui_set_peek_icon(on);
    ESP_LOGI(TAG, "peek %s", on ? "on" : "off");
}

esp_err_t camui_remote_snap(uint8_t **jpeg, size_t *len)
{
    if (!s_peek) return ESP_ERR_NOT_ALLOWED;
    xSemaphoreTake(s_cam_lock, portMAX_DELAY);
    bool was_on = camera_ready();
    esp_err_t err = was_on ? ESP_OK : camera_init();
    if (err == ESP_OK) err = camera_capture_jpeg(jpeg, len);
    if (!was_on) camera_deinit();
    xSemaphoreGive(s_cam_lock);
    return err;
}

void camui_print_photos(void)
{
    storage_init();
    printf("storage: %s (%s)\n", storage_dir() ? storage_dir() : "none", storage_is_sd() ? "SD card" : "flash");
    char (*names)[STORAGE_NAME_LEN] = heap_caps_malloc(STORAGE_MAX_FILES * STORAGE_NAME_LEN, MALLOC_CAP_SPIRAM);
    if (!names) return;
    int n = storage_list(names);
    for (int i = 0; i < n; i++) printf("  %s\n", names[i]);
    printf("%d photo(s)\n", n);
    free(names);
}
