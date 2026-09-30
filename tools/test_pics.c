/* Host test for main/pics.c: shrinking, the picture pool and its ids. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pics.h"

static int fails;
#define CHECK(cond, ...) do { if (cond) printf("ok:   "); else { printf("FAIL: "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)

static uint16_t *solid(int w, int h, uint16_t v)
{
    uint16_t *p = malloc((size_t)w * h * 2);
    for (int i = 0; i < w * h; i++) p[i] = v;
    return p;
}

int main(void)
{
    uint16_t red = 0x00F8;                       /* 0xF800 byte-swapped */
    uint16_t *src = solid(480, 320, red), *o = NULL;
    int ow, oh;
    CHECK(rgb565_fit(src, 480, 320, 200, 150, &o, &ow, &oh) && ow == 200 && oh == 133, "480x320 -> %dx%d inside 200x150, aspect kept", ow, oh);
    int bad = 0; for (int i = 0; i < ow * oh; i++) if (o[i] != red) bad++;
    CHECK(bad == 0, "a solid colour stays exactly that colour after averaging");
    free(o);
    CHECK(rgb565_fit(src, 480, 320, 1000, 1000, &o, &ow, &oh) && ow == 480 && oh == 320 && !memcmp(o, src, 480 * 320 * 2), "a picture that fits is copied, not enlarged");
    free(o);
    /* two-colour picture: the average of black and white halves is mid grey */
    uint16_t *bw = solid(64, 64, 0);
    for (int y = 0; y < 64; y++) for (int x = 32; x < 64; x++) bw[y * 64 + x] = 0xFFFF;
    CHECK(rgb565_fit(bw, 64, 64, 32, 32, &o, &ow, &oh) && ow == 32, "64x64 -> 32x32");
    CHECK(o[0] == 0 && o[31] == 0xFFFF, "the halves stay black and white away from the seam");
    free(o); free(bw);
    CHECK(!rgb565_fit(src, 0, 320, 10, 10, &o, &ow, &oh), "empty picture refused");

    int ids[5];
    for (int i = 0; i < 5; i++) ids[i] = pics_store(solid(300, 200, (uint16_t)(0x0800 * (i + 1))), 300, 200);
    CHECK(ids[0] > 0 && ids[4] > ids[0], "ids count up (%d .. %d)", ids[0], ids[4]);
    CHECK(pics_get(ids[0]) == NULL && pics_get(ids[1]) == NULL, "the two oldest of five are gone (only %d kept)", PICS_SLOTS);
    const pic_t *p = pics_get(ids[4]);
    CHECK(p && p->fw == 300 && p->fh == 200 && p->tw == 200 && p->th == 133 && p->thumb[0] == 0x0800 * 5, "newest picture and its thumbnail (%dx%d) are there", p ? p->tw : 0, p ? p->th : 0);
    CHECK(pics_get(0) == NULL && pics_get(-1) == NULL && pics_get(60000) == NULL, "unknown ids give NULL");
    CHECK(pics_store(NULL, 10, 10) == 0, "storing nothing fails cleanly");
    free(src);
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
