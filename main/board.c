/* Board support for Waveshare ESP32-S3-Touch-LCD-3.5 / -3.5-C.
 *
 * Everything hardware-specific below is taken from the official repository
 * https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5 :
 *   ESP-IDF/07_lvgl_wifi/components/esp_port/esp_3inch5_lcd_port.cpp  (SPI/LCD/backlight/touch)
 *   ESP-IDF/07_lvgl_wifi/main/main.cpp                                (I2C pins, TCA9554 reset, rotation table)
 *   Arduino/examples/08_gfx_helloworld/08_gfx_helloworld.ino           (same pins, reset sequence)
 *
 *   LCD   : ST7796, 4-wire SPI, 320x480, MOSI=GPIO1 SCLK=GPIO5 DC=GPIO3, CS/RST not wired to the SoC
 *   Reset : LCD reset line is driven by a TCA9554 I/O expander (I2C 0x20), pin 1
 *   BL    : GPIO6 (PWM)
 *   Touch : FT6336 on I2C (0x38), INT/RST not wired to the SoC
 *   I2C   : SDA=GPIO8 SCL=GPIO7
 */
#include "board.h"
#include "colorcal.h"

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7796.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "board";

/* --- pins (official) --- */
#define PIN_LCD_MOSI   GPIO_NUM_1
#define PIN_LCD_SCLK   GPIO_NUM_5
#define PIN_LCD_DC     GPIO_NUM_3
#define PIN_LCD_BL     GPIO_NUM_6
#define PIN_I2C_SDA    GPIO_NUM_8
#define PIN_I2C_SCL    GPIO_NUM_7

#define LCD_SPI_HOST   SPI2_HOST
#define LCD_PCLK_HZ    (80 * 1000 * 1000)

#define TCA9554_ADDR   0x20
#define FT6336_ADDR    0x38

/* Bytes per SPI transaction when flushing the PSRAM framebuffer:
 * 24 rows in portrait (320 px wide) or 16 rows in landscape (480 px wide). */
#define FLUSH_BYTES    (320 * 24 * 2)

static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_tca;
static i2c_master_dev_handle_t s_tp;
static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_flush_done;
static uint8_t *s_bounce[2];
static int s_rotation = 0;
static int s_w = BOARD_LCD_W, s_h = BOARD_LCD_H;
static bool s_touch_log;

/* --- I2C helpers --- */
static esp_err_t i2c_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(dev, buf, 2, 100);
}

static esp_err_t i2c_read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *out, size_t len)
{
    return i2c_master_transmit_receive(dev, &reg, 1, out, len, 100);
}

static esp_err_t i2c_init(void)
{
    i2c_master_bus_config_t bus = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = 0,
        .scl_io_num = PIN_I2C_SCL,
        .sda_io_num = PIN_I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = 1,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &s_i2c), TAG, "i2c bus");

    i2c_device_config_t tca = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = TCA9554_ADDR, .scl_speed_hz = 400000 };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c, &tca, &s_tca), TAG, "tca9554 dev");
    i2c_device_config_t tp = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = FT6336_ADDR, .scl_speed_hz = 400000 };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c, &tp, &s_tp), TAG, "ft6336 dev");
    return ESP_OK;
}

i2c_master_bus_handle_t board_i2c_bus(void)
{
    return s_i2c;
}

/* TCA9554 registers: 0 input, 1 output, 2 polarity, 3 config (1 = input).
 * Official sequence (main.cpp io_expander_init): pin1 output, low 100 ms, high 100 ms. */
static esp_err_t lcd_hw_reset_via_expander(void)
{
    uint8_t cfg = 0xFF, out = 0xFF;
    esp_err_t err = i2c_read_reg(s_tca, 0x03, &cfg, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TCA9554 not responding (%s) - LCD reset skipped", esp_err_to_name(err));
        return err;
    }
    i2c_read_reg(s_tca, 0x01, &out, 1);
    cfg &= ~(1 << 1);
    ESP_RETURN_ON_ERROR(i2c_write_reg(s_tca, 0x03, cfg), TAG, "tca cfg");
    ESP_RETURN_ON_ERROR(i2c_write_reg(s_tca, 0x01, out & ~(1 << 1)), TAG, "tca low");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(i2c_write_reg(s_tca, 0x01, out | (1 << 1)), TAG, "tca high");
    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

/* Speaker amplifier enable: TCA9554 P7, high = amplifier on.
 * Found on the bench (2026-09-29): with P7 low the speaker is silent even though the
 * ES8311 DAC output is confirmed via the internal loopback; P7 high = sound.
 * Not documented in any official example. */
#define TCA_PIN_AMP 7

esp_err_t board_amp_enable(bool on)
{
    uint8_t cfg, out;
    ESP_RETURN_ON_ERROR(i2c_read_reg(s_tca, 0x03, &cfg, 1), TAG, "tca read cfg");
    ESP_RETURN_ON_ERROR(i2c_read_reg(s_tca, 0x01, &out, 1), TAG, "tca read out");
    if (on) out |= (1 << TCA_PIN_AMP); else out &= ~(1 << TCA_PIN_AMP);
    ESP_RETURN_ON_ERROR(i2c_write_reg(s_tca, 0x01, out), TAG, "tca out");
    if (cfg & (1 << TCA_PIN_AMP)) {
        cfg &= ~(1 << TCA_PIN_AMP);
        ESP_RETURN_ON_ERROR(i2c_write_reg(s_tca, 0x03, cfg), TAG, "tca cfg");
    }
    return ESP_OK;
}

esp_err_t board_expander_set(int pin, int mode)
{
    if (pin < 0 || pin > 7 || mode < 0 || mode > 2) return ESP_ERR_INVALID_ARG;
    uint8_t cfg, out;
    ESP_RETURN_ON_ERROR(i2c_read_reg(s_tca, 0x03, &cfg, 1), TAG, "tca read cfg");
    ESP_RETURN_ON_ERROR(i2c_read_reg(s_tca, 0x01, &out, 1), TAG, "tca read out");
    if (mode == 2) {
        cfg |= (1 << pin);
        ESP_RETURN_ON_ERROR(i2c_write_reg(s_tca, 0x03, cfg), TAG, "tca cfg");
    } else {
        if (mode) out |= (1 << pin); else out &= ~(1 << pin);
        ESP_RETURN_ON_ERROR(i2c_write_reg(s_tca, 0x01, out), TAG, "tca out");
        cfg &= ~(1 << pin);
        ESP_RETURN_ON_ERROR(i2c_write_reg(s_tca, 0x03, cfg), TAG, "tca cfg");
    }
    ESP_LOGI(TAG, "TCA9554 pin %d -> %s", pin, mode == 2 ? "input" : mode ? "high" : "low");
    return ESP_OK;
}

esp_err_t board_expander_dump(void)
{
    uint8_t in, out, cfg;
    ESP_RETURN_ON_ERROR(i2c_read_reg(s_tca, 0x00, &in, 1), TAG, "tca in");
    ESP_RETURN_ON_ERROR(i2c_read_reg(s_tca, 0x01, &out, 1), TAG, "tca out");
    ESP_RETURN_ON_ERROR(i2c_read_reg(s_tca, 0x03, &cfg, 1), TAG, "tca cfg");
    printf("TCA9554 input=0x%02x output=0x%02x config=0x%02x (1=input)\n", in, out, cfg);
    for (int p = 0; p < 8; p++) {
        printf("  pin %d: %s, level %d%s\n", p, (cfg >> p) & 1 ? "input " : "output", (in >> p) & 1,
               p == 1 ? "  (LCD reset)" : p == TCA_PIN_AMP ? "  (speaker amplifier enable)" : "");
    }
    return ESP_OK;
}

static bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *d, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_flush_done, &woken);
    return woken == pdTRUE;
}

/* The official ESP-IDF port restarts the chip once after a cold power-on
 * (esp_3inch5_lcd_port.cpp: soft_reset_once). Kept for fidelity. */
static void soft_reset_once(void)
{
    if (esp_reset_reason() == ESP_RST_POWERON) {
        ESP_LOGW(TAG, "cold boot: one-time software restart (as in official example)");
        fflush(stdout);
        esp_restart();
    }
}

static esp_err_t lcd_init(void)
{
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_LCD_SCLK,
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = FLUSH_BYTES,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO), TAG, "spi bus");

    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = GPIO_NUM_NC,
        .dc_gpio_num = PIN_LCD_DC,
        .spi_mode = 0,
        .pclk_hz = LCD_PCLK_HZ,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_color_trans_done,
        .user_ctx = NULL,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, &s_io), TAG, "panel io");
    soft_reset_once();

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7796(s_io, &panel_config, &s_panel), TAG, "st7796");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG, "invert");
    ESP_RETURN_ON_ERROR(board_lcd_set_rotation(0), TAG, "rotation");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "disp on");
    return ESP_OK;
}

/* Official table (main.cpp, display_cfg.rotation for EXAMPLE_DISPLAY_ROTATION):
 *   0: swap 0, mirror_x 1, mirror_y 0   (verified on hardware in phase 1)
 *  90: swap 1, mirror_x 1, mirror_y 1
 * 180: swap 0, mirror_x 0, mirror_y 1
 * 270: swap 1, mirror_x 0, mirror_y 0 */
esp_err_t board_lcd_set_rotation(int rotation)
{
    bool swap, mx, my;
    switch (rotation) {
    case 0:   swap = false; mx = true;  my = false; break;
    case 90:  swap = true;  mx = true;  my = true;  break;
    case 180: swap = false; mx = false; my = true;  break;
    case 270: swap = true;  mx = false; my = false; break;
    default: return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, swap), TAG, "swap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, mx, my), TAG, "mirror");
    s_rotation = rotation;
    s_w = swap ? BOARD_LCD_H : BOARD_LCD_W;
    s_h = swap ? BOARD_LCD_W : BOARD_LCD_H;
    ESP_LOGI(TAG, "rotation %d -> %dx%d (swap=%d mx=%d my=%d)", rotation, s_w, s_h, swap, mx, my);
    return ESP_OK;
}

int board_lcd_rotation(void) { return s_rotation; }
int board_lcd_width(void) { return s_w; }
int board_lcd_height(void) { return s_h; }

static esp_err_t backlight_init(void)
{
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_1,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&t), TAG, "ledc timer");
    ledc_channel_config_t c = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_1,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = PIN_LCD_BL,
        .duty = 0,
        .hpoint = 0,
    };
    return ledc_channel_config(&c);
}

void board_backlight_set(uint8_t percent)
{
    if (percent > 100) percent = 100;
    uint32_t duty = (percent * 1023) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

esp_err_t board_init(void)
{
    s_flush_done = xSemaphoreCreateCounting(16, 0);
    for (int i = 0; i < 2; i++) {
        s_bounce[i] = heap_caps_malloc(FLUSH_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        ESP_RETURN_ON_FALSE(s_bounce[i], ESP_ERR_NO_MEM, TAG, "bounce buffer");
    }
    ESP_RETURN_ON_ERROR(i2c_init(), TAG, "i2c");
    lcd_hw_reset_via_expander();           /* non-fatal: log and continue */
    if (board_amp_enable(false) != ESP_OK) {   /* amplifier off until something plays */
        ESP_LOGW(TAG, "could not set amplifier enable (TCA9554 P7)");
    }
    ESP_RETURN_ON_ERROR(lcd_init(), TAG, "lcd");
    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");
    ESP_LOGI(TAG, "board init done");
    return ESP_OK;
}

void board_lcd_set_calibration(int gamma_x100, int r_pct, int g_pct, int b_pct) { colorcal_set(gamma_x100, r_pct, g_pct, b_pct); }
void board_lcd_get_calibration(int out[4]) { colorcal_get(out); }

void board_lcd_flush(const uint16_t *fb)
{
    const int w = s_w, h = s_h;
    const int chunk_rows = FLUSH_BYTES / (w * 2);
    int buf = 0;
    int inflight = 0;   /* colour transfers queued but not yet reported done */
    for (int y = 0; y < h; y += chunk_rows) {
        int rows = (y + chunk_rows <= h) ? chunk_rows : h - y;
        size_t bytes = (size_t)rows * w * 2;
        /* both bounce buffers busy: wait for the older transfer before reusing */
        if (inflight == 2) {
            xSemaphoreTake(s_flush_done, portMAX_DELAY);
            inflight--;
        }
        if (colorcal_active()) colorcal_apply((uint16_t *)s_bounce[buf], fb + (size_t)y * w, bytes / 2);
        else memcpy(s_bounce[buf], fb + (size_t)y * w, bytes);
        if (esp_lcd_panel_draw_bitmap(s_panel, 0, y, w, y + rows, s_bounce[buf]) == ESP_OK) {
            inflight++;
        }
        buf ^= 1;
    }
    while (inflight > 0) {
        xSemaphoreTake(s_flush_done, portMAX_DELAY);
        inflight--;
    }
}

void board_touch_log(bool on) { s_touch_log = on; }

bool board_touch_read(uint16_t *x, uint16_t *y)
{
    /* FT6336 registers (official esp_lcd_touch_ft6336.c): 0x02 = touch points,
     * 0x03/0x04 = P1 X high/low, 0x05/0x06 = P1 Y high/low */
    uint8_t n = 0;
    if (i2c_read_reg(s_tp, 0x02, &n, 1) != ESP_OK) return false;
    n &= 0x0F;
    if (n == 0 || n > 2) return false;
    uint8_t d[4];
    if (i2c_read_reg(s_tp, 0x03, d, 4) != ESP_OK) return false;
    int rx = ((d[0] & 0x0F) << 8) | d[1];   /* raw, portrait frame 0..319 */
    int ry = ((d[2] & 0x0F) << 8) | d[3];   /* raw, portrait frame 0..479 */

    /* Official esp_lcd_touch flags per rotation (esp_3inch5_touch_port_init) applied
     * the way esp_lcd_touch does it: mirror first, then swap.
     *   90: mirror_y + swap    180: mirror_x + mirror_y    270: mirror_x + swap */
    int mx = rx, my = ry;
    switch (s_rotation) {
    case 90:  my = BOARD_LCD_H - ry; break;
    case 180: mx = BOARD_LCD_W - rx; my = BOARD_LCD_H - ry; break;
    case 270: mx = BOARD_LCD_W - rx; break;
    default: break;
    }
    int lx = mx, ly = my;
    if (s_rotation == 90 || s_rotation == 270) { lx = my; ly = mx; }
    if (lx < 0) lx = 0;
    if (ly < 0) ly = 0;
    if (lx >= s_w) lx = s_w - 1;
    if (ly >= s_h) ly = s_h - 1;
    if (x) *x = (uint16_t)lx;
    if (y) *y = (uint16_t)ly;

    if (s_touch_log) {
        static int64_t last_log;
        int64_t now = esp_timer_get_time();
        if (now - last_log > 200000) {
            last_log = now;
            printf("touch raw=(%d,%d) rot%d -> (%d,%d) of %dx%d\n", rx, ry, s_rotation, lx, ly, s_w, s_h);
        }
    }
    return true;
}

/* Drive / read an ESP32 GPIO for bench tests (hunting for an amplifier enable). */
esp_err_t board_gpio_set(int gpio, int mode)
{
    static const int reserved[] = { 0, 1, 3, 5, 6, 7, 8, 12, 13, 14, 15, 16, 19, 20 };
    if (gpio < 0 || gpio > 48 || mode < 0 || mode > 2) return ESP_ERR_INVALID_ARG;
    if (gpio >= 26 && gpio <= 37) return ESP_ERR_INVALID_ARG;   /* flash / PSRAM */
    for (unsigned i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++) {
        if (reserved[i] == gpio) return ESP_ERR_INVALID_ARG;
    }
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = mode == 2 ? GPIO_MODE_INPUT : GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "gpio config");
    if (mode != 2) gpio_set_level(gpio, mode);
    vTaskDelay(pdMS_TO_TICKS(5));
    printf("gpio %d: %s, level now %d\n", gpio, mode == 2 ? "input" : mode ? "output high" : "output low",
           gpio_get_level(gpio));
    return ESP_OK;
}
