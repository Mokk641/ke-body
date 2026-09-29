/* Camera pins and settings from the official example
 * waveshareteam/ESP32-S3-Touch-LCD-3.5 ESP-IDF/07_lvgl_wifi/components/esp_port/esp_camera_port.cpp:
 *   XCLK=38 PCLK=41 VSYNC=17 HREF=18 D0..D7 = 45 47 48 46 42 40 39 21, SCCB over I2C port 0
 *   (shared with the rest of the board), no PWDN/RESET pins, 20 MHz XCLK, vflip = 1.
 * The official example uses RGB565 at 320x480 for LVGL; we use the sensor's JPEG
 * output instead: HVGA (480x320) for the live view, SXGA (1280x1024) for photos.
 * XCLK uses LEDC timer 0 / channel 1 (the backlight owns channel 0). */
#include "camera.h"
#include "settings.h"

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "esp_heap_caps.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "camera";

#define PIN_XCLK  38
#define PIN_PCLK  41
#define PIN_VSYNC 17
#define PIN_HREF  18
#define PIN_D0 45
#define PIN_D1 47
#define PIN_D2 48
#define PIN_D3 46
#define PIN_D4 42
#define PIN_D5 40
#define PIN_D6 39
#define PIN_D7 21

#define PHOTO_SIZE   FRAMESIZE_SXGA
#define PREVIEW_SIZE FRAMESIZE_HVGA

static bool s_ready;
static bool s_vflip = true, s_hmirror = false;
static uint16_t *s_preview[2];
static int s_preview_idx;

/* jpg2rgb565 writes native little-endian RGB565; the framebuffer wants the bytes
 * swapped (high byte first) so it can go straight to the panel. */
static void swap_bytes(uint16_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) p[i] = (uint16_t)((p[i] << 8) | (p[i] >> 8));
}

static void apply_orientation(void)
{
    sensor_t *s = esp_camera_sensor_get();
    if (!s) return;
    s->set_vflip(s, s_vflip);
    s->set_hmirror(s, s_hmirror);
}

esp_err_t camera_init(void)
{
    if (s_ready) return ESP_OK;
    char buf[8];
    if (settings_get_str("cam_vflip", buf, sizeof buf)) s_vflip = strcmp(buf, "off") != 0;
    if (settings_get_str("cam_mirror", buf, sizeof buf)) s_hmirror = strcmp(buf, "on") == 0;

    for (int i = 0; i < 2; i++) {
        if (!s_preview[i]) s_preview[i] = heap_caps_malloc(CAM_PREVIEW_W * CAM_PREVIEW_H * 2, MALLOC_CAP_SPIRAM);
        ESP_RETURN_ON_FALSE(s_preview[i], ESP_ERR_NO_MEM, TAG, "preview buffer");
    }

    camera_config_t cfg = {
        .pin_pwdn = -1,
        .pin_reset = -1,
        .pin_xclk = PIN_XCLK,
        .pin_sccb_sda = -1,
        .pin_sccb_scl = -1,
        .pin_d7 = PIN_D7, .pin_d6 = PIN_D6, .pin_d5 = PIN_D5, .pin_d4 = PIN_D4,
        .pin_d3 = PIN_D3, .pin_d2 = PIN_D2, .pin_d1 = PIN_D1, .pin_d0 = PIN_D0,
        .pin_vsync = PIN_VSYNC,
        .pin_href = PIN_HREF,
        .pin_pclk = PIN_PCLK,
        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_1,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = PHOTO_SIZE,        /* buffers are sized for the largest frame we use */
        .jpeg_quality = 12,
        .fb_count = 1,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_LATEST,
        .sccb_i2c_port = 0,
    };
    esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_camera_init failed: %s", esp_err_to_name(err));
        return err;
    }
    sensor_t *s = esp_camera_sensor_get();
    ESP_LOGI(TAG, "sensor PID 0x%04x (OV5640 = 0x5640)", s ? s->id.PID : 0);
    apply_orientation();
    if (s) s->set_framesize(s, PREVIEW_SIZE);
    vTaskDelay(pdMS_TO_TICKS(100));
    s_ready = true;
    return ESP_OK;
}

void camera_deinit(void)
{
    if (!s_ready) return;
    esp_camera_deinit();
    s_ready = false;
    ESP_LOGI(TAG, "camera off");
}

bool camera_ready(void) { return s_ready; }

const uint16_t *camera_preview(void)
{
    if (!s_ready) return NULL;
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return NULL;
    bool ok = false;
    uint16_t *dst = s_preview[s_preview_idx ^ 1];   /* decode into the buffer not on screen */
    if (fb->format == PIXFORMAT_JPEG && fb->width == CAM_PREVIEW_W && fb->height == CAM_PREVIEW_H) {
        ok = jpg2rgb565(fb->buf, fb->len, (uint8_t *)dst, JPG_SCALE_NONE);
    }
    esp_camera_fb_return(fb);
    if (!ok) return NULL;
    swap_bytes(dst, CAM_PREVIEW_W * CAM_PREVIEW_H);
    s_preview_idx ^= 1;
    return dst;
}

esp_err_t camera_capture_jpeg(uint8_t **jpeg, size_t *len)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    sensor_t *s = esp_camera_sensor_get();
    if (!s) return ESP_FAIL;
    s->set_framesize(s, PHOTO_SIZE);
    vTaskDelay(pdMS_TO_TICKS(150));
    /* discard frames captured during the size switch */
    for (int i = 0; i < 2; i++) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) esp_camera_fb_return(fb);
    }
    camera_fb_t *fb = esp_camera_fb_get();
    esp_err_t err = ESP_FAIL;
    if (fb && fb->format == PIXFORMAT_JPEG && fb->len > 0) {
        uint8_t *copy = heap_caps_malloc(fb->len, MALLOC_CAP_SPIRAM);
        if (copy) {
            memcpy(copy, fb->buf, fb->len);
            *jpeg = copy;
            *len = fb->len;
            err = ESP_OK;
            ESP_LOGI(TAG, "photo %ux%u, %u bytes", fb->width, fb->height, (unsigned)fb->len);
        } else {
            err = ESP_ERR_NO_MEM;
        }
    }
    if (fb) esp_camera_fb_return(fb);
    s->set_framesize(s, PREVIEW_SIZE);
    vTaskDelay(pdMS_TO_TICKS(100));
    return err;
}

void camera_set_vflip(bool on)
{
    s_vflip = on;
    settings_set_str("cam_vflip", on ? "on" : "off");
    if (s_ready) apply_orientation();
}

void camera_set_hmirror(bool on)
{
    s_hmirror = on;
    settings_set_str("cam_mirror", on ? "on" : "off");
    if (s_ready) apply_orientation();
}

bool camera_get_vflip(void) { return s_vflip; }
bool camera_get_hmirror(void) { return s_hmirror; }

/* JPEG size from the SOF marker */
static bool jpeg_dims(const uint8_t *d, size_t len, int *w, int *h)
{
    size_t i = 2;
    while (i + 9 < len) {
        if (d[i] != 0xFF) { i++; continue; }
        uint8_t m = d[i + 1];
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { i += 2; continue; }
        size_t seg = ((size_t)d[i + 2] << 8) | d[i + 3];
        if (m == 0xC0 || m == 0xC1 || m == 0xC2) {
            *h = (d[i + 5] << 8) | d[i + 6];
            *w = (d[i + 7] << 8) | d[i + 8];
            return *w > 0 && *h > 0;
        }
        i += 2 + seg;
    }
    return false;
}

esp_err_t camera_decode_to_fit(const uint8_t *jpeg, size_t len, int max_w, int max_h,
                               uint16_t **out, int *w, int *h)
{
    int jw, jh;
    if (!jpeg_dims(jpeg, len, &jw, &jh)) return ESP_ERR_INVALID_ARG;
    int div = 1;
    jpg_scale_t sc = JPG_SCALE_NONE;
    while ((jw / div > max_w || jh / div > max_h) && div < 8) {
        div *= 2;
        sc = div == 2 ? JPG_SCALE_2X : div == 4 ? JPG_SCALE_4X : JPG_SCALE_8X;
    }
    int ow = jw / div, oh = jh / div;
    uint16_t *buf = heap_caps_malloc((size_t)ow * oh * 2, MALLOC_CAP_SPIRAM);
    if (!buf) return ESP_ERR_NO_MEM;
    if (!jpg2rgb565(jpeg, len, (uint8_t *)buf, sc)) {
        free(buf);
        return ESP_FAIL;
    }
    swap_bytes(buf, (size_t)ow * oh);
    *out = buf;
    *w = ow;
    *h = oh;
    return ESP_OK;
}
