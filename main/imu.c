#include "imu.h"
#include "settings.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "imu";

/* QMI8658 (official SensorLib register map) */
#define REG_WHOAMI   0x00
#define REG_CTRL1    0x02
#define REG_CTRL2    0x03
#define REG_CTRL7    0x08
#define REG_AX_L     0x35
#define WHOAMI_VAL   0x05

#define POLL_MS         50
#define SHAKE_G         1.9f     /* |a| above this counts as one shake peak (1 g is rest) */
#define SHAKE_WINDOW_MS 900
#define SHAKE_COOLDOWN  2500
#define FACE_DOWN_G     -0.75f   /* z below this (screen down) ... */
#define FACE_UP_G       -0.45f   /* ... and above this = picked up (hysteresis) */
#define FACE_DOWN_MS    1500
#define ORIENT_G        0.65f
#define ORIENT_MS       1000

static i2c_master_dev_handle_t s_dev;
static imu_cb_t s_cb;
static bool s_ready, s_invert = true, s_autorot;   /* invert on by default: measured on the board (flat, screen up: z = -1.07) */
static float s_ax, s_ay, s_az;

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { reg, val };
    return i2c_master_transmit(s_dev, b, 2, 100);
}

static esp_err_t rd(uint8_t reg, uint8_t *out, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, out, n, 100);
}

bool imu_read(float *ax, float *ay, float *az)
{
    uint8_t d[6];
    if (!s_ready || rd(REG_AX_L, d, 6) != ESP_OK) return false;
    int16_t x = (int16_t)(d[0] | (d[1] << 8));
    int16_t y = (int16_t)(d[2] | (d[3] << 8));
    int16_t z = (int16_t)(d[4] | (d[5] << 8));
    const float k = 4.0f / 32768.0f;     /* ±4 g range */
    *ax = x * k; *ay = y * k; *az = z * k;
    return true;
}

static void task(void *arg)
{
    int64_t last_peak = 0, first_peak = 0;
    int peaks = 0;
    int64_t cooldown_until = 0;
    int64_t down_since = 0, up_since = 0;
    bool face_down = false;
    int orient_cand = -1;
    int64_t orient_since = 0;
    int orient_sent = -1;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        float ax, ay, az;
        if (!imu_read(&ax, &ay, &az)) continue;
        s_ax = ax; s_ay = ay; s_az = az;
        int64_t now = esp_timer_get_time() / 1000;

        /* --- shake: two |a| peaks above SHAKE_G within SHAKE_WINDOW_MS --- */
        float mag = sqrtf(ax * ax + ay * ay + az * az);
        if (mag > SHAKE_G && now - last_peak > 150 && now > cooldown_until) {
            last_peak = now;
            if (peaks == 0 || now - first_peak > SHAKE_WINDOW_MS) { peaks = 1; first_peak = now; }
            else peaks++;
            if (peaks >= 2) {
                peaks = 0;
                cooldown_until = now + SHAKE_COOLDOWN;
                ESP_LOGI(TAG, "shake (|a|=%.2f g)", mag);
                if (s_cb) s_cb(IMU_EVT_SHAKE, 0);
            }
        }

        /* --- face down: z (screen normal) pointing down for FACE_DOWN_MS --- */
        float zz = s_invert ? -az : az;
        if (!face_down) {
            if (zz < FACE_DOWN_G && mag < 1.4f) {
                if (!down_since) down_since = now;
                if (now - down_since > FACE_DOWN_MS) {
                    face_down = true;
                    up_since = 0;
                    ESP_LOGI(TAG, "face down");
                    if (s_cb) s_cb(IMU_EVT_FACE_DOWN, 0);
                }
            } else {
                down_since = 0;
            }
        } else {
            if (zz > FACE_UP_G) {
                if (!up_since) up_since = now;
                if (now - up_since > 400) {
                    face_down = false;
                    down_since = 0;
                    ESP_LOGI(TAG, "face up");
                    if (s_cb) s_cb(IMU_EVT_FACE_UP, 0);
                }
            } else {
                up_since = 0;
            }
        }

        /* --- auto-rotate (off by default): which edge points down --- */
        if (s_autorot && !face_down) {
            int cand = -1;
            /* UNVERIFIED axis mapping: raw x along the panel's short edge, y along the long edge */
            if (ay < -ORIENT_G) cand = 0;
            else if (ay > ORIENT_G) cand = 180;
            else if (ax > ORIENT_G) cand = 90;
            else if (ax < -ORIENT_G) cand = 270;
            if (cand != orient_cand) { orient_cand = cand; orient_since = now; }
            else if (cand >= 0 && cand != orient_sent && now - orient_since > ORIENT_MS) {
                orient_sent = cand;
                if (s_cb) s_cb(IMU_EVT_ORIENTATION, cand);
            }
        }
    }
}

esp_err_t imu_init(i2c_master_bus_handle_t bus, imu_cb_t cb)
{
    s_cb = cb;
    char buf[8];
    if (settings_get_str("imu_inv", buf, sizeof buf)) s_invert = strcmp(buf, "on") == 0;
    if (settings_get_str("autorot", buf, sizeof buf)) s_autorot = strcmp(buf, "on") == 0;

    const uint8_t addrs[] = { 0x6B, 0x6A };
    for (unsigned i = 0; i < 2; i++) {
        i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addrs[i], .scl_speed_hz = 400000 };
        if (i2c_master_bus_add_device(bus, &cfg, &s_dev) != ESP_OK) continue;
        uint8_t id = 0;
        if (rd(REG_WHOAMI, &id, 1) == ESP_OK && id == WHOAMI_VAL) {
            ESP_LOGI(TAG, "QMI8658 found at 0x%02x", addrs[i]);
            break;
        }
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
    if (!s_dev) {
        ESP_LOGW(TAG, "QMI8658 not found (0x6B/0x6A); shake / face-down disabled");
        return ESP_ERR_NOT_FOUND;
    }
    /* CTRL1: address auto-increment; CTRL2: ±4 g, 125 Hz; CTRL7: accelerometer on */
    ESP_RETURN_ON_ERROR(wr(REG_CTRL1, 0x40), TAG, "ctrl1");
    ESP_RETURN_ON_ERROR(wr(REG_CTRL2, 0x16), TAG, "ctrl2");
    ESP_RETURN_ON_ERROR(wr(REG_CTRL7, 0x01), TAG, "ctrl7");
    vTaskDelay(pdMS_TO_TICKS(20));
    s_ready = true;
    xTaskCreate(task, "imu", 3072, NULL, 3, NULL);
    ESP_LOGI(TAG, "ready (invert %s, autorotate %s)", s_invert ? "on" : "off", s_autorot ? "on" : "off");
    return ESP_OK;
}

bool imu_ready(void) { return s_ready; }

void imu_set_invert(bool inv) { s_invert = inv; settings_set_str("imu_inv", inv ? "on" : "off"); }
bool imu_get_invert(void) { return s_invert; }
void imu_set_autorotate(bool on) { s_autorot = on; settings_set_str("autorot", on ? "on" : "off"); }
bool imu_get_autorotate(void) { return s_autorot; }

void imu_print(void)
{
    if (!s_ready) { printf("imu: not found\n"); return; }
    printf("accel x=%+.2f y=%+.2f z=%+.2f g  |a|=%.2f   invert=%s autorotate=%s\n",
           s_ax, s_ay, s_az, sqrtf(s_ax * s_ax + s_ay * s_ay + s_az * s_az),
           s_invert ? "on" : "off", s_autorot ? "on" : "off");
}
