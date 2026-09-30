/* Host test for main/imgrot.c: quarter turns and the byte swap. */
#include <stdio.h>
#include <string.h>
#include "imgrot.h"

static int fails;
#define CHECK(cond, ...) do { if (cond) printf("ok:   "); else { printf("FAIL: "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)
static uint16_t sw(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }

int main(void)
{
    /* 3 wide, 2 tall:   1 2 3
     *                   4 5 6   */
    const uint16_t src[6] = { 1, 2, 3, 4, 5, 6 };
    uint16_t d[6];

    img_rotate_swap(src, 3, 2, d, 0);
    CHECK(d[0] == sw(1) && d[5] == sw(6), "0 deg keeps the order and swaps bytes");

    img_rotate_swap(src, 3, 2, d, 90);        /* clockwise -> 2 wide, 3 tall:  4 1 / 5 2 / 6 3 */
    uint16_t e90[6] = { 4, 1, 5, 2, 6, 3 };
    int ok = 1; for (int i = 0; i < 6; i++) ok &= d[i] == sw(e90[i]);
    CHECK(ok, "90 deg clockwise: top-left pixel ends up top-right");

    img_rotate_swap(src, 3, 2, d, 270);       /* counter-clockwise -> 3 6 / 2 5 / 1 4 */
    uint16_t e270[6] = { 3, 6, 2, 5, 1, 4 };
    ok = 1; for (int i = 0; i < 6; i++) ok &= d[i] == sw(e270[i]);
    CHECK(ok, "270 deg clockwise (= 90 counter-clockwise): top-right pixel ends up top-left");

    img_rotate_swap(src, 3, 2, d, 180);
    uint16_t e180[6] = { 6, 5, 4, 3, 2, 1 };
    ok = 1; for (int i = 0; i < 6; i++) ok &= d[i] == sw(e180[i]);
    CHECK(ok, "180 deg");

    /* four quarter turns give the original back */
    uint16_t a[6], b[6], c[6], f[6];
    img_rotate_swap(src, 3, 2, a, 90);
    for (int i = 0; i < 6; i++) a[i] = sw(a[i]);
    img_rotate_swap(a, 2, 3, b, 90);
    for (int i = 0; i < 6; i++) b[i] = sw(b[i]);
    img_rotate_swap(b, 3, 2, c, 90);
    for (int i = 0; i < 6; i++) c[i] = sw(c[i]);
    img_rotate_swap(c, 2, 3, f, 90);
    ok = 1; for (int i = 0; i < 6; i++) ok &= f[i] == sw(src[i]);
    CHECK(ok, "four 90 deg turns are the identity");

    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
