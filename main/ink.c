#include "ink.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
/* the buffers are 100+ KB: PSRAM, not the internal heap */
static void *big_alloc(size_t n) { void *p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM); return p ? p : calloc(1, n); }
#else
static void *big_alloc(size_t n) { return calloc(1, n); }
#endif

/* ---- editing ------------------------------------------------------------------------------------------ */

void ink_reset(ink_t *k) { memset(k, 0, sizeof *k); }

bool ink_is_empty(const ink_char_t *c) { return c->nstrokes == 0; }

bool ink_pen_down(ink_t *k, int x, int y)
{
    ink_char_t *c = &k->cur;
    if (c->nstrokes >= INK_MAX_STROKES || c->npts >= INK_MAX_PTS) return false;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > INK_RANGE - 1) x = INK_RANGE - 1;
    if (y > INK_RANGE - 1) y = INK_RANGE - 1;
    c->start[c->nstrokes] = c->npts;
    c->pts[c->npts].x = (uint16_t)x;
    c->pts[c->npts].y = (uint16_t)y;
    c->npts++;                     /* points first, counts last: the renderer runs in another task */
    c->nstrokes++;
    k->pen_down = true;
    return true;
}

bool ink_pen_move(ink_t *k, int x, int y)
{
    ink_char_t *c = &k->cur;
    if (!k->pen_down || c->nstrokes == 0 || c->npts >= INK_MAX_PTS) return false;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > INK_RANGE - 1) x = INK_RANGE - 1;
    if (y > INK_RANGE - 1) y = INK_RANGE - 1;
    const ink_pt_t *p = &c->pts[c->npts - 1];
    int dx = x - p->x, dy = y - p->y;
    if (dx * dx + dy * dy < 6 * 6) return false;                 /* ~2 px on a 300 px pad: sensor noise */
    c->pts[c->npts].x = (uint16_t)x;
    c->pts[c->npts].y = (uint16_t)y;
    c->npts++;
    return true;
}

void ink_pen_up(ink_t *k) { k->pen_down = false; }

bool ink_next(ink_t *k)
{
    if (ink_is_empty(&k->cur) || k->ndone >= INK_MAX_CHARS) return false;
    k->pen_down = false;
    k->done[k->ndone++] = k->cur;
    memset(&k->cur, 0, sizeof k->cur);
    return true;
}

static void drop_last_stroke(ink_char_t *c)
{
    if (c->nstrokes == 0) return;
    c->npts = c->start[c->nstrokes - 1];
    c->nstrokes--;
}

void ink_undo(ink_t *k)
{
    k->pen_down = false;
    if (ink_is_empty(&k->cur) && k->ndone > 0) {
        k->cur = k->done[--k->ndone];
        memset(&k->done[k->ndone], 0, sizeof k->done[0]);
    }
    drop_last_stroke(&k->cur);
}

void ink_clear(ink_t *k)
{
    k->pen_down = false;
    if (!ink_is_empty(&k->cur)) memset(&k->cur, 0, sizeof k->cur);
    else { memset(k->done, 0, sizeof k->done); k->ndone = 0; }
}

bool ink_finish(ink_t *k)
{
    if (!ink_is_empty(&k->cur)) ink_next(k);
    return k->ndone > 0;
}

/* ---- smoothing ------------------------------------------------------------------------------------------- */

void ink_flatten(const ink_char_t *c, int stroke, float ox, float oy, float scale, ink_seg_fn seg, void *ctx)
{
    if (stroke < 0 || stroke >= c->nstrokes) return;
    int a = c->start[stroke];
    int b = stroke + 1 < c->nstrokes ? c->start[stroke + 1] : c->npts;
    if (b > c->npts) b = c->npts;
    int n = b - a;
    if (n <= 0) return;
    const ink_pt_t *p = &c->pts[a];
#define PX(i) (ox + (float)p[i].x * scale)
#define PY(i) (oy + (float)p[i].y * scale)
    if (n == 1) { seg(ctx, PX(0), PY(0), PX(0), PY(0)); return; }
    if (n == 2) { seg(ctx, PX(0), PY(0), PX(1), PY(1)); return; }
    float cx = PX(0), cy = PY(0);                              /* current end of the drawn path */
    for (int i = 1; i < n - 1; i++) {
        float mx = (PX(i) + PX(i + 1)) * 0.5f, my = (PY(i) + PY(i + 1)) * 0.5f;
        float qx = PX(i), qy = PY(i);                          /* control point */
        float px = cx, py = cy;
        const int steps = 4;
        for (int s = 1; s <= steps; s++) {
            float t = (float)s / steps, u = 1.f - t;
            float x = u * u * cx + 2.f * u * t * qx + t * t * mx;
            float y = u * u * cy + 2.f * u * t * qy + t * t * my;
            seg(ctx, px, py, x, y);
            px = x; py = y;
        }
        cx = mx; cy = my;
    }
    seg(ctx, cx, cy, PX(n - 1), PY(n - 1));
#undef PX
#undef PY
}

/* ---- coverage rasteriser (8-bit, anti-aliased, round caps) ------------------------------------------------------ */

typedef struct { uint8_t *cov; int w, h; float half; } raster_t;

static void raster_seg(void *ctx, float x0, float y0, float x1, float y1)
{
    raster_t *r = ctx;
    int bx0 = (int)floorf(fminf(x0, x1) - r->half - 1.f), bx1 = (int)ceilf(fmaxf(x0, x1) + r->half + 1.f);
    int by0 = (int)floorf(fminf(y0, y1) - r->half - 1.f), by1 = (int)ceilf(fmaxf(y0, y1) + r->half + 1.f);
    if (bx0 < 0) bx0 = 0;
    if (by0 < 0) by0 = 0;
    if (bx1 > r->w - 1) bx1 = r->w - 1;
    if (by1 > r->h - 1) by1 = r->h - 1;
    float vx = x1 - x0, vy = y1 - y0, len2 = vx * vx + vy * vy;
    for (int y = by0; y <= by1; y++)
        for (int x = bx0; x <= bx1; x++) {
            float px = (float)x + 0.5f - x0, py = (float)y + 0.5f - y0;
            float t = len2 > 0.f ? (px * vx + py * vy) / len2 : 0.f;
            t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
            float dx = px - t * vx, dy = py - t * vy;
            float cv = r->half + 0.5f - sqrtf(dx * dx + dy * dy);
            if (cv <= 0.f) continue;
            int v = cv >= 1.f ? 255 : (int)(cv * 255.f);
            uint8_t *o = &r->cov[y * r->w + x];
            if (v > *o) *o = (uint8_t)v;                       /* max, not add: crossings stay as dark as a single stroke */
        }
}

/* draw characters into a coverage buffer: `cell` px per character, per_row characters per row, stroke width `line` */
static void raster_chars(const ink_char_t *chars, int n, int cell, int per_row, float margin, float line, uint8_t *cov, int w, int h)
{
    raster_t r = { cov, w, h, line * 0.5f };
    float inner = (float)cell - 2.f * margin;
    float scale = inner / (float)INK_RANGE;
    for (int i = 0; i < n; i++) {
        float ox = (float)((i % per_row) * cell) + margin, oy = (float)((i / per_row) * cell) + margin;
        for (int s = 0; s < chars[i].nstrokes; s++) ink_flatten(&chars[i], s, ox, oy, scale, raster_seg, &r);
    }
}

bool ink_make_thumb(const ink_t *k, uint8_t *mask, int *w, int *h)
{
    int n = k->ndone;
    if (n <= 0) return false;
    int cols = n < INK_THUMB_PER_ROW ? n : INK_THUMB_PER_ROW;
    int rows = (n + INK_THUMB_PER_ROW - 1) / INK_THUMB_PER_ROW;
    if (rows > 3) rows = 3, n = 3 * INK_THUMB_PER_ROW;
    *w = cols * INK_THUMB_CELL;
    *h = rows * INK_THUMB_CELL;
    memset(mask, 0, (size_t)*w * *h);
    raster_chars(k->done, n, INK_THUMB_CELL, INK_THUMB_PER_ROW, 3.f, 2.6f, mask, *w, *h);
    return true;
}

/* ---- PNG ----------------------------------------------------------------------------------------------------- */

static uint32_t crc_table[256];
static bool crc_ready;

static uint32_t crc32_update(uint32_t crc, const uint8_t *d, size_t n)
{
    if (!crc_ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int j = 0; j < 8; j++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crc_table[i] = c;
        }
        crc_ready = true;
    }
    crc = ~crc;
    while (n--) crc = crc_table[(crc ^ *d++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static size_t put_chunk(uint8_t *out, const char *type, const uint8_t *data, size_t len)
{
    put32(out, (uint32_t)len);
    memcpy(out + 4, type, 4);
    if (len) memcpy(out + 8, data, len);
    put32(out + 8 + len, crc32_update(crc32_update(0, out + 4, 4), out + 8, len));
    return 12 + len;
}

bool ink_png_from_gray(const uint8_t *gray, int w, int h, uint8_t **out, size_t *len)
{
    size_t raw = (size_t)(w + 1) * h;                          /* filter byte 0 + row */
    size_t blocks = (raw + 65534) / 65535;
    size_t zlen = 2 + raw + blocks * 5 + 4;                    /* zlib header, stored blocks, adler32 */
    uint8_t *z = big_alloc(zlen);
    uint8_t *png = big_alloc(8 + 25 + 12 + zlen + 12);
    if (!z || !png) { free(z); free(png); return false; }

    size_t zp = 0;
    z[zp++] = 0x78; z[zp++] = 0x01;
    uint32_t a = 1, b = 0;
    size_t pos = 0;                                            /* position in the virtual raw stream */
    for (size_t blk = 0; blk < blocks; blk++) {
        size_t n = raw - pos > 65535 ? 65535 : raw - pos;
        z[zp++] = blk + 1 == blocks ? 1 : 0;
        z[zp++] = (uint8_t)(n & 0xFF); z[zp++] = (uint8_t)(n >> 8);
        z[zp++] = (uint8_t)(~n & 0xFF); z[zp++] = (uint8_t)((~n >> 8) & 0xFF);
        for (size_t i = 0; i < n; i++, pos++) {
            size_t row = pos / (size_t)(w + 1), col = pos % (size_t)(w + 1);
            uint8_t v = col == 0 ? 0 : gray[row * w + col - 1];
            z[zp++] = v;
            a = (a + v) % 65521; b = (b + a) % 65521;
        }
    }
    put32(z + zp, (b << 16) | a);
    zp += 4;

    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    memcpy(png, sig, 8);
    size_t o = 8;
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w); put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 0; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;      /* 8-bit grey, no interlace */
    o += put_chunk(png + o, "IHDR", ihdr, 13);
    o += put_chunk(png + o, "IDAT", z, zp);
    o += put_chunk(png + o, "IEND", NULL, 0);
    free(z);
    *out = png;
    *len = o;
    return true;
}

bool ink_make_png(const ink_t *k, uint8_t **png, size_t *len, int *w, int *h)
{
    int n = k->ndone;
    if (n <= 0) return false;
    int W = n * INK_PNG_CELL, H = INK_PNG_CELL;
    uint8_t *cov = big_alloc((size_t)W * H);
    if (!cov) return false;
    raster_chars(k->done, n, INK_PNG_CELL, n, 8.f, 6.5f, cov, W, H);
    for (size_t i = 0; i < (size_t)W * H; i++) cov[i] = (uint8_t)(255 - cov[i]);     /* black on white */
    bool ok = ink_png_from_gray(cov, W, H, png, len);
    free(cov);
    if (ok) { if (w) *w = W; if (h) *h = H; }
    return ok;
}

/* ---- thumbnail pool -------------------------------------------------------------------------------------------- */

typedef struct { uint16_t w, h; uint8_t a[INK_THUMB_MAX_W * INK_THUMB_MAX_H]; } thumb_t;
static thumb_t *s_pool;
static int s_next_slot;

int ink_thumb_store(const uint8_t *mask, int w, int h)
{
    if (!s_pool) s_pool = big_alloc(INK_THUMB_SLOTS * sizeof(thumb_t));
    if (!s_pool || w <= 0 || h <= 0 || w > INK_THUMB_MAX_W || h > INK_THUMB_MAX_H) return -1;
    int slot = s_next_slot;
    s_next_slot = (s_next_slot + 1) % INK_THUMB_SLOTS;
    s_pool[slot].w = (uint16_t)w;
    s_pool[slot].h = (uint16_t)h;
    memcpy(s_pool[slot].a, mask, (size_t)w * h);
    return slot;
}

const uint8_t *ink_thumb_get(int slot, int *w, int *h)
{
    if (!s_pool || slot < 0 || slot >= INK_THUMB_SLOTS || s_pool[slot].w == 0) return NULL;
    *w = s_pool[slot].w;
    *h = s_pool[slot].h;
    return s_pool[slot].a;
}
