#include "pics.h"
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
static void *big_alloc(size_t n) { void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM); return p ? p : malloc(n); }
#else
static void *big_alloc(size_t n) { return malloc(n); }
#endif

bool rgb565_fit(const uint16_t *src, int w, int h, int max_w, int max_h, uint16_t **out, int *ow, int *oh)
{
    if (w <= 0 || h <= 0 || max_w <= 0 || max_h <= 0) return false;
    int nw = w, nh = h;
    if (w > max_w || h > max_h) {
        if ((long)max_w * h <= (long)max_h * w) { nw = max_w; nh = (int)((long)h * max_w / w); }      /* width is the limit */
        else { nh = max_h; nw = (int)((long)w * max_h / h); }
        if (nw < 1) nw = 1;
        if (nh < 1) nh = 1;
    }
    uint16_t *o = big_alloc((size_t)nw * nh * 2);
    if (!o) return false;
    if (nw == w && nh == h) {
        memcpy(o, src, (size_t)w * h * 2);
    } else {
        for (int y = 0; y < nh; y++) {
            int sy0 = (int)((long)y * h / nh), sy1 = (int)((long)(y + 1) * h / nh);
            if (sy1 <= sy0) sy1 = sy0 + 1;
            for (int x = 0; x < nw; x++) {
                int sx0 = (int)((long)x * w / nw), sx1 = (int)((long)(x + 1) * w / nw);
                if (sx1 <= sx0) sx1 = sx0 + 1;
                unsigned r = 0, g = 0, b = 0, cnt = 0;
                for (int yy = sy0; yy < sy1; yy++)
                    for (int xx = sx0; xx < sx1; xx++) {
                        uint16_t v = src[(size_t)yy * w + xx];
                        v = (uint16_t)((v << 8) | (v >> 8));
                        r += (unsigned)((v >> 11) & 31) * 255 / 31;
                        g += (unsigned)((v >> 5) & 63) * 255 / 63;
                        b += (unsigned)(v & 31) * 255 / 31;
                        cnt++;
                    }
                r /= cnt; g /= cnt; b /= cnt;
                uint16_t v = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
                o[(size_t)y * nw + x] = (uint16_t)((v << 8) | (v >> 8));
            }
        }
    }
    *out = o; *ow = nw; *oh = nh;
    return true;
}

static pic_t s_pool[PICS_SLOTS];
static int s_next;
static int s_serial;

static void drop(pic_t *p)
{
    free(p->full);
    free(p->thumb);
    memset(p, 0, sizeof *p);
}

int pics_store(uint16_t *full, int w, int h)
{
    pic_t *p = &s_pool[s_next];
    uint16_t *thumb = NULL;
    int tw, th;
    if (!full || !rgb565_fit(full, w, h, PICS_THUMB_W, PICS_THUMB_H, &thumb, &tw, &th)) { free(full); return 0; }
    drop(p);
    s_next = (s_next + 1) % PICS_SLOTS;
    if (++s_serial <= 0 || s_serial > 65535) s_serial = 1;
    p->id = s_serial;
    p->full = full; p->fw = w; p->fh = h;
    p->thumb = thumb; p->tw = tw; p->th = th;
    return p->id;
}

const pic_t *pics_get(int id)
{
    if (id <= 0) return NULL;
    for (int i = 0; i < PICS_SLOTS; i++) if (s_pool[i].id == id) return &s_pool[i];
    return NULL;
}
