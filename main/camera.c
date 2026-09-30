/* Camera pins and settings from the official example
 * waveshareteam/ESP32-S3-Touch-LCD-3.5 ESP-IDF/07_lvgl_wifi/components/esp_port/esp_camera_port.cpp:
 *   XCLK=38 PCLK=41 VSYNC=17 HREF=18 D0..D7 = 45 47 48 46 42 40 39 21, SCCB over I2C port 0
 *   (shared with the rest of the board), no PWDN/RESET pins, 20 MHz XCLK, vflip = 1.
 * The official example uses RGB565 at 320x480 for LVGL; we use the sensor's JPEG
 * output instead: HVGA (480x320) for the live view, SXGA (1280x1024) for photos.
 * XCLK uses LEDC timer 0 / channel 1 (the backlight owns channel 0). */
#include "camera.h"
#include "settings.h"
#include "imgrot.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
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

/* The OV5640 sits in the case with its x axis along the board's SHORT side (that is why Waveshare's own demo
 * asks the sensor for 320x480, portrait, and shows it upright on the portrait screen). So the sensor is always
 * asked for portrait sizes, and the image is rotated in software to match the display rotation:
 *   display 0 (portrait)  -> as is          display 90 -> 90 deg clockwise
 *   display 180           -> 180 deg        display 270 -> 270 deg clockwise (= 90 counter-clockwise)
 * `cam rot` adds a correction on top (saved), in case the guess above is off by a quarter turn. */
#define PHOTO_SIZE   FRAMESIZE_P_3MP     /* 864x1536 portrait -> 1536x864 after rotation */
#define PREVIEW_SIZE FRAMESIZE_320X480
#define SRC_PREVIEW_W 320
#define SRC_PREVIEW_H 480
#define PHOTO_W 864
#define PHOTO_H 1536

static bool s_ready;
static bool s_vflip = false, s_hmirror = true;   /* measured on the board: this is upright (v6.1); the quarter turn is done in software */
static int s_xclk_mhz = 10;      /* 20 -> 10: fewer stripes (PSRAM bandwidth shared with the LCD) */
static int s_quality = 10;       /* sensor JPEG quality register, 4..63, LOWER = better */
static int s_rot_off = 0;        /* extra clockwise correction, 0/90/180/270 */
static bool s_awb = true;
static int s_wb_mode = 0;        /* 0 auto, 1 sunny, 2 cloudy, 3 office, 4 home */
static uint16_t *s_preview[2];
static int s_preview_idx;

static const char *WB_NAMES[] = { "auto", "sunny", "cloudy", "office", "home" };

/* jpg2rgb565 writes native little-endian RGB565; the framebuffer wants the bytes
 * swapped (high byte first) so it can go straight to the panel. */
static void swap_bytes(uint16_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) p[i] = (uint16_t)((p[i] << 8) | (p[i] >> 8));
}

static int total_rot(int display_rot) { return ((display_rot % 360) + s_rot_off + 360) % 360; }

static void apply_orientation(void)
{
    sensor_t *s = esp_camera_sensor_get();
    if (!s) return;
    s->set_vflip(s, s_vflip);
    s->set_hmirror(s, s_hmirror);
}

/* white balance / exposure: everything automatic, lens correction on */
static void apply_tuning(void)
{
    sensor_t *s = esp_camera_sensor_get();
    if (!s) return;
    s->set_whitebal(s, s_awb);
    s->set_awb_gain(s, s_awb);
    s->set_wb_mode(s, s_awb ? s_wb_mode : 0);
    s->set_gain_ctrl(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_aec2(s, 1);
    s->set_ae_level(s, 0);
    s->set_lenc(s, 1);
    s->set_bpc(s, 1);
    s->set_wpc(s, 1);
    s->set_dcw(s, 1);
    s->set_brightness(s, 0);
    s->set_contrast(s, 0);
    s->set_saturation(s, 0);
    s->set_quality(s, s_quality);
}

static void load_settings(void)
{
    char buf[12];
    if (settings_get_str("cam2_vflip", buf, sizeof buf)) s_vflip = strcmp(buf, "off") != 0;
    if (settings_get_str("cam2_mirror", buf, sizeof buf)) s_hmirror = strcmp(buf, "on") == 0;
    if (settings_get_str("cam_xclk", buf, sizeof buf)) s_xclk_mhz = atoi(buf);
    if (settings_get_str("cam_quality", buf, sizeof buf)) s_quality = atoi(buf);
    if (settings_get_str("cam2_rot", buf, sizeof buf)) s_rot_off = atoi(buf);
    if (settings_get_str("cam_awb", buf, sizeof buf)) s_awb = strcmp(buf, "off") != 0;
    if (settings_get_str("cam_wb", buf, sizeof buf)) s_wb_mode = atoi(buf);
    if (s_xclk_mhz < 6 || s_xclk_mhz > 24) s_xclk_mhz = 10;
    if (s_quality < 4 || s_quality > 63) s_quality = 10;
    if (s_rot_off % 90 != 0) s_rot_off = 0;
    if (s_wb_mode < 0 || s_wb_mode > 4) s_wb_mode = 0;
}

esp_err_t camera_init(void)
{
    if (s_ready) return ESP_OK;
    load_settings();

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
        .xclk_freq_hz = s_xclk_mhz * 1000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_1,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = PHOTO_SIZE,        /* buffers are sized for the largest frame we use */
        .jpeg_quality = s_quality,
        .fb_count = 2,
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
    ESP_LOGI(TAG, "sensor PID 0x%04x (OV5640 = 0x5640), XCLK %d MHz, quality %d", s ? s->id.PID : 0, s_xclk_mhz, s_quality);
    apply_orientation();
    apply_tuning();
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

const uint16_t *camera_preview(int display_rot, int *w, int *h)
{
    if (!s_ready) return NULL;
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return NULL;
    bool ok = false;
    static uint16_t *raw;                              /* decoded frame in sensor orientation (little-endian) */
    if (!raw) raw = heap_caps_malloc(SRC_PREVIEW_W * SRC_PREVIEW_H * 2, MALLOC_CAP_SPIRAM);
    if (raw && fb->format == PIXFORMAT_JPEG && fb->width == SRC_PREVIEW_W && fb->height == SRC_PREVIEW_H) {
        ok = jpg2rgb565(fb->buf, fb->len, (uint8_t *)raw, JPG_SCALE_NONE);
    }
    esp_camera_fb_return(fb);
    if (!ok) return NULL;
    uint16_t *dst = s_preview[s_preview_idx ^ 1];      /* into the buffer that is not on screen */
    int rot = total_rot(display_rot);
    img_rotate_swap(raw, SRC_PREVIEW_W, SRC_PREVIEW_H, dst, rot);
    *w = (rot == 90 || rot == 270) ? SRC_PREVIEW_H : SRC_PREVIEW_W;
    *h = (rot == 90 || rot == 270) ? SRC_PREVIEW_W : SRC_PREVIEW_H;
    s_preview_idx ^= 1;
    return dst;
}

static esp_err_t capture_impl(int display_rot, uint8_t **jpeg, size_t *len)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    sensor_t *s = esp_camera_sensor_get();
    if (!s) return ESP_FAIL;
    s->set_framesize(s, PHOTO_SIZE);
    s->set_quality(s, s_quality);
    vTaskDelay(pdMS_TO_TICKS(150));
    /* discard frames: the size switch and the exposure / white balance need a few frames to settle */
    for (int i = 0; i < 5; i++) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) esp_camera_fb_return(fb);
    }
    /* take the first complete JPEG (FFD8 ... FFD9) of the right size; a frame whose start or end was lost to a
     * DMA overrun ("NO-SOI") is thrown away and the next one tried */
    camera_fb_t *fb = NULL;
    for (int tries = 0; tries < 4; tries++) {
        fb = esp_camera_fb_get();
        if (fb && fb->format == PIXFORMAT_JPEG && fb->len > 4 && fb->width == PHOTO_W && fb->height == PHOTO_H &&
            fb->buf[0] == 0xFF && fb->buf[1] == 0xD8 && fb->buf[fb->len - 2] == 0xFF && fb->buf[fb->len - 1] == 0xD9) break;
        ESP_LOGW(TAG, "photo frame %d unusable (%s), trying again", tries, fb ? "bad JPEG" : "timeout");
        if (fb) esp_camera_fb_return(fb);
        fb = NULL;
    }
    esp_err_t err = ESP_FAIL;
    uint8_t *copy = NULL;
    size_t copy_len = 0;
    if (fb && fb->format == PIXFORMAT_JPEG && fb->len > 0) {
        copy = heap_caps_malloc(fb->len, MALLOC_CAP_SPIRAM);
        if (copy) {
            memcpy(copy, fb->buf, fb->len);
            copy_len = fb->len;
            err = ESP_OK;
            ESP_LOGI(TAG, "photo %ux%u, %u bytes", fb->width, fb->height, (unsigned)fb->len);
        } else {
            err = ESP_ERR_NO_MEM;
        }
    }
    bool size_ok = fb && fb->width == PHOTO_W && fb->height == PHOTO_H;
    if (fb) esp_camera_fb_return(fb);
    s->set_framesize(s, PREVIEW_SIZE);
    vTaskDelay(pdMS_TO_TICKS(100));
    if (err != ESP_OK) return err;

    int rot = total_rot(display_rot);
    if (rot == 0 || !size_ok) {
        *jpeg = copy;
        *len = copy_len;
        return ESP_OK;
    }
    /* turn it upright: decode, rotate, encode again (the sensor cannot rotate) */
    size_t px = (size_t)PHOTO_W * PHOTO_H;
    uint16_t *raw = heap_caps_malloc(px * 2, MALLOC_CAP_SPIRAM);
    uint16_t *rotd = heap_caps_malloc(px * 2, MALLOC_CAP_SPIRAM);
    err = ESP_ERR_NO_MEM;
    if (raw && rotd) {
        err = ESP_FAIL;
        if (jpg2rgb565(copy, copy_len, (uint8_t *)raw, JPG_SCALE_NONE)) {
            img_rotate_swap(raw, PHOTO_W, PHOTO_H, rotd, rot);        /* big-endian RGB565, what fmt2jpg reads */
            bool swap_dims = rot == 90 || rot == 270;
            uint8_t *out = NULL;
            size_t out_len = 0;
            int enc_q = 100 - s_quality;
            if (enc_q > 95) enc_q = 95;
            if (enc_q < 60) enc_q = 60;
            if (fmt2jpg((uint8_t *)rotd, px * 2, swap_dims ? PHOTO_H : PHOTO_W, swap_dims ? PHOTO_W : PHOTO_H,
                        PIXFORMAT_RGB565, (uint8_t)enc_q, &out, &out_len) && out) {
                free(copy);
                *jpeg = out;
                *len = out_len;
                ESP_LOGI(TAG, "rotated %d deg and re-encoded: %u bytes", rot, (unsigned)out_len);
                err = ESP_OK;
            }
        }
    }
    free(raw);
    free(rotd);
    if (err != ESP_OK) {               /* could not rotate: better a lying photo than none */
        ESP_LOGW(TAG, "rotation failed (%s), keeping the unrotated photo", esp_err_to_name(err));
        *jpeg = copy;
        *len = copy_len;
        return ESP_OK;
    }
    return ESP_OK;
}

/* The grab, JPEG decode, rotation and re-encode run in a task of their own with a big stack, whoever calls
 * (touch task, HTTP server task, console): none of those has stack to spare for the JPEG codec. */
typedef struct {
    int rot;
    uint8_t **jpeg;
    size_t *len;
    esp_err_t err;
    TaskHandle_t waiter;
} capture_job_t;

static void capture_task(void *arg)
{
    capture_job_t *j = arg;
    j->err = capture_impl(j->rot, j->jpeg, j->len);
    xTaskNotifyGive(j->waiter);
    vTaskDelete(NULL);
}

esp_err_t camera_capture_jpeg(int display_rot, uint8_t **jpeg, size_t *len)
{
    capture_job_t j = { .rot = display_rot, .jpeg = jpeg, .len = len, .err = ESP_FAIL, .waiter = xTaskGetCurrentTaskHandle() };
    if (xTaskCreate(capture_task, "cam_shot", 16384, &j, 4, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    return j.err;
}

void camera_set_vflip(bool on)
{
    s_vflip = on;
    settings_set_str("cam2_vflip", on ? "on" : "off");
    if (s_ready) apply_orientation();
}

void camera_set_hmirror(bool on)
{
    s_hmirror = on;
    settings_set_str("cam2_mirror", on ? "on" : "off");
    if (s_ready) apply_orientation();
}

bool camera_get_vflip(void) { return s_vflip; }
bool camera_get_hmirror(void) { return s_hmirror; }

void camera_set_xclk(int mhz)
{
    if (mhz < 6) mhz = 6;
    if (mhz > 24) mhz = 24;
    s_xclk_mhz = mhz;
    char b[8];
    snprintf(b, sizeof b, "%d", mhz);
    settings_set_str("cam_xclk", b);
    if (s_ready) { camera_deinit(); camera_init(); }       /* the clock is fixed at init */
}

void camera_set_quality(int q)
{
    if (q < 4) q = 4;
    if (q > 63) q = 63;
    s_quality = q;
    char b[8];
    snprintf(b, sizeof b, "%d", q);
    settings_set_str("cam_quality", b);
    if (s_ready) apply_tuning();
}

void camera_set_awb(bool on)
{
    s_awb = on;
    settings_set_str("cam_awb", on ? "on" : "off");
    if (s_ready) apply_tuning();
}

bool camera_set_wb(const char *name)
{
    for (int i = 0; i < 5; i++) {
        if (!strcmp(name, WB_NAMES[i])) {
            s_wb_mode = i;
            char b[4];
            snprintf(b, sizeof b, "%d", i);
            settings_set_str("cam_wb", b);
            if (s_ready) apply_tuning();
            return true;
        }
    }
    return false;
}

bool camera_set_rot(int deg)
{
    if (deg % 90 != 0 || deg < 0 || deg > 270) return false;
    s_rot_off = deg;
    char b[8];
    snprintf(b, sizeof b, "%d", deg);
    settings_set_str("cam2_rot", b);
    return true;
}

void camera_print_settings(void)
{
    if (!s_ready) load_settings();
    printf("camera: xclk %d MHz, quality %d (4 best .. 63 worst), rot +%d deg, awb %s, wb %s, vflip %s, mirror %s\n",
           s_xclk_mhz, s_quality, s_rot_off, s_awb ? "on" : "off", WB_NAMES[s_wb_mode], s_vflip ? "on" : "off",
           s_hmirror ? "on" : "off");
}

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
