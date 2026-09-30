/* Tiny software renderer. C99 + libm, no platform dependencies. */
#include "gfx.h"
#include <math.h>
#include <string.h>

static uint16_t *s_fb;
static int s_w, s_h;
static int s_ox, s_oy;                    /* origin */
static int s_cx0, s_cy0, s_cx1, s_cy1;    /* clip in absolute coordinates: [x0,x1) x [y0,y1) */

static inline uint16_t swap16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }

void gfx_init(uint16_t *fb, int w, int h)
{
    s_fb = fb;
    s_w = w;
    s_h = h;
    s_ox = s_oy = 0;
    gfx_clear_clip();
}

int gfx_width(void) { return s_w; }
int gfx_height(void) { return s_h; }
uint16_t *gfx_fb(void) { return s_fb; }

void gfx_set_origin(int ox, int oy) { s_ox = ox; s_oy = oy; }

void gfx_set_clip(int x, int y, int w, int h)
{
    x += s_ox;
    y += s_oy;
    s_cx0 = x < 0 ? 0 : x;
    s_cy0 = y < 0 ? 0 : y;
    s_cx1 = x + w > s_w ? s_w : x + w;
    s_cy1 = y + h > s_h ? s_h : y + h;
    if (s_cx1 < s_cx0) s_cx1 = s_cx0;
    if (s_cy1 < s_cy0) s_cy1 = s_cy0;
}

void gfx_clear_clip(void)
{
    s_cx0 = 0; s_cy0 = 0; s_cx1 = s_w; s_cy1 = s_h;
}

static inline bool in_clip(int x, int y)
{
    return x >= s_cx0 && x < s_cx1 && y >= s_cy0 && y < s_cy1;
}

/* ---- pixels (absolute coordinates) ---------------------------------------------- */

/* blend `color` over the framebuffer pixel with alpha 0..255 */
static inline void blend8(int x, int y, uint16_t color, int a)
{
    if (a <= 0 || !in_clip(x, y)) return;
    uint16_t *p = s_fb + y * s_w + x;
    if (a >= 255) { *p = swap16(color); return; }
    uint16_t bg = swap16(*p);
    int br = (bg >> 11) & 31, bgn = (bg >> 5) & 63, bb = bg & 31;
    int fr = (color >> 11) & 31, fg = (color >> 5) & 63, fbl = color & 31;
    int r = (fr * a + br * (255 - a)) / 255;
    int g = (fg * a + bgn * (255 - a)) / 255;
    int b = (fbl * a + bb * (255 - a)) / 255;
    *p = swap16((uint16_t)((r << 11) | (g << 5) | b));
}

static void fill_rect_abs(int x, int y, int w, int h, uint16_t color)
{
    int x0 = x < s_cx0 ? s_cx0 : x;
    int y0 = y < s_cy0 ? s_cy0 : y;
    int x1 = x + w > s_cx1 ? s_cx1 : x + w;
    int y1 = y + h > s_cy1 ? s_cy1 : y + h;
    if (x1 <= x0 || y1 <= y0) return;
    uint16_t c = swap16(color);
    for (int yy = y0; yy < y1; yy++) {
        uint16_t *row = s_fb + yy * s_w + x0;
        for (int i = 0; i < x1 - x0; i++) row[i] = c;
    }
}

static inline int clamp255(float v) { return v <= 0.f ? 0 : (v >= 1.f ? 255 : (int)(v * 255.f + 0.5f)); }

/* Coverage (0..255) of pixel centre (px,py) by a rounded rectangle, from its signed distance. */
static int rr_cov(float px, float py, float x, float y, float w, float h, float r)
{
    float cx = x + w * 0.5f, cy = y + h * 0.5f;
    float qx = fabsf(px - cx) - (w * 0.5f - r);
    float qy = fabsf(py - cy) - (h * 0.5f - r);
    float ox = qx > 0.f ? qx : 0.f, oy = qy > 0.f ? qy : 0.f;
    float inner = qx > qy ? qx : qy;
    float sd = sqrtf(ox * ox + oy * oy) + (inner < 0.f ? inner : 0.f) - r;
    return clamp255(0.5f - sd);
}

static void round_rect_fill_abs(int x, int y, int w, int h, int r, uint16_t color)
{
    if (w <= 0 || h <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r <= 0) { fill_rect_abs(x, y, w, h, color); return; }
    fill_rect_abs(x, y + r, w, h - 2 * r, color);          /* middle band */
    fill_rect_abs(x + r, y, w - 2 * r, r, color);          /* top / bottom between the corners */
    fill_rect_abs(x + r, y + h - r, w - 2 * r, r, color);
    /* the four r x r corner blocks share one coverage computation */
    for (int j = 0; j < r; j++) {
        for (int i = 0; i < r; i++) {
            float dx = (float)i + 0.5f - (float)r, dy = (float)j + 0.5f - (float)r;
            int a = clamp255((float)r - sqrtf(dx * dx + dy * dy) + 0.5f);
            if (a <= 0) continue;
            blend8(x + i, y + j, color, a);
            blend8(x + w - 1 - i, y + j, color, a);
            blend8(x + i, y + h - 1 - j, color, a);
            blend8(x + w - 1 - i, y + h - 1 - j, color, a);
        }
    }
}

static void round_rect_outline_abs(int x, int y, int w, int h, int r, int t, uint16_t color)
{
    if (w <= 0 || h <= 0 || t <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int yy = y; yy < y + h; yy++) {
        if (yy < s_cy0 || yy >= s_cy1) continue;
        bool edge_row = yy < y + t + r || yy >= y + h - t - r;
        for (int xx = x; xx < x + w; xx++) {
            if (xx < s_cx0 || xx >= s_cx1) continue;
            if (!edge_row && xx >= x + t && xx < x + w - t) { xx = x + w - t - 1; continue; }   /* hollow middle */
            int outer = rr_cov(xx + 0.5f, yy + 0.5f, (float)x, (float)y, (float)w, (float)h, (float)r);
            int inner = rr_cov(xx + 0.5f, yy + 0.5f, (float)(x + t), (float)(y + t), (float)(w - 2 * t), (float)(h - 2 * t),
                               (float)(r > t ? r - t : 0));
            blend8(xx, yy, color, outer * (255 - inner) / 255);
        }
    }
}

/* ---- public shapes (origin applied here) ------------------------------------------- */

void gfx_fill(uint16_t color)
{
    uint16_t c = swap16(color);
    int n = s_w * s_h;
    for (int i = 0; i < n; i++) s_fb[i] = c;
}

void gfx_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    fill_rect_abs(x + s_ox, y + s_oy, w, h, color);
}

void gfx_fill_round_rect(int x, int y, int w, int h, int r, uint16_t color)
{
    round_rect_fill_abs(x + s_ox, y + s_oy, w, h, r, color);
}

void gfx_draw_round_rect(int x, int y, int w, int h, int r, int thickness, uint16_t color)
{
    round_rect_outline_abs(x + s_ox, y + s_oy, w, h, r, thickness, color);
}

void gfx_fill_circle(int cx, int cy, int r, uint16_t color)
{
    cx += s_ox; cy += s_oy;
    for (int yy = cy - r - 1; yy <= cy + r + 1; yy++) {
        for (int xx = cx - r - 1; xx <= cx + r + 1; xx++) {
            float dx = (float)xx + 0.5f - (float)cx - 0.5f, dy = (float)yy + 0.5f - (float)cy - 0.5f;
            blend8(xx, yy, color, clamp255((float)r + 0.5f - sqrtf(dx * dx + dy * dy)));
        }
    }
}

void gfx_ring(float cx, float cy, float r, float thickness, uint16_t color)
{
    cx += (float)s_ox; cy += (float)s_oy;
    float ro = r + thickness * 0.5f + 1.f;
    for (int yy = (int)(cy - ro); yy <= (int)(cy + ro) + 1; yy++) {
        for (int xx = (int)(cx - ro); xx <= (int)(cx + ro) + 1; xx++) {
            float dx = (float)xx + 0.5f - cx, dy = (float)yy + 0.5f - cy;
            float d = fabsf(sqrtf(dx * dx + dy * dy) - r);
            blend8(xx, yy, color, clamp255(thickness * 0.5f + 0.5f - d));
        }
    }
}

void gfx_line(float x0, float y0, float x1, float y1, float width, uint16_t color)
{
    x0 += (float)s_ox; x1 += (float)s_ox; y0 += (float)s_oy; y1 += (float)s_oy;
    float half = width * 0.5f;
    int bx0 = (int)floorf(fminf(x0, x1) - half - 1.f), bx1 = (int)ceilf(fmaxf(x0, x1) + half + 1.f);
    int by0 = (int)floorf(fminf(y0, y1) - half - 1.f), by1 = (int)ceilf(fmaxf(y0, y1) + half + 1.f);
    float vx = x1 - x0, vy = y1 - y0;
    float len2 = vx * vx + vy * vy;
    for (int yy = by0; yy <= by1; yy++) {
        for (int xx = bx0; xx <= bx1; xx++) {
            float px = (float)xx + 0.5f - x0, py = (float)yy + 0.5f - y0;
            float t = len2 > 0.f ? (px * vx + py * vy) / len2 : 0.f;
            t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
            float dx = px - t * vx, dy = py - t * vy;
            blend8(xx, yy, color, clamp255(half + 0.5f - sqrtf(dx * dx + dy * dy)));
        }
    }
}

void gfx_blit_mask(int x, int y, const uint8_t *mask, int w, int h, uint16_t color)
{
    x += s_ox; y += s_oy;
    for (int yy = 0; yy < h; yy++) {
        int dy = y + yy;
        if (dy < s_cy0 || dy >= s_cy1) continue;
        for (int xx = 0; xx < w; xx++) {
            int dx = x + xx;
            if (dx < s_cx0 || dx >= s_cx1) continue;
            uint8_t a = mask[yy * w + xx];
            if (a) blend8(dx - s_ox, dy - s_oy, color, a);
        }
    }
}

void gfx_blit(int x, int y, const uint16_t *src, int src_w, int src_h)
{
    x += s_ox; y += s_oy;
    for (int yy = 0; yy < src_h; yy++) {
        int dy = y + yy;
        if (dy < s_cy0 || dy >= s_cy1) continue;
        int x0 = x < s_cx0 ? s_cx0 : x;
        int x1 = x + src_w > s_cx1 ? s_cx1 : x + src_w;
        if (x1 <= x0) continue;
        memcpy(s_fb + dy * s_w + x0, src + yy * src_w + (x0 - x), (size_t)(x1 - x0) * 2);
    }
}

uint16_t gfx_mix(uint16_t a, uint16_t b, int t)
{
    if (t <= 0) return a;
    if (t >= 255) return b;
    int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
    int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
    int r = (ar * (255 - t) + br * t) / 255;
    int g = (ag * (255 - t) + bg * t) / 255;
    int bl = (ab * (255 - t) + bb * t) / 255;
    return (uint16_t)((r << 11) | (g << 5) | bl);
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

bool gfx_has_glyph(const kb_font_t *f, uint32_t cp) { return find_glyph(f, cp) != NULL; }

int gfx_missing_glyphs(const kb_font_t *f, const char *utf8)
{
    int n = 0;
    while (*utf8) {
        uint32_t cp = gfx_utf8_next(&utf8);
        if (cp > 0x20 && !find_glyph(f, cp)) n++;
    }
    return n;
}

/* advance for a code point; missing glyphs get a "tofu" box */
static int glyph_advance(const kb_font_t *f, uint32_t cp)
{
    const kb_glyph_t *g = find_glyph(f, cp);
    if (g) return g->adv;
    return f->size * 3 / 4 + 2;
}

static void draw_glyph(const kb_font_t *f, const kb_glyph_t *g, int px, int py, uint16_t color)
{
    int stride = (g->w + 1) / 2;
    const uint8_t *bm = f->bitmap + g->off;
    int x0 = px + g->xoff, y0 = py + g->yoff;
    if (x0 >= s_cx1 || y0 >= s_cy1 || x0 + g->w <= s_cx0 || y0 + g->h <= s_cy0) return;
    for (int yy = 0; yy < g->h; yy++) {
        const uint8_t *row = bm + yy * stride;
        for (int xx = 0; xx < g->w; xx++) {
            int a = (xx & 1) ? (row[xx >> 1] & 0x0F) : (row[xx >> 1] >> 4);
            blend8(x0 + xx, y0 + yy, color, a * 17);
        }
    }
}

/* ---- scaled text (used for the tiny face in the chat top bar) ---------------------------------- */

static inline int glyph_alpha(const kb_font_t *f, const kb_glyph_t *g, int xx, int yy)
{
    int stride = (g->w + 1) / 2;
    const uint8_t *row = f->bitmap + g->off + yy * stride;
    return (xx & 1) ? (row[xx >> 1] & 0x0F) : (row[xx >> 1] >> 4);
}

/* area-averaged down-scale of one glyph by num/den (num <= den) */
static void draw_glyph_scaled(const kb_font_t *f, const kb_glyph_t *g, int px, int py, uint16_t color, int num, int den)
{
    const float sc = (float)num / (float)den;
    int x0 = px + (int)floorf((float)g->xoff * sc + 0.5f), y0 = py + (int)floorf((float)g->yoff * sc + 0.5f);
    int dw = (int)ceilf((float)g->w * sc), dh = (int)ceilf((float)g->h * sc);
    for (int j = 0; j < dh; j++) {
        float sy0 = (float)j / sc, sy1 = fminf((float)(j + 1) / sc, (float)g->h);
        for (int i = 0; i < dw; i++) {
            float sx0 = (float)i / sc, sx1 = fminf((float)(i + 1) / sc, (float)g->w);
            float acc = 0.f, area = 0.f;
            for (int yy = (int)sy0; yy < (int)ceilf(sy1) && yy < g->h; yy++) {
                float wy = fminf((float)(yy + 1), sy1) - fmaxf((float)yy, sy0);
                for (int xx = (int)sx0; xx < (int)ceilf(sx1) && xx < g->w; xx++) {
                    float wx = fminf((float)(xx + 1), sx1) - fmaxf((float)xx, sx0);
                    acc += (float)glyph_alpha(f, g, xx, yy) * wx * wy;
                    area += wx * wy;
                }
            }
            if (area > 0.f) {
                int a = (int)(acc / area * 17.f * 1.15f);          /* slight boost: thin strokes get lost when shrunk */
                blend8(x0 + i, y0 + j, color, a > 255 ? 255 : a);
            }
        }
    }
}

int gfx_text_width_scaled(const kb_font_t *f, const char *utf8, int num, int den)
{
    return gfx_text_width(f, utf8) * num / den;
}

void gfx_draw_text_scaled(const kb_font_t *f, int x, int y, const char *utf8, uint16_t color, int num, int den)
{
    x += s_ox; y += s_oy;
    float fx = (float)x;
    while (*utf8) {
        uint32_t cp = gfx_utf8_next(&utf8);
        const kb_glyph_t *g = find_glyph(f, cp);
        if (!g) { fx += (float)glyph_advance(f, cp) * num / den; continue; }
        if (g->w) draw_glyph_scaled(f, g, (int)(fx + 0.5f), y, color, num, den);
        fx += (float)g->adv * num / den;
    }
}

static void draw_tofu(const kb_font_t *f, int px, int py, uint16_t color)
{
    int w = f->size * 3 / 4, h = f->ascent;
    round_rect_outline_abs(px + 1, py - h + 2, w - 2, h - 2, 2, 1, color);
}

int gfx_text_width_n(const kb_font_t *f, const char *utf8, int len)
{
    int w = 0;
    const char *end = utf8 + len;
    while (utf8 < end && *utf8) w += glyph_advance(f, gfx_utf8_next(&utf8));
    return w;
}

int gfx_text_width(const kb_font_t *f, const char *utf8)
{
    return gfx_text_width_n(f, utf8, (int)strlen(utf8));
}

int gfx_fit_len(const kb_font_t *f, const char *utf8, int max_w)
{
    int w = 0;
    const char *p = utf8;
    while (*p) {
        const char *q = p;
        int a = glyph_advance(f, gfx_utf8_next(&q));
        if (w + a > max_w) break;
        w += a;
        p = q;
    }
    return (int)(p - utf8);
}

void gfx_draw_text_n(const kb_font_t *f, int x, int y, const char *utf8, int len, uint16_t color)
{
    x += s_ox; y += s_oy;
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
    gfx_draw_text_n(f, x, y, utf8, (int)strlen(utf8), color);
}

void gfx_draw_text_centered(const kb_font_t *f, int cx, int y, const char *utf8, uint16_t color)
{
    gfx_draw_text(f, cx - gfx_text_width(f, utf8) / 2, y, utf8, color);
}

/* Punctuation that must not start a line (Chinese line-breaking rules): it stays on the previous line. */
static bool no_line_start(uint32_t cp)
{
    switch (cp) {
    case 0x3002: case 0xFF0C: case 0x3001: case 0xFF01: case 0xFF1F: case 0xFF1B: case 0xFF1A:   /* 。，、！？；： */
    case 0xFF09: case 0x300D: case 0x300F: case 0x3011: case 0x300B: case 0x2019: case 0x201D:   /* ）」』】》’” */
    case 0x2026: case 0x2014: case 0xFF5E:                                                        /* … — ～ */
    case '.': case ',': case '!': case '?': case ';': case ':': case ')': case '%':
        return true;
    default:
        return false;
    }
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
            if (w + a > max_w && p != start && !no_line_start(cp)) { last_ok = p; goto line_done; }
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
