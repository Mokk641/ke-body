/* Host test for main/ink.c: stroke editing, smoothing, the PNG and the thumbnail. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ink.h"

static int fails;
#define CHECK(cond, ...) do { if (cond) printf("ok:   "); else { printf("FAIL: "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)

static void line(ink_t *k, int x0, int y0, int x1, int y1)
{
    ink_pen_down(k, x0, y0);
    for (int i = 1; i <= 10; i++) ink_pen_move(k, x0 + (x1 - x0) * i / 10, y0 + (y1 - y0) * i / 10);
    ink_pen_up(k);
}

typedef struct { int n; float minx, maxx, miny, maxy; float lastx, lasty; int jumps; } stat_t;
static void seg_stat(void *c, float x0, float y0, float x1, float y1)
{
    stat_t *s = c;
    if (s->n == 0) { s->minx = s->maxx = x0; s->miny = s->maxy = y0; }
    else if ((x0 - s->lastx) * (x0 - s->lastx) + (y0 - s->lasty) * (y0 - s->lasty) > 0.01f) s->jumps++;   /* path must be continuous */
    if (x1 < s->minx) s->minx = x1;
    if (x1 > s->maxx) s->maxx = x1;
    if (y1 < s->miny) s->miny = y1;
    if (y1 > s->maxy) s->maxy = y1;
    s->lastx = x1; s->lasty = y1; s->n++;
}

int main(void)
{
    ink_t *k = calloc(1, sizeof *k);

    CHECK(!ink_finish(k), "nothing to send when empty");
    line(k, 100, 500, 900, 500);
    line(k, 500, 100, 500, 900);
    CHECK(k->cur.nstrokes == 2 && k->cur.npts >= 10, "two strokes recorded (%d pts)", k->cur.npts);

    /* jitter below ~2 px is dropped */
    ink_pen_down(k, 300, 300);
    int before = k->cur.npts;
    ink_pen_move(k, 301, 301);
    CHECK(k->cur.npts == before, "sub-pixel jitter ignored");
    ink_pen_move(k, 340, 340);
    CHECK(k->cur.npts == before + 1, "real movement recorded");
    ink_pen_up(k);
    ink_undo(k);
    CHECK(k->cur.nstrokes == 2, "撤销一笔 removes exactly the last stroke");

    /* a dot */
    ink_pen_down(k, 700, 700); ink_pen_up(k);
    stat_t st = {0};
    ink_flatten(&k->cur, 2, 0, 0, 1, seg_stat, &st);
    CHECK(st.n == 1, "a tap is a single round dot");
    ink_undo(k);

    /* smoothing stays inside the hull of the points and is continuous */
    ink_pen_down(k, 100, 800);
    ink_pen_move(k, 200, 300); ink_pen_move(k, 400, 200); ink_pen_move(k, 700, 250); ink_pen_move(k, 900, 700);
    ink_pen_up(k);
    memset(&st, 0, sizeof st);
    ink_flatten(&k->cur, 2, 0, 0, 1, seg_stat, &st);
    CHECK(st.jumps == 0 && st.n > 10, "smoothed curve is continuous (%d segments)", st.n);
    CHECK(st.minx >= 100 - 1 && st.maxx <= 900 + 1 && st.miny >= 200 - 1 && st.maxy <= 800 + 1, "smoothed curve stays inside the control points' box");

    CHECK(ink_next(k) && k->ndone == 1 && ink_is_empty(&k->cur), "下一个 moves the character into the strip");
    CHECK(!ink_next(k), "下一个 on an empty pad does nothing");
    line(k, 200, 200, 800, 800);
    ink_next(k);
    CHECK(k->ndone == 2, "two characters in the strip");

    /* undo on an empty pad brings the last character back and removes its last stroke */
    ink_undo(k);
    CHECK(k->ndone == 1 && k->cur.nstrokes == 0, "undo on an empty pad reopens the last character (its only stroke removed)");
    line(k, 100, 100, 900, 100);
    ink_clear(k);
    CHECK(ink_is_empty(&k->cur) && k->ndone == 1, "清空 clears the pad only");
    ink_clear(k);
    CHECK(k->ndone == 0, "清空 on an empty pad clears the strip");

    /* full strip */
    for (int i = 0; i < INK_MAX_CHARS; i++) { line(k, 100, 100 + i * 20, 900, 900 - i * 20); ink_next(k); }
    line(k, 1, 1, 2, 2);
    CHECK(k->ndone == INK_MAX_CHARS && !ink_next(k), "strip holds %d characters, the 17th is refused", INK_MAX_CHARS);
    ink_reset(k);

    /* sentence -> PNG */
    line(k, 100, 500, 900, 500); line(k, 500, 100, 500, 900);
    ink_next(k);
    line(k, 150, 150, 850, 850); line(k, 850, 150, 150, 850);
    CHECK(ink_finish(k) && k->ndone == 2, "寄 commits the character still on the pad");
    uint8_t *png = NULL; size_t plen = 0; int w = 0, h = 0;
    CHECK(ink_make_png(k, &png, &plen, &w, &h), "PNG built");
    CHECK(w == 2 * INK_PNG_CELL && h == INK_PNG_CELL, "PNG is one row of cells: %dx%d", w, h);
    CHECK(plen > 8 && !memcmp(png, "\x89PNG\r\n\x1a\n", 8), "PNG signature (%u bytes)", (unsigned)plen);
    FILE *f = fopen(getenv("INK_PNG_OUT") ? getenv("INK_PNG_OUT") : "/dev/null", "wb");
    if (f) { fwrite(png, 1, plen, f); fclose(f); }
    free(png);

    /* thumbnail */
    uint8_t *mask = malloc(INK_HAND_W * INK_HAND_H);
    int tw, th;
    CHECK(ink_make_thumb(k, mask, &tw, &th) && tw == 64 && th == 32, "thumbnail %dx%d", tw, th);
    int ink_px = 0; for (int i = 0; i < tw * th; i++) if (mask[i] > 128) ink_px++;
    CHECK(ink_px > 100, "thumbnail has ink (%d px)", ink_px);
    for (int i = 0; i < 13; i++) { ink_reset(k); for (int j = 0; j <= i; j++) { line(k, 100, 100, 900, 900); ink_next(k); } }
    CHECK(ink_make_thumb(k, mask, &tw, &th) && tw == 192 && th == 96, "13 characters wrap to 3 rows of 6 at 32 px (%dx%d)", tw, th);
    /* 40 characters: smaller cells, still inside the box; and a multi-row PNG */
    ink_reset(k);
    for (int i = 0; i < INK_MAX_CHARS; i++) { line(k, 100, 100 + i * 15, 900, 900 - i * 15); ink_next(k); }
    CHECK(k->ndone == 40 && !ink_next(k), "40 characters fit, the 41st is refused");
    CHECK(ink_make_thumb(k, mask, &tw, &th) && tw <= INK_HAND_W && th <= INK_HAND_H && tw == 192 && th == 64, "40 characters -> thumbnail at 16 px, 12 per row, 4 rows (%dx%d)", tw, th);
    uint8_t *png2 = NULL; size_t plen2 = 0; int pw2, ph2;
    CHECK(ink_make_png(k, &png2, &plen2, &pw2, &ph2) && pw2 == 16 * INK_PNG_CELL && ph2 == 3 * INK_PNG_CELL, "40 characters -> PNG of 3 rows of 16 (%dx%d)", pw2, ph2);
    free(png2);
    ink_reset(k);
    for (int i = 0; i < 17; i++) { line(k, 100, 100, 900, 900); ink_next(k); }
    CHECK(ink_make_png(k, &png2, &plen2, &pw2, &ph2) && pw2 == 16 * INK_PNG_CELL && ph2 == 2 * INK_PNG_CELL, "17 characters wrap after 16 (%dx%d)", pw2, ph2);
    free(png2);
    ink_reset(k);
    for (int i = 0; i < 13; i++) { line(k, 100, 100, 900, 900); ink_next(k); }
    ink_make_thumb(k, mask, &tw, &th);
    int slot = ink_thumb_store(mask, tw, th);
    int gw, gh;
    const uint8_t *g = ink_thumb_get(slot, &gw, &gh);
    CHECK(slot > 0 && g && gw == tw && gh == th && !memcmp(g, mask, tw * th), "thumbnail pool round trip");
    int first = slot;
    for (int i = 0; i < INK_THUMB_SLOTS; i++) ink_thumb_store(mask, tw, th);
    CHECK(ink_thumb_get(first, &gw, &gh) == NULL, "an old thumbnail that was pushed out is gone (never replaced by a newer picture)");
    CHECK(ink_thumb_get(0, &gw, &gh) == NULL && ink_thumb_get(-5, &gw, &gh) == NULL, "id 0 / bad ids give NULL");
    free(mask);
    free(k);
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
