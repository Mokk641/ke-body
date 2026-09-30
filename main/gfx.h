/* Tiny software renderer: RGB565 framebuffer, anti-aliased shapes, 4bpp AA text.
 * Pure C (needs libm), no ESP-IDF dependency, so it also builds on the host. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "kb_font.h"

#define GFX_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

/* fb must hold w*h uint16_t. Pixels are stored byte-swapped (big-endian RGB565)
 * so the buffer can be streamed to the ST7796 over SPI without conversion. */
void gfx_init(uint16_t *fb, int w, int h);
int gfx_width(void);
int gfx_height(void);
uint16_t *gfx_fb(void);

/* Origin: every drawing call below is translated by (ox, oy). Used to slide a page
 * or a panel across the screen. gfx_init() resets it to (0,0). */
void gfx_set_origin(int ox, int oy);

/* Clip rectangle, given in the current origin's coordinates (default: whole screen).
 * Changing the origin afterwards does not move an already set clip. */
void gfx_set_clip(int x, int y, int w, int h);
void gfx_clear_clip(void);

void gfx_fill(uint16_t color);                       /* whole framebuffer, ignores origin and clip */
void gfx_fill_rect(int x, int y, int w, int h, uint16_t color);
void gfx_fill_round_rect(int x, int y, int w, int h, int r, uint16_t color);          /* anti-aliased */
void gfx_draw_round_rect(int x, int y, int w, int h, int r, int thickness, uint16_t color);  /* anti-aliased outline */
void gfx_fill_circle(int cx, int cy, int r, uint16_t color);                          /* anti-aliased */
void gfx_ring(float cx, float cy, float r, float thickness, uint16_t color);          /* anti-aliased circle outline */
void gfx_line(float x0, float y0, float x1, float y1, float width, uint16_t color);   /* anti-aliased, round caps */

/* Copy a byte-swapped RGB565 image (same format as the framebuffer) to x,y; clipped. */
void gfx_blit(int x, int y, const uint16_t *src, int src_w, int src_h);

/* Mix two colours: t = 0..255 (0 = a, 255 = b). */
uint16_t gfx_mix(uint16_t a, uint16_t b, int t);

/* Text. Strings are UTF-8. y is the baseline. */
int gfx_text_width(const kb_font_t *f, const char *utf8);
int gfx_text_width_n(const kb_font_t *f, const char *utf8, int len);
void gfx_draw_text(const kb_font_t *f, int x, int y, const char *utf8, uint16_t color);
void gfx_draw_text_n(const kb_font_t *f, int x, int y, const char *utf8, int len, uint16_t color);
void gfx_draw_text_centered(const kb_font_t *f, int cx, int y, const char *utf8, uint16_t color);

/* Byte length of the longest prefix of utf8 (whole characters) that is at most max_w wide. */
int gfx_fit_len(const kb_font_t *f, const char *utf8, int max_w);

/* Does the font have a real glyph for cp? Missing glyphs are drawn as an empty box. */
bool gfx_has_glyph(const kb_font_t *f, uint32_t cp);
/* Number of characters of utf8 that the font has no glyph for (spaces and controls excluded). */
int gfx_missing_glyphs(const kb_font_t *f, const char *utf8);

/* Greedy per-character word wrap. Fills `lines` with (start,len) byte ranges
 * into utf8. Returns the number of lines (capped at max_lines; when capped the
 * last line is truncated to fit and the caller may append an ellipsis). */
typedef struct { const char *start; int len; } gfx_line_t;
int gfx_wrap(const kb_font_t *f, const char *utf8, int max_w, gfx_line_t *lines, int max_lines);

/* UTF-8 helpers */
uint32_t gfx_utf8_next(const char **p);
int gfx_utf8_count(const char *utf8);
