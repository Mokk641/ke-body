/* Small PNG reader for pictures that arrive from the PC (pure C, host-testable): every colour type and bit depth of
 * non-interlaced PNG, decoded and shrunk (box filter, never enlarged) to fit a given box. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PNG_MAX_DIM      8192
#define PNG_MAX_RAW      (6 * 1024 * 1024)      /* decompressed size limit, bytes */

bool png_info(const uint8_t *d, size_t n, int *w, int *h);

/* Picture -> byte-swapped RGB565 (what the framebuffer and gfx_blit use); transparency is composited on black.
 * *out is malloc'd (PSRAM on the board). */
bool png_decode_rgb565(const uint8_t *d, size_t n, int max_w, int max_h, uint16_t **out, int *ow, int *oh);

/* Handwriting -> 8-bit coverage mask (255 = ink). Works for black-on-white, white-on-black and transparent-background
 * PNGs: with transparency the alpha is the ink, otherwise the difference from the corner colour is. */
bool png_decode_ink_mask(const uint8_t *d, size_t n, int max_w, int max_h, uint8_t **out, int *ow, int *oh);

/* raw deflate (no zlib header), exposed for tests. Returns bytes written or -1. */
long png_inflate(const uint8_t *in, size_t inlen, uint8_t *out, size_t outlen);
