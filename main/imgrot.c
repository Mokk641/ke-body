#include "imgrot.h"
#include <stddef.h>

static inline uint16_t sw(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }

void img_rotate_swap(const uint16_t *src, int w, int h, uint16_t *dst, int rot)
{
    switch (rot) {
    case 90:     /* dst is h wide, w tall: dst(x', y') = src(x = y', y = h-1-x') */
        for (int y = 0; y < w; y++)
            for (int x = 0; x < h; x++) dst[(size_t)y * h + x] = sw(src[(size_t)(h - 1 - x) * w + y]);
        break;
    case 270:    /* dst(x', y') = src(x = w-1-y', y = x') */
        for (int y = 0; y < w; y++)
            for (int x = 0; x < h; x++) dst[(size_t)y * h + x] = sw(src[(size_t)x * w + (w - 1 - y)]);
        break;
    case 180:
        for (size_t i = 0; i < (size_t)w * h; i++) dst[i] = sw(src[(size_t)w * h - 1 - i]);
        break;
    default:
        for (size_t i = 0; i < (size_t)w * h; i++) dst[i] = sw(src[i]);
        break;
    }
}
