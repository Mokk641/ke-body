/* Host test for main/png.c against the fixtures written by tools/make_test_pngs.py (real zlib output: dynamic Huffman
 * codes, all five row filters, every colour type / bit depth). Usage: test_png FIXTURE_DIR   */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "png.h"
#include "ink.h"

static int fails;
#define CHECK(cond, ...) do { if (cond) printf("ok:   "); else { printf("FAIL: "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)

static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)n + 1);
    *len = fread(b, 1, (size_t)n, f);
    fclose(f);
    return b;
}

/* expected RGBA (composited on black) as RGB565 byte-swapped, compared exactly for same-size decodes */
static void check_fixture(const char *dir, const char *name)
{
    char p[512];
    snprintf(p, sizeof p, "%s/%s.png", dir, name);
    size_t n;
    uint8_t *d = slurp(p, &n);
    snprintf(p, sizeof p, "%s/%s.txt", dir, name);
    FILE *t = fopen(p, "r");
    if (!d || !t) { CHECK(0, "%s: missing fixture", name); return; }
    int w, h;
    if (fscanf(t, "%d %d", &w, &h) != 2) { CHECK(0, "%s: bad txt", name); return; }
    int iw, ih;
    CHECK(png_info(d, n, &iw, &ih) && iw == w && ih == h, "%s: size %dx%d", name, w, h);
    uint16_t *out = NULL; int ow, oh;
    bool ok = png_decode_rgb565(d, n, 4096, 4096, &out, &ow, &oh);
    CHECK(ok && ow == w && oh == h, "%s: decodes at full size", name);
    int bad = 0;
    for (int y = 0; y < h && ok; y++)
        for (int x = 0; x < w; x++) {
            int r, g, b, a;
            if (fscanf(t, " %d,%d,%d,%d", &r, &g, &b, &a) != 4) { bad++; continue; }
            r = r * a / 255; g = g * a / 255; b = b * a / 255;
            uint16_t v = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            uint16_t e = (uint16_t)((v << 8) | (v >> 8));
            if (out[y * w + x] != e) bad++;
        }
    CHECK(ok && bad == 0, "%s: every pixel matches (%d wrong)", name, bad);
    free(out); free(d); fclose(t);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "png_fixtures";
    const char *names[] = { "gray8_f0", "gray8_f1", "gray8_f2", "gray8_f3", "gray8_f4", "gray8_f5", "rgb8_f5", "rgba8_f4", "graya8_f3",
                            "rgb16_f2", "pal4_trns", "gray1_f0", "gray2_f2", "gray8_big" };
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) check_fixture(dir, names[i]);

    /* shrinking */
    {
        char p[512]; size_t n;
        snprintf(p, sizeof p, "%s/gray8_big.png", dir);
        uint8_t *d = slurp(p, &n);
        uint16_t *o = NULL; int ow, oh;
        CHECK(png_decode_rgb565(d, n, 150, 150, &o, &ow, &oh) && ow == 150 && oh == 100, "300x200 shrinks to fit 150x150 as %dx%d", ow, oh);
        free(o);
        CHECK(png_decode_rgb565(d, n, 1000, 1000, &o, &ow, &oh) && ow == 300 && oh == 200, "a small picture is never enlarged");
        free(o);
        d[n / 2] ^= 0xFF;
        CHECK(!png_decode_rgb565(d, n, 1000, 1000, &o, &ow, &oh), "a damaged file is refused, not decoded into garbage");
        CHECK(!png_decode_rgb565((const uint8_t *)"not a png at all, no", 20, 100, 100, &o, &ow, &oh), "garbage is refused");
        free(d);
    }

    /* handwriting masks: any polarity gives ink = high values, the same blob */
    const char *inks[] = { "ink_bw", "ink_wb", "ink_alpha" };
    int centre[3], corner[3];
    for (int i = 0; i < 3; i++) {
        char p[512]; size_t n;
        snprintf(p, sizeof p, "%s/%s.png", dir, inks[i]);
        uint8_t *d = slurp(p, &n);
        uint8_t *m = NULL; int mw, mh;
        bool ok = png_decode_ink_mask(d, n, 80, 40, &m, &mw, &mh);
        CHECK(ok && mw == 80 && mh == 40, "%s: mask %dx%d", inks[i], mw, mh);
        centre[i] = ok ? m[20 * mw + 40] : -1;
        corner[i] = ok ? m[0] : -1;
        CHECK(centre[i] > 240 && corner[i] < 16, "%s: ink is high, paper is low (centre %d, corner %d)", inks[i], centre[i], corner[i]);
        free(m); free(d);
    }

    /* our own writer's PNG (stored blocks) reads back through the decoder */
    {
        static ink_t k;
        ink_pen_down(&k, 100, 500);
        for (int x = 150; x <= 900; x += 50) ink_pen_move(&k, x, 500);
        ink_pen_up(&k);
        ink_next(&k);
        uint8_t *png = NULL; size_t len = 0; int w, h;
        CHECK(ink_make_png(&k, &png, &len, &w, &h), "ink_make_png");
        uint8_t *m = NULL; int mw, mh;
        CHECK(png_decode_ink_mask(png, len, 500, 500, &m, &mw, &mh) && mw == w && mh == h, "the board's own PNG decodes again (%dx%d)", mw, mh);
        int ink_px = 0; for (int i = 0; i < mw * mh; i++) if (m[i] > 128) ink_px++;
        CHECK(ink_px > 300, "and the stroke is there (%d px)", ink_px);
        free(m); free(png);
    }
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
