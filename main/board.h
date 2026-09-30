/* Board support for Waveshare ESP32-S3-Touch-LCD-3.5 (-C variant: same PCB + case + OV5640).
 * Pin numbers and init sequence follow the official examples in
 * https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5  (see README). */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

/* native panel size (portrait) */
#define BOARD_LCD_W 320
#define BOARD_LCD_H 480

esp_err_t board_init(void);

/* Shared I2C bus (TCA9554, FT6336, ES8311, AXP2101 all live here). */
i2c_master_bus_handle_t board_i2c_bus(void);

/* Display rotation: 0 / 90 / 180 / 270. Changes the panel's MADCTL (official
 * swap/mirror table) and the logical size returned by board_lcd_width/height. */
esp_err_t board_lcd_set_rotation(int rotation);
int board_lcd_rotation(void);
int board_lcd_width(void);
int board_lcd_height(void);

/* Push a full frame (board_lcd_width x board_lcd_height, RGB565 byte-swapped)
 * to the panel. Blocks until sent. */
void board_lcd_flush(const uint16_t *fb);
/* Push only the rectangle [x0,x1) x [y0,y1) of a full-size framebuffer (stride = current width) - a small
 * rectangle is ~1 ms, a whole frame ~60 ms. */
void board_lcd_flush_rect(const uint16_t *fb, int x0, int y0, int x1, int y1);

/* Touch panel active area: raw FT6336 values are stretched from [xmin,xmax] x [ymin,ymax] to the full screen.
 * Default 0..319 x 0..479 (identity). `board_touch_seen` = smallest / largest raw value seen since boot. */
void board_touch_set_range(int xmin, int xmax, int ymin, int ymax);
void board_touch_get_range(int out[4]);
void board_touch_seen(int out[4], bool reset);      /* xmin xmax ymin ymax */
int board_touch_samples(void);                      /* touch samples counted since the last reset */
/* Optional colour calibration: gamma x100 (100 = none) and per-channel gain in percent. */
void board_lcd_set_calibration(int gamma_x100, int r_pct, int g_pct, int b_pct);
void board_lcd_get_calibration(int out[4]);

/* Backlight 0..100 */
void board_backlight_set(uint8_t percent);

/* Poll the FT6336. Returns true if a finger is down; x/y are in the current
 * rotation's coordinate system (official esp_lcd_touch mirror/swap table). */
bool board_touch_read(uint16_t *x, uint16_t *y);
void board_touch_log(bool on);   /* print raw + mapped coordinates while pressed */

/* Speaker amplifier enable (TCA9554 P7, high = on). Off after board_init. */
esp_err_t board_amp_enable(bool on);

/* TCA9554 I/O expander pin control for bench tests. mode: 0 = output low,
 * 1 = output high, 2 = input. Pin 1 is the LCD reset (keep it high). */
esp_err_t board_expander_set(int pin, int mode);
esp_err_t board_expander_dump(void);

/* ESP32 GPIO control for bench tests. mode: 0 = output low, 1 = output high, 2 = input.
 * Refuses pins used by the board (LCD, I2C, I2S, USB, flash/PSRAM). */
esp_err_t board_gpio_set(int gpio, int mode);
