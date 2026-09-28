/* Tiny software renderer. Pure C99, no platform dependencies. */
#include "gfx.h"
#include <string.h>

static uint16_t *s_fb;
static int s_w, s_h;

static inline uint16_t swap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }

void gfx_init(uint16_t *fb, int w, int h)
{
    s_fb = fb;
    s_w = w;
    s_h = h;
}

int gfx_width(void) { return s_w; }
int gfx_height(void) { return s_h; }

void gfx_fill(uint16_t color)
{
    uint16_t c = swap16(color);
    int n = s_w * s_h;
    for (int i = 0; i < n; i++) s_fb[i] = c;
}

void gfx_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s_w) w = s_w - x;
    if (y + h > s_h) h = s_h - y;
    if (w <= 0 || h <= 0) return;
    uint16_t c = swap16(color);
    for (int yy = y; yy < y + h; yy++) {
        uint16_t *row = s_fb + yy * s_w + x;
        for (int i = 0; i < w; i++) row[i] = c;
    }
}

/* Is (px,py) inside a rounded rect with corner radius r? */
static bool in_round_rect(int px, int py, int x, int y, int w, int h, int r)
{
    if (px < x || py < y || px >= x + w || py >= y + h) return false;
    int cx, cy;
    if (px < x + r) cx = x + r; else if (px >= x + w - r) cx = x + w - r - 1; else return true;
    if (py < y + r) cy = y + r; else if (py >= y + h - r) cy = y + h - r - 1; else return true;
    int dx = px - cx, dy = py - cy;
    return dx * dx + dy * dy <= r * r;
}

void gfx_fill_round_rect(int x, int y, int w, int h, int r, uint16_t color)
{
    if (r <= 0) { gfx_fill_rect(x, y, w, h, color); return; }
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    /* middle band */
    gfx_fill_rect(x, y + r, w, h - 2 * r, color);
    /* top and bottom bands with rounded corners */
    uint16_t c = swap16(color);
    for (int yy = 0; yy < r; yy++) {
        int ty = y + yy, by = y + h - 1 - yy;
        for (int xx = x; xx < x + w; xx++) {
            if (in_round_rect(xx, ty, x, y, w, h, r)) {
                if (ty >= 0 && ty < s_h && xx >= 0 && xx < s_w) s_fb[ty * s_w + xx] = c;
                if (by >= 0 && by < s_h && xx >= 0 && xx < s_w) s_fb[by * s_w + xx] = c;
            }
        }
    }
}

void gfx_draw_round_rect(int x, int y, int w, int h, int r, int thickness, uint16_t color)
{
    uint16_t c = swap16(color);
    for (int yy = y; yy < y + h; yy++) {
        if (yy < 0 || yy >= s_h) continue;
        for (int xx = x; xx < x + w; xx++) {
            if (xx < 0 || xx >= s_w) continue;
            if (in_round_rect(xx, yy, x, y, w, h, r) &&
                !in_round_rect(xx, yy, x + thickness, y + thickness, w - 2 * thickness, h - 2 * thickness, r - thickness))
                s_fb[yy * s_w + xx] = c;
        }
    }
}

/* ---- UTF-8 ------------------------------------------------------------- */

uint32_t gfx_utf8_next(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    uint32_t cp;
    int n;
    if (s[0] < 0x80) { cp = s[0]; n = 1; }
    else if ((s[0] & 0xE0) == 0xC0) { cp = s[0] & 0x1F; n = 2; }
    else if ((s[0] & 0xF0) == 0xE0) { cp = s[0] & 0x0F; n = 3; }
    else if ((s[0] & 0xF8) == 0xF0) { cp = s[0] & 0x07; n = 4; }
    else { *p += 1; return 0xFFFD; }
    for (int i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) { *p += i; return 0xFFFD; }
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    *p += n;
    return cp;
}

int gfx_utf8_count(const char *utf8)
{
    int n = 0;
    while (*utf8) { gfx_utf8_next(&utf8); n++; }
    return n;
}

/* ---- text -------------------------------------------------------------- */

static const kb_glyph_t *find_glyph(const kb_font_t *f, uint32_t cp)
{
    int lo = 0, hi = (int)f->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t c = f->glyphs[mid].cp;
        if (c == cp) return &f->glyphs[mid];
        if (c < cp) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

/* advance for a code point; missing glyphs get a "tofu" box */
static int glyph_advance(const kb_font_t *f, uint32_t cp)
{
    const kb_glyph_t *g = find_glyph(f, cp);
    if (g) return g->adv;
    return f->size * 3 / 4 + 2;
}

static inline void blend_px(int x, int y, uint16_t color, int a /*0..15*/)
{
    if (x < 0 || y < 0 || x >= s_w || y >= s_h || a == 0) return;
    uint16_t *p = s_fb + y * s_w + x;
    if (a >= 15) { *p = swap16(color); return; }
    uint16_t bg = swap16(*p);
    int br = (bg >> 11) & 31, bgn = (bg >> 5) & 63, bb = bg & 31;
    int fr = (color >> 11) & 31, fg = (color >> 5) & 63, fbl = color & 31;
    int r = (fr * a + br * (15 - a)) / 15;
    int g = (fg * a + bgn * (15 - a)) / 15;
    int b = (fbl * a + bb * (15 - a)) / 15;
    *p = swap16((uint16_t)((r << 11) | (g << 5) | b));
}

static void draw_glyph(const kb_font_t *f, const kb_glyph_t *g, int px, int py, uint16_t color)
{
    int stride = (g->w + 1) / 2;
    const uint8_t *bm = f->bitmap + g->off;
    int x0 = px + g->xoff, y0 = py + g->yoff;
    for (int yy = 0; yy < g->h; yy++) {
        const uint8_t *row = bm + yy * stride;
        for (int xx = 0; xx < g->w; xx++) {
            int a = (xx & 1) ? (row[xx >> 1] & 0x0F) : (row[xx >> 1] >> 4);
            blend_px(x0 + xx, y0 + yy, color, a);
        }
    }
}

static void draw_tofu(const kb_font_t *f, int px, int py, uint16_t color)
{
    int w = f->size * 3 / 4, h = f->ascent;
    gfx_draw_round_rect(px + 1, py - h + 2, w - 2, h - 2, 2, 1, color);
}

int gfx_text_width(const kb_font_t *f, const char *utf8)
{
    int w = 0;
    while (*utf8) w += glyph_advance(f, gfx_utf8_next(&utf8));
    return w;
}

static void draw_text_n(const kb_font_t *f, int x, int y, const char *utf8, int len, uint16_t color)
{
    const char *end = utf8 + len;
    while (utf8 < end && *utf8) {
        uint32_t cp = gfx_utf8_next(&utf8);
        const kb_glyph_t *g = find_glyph(f, cp);
        if (g) {
            if (g->w) draw_glyph(f, g, x, y, color);
            x += g->adv;
        } else {
            draw_tofu(f, x, y, color);
            x += glyph_advance(f, cp);
        }
    }
}

void gfx_draw_text(const kb_font_t *f, int x, int y, const char *utf8, uint16_t color)
{
    draw_text_n(f, x, y, utf8, (int)strlen(utf8), color);
}

void gfx_draw_text_centered(const kb_font_t *f, int cx, int y, const char *utf8, uint16_t color)
{
    gfx_draw_text(f, cx - gfx_text_width(f, utf8) / 2, y, utf8, color);
}

int gfx_wrap(const kb_font_t *f, const char *utf8, int max_w, gfx_line_t *lines, int max_lines)
{
    int n = 0;
    const char *p = utf8;
    while (*p && n < max_lines) {
        const char *start = p;
        int w = 0;
        const char *last_ok = p;
        while (*p) {
            const char *q = p;
            uint32_t cp = gfx_utf8_next(&q);
            if (cp == '\n') { last_ok = p; p = q; goto line_done; }
            if (cp == '\r') { p = q; continue; }
            int a = glyph_advance(f, cp);
            if (w + a > max_w && p != start) { last_ok = p; goto line_done; }
            w += a;
            p = q;
            last_ok = p;
        }
        last_ok = p;
    line_done:
        lines[n].start = start;
        lines[n].len = (int)(last_ok - start);
        n++;
    }
    return n;
}
