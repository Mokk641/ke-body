/* JPEG or PNG bytes -> byte-swapped RGB565 that fits a box (never enlarged). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* *out is malloc'd (PSRAM). false: not a picture, unsupported (progressive JPEG, interlaced PNG) or no memory. */
bool imgdec_decode(const uint8_t *data, size_t len, int max_w, int max_h, uint16_t **out, int *w, int *h);
const char *imgdec_ext(const uint8_t *data, size_t len);      /* "jpg" / "png" / NULL */
