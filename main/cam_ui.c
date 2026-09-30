#include "cam_ui.h"
#include "camera.h"
#include "storage.h"
#include "ui.h"
#include "bridge.h"
#include "settings.h"
#include "gfx.h"

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
static bool s_review;                  /* gallery screen shows the photo just taken */

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
            int w = 0, h = 0;
            const uint16_t *f = camera_ready() ? camera_preview(ui_get_rotation(), &w, &h) : NULL;
            xSemaphoreGive(s_cam_lock);
            if (f && ui_get_screen() == UI_SCREEN_CAMERA) ui_set_frame(f, w, h);
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
    s_review = false;
    ui_set_screen(UI_SCREEN_CHAT);
}

void camui_shoot(void)
{
    if (!storage_dir()) { ui_set_cam_text("没地方存照片，请插卡"); return; }
    ui_set_cam_text("拍照中...");
    vTaskDelay(pdMS_TO_TICKS(150));                 /* let the text reach the screen before it freezes */
    uint8_t *jpeg = NULL;
    size_t len = 0;
    preview_stop();                                 /* nothing else may touch the camera while it grabs a photo */
    xSemaphoreTake(s_cam_lock, portMAX_DELAY);
    ui_display_hold(true);                          /* no LCD refresh while the camera DMA writes into PSRAM */
    esp_err_t err = camera_capture_jpeg(ui_get_rotation(), &jpeg, &len);
    ui_display_hold(false);
    xSemaphoreGive(s_cam_lock);
    if (err != ESP_OK) { ui_set_cam_text("拍照失败"); preview_start(); return; }
    char name[STORAGE_NAME_LEN];
    err = storage_save_jpeg(jpeg, len, name, sizeof name);
    free(jpeg);
    if (err != ESP_OK) {
        ui_set_cam_text(err == ESP_ERR_NO_MEM ? "内部存满了，请插卡" : "保存失败");
        preview_start();
        return;
    }
    camui_review(name);
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
    esp_err_t err = camera_decode_to_fit(jpeg, len, gfx_width(), gfx_height() - 44, &s_shown, &w, &h);
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
    s_review = false;
    ui_set_screen(UI_SCREEN_GALLERY);
    gallery_show();
}

/* the photo just taken, shown before deciding: 重拍 (delete + live view) / 寄给克 / 保留 (live view) */
void camui_review(const char *name)
{
    (void)name;                                     /* it is the newest file */
    camui_gallery_enter();
    s_review = true;
    ui_set_review(true);
}

static void back_to_live(void)
{
    s_review = false;
    free(s_shown);
    s_shown = NULL;
    ui_set_screen(UI_SCREEN_CAMERA);
    ui_set_cam_text(storage_dir() ? (storage_is_sd() ? "SD 卡" : "没插卡，存内部") : "没地方存照片");
    preview_start();
}

/* 返回 / 保留 */
void camui_back(void)
{
    if (s_review) back_to_live();
    else camui_leave();
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
    bool retake = s_review;
    if (storage_delete(s_names[s_index]) == ESP_OK) {
        memmove(s_names[s_index], s_names[s_index + 1], (size_t)(s_count - s_index - 1) * STORAGE_NAME_LEN);
        s_count--;
        if (s_index >= s_count) s_index = s_count - 1;
        ui_toast("删掉了", 1000);
    } else {
        ui_toast("删除失败", 1500);
    }
    if (retake) { back_to_live(); return; }
    gallery_show();
}

void camui_gallery_send(void)
{
    if (s_count == 0) return;
    uint8_t *jpeg = NULL;
    size_t len = 0;
    if (ui_is_sending()) return;                    /* one photo, one send: no double taps */
    if (storage_read(s_names[s_index], &jpeg, &len) != ESP_OK) { ui_toast("读不出来", 1500); return; }
    bridge_send_photo(jpeg, len);
    free(jpeg);
    ui_toast("寄给克...", 1500);
}

/* `cam sweep`: photos and frame-loss numbers for several XCLK / JPEG-quality settings, one after the other, so the
 * stripes can be compared on one screen of the PC. Keep the board still, pointed at something bright. */
void camui_sweep(void)
{
    if (storage_init() != ESP_OK || !storage_dir()) { printf("sweep: no storage for the photos\n"); return; }
    if (ui_get_screen() == UI_SCREEN_CAMERA) { printf("sweep: leave the camera screen first (cam off)\n"); return; }
    int old_x = camera_get_xclk(), old_q = camera_get_quality();
    static const int xs[] = { 6, 8, 10 }, qs[] = { 10, 20 };
    printf("sweep: %s files sweep-x<MHz>-q<quality>.jpg in DCIM\n", storage_is_sd() ? "SD card" : "flash (max 12 photos: delete some first)");
    printf("  xclk quality | good bad timeout | NO-SOI NO-EOI | ms/frame | KB/frame\n");
    xSemaphoreTake(s_cam_lock, portMAX_DELAY);
    for (unsigned xi = 0; xi < sizeof xs / sizeof xs[0]; xi++) {
        for (unsigned qi = 0; qi < sizeof qs / sizeof qs[0]; qi++) {
            camera_deinit();
            camera_set_xclk(xs[xi]);          /* saved, but nothing is running yet */
            camera_set_quality(qs[qi]);
            if (camera_init() != ESP_OK) { printf("  %4d %7d | camera init failed\n", xs[xi], qs[qi]); continue; }
            camera_probe_t pr;
            camera_probe_frames(10, &pr);
            printf("  %4d %7d | %4d %3d %7d | %6d %6d | %8d | %8u\n", xs[xi], qs[qi], pr.good, pr.bad, pr.timeouts,
                   pr.no_soi, pr.no_eoi, pr.ms_per_frame, (unsigned)(pr.avg_bytes / 1024));
            uint8_t *jpeg = NULL;
            size_t len = 0;
            if (camera_capture_jpeg(ui_get_rotation(), &jpeg, &len) == ESP_OK) {
                char name[STORAGE_NAME_LEN];
                snprintf(name, sizeof name, "sweep-x%d-q%d.jpg", xs[xi], qs[qi]);
                esp_err_t err = storage_save_named(name, jpeg, len);
                if (err != ESP_OK) printf("       (could not save %s: %s)\n", name, esp_err_to_name(err));
                free(jpeg);
            }
        }
    }
    camera_deinit();
    camera_set_xclk(old_x);
    camera_set_quality(old_q);
    xSemaphoreGive(s_cam_lock);
    printf("sweep done, settings restored (xclk %d, quality %d). Compare the sweep-*.jpg files: fewest stripes wins, then `cam xclk N`.\n", old_x, old_q);
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
    if (err == ESP_OK) err = camera_capture_jpeg(ui_get_rotation(), jpeg, len);
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
