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
