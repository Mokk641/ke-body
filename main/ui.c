#include "ui.h"
#include "ui_render.h"
#include "gfx.h"
#include "board.h"

#include <string.h>
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

static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_dirty;
static ui_state_t s_state;            /* what is drawn */
static char s_base_face[UI_FACE_BUF];  /* face to restore after a blush */
static bool s_blushing;
static esp_timer_handle_t s_blush_timer;
static uint16_t *s_fb;

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
        ui_render(&snap);
        board_lcd_flush(s_fb);
    }
}

static void blush_timeout(void *arg)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_blushing = false;
    strcpy(s_state.face, s_base_face);
    xSemaphoreGive(s_lock);
    mark_dirty();
}

void ui_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_dirty = xSemaphoreCreateBinary();
    s_fb = heap_caps_malloc(BOARD_LCD_W * BOARD_LCD_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!s_fb) {
        ESP_LOGW(TAG, "no PSRAM framebuffer, falling back to internal RAM");
        s_fb = heap_caps_malloc(BOARD_LCD_W * BOARD_LCD_H * sizeof(uint16_t), MALLOC_CAP_DEFAULT);
    }
    assert(s_fb);
    gfx_init(s_fb, BOARD_LCD_W, BOARD_LCD_H);

    strcpy(s_base_face, "(—_—)");
    strcpy(s_state.face, s_base_face);

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
    if (!s_blushing) strcpy(s_state.face, s_base_face);
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
    copy_limited(s_state.corner, sizeof(s_state.corner), utf8, 40);
    xSemaphoreGive(s_lock);
    mark_dirty();
}

void ui_blush(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_blushing = true;
    strcpy(s_state.face, BLUSH_FACE);
    xSemaphoreGive(s_lock);
    esp_timer_stop(s_blush_timer);
    esp_timer_start_once(s_blush_timer, (uint64_t)BLUSH_MS * 1000);
    mark_dirty();
}
