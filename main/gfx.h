/* Tiny software renderer: RGB565 framebuffer, filled shapes, 4bpp AA text.
 * Pure C, no ESP-IDF dependency, so it can be compiled on the host too. */
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

/* Clip rectangle for all drawing (default: whole screen). */
void gfx_set_clip(int x, int y, int w, int h);
void gfx_clear_clip(void);

void gfx_fill(uint16_t color);
void gfx_fill_rect(int x, int y, int w, int h, uint16_t color);
void gfx_fill_round_rect(int x, int y, int w, int h, int r, uint16_t color);
void gfx_draw_round_rect(int x, int y, int w, int h, int r, int thickness, uint16_t color);
void gfx_fill_circle(int cx, int cy, int r, uint16_t color);

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

/* Greedy per-character word wrap. Fills `lines` with (start,len) byte ranges
 * into utf8. Returns the number of lines (capped at max_lines; when capped the
 * last line is truncated to fit and the caller may append an ellipsis). */
typedef struct { const char *start; int len; } gfx_line_t;
int gfx_wrap(const kb_font_t *f, const char *utf8, int max_w, gfx_line_t *lines, int max_lines);

/* UTF-8 helpers */
uint32_t gfx_utf8_next(const char **p);
int gfx_utf8_count(const char *utf8);
