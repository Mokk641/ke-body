/* Bitmap font format shared by tools/gen_fonts.py, gfx.c and the host preview. */
#pragma once
#include <stdint.h>

typedef struct {
    uint32_t cp;      /* Unicode code point (glyph table is sorted by cp) */
    uint16_t w, h;    /* bitmap size in pixels (0,0 for blank glyphs like space) */
    int16_t xoff;     /* bitmap left edge relative to pen x */
    int16_t yoff;     /* bitmap top edge relative to baseline (negative = above) */
    uint16_t adv;     /* horizontal advance */
    uint32_t off;     /* byte offset into the bitmap blob */
} kb_glyph_t;

typedef struct {
    const kb_glyph_t *glyphs;
    uint32_t count;
    const uint8_t *bitmap;   /* 4 bpp alpha, high nibble first, rows padded to bytes */
    int16_t ascent;
    int16_t descent;
    int16_t line_height;
    int16_t size;            /* nominal pixel size */
} kb_font_t;
