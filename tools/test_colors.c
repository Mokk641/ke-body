/* Host test: the colours in ui_colors.h really end up in the framebuffer as the intended RGB565 values,
 * byte-swapped exactly as the panel expects (the same format Waveshare's LVGL demo sends), and the optional
 * calibration does what it says. Run via tools/run_host_tests.sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "ui_render.h"
#include "ui_colors.h"
#include "colorcal.h"

static int fails;
#define CHECK(cond, ...) do { if (cond) printf("ok:   "); else { printf("FAIL: "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)

static uint16_t fb[320 * 480];

static uint16_t px(int x, int y) { return fb[y * gfx_width() + x]; }     /* byte-swapped 565 as stored */
static uint16_t be(uint16_t native) { return (uint16_t)((native << 8) | (native >> 8)); }

/* 565 -> 8-bit the way the panel's 5/6-bit inputs expand */
static void unpack(uint16_t native, int *r, int *g, int *b)
{
    *r = ((native >> 11) & 31) * 255 / 31;
    *g = ((native >> 5) & 63) * 255 / 63;
    *b = (native & 31) * 255 / 31;
}

static int close_to(uint16_t native, int r, int g, int b)
{
    int rr, gg, bb;
    unpack(native, &rr, &gg, &bb);
    return abs(rr - r) <= 8 && abs(gg - g) <= 4 && abs(bb - b) <= 8;
}

static void add(ui_state_t *s, int who, const char *t)
{
    chat_msg_t *m = &s->msgs[s->msg_count++];
    m->who = who;
    snprintf(m->text, sizeof m->text, "%s", t);
}

int main(void)
{
    /* 1. the macros are the specified colours (to within 565 precision) */
    CHECK(close_to(COL_D_BG, 0, 0, 0) && COL_D_BG == 0, "dark background is pure black");
    {   /* every GFX_GREY is exactly neutral once the panel expands 5/6/5 bits to 8 */
        int bad = 0;
        for (int v = 0; v < 256; v++) {
            int r, g, b;
            unpack(GFX_GREY(v), &r, &g, &b);
            if (abs(r - g) > 2 || abs(g - b) > 2 || r != b) bad++;
        }
        CHECK(bad == 0, "GFX_GREY stays neutral for all 256 levels (%d off)", bad);
        int worst = 0;
        for (int v = 0; v < 256; v++) {
            int r, g, b;
            unpack(GFX_GREY(v), &r, &g, &b);
            if (abs(r - v) > worst) worst = abs(r - v);
        }
        CHECK(worst <= 5, "GFX_GREY within %d levels of the requested grey (5-bit steps)", worst);
    }
    CHECK(GFX_RGB(255, 255, 255) == 0xFFFF && GFX_GREY(255) == 0xFFFF, "white is 0xFFFF");
    CHECK(close_to(COL_D_KE_BUBBLE, 0x26, 0x26, 0x28), "my bubble ~ #262628");
    CHECK(close_to(COL_D_HER_BUBBLE, 0x0A, 0x84, 0xFF), "her bubble ~ #0A84FF");
    CHECK(close_to(COL_D_BAR, 0x1C, 0x1C, 0x1E), "bars ~ #1C1C1E");
    CHECK(close_to(COL_D_CAP, 0x2C, 0x2C, 0x2E) && close_to(COL_D_CELL, 0x2C, 0x2C, 0x2E), "capsules / cells ~ #2C2C2E");
    CHECK(COL_D_KE_TEXT == GFX_RGB(255, 255, 255) && COL_D_HER_TEXT == GFX_RGB(255, 255, 255), "both text colours are white");
    CHECK(GFX_RGB(0xFF, 0x00, 0x00) == 0xF800 && GFX_RGB(0x00, 0xFF, 0x00) == 0x07E0 && GFX_RGB(0x00, 0x00, 0xFF) == 0x001F,
          "GFX_RGB packs R=bits 15..11, G=10..5, B=4..0 (0xF800 / 0x07E0 / 0x001F)");

    /* 2. what ui_render leaves in the framebuffer (byte-swapped) */
    gfx_init(fb, 320, 480);
    ui_state_t *s = calloc(1, sizeof *s);
    s->pressed = UI_HIT_NONE;
    s->theme = UI_THEME_DARK;
    s->screen = UI_SCREEN_CHAT;
    s->page_pos = 255;
    snprintf(s->face, sizeof s->face, "(—_—)");
    add(s, CHAT_KE, "在吗");
    add(s, CHAT_HER, "在的");
    ui_render(s);
    int H = gfx_height(), W = gfx_width();
    /* newest bubble (hers) sits at the bottom right above the bottom bar, mine above it on the left */
    int found_blue = 0, found_grey = 0, found_bar = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint16_t v = px(x, y);
            if (v == be(COL_D_HER_BUBBLE)) found_blue++;
            if (v == be(COL_D_KE_BUBBLE)) found_grey++;
            if (v == be(COL_D_BAR)) found_bar++;
        }
    CHECK(found_blue > 500, "her bubble pixels in the framebuffer are byte-swapped #0A84FF (%d px)", found_blue);
    CHECK(found_grey > 500, "my bubble pixels are byte-swapped #262628 (%d px)", found_grey);
    CHECK(found_bar > 3000, "top bar / bottom capsule pixels are byte-swapped #1C1C1E (%d px)", found_bar);
    CHECK(px(1, H / 2) == 0 && px(W - 1, H / 3) == 0, "space beside the bubbles is pure black");
    int old_green = 0;
    for (int i = 0; i < W * H; i++) if (fb[i] == be(GFX_RGB(0x2F, 0x4A, 0x3A))) old_green++;
    CHECK(old_green == 0, "no trace of the old ink-green / cream palette");

    /* 3. colour test screen shows the reference swatches */
    s->screen = UI_SCREEN_COLORTEST;
    ui_render(s);
    int sw_blue = 0;
    for (int i = 0; i < W * H; i++) if (fb[i] == be(GFX_RGB(0x0A, 0x84, 0xFF))) sw_blue++;
    CHECK(sw_blue > 1000, "colour test screen contains the #0A84FF swatch (%d px)", sw_blue);
    CHECK(ui_hit_test(s, 10, 10) == UI_HIT_TEST_EXIT, "any tap on the colour test leaves it");

    /* 4. calibration */
    colorcal_set(100, 100, 100, 100);
    CHECK(!colorcal_active(), "calibration is off by default");
    uint16_t src[4] = { be(GFX_RGB(255, 255, 255)), be(GFX_RGB(0, 0, 0)), be(COL_D_HER_BUBBLE), be(GFX_RGB(0x80, 0x80, 0x80)) }, dst[4];
    colorcal_apply(dst, src, 4);       /* identity LUTs even when "off" */
    CHECK(!memcmp(src, dst, sizeof src), "identity calibration leaves pixels untouched");
    colorcal_set(100, 100, 100, 80);
    CHECK(colorcal_active(), "blue gain 80%% turns it on");
    colorcal_apply(dst, src, 4);
    int r, g, b;
    unpack(be(dst[0]), &r, &g, &b);
    CHECK(r == 255 && g == 255 && b < 230 && b > 190, "white loses blue only (r%d g%d b%d)", r, g, b);
    CHECK(dst[1] == 0, "black stays black");
    colorcal_set(130, 100, 100, 100);
    colorcal_apply(dst, src, 4);
    unpack(be(dst[3]), &r, &g, &b);
    CHECK(r < 0x80 && g < 0x80 && b < 0x80, "gamma 1.3 darkens mid-grey (%d %d %d)", r, g, b);
    unpack(be(dst[0]), &r, &g, &b);
    CHECK(r == 255 && g == 255 && b == 255, "gamma keeps white");
    colorcal_set(10, 500, -3, 100);
    int c[4];
    colorcal_get(c);
    CHECK(c[0] == 50 && c[1] == 100 && c[2] == 40 && c[3] == 100, "out-of-range values are clamped (%d %d %d %d)", c[0], c[1], c[2], c[3]);

    free(s);
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
