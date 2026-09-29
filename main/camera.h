/* OV5640 on the back of the case (DVP, pins from the official esp_camera_port.cpp). */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#define CAM_PREVIEW_W 480
#define CAM_PREVIEW_H 320

esp_err_t camera_init(void);       /* JPEG mode; buffers sized for the photo size, preview at HVGA */
void camera_deinit(void);
bool camera_ready(void);

/* Grab one preview frame and decode it into the internal RGB565 (byte-swapped)
 * buffer. Returns the buffer (CAM_PREVIEW_W x CAM_PREVIEW_H) or NULL. */
const uint16_t *camera_preview(void);

/* Take a photo at the photo size (SXGA 1280x1024 JPEG). *jpeg is malloc'd (PSRAM). */
esp_err_t camera_capture_jpeg(uint8_t **jpeg, size_t *len);

/* Orientation tweaks (saved to NVS). */
void camera_set_vflip(bool on);
void camera_set_hmirror(bool on);
bool camera_get_vflip(void);
bool camera_get_hmirror(void);

/* Decode a JPEG file's content to fit max_w x max_h. Output is a malloc'd
 * byte-swapped RGB565 buffer (PSRAM); w and h receive its size. */
esp_err_t camera_decode_to_fit(const uint8_t *jpeg, size_t len, int max_w, int max_h,
                               uint16_t **out, int *w, int *h);
