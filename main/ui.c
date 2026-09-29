#include "ui.h"
#include "ui_render.h"
#include "gfx.h"
#include "board.h"
#include "settings.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "ui";

#define BLUSH_FACE "(—//—)"
#define BLUSH_MS   2000
#define KEY_ROTATE "rotate"
#define KEY_THEME  "theme"
#define DEFAULT_ROTATION 90
#define DEFAULT_THEME    UI_THEME_DARK

static SemaphoreHandle_t s_lock;      /* protects the state below */
static SemaphoreHandle_t s_fb_lock;   /* protects framebuffer + gfx dimensions */
static SemaphoreHandle_t s_dirty;
static ui_state_t s_state;                 /* what is drawn */
static char s_base_face[UI_FACE_BUF];      /* set by /face */
static char s_override_face[UI_FACE_BUF];  /* blush / recording / playing; "" = none */
static char s_base_corner[48];
static char s_override_corner[48];
static esp_timer_handle_t s_blush_timer;
static uint16_t *s_fb;
static int s_rotation = DEFAULT_ROTATION;

/* copy at most max_chars code points of valid UTF-8 into dst */
static void copy_limited(char *dst, size_t dst_size, const char *src, int max_chars)
{
    size_t out = 0;
    int n = 0;
    const char *p = src;
    while (*p && n < max_chars) {
        const char *q = p;
        uint32_t cp = gfx_utf8_next(&q);
        size_t len = (size_t)(q - p);
        if (cp == 0xFFFD && len == 1) { p = q; continue; }     /* skip invalid bytes */
        if (cp == '\r') { p = q; continue; }
        if (out + len >= dst_size) break;
        memcpy(dst + out, p, len);
        out += len;
        n++;
        p = q;
    }
    dst[out] = 0;
}

/* recompute the drawn face/corner from base + override; call with lock held */
static void recompute(void)
{
    strcpy(s_state.face, s_override_face[0] ? s_override_face : s_base_face);
    strcpy(s_state.corner, s_override_corner[0] ? s_override_corner : s_base_corner);
}

static void mark_dirty(void)
{
    xSemaphoreGive(s_dirty);
}

static void render_task(void *arg)
{
    ui_state_t snap;
    for (;;) {
        xSemaphoreTake(s_dirty, portMAX_DELAY);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        snap = s_state;
        xSemaphoreGive(s_lock);
        xSemaphoreTake(s_fb_lock, portMAX_DELAY);
        ui_render(&snap);
        board_lcd_flush(s_fb);
        xSemaphoreGive(s_fb_lock);
    }
}

static void blush_timeout(void *arg)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (strcmp(s_override_face, BLUSH_FACE) == 0) {
        s_override_face[0] = 0;
        recompute();
    }
    xSemaphoreGive(s_lock);
    mark_dirty();
}

static esp_err_t apply_rotation(int rotation)
{
    xSemaphoreTake(s_fb_lock, portMAX_DELAY);
    esp_err_t err = board_lcd_set_rotation(rotation);
    if (err == ESP_OK) {
        s_rotation = rotation;
        gfx_init(s_fb, board_lcd_width(), board_lcd_height());
    }
    xSemaphoreGive(s_fb_lock);
    return err;
}

void ui_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_fb_lock = xSemaphoreCreateMutex();
    s_dirty = xSemaphoreCreateBinary();
    s_fb = heap_caps_malloc(BOARD_LCD_W * BOARD_LCD_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!s_fb) {
        ESP_LOGW(TAG, "no PSRAM framebuffer, falling back to internal RAM");
        s_fb = heap_caps_malloc(BOARD_LCD_W * BOARD_LCD_H * sizeof(uint16_t), MALLOC_CAP_DEFAULT);
    }
    assert(s_fb);

    /* rotation + theme from NVS */
    char buf[16];
    int rot = DEFAULT_ROTATION;
    if (settings_get_str(KEY_ROTATE, buf, sizeof buf)) {
        int v = atoi(buf);
        if (v == 0 || v == 90 || v == 180 || v == 270) rot = v;
    }
    s_state.theme = DEFAULT_THEME;
    if (settings_get_str(KEY_THEME, buf, sizeof buf)) {
        if (strcmp(buf, "light") == 0) s_state.theme = UI_THEME_LIGHT;
        else if (strcmp(buf, "dark") == 0) s_state.theme = UI_THEME_DARK;
    }
    if (apply_rotation(rot) != ESP_OK) apply_rotation(0);
    ESP_LOGI(TAG, "rotation %d, theme %s", s_rotation, ui_get_theme());

    strcpy(s_base_face, "(—_—)");
    recompute();

    const esp_timer_create_args_t targs = { .callback = blush_timeout, .name = "blush" };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_blush_timer));

    xTaskCreatePinnedToCore(render_task, "ui_render", 6144, NULL, 5, NULL, 1);
    mark_dirty();
}

void ui_set_face(const char *utf8)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    copy_limited(s_base_face, sizeof(s_base_face), utf8, UI_FACE_MAX_CHARS);
    if (!s_base_face[0]) strcpy(s_base_face, "(—_—)");
    recompute();
    xSemaphoreGive(s_lock);
    mark_dirty();
}

void ui_set_say(const char *utf8)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    copy_limited(s_state.say, sizeof(s_state.say), utf8, UI_SAY_MAX_CHARS);
    xSemaphoreGive(s_lock);
    mark_dirty();
}

void ui_set_corner(const char *utf8)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    copy_limited(s_base_corner, sizeof(s_base_corner), utf8, 40);
    recompute();
    xSemaphoreGive(s_lock);
    mark_dirty();
}

void ui_override_face(const char *utf8)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (utf8) copy_limited(s_override_face, sizeof(s_override_face), utf8, UI_FACE_MAX_CHARS);
    else s_override_face[0] = 0;
    recompute();
    xSemaphoreGive(s_lock);
    esp_timer_stop(s_blush_timer);
    mark_dirty();
}

void ui_override_corner(const char *utf8)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (utf8) copy_limited(s_override_corner, sizeof(s_override_corner), utf8, 40);
    else s_override_corner[0] = 0;
    recompute();
    xSemaphoreGive(s_lock);
    mark_dirty();
}

void ui_blush(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    /* don't interrupt a recording/playing face */
    if (s_override_face[0] && strcmp(s_override_face, BLUSH_FACE) != 0) {
        xSemaphoreGive(s_lock);
        return;
    }
    strcpy(s_override_face, BLUSH_FACE);
    recompute();
    xSemaphoreGive(s_lock);
    esp_timer_stop(s_blush_timer);
    esp_timer_start_once(s_blush_timer, (uint64_t)BLUSH_MS * 1000);
    mark_dirty();
}

esp_err_t ui_set_rotation(int rotation)
{
    if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) return ESP_ERR_INVALID_ARG;
    esp_err_t err = apply_rotation(rotation);
    if (err != ESP_OK) return err;
    char buf[8];
    snprintf(buf, sizeof buf, "%d", rotation);
    settings_set_str(KEY_ROTATE, buf);
    mark_dirty();
    return ESP_OK;
}

int ui_get_rotation(void) { return s_rotation; }

esp_err_t ui_set_theme(const char *name)
{
    int t;
    if (strcmp(name, "dark") == 0) t = UI_THEME_DARK;
    else if (strcmp(name, "light") == 0) t = UI_THEME_LIGHT;
    else return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.theme = t;
    xSemaphoreGive(s_lock);
    settings_set_str(KEY_THEME, name);
    mark_dirty();
    return ESP_OK;
}

const char *ui_get_theme(void)
{
    return s_state.theme == UI_THEME_LIGHT ? "light" : "dark";
}
