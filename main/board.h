/* Board support for Waveshare ESP32-S3-Touch-LCD-3.5 (-C variant: same PCB + case + OV5640).
 * Pin numbers and init sequence follow the official examples in
 * https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5  (see README). */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define BOARD_LCD_W 320
#define BOARD_LCD_H 480

esp_err_t board_init(void);

/* Push a full 320x480 RGB565 (byte-swapped) frame to the panel. Blocks until sent. */
void board_lcd_flush(const uint16_t *fb);

/* Backlight 0..100 */
void board_backlight_set(uint8_t percent);

/* Poll the FT6336 touch controller. Returns true if at least one finger is down. */
bool board_touch_read(uint16_t *x, uint16_t *y);
