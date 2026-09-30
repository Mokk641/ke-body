/* OV5640 on the back of the case (DVP, pins from the official esp_camera_port.cpp). */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#define CAM_PREVIEW_W 480      /* buffer size: 480x320 (landscape) or 320x480 (portrait) */
#define CAM_PREVIEW_H 320

esp_err_t camera_init(void);       /* JPEG mode; sensor always in a portrait size, see camera.c */
void camera_deinit(void);
bool camera_ready(void);

/* Grab one preview frame, decode it, rotate it for the given display rotation (0/90/180/270; the
 * `cam rot` correction is added) and return a byte-swapped RGB565 buffer of *w x *h, or NULL. */
const uint16_t *camera_preview(int display_rot, int *w, int *h);

/* Take a photo (864x1536 sensor JPEG) and return it upright for the display rotation: rotated and
 * re-encoded when needed. *jpeg is malloc'd (PSRAM). */
esp_err_t camera_capture_jpeg(int display_rot, uint8_t **jpeg, size_t *len);

/* Tuning knobs for the serial `cam` command (all saved to NVS). */
void camera_set_xclk(int mhz);           /* 6..24, default 10; re-initialises the camera if it is on */
void camera_set_quality(int q);          /* 4..63, lower = better, default 10 */
void camera_set_awb(bool on);
bool camera_set_wb(const char *name);    /* auto sunny cloudy office home */
bool camera_set_rot(int deg);            /* extra clockwise correction 0/90/180/270 */
void camera_print_settings(void);

/* Orientation tweaks (saved to NVS). */
void camera_set_vflip(bool on);
void camera_set_hmirror(bool on);
bool camera_get_vflip(void);
bool camera_get_hmirror(void);

/* Decode a JPEG file's content to fit max_w x max_h. Output is a malloc'd
 * byte-swapped RGB565 buffer (PSRAM); w and h receive its size. */
esp_err_t camera_decode_to_fit(const uint8_t *jpeg, size_t len, int max_w, int max_h,
                               uint16_t **out, int *w, int *h);
