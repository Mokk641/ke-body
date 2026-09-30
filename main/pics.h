/* Pictures that Ke sends (POST /image): a small pool of decoded pictures in PSRAM, each with a chat thumbnail.
 * Pure C (no ESP-IDF) so the host tests can run it. Pixels are byte-swapped RGB565, like the framebuffer. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PICS_SLOTS      3       /* pictures kept (the oldest is dropped for a new one) */
#define PICS_THUMB_W    200     /* chat thumbnail box */
#define PICS_THUMB_H    150

typedef struct {
    int id;                     /* > 0; the chat message remembers this, so a dropped picture is never replaced by another */
    uint16_t *full; int fw, fh;
    uint16_t *thumb; int tw, th;
} pic_t;

/* Shrink an RGB565 picture to fit max_w x max_h (box filter, never enlarged; a picture that already fits is copied).
 * *out is malloc'd. */
bool rgb565_fit(const uint16_t *src, int w, int h, int max_w, int max_h, uint16_t **out, int *ow, int *oh);

/* Take ownership of a decoded picture (free()-able buffer), make its thumbnail, return its id (0 = failed;
 * the picture is freed then). */
int pics_store(uint16_t *full, int w, int h);
const pic_t *pics_get(int id);                       /* NULL if unknown or already dropped */
