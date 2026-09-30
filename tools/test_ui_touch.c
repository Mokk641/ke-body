/* Host test of the real main/ui.c touch state machine + main/ui_render.c hit testing.
 * ESP-IDF pieces are stubbed in tools/hoststubs. Run:
 *
 *   tools/run_host_tests.sh
 *
 * Regression for the v4 bug: the touch controller reports nothing on release, so
 * the touch task called ui_touch(false, 0, 0); the tap was then hit-tested at
 * (0,0), which is the face, not the button that was pressed, and every tap was
 * swallowed.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "ui.h"
#include "ui_render.h"
#include "gfx.h"
#include "app_actions.h"

/* ---- stubs the linker needs ---------------------------------------------- */
int64_t host_now_us;
void (*host_tick_cb)(void *);

static int s_rot_w = 480, s_rot_h = 320;
esp_err_t board_lcd_set_rotation(int r) { bool sw = r == 90 || r == 270; s_rot_w = sw ? 480 : 320; s_rot_h = sw ? 320 : 480; return ESP_OK; }
int board_lcd_width(void) { return s_rot_w; }
int board_lcd_height(void) { return s_rot_h; }
void board_lcd_flush(const uint16_t *fb) { (void)fb; }
bool settings_get_str(const char *k, char *out, unsigned n)
{
    if (strcmp(k, "rotate") == 0) { snprintf(out, n, "270"); return true; }
    return false;
}
esp_err_t settings_set_str(const char *k, const char *v) { (void)k; (void)v; return ESP_OK; }
void app_on_new_message(void) {}

/* ---- app callbacks recorded ----------------------------------------------- */
static int taps[16], n_taps, n_long_press, n_long_release, n_swipes, last_swipe;
void app_on_tap(int hit) { if (n_taps < 16) taps[n_taps++] = hit; }
void app_on_long_press(void) { n_long_press++; }
void app_on_long_release(void) { n_long_release++; }
void app_on_swipe(int dir) { n_swipes++; last_swipe = dir; }
void app_on_touch_activity(void) {}

static void reset_counts(void) { n_taps = n_long_press = n_long_release = n_swipes = 0; }

/* ---- helpers ------------------------------------------------------------------ */
static int fails;
#define CHECK(cond, ...) do { if (cond) { printf("ok:   "); } else { printf("FAIL: "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)

/* what the touch task does: touch samples while down, then a release with NO coordinates */
static void press_release(int x, int y, int down_ms, bool old_task_behaviour)
{
    int steps = down_ms / 30;
    if (steps < 2) steps = 2;
    for (int i = 0; i < steps; i++) {
        host_now_us += 30000;
        ui_touch(true, x, y);
    }
    host_now_us += 30000;
    if (old_task_behaviour) ui_touch(false, 0, 0);     /* v4 touch_task: x,y left at 0 */
    else ui_touch(false, x, y);
}

int main(void)
{
    ui_start();                       /* loads defaults: rotation 270 -> 480x320, 4 text + 4 emoji buttons */
    printf("screen %dx%d\n", gfx_width(), gfx_height());
    CHECK(gfx_width() == 480 && gfx_height() == 320, "landscape 480x320 after ui_start");

    ui_state_t *st = calloc(1, sizeof *st);
    ui_get_state_copy(st);
    CHECK(st->text_btn_n == 4 && st->emoji_btn_n == 4, "default buttons loaded (%d text, %d emoji)", st->text_btn_n, st->emoji_btn_n);

    /* ---- layout sanity for the coordinates she reported (y 261..283 in rot270 480x320) ---- */
    int row1 = 0, row2 = 0, gap = 0, other = 0;
    for (int y = 261; y <= 283; y++) {
        for (int x = 10; x < 470; x += 5) {
            int h = ui_hit_test(st, x, y);
            if (h >= UI_HIT_TEXT_BTN0 && h < UI_HIT_EMOJI_BTN0) row1++;
            else if (h >= UI_HIT_EMOJI_BTN0) row2++;
            else if (h == UI_HIT_CAM_BTN) row1++;
            else if (h == UI_HIT_CHAT || h == UI_HIT_NONE) gap++;
            else other++;
        }
    }
    printf("      hits for y=261..283: text row %d, emoji row %d, gaps %d, other %d\n", row1, row2, gap, other);
    CHECK(row1 > 0 && other == 0, "the reported touch band lands on the button rows");

    /* ---- text button at its centre ---- */
    int bx = 20, by = 262;
    int hit = ui_hit_test(st, bx, by);
    CHECK(hit == UI_HIT_TEXT_BTN0, "first text button at (%d,%d) -> hit %d", bx, by, hit);

    reset_counts();
    press_release(bx, by, 120, true);                  /* exactly what v4 did */
    CHECK(n_taps == 1 && taps[0] == UI_HIT_TEXT_BTN0, "tap with release at (0,0) still fires button 0 (taps=%d first=%d)", n_taps, n_taps ? taps[0] : -1);

    reset_counts();
    press_release(bx, by, 120, false);
    CHECK(n_taps == 1 && taps[0] == UI_HIT_TEXT_BTN0, "tap with release at the real position fires button 0");

    /* every button, every screen position that maps to it */
    int wrong = 0, total = 0;
    for (int y = 250; y < 312; y += 3) {
        for (int x = 8; x < 472; x += 7) {
            int expect = ui_hit_test(st, x, y);
            if (expect < UI_HIT_TEXT_BTN0 && expect != UI_HIT_CAM_BTN) continue;
            reset_counts();
            press_release(x, y, 90, true);
            total++;
            if (n_taps != 1 || taps[0] != expect) wrong++;
        }
    }
    CHECK(wrong == 0, "all %d sampled button positions tap correctly (wrong=%d)", total, wrong);

    /* emoji row */
    int ex = 40, ey = 295;
    reset_counts();
    press_release(ex, ey, 90, true);
    CHECK(n_taps == 1 && taps[0] == UI_HIT_EMOJI_BTN0, "first emoji button taps (hit %d)", n_taps ? taps[0] : -1);

    /* camera button (after the last text button) */
    int cx = 420, cy = 262;   /* last button of the text row = camera */
    reset_counts();
    press_release(cx, cy, 90, true);
    CHECK(n_taps == 1 && taps[0] == UI_HIT_CAM_BTN, "camera button taps (hit %d)", n_taps ? taps[0] : -1);

    /* tap on the face */
    reset_counts();
    press_release(240, 30, 90, true);
    CHECK(n_taps == 1 && taps[0] == UI_HIT_FACE, "tap on the face -> face (hit %d)", n_taps ? taps[0] : -1);

    /* press a button, slide away, release elsewhere: no tap */
    reset_counts();
    host_now_us += 30000; ui_touch(true, bx, by);
    host_now_us += 30000; ui_touch(true, bx + 60, by);
    host_now_us += 30000; ui_touch(false, 0, 0);
    CHECK(n_taps == 0, "dragging off a button cancels the tap");

    /* long press on the face: talk, and no tap on release */
    reset_counts();
    for (int i = 0; i < 25; i++) { host_now_us += 30000; ui_touch(true, 240, 30); }     /* 750 ms */
    host_now_us += 30000; ui_touch(false, 0, 0);
    CHECK(n_long_press == 1 && n_long_release == 1 && n_taps == 0, "long press on the face -> talk start/stop (press=%d release=%d taps=%d)", n_long_press, n_long_release, n_taps);

    /* long press on a button does not start talking */
    reset_counts();
    for (int i = 0; i < 25; i++) { host_now_us += 30000; ui_touch(true, bx, by); }
    host_now_us += 30000; ui_touch(false, 0, 0);
    CHECK(n_long_press == 0 && n_taps == 1, "long press on a button = one tap, no talking");

    /* drag in the chat area scrolls and does not tap */
    for (int i = 0; i < 12; i++) ui_chat_add(CHAT_KE, "一条比较长的消息，用来把聊天区撑满，这样才能滚动，看看效果怎么样。");
    reset_counts();
    host_now_us += 30000; ui_touch(true, 240, 120);
    for (int y = 120; y <= 200; y += 10) { host_now_us += 30000; ui_touch(true, 240, y); }   /* finger moves down 80 px */
    host_now_us += 30000; ui_touch(false, 0, 0);
    ui_get_state_copy(st);
    int scrolled = st->scroll;
    CHECK(n_taps == 0 && scrolled > 0 && scrolled <= 80, "dragging down shows older messages (scroll=%d) and does not tap", scrolled);

    host_now_us += 30000; ui_touch(true, 240, 200);
    for (int y = 200; y >= 150; y -= 10) { host_now_us += 30000; ui_touch(true, 240, y); }   /* finger moves up 50 px */
    host_now_us += 30000; ui_touch(false, 0, 0);
    ui_get_state_copy(st);
    CHECK(st->scroll < scrolled && st->scroll >= 0, "dragging up scrolls back toward the newest (scroll %d -> %d)", scrolled, st->scroll);

    for (int round = 0; round < 20; round++) {                      /* 20 x 170 px of finger travel, far beyond the content */
        host_now_us += 30000; ui_touch(true, 240, 70);
        for (int y = 70; y <= 240; y += 10) { host_now_us += 30000; ui_touch(true, 240, y); }
        host_now_us += 30000; ui_touch(false, 0, 0);
    }
    ui_get_state_copy(st);
    int max_scroll = ui_chat_content_height(st) - ui_chat_area_height();
    CHECK(st->scroll == max_scroll && max_scroll > 0, "scroll stops exactly at the oldest message (scroll=%d, max=%d)", st->scroll, max_scroll);

    ui_chat_add(CHAT_HER, "新消息");
    ui_get_state_copy(st);
    CHECK(st->scroll == 0, "a new message jumps back to the bottom");

    /* ---- camera screen: buttons along the bottom row, tap the viewfinder to shoot ---- */
    ui_set_screen(UI_SCREEN_CAMERA);
    ui_get_state_copy(st);
    static const struct { int x; int hit; const char *name; } cam_taps[] = {
        { 80, UI_HIT_CAM_VIEW, "拍照 button" }, { 240, UI_HIT_CAM_GALLERY, "相册 button" }, { 400, UI_HIT_CAM_BACK, "返回 button" },
        { 240, UI_HIT_CAM_VIEW, "viewfinder" },
    };
    for (unsigned i = 0; i < sizeof cam_taps / sizeof cam_taps[0]; i++) {
        int y = (i == 3) ? 150 : 295;
        reset_counts();
        press_release(cam_taps[i].x, y, 90, true);
        CHECK(n_taps == 1 && taps[0] == cam_taps[i].hit, "camera screen: %s -> hit %d (got %d)", cam_taps[i].name, cam_taps[i].hit, n_taps ? taps[0] : -1);
    }
    reset_counts();
    for (int i = 0; i < 25; i++) { host_now_us += 30000; ui_touch(true, 240, 150); }
    host_now_us += 30000; ui_touch(false, 0, 0);
    CHECK(n_long_press == 0 && n_taps == 1, "holding the viewfinder does not start talking");

    /* ---- gallery: buttons and left/right swipes ---- */
    ui_set_screen(UI_SCREEN_GALLERY);
    static const struct { int x; int hit; const char *name; } gal_taps[] = {
        { 80, UI_HIT_GAL_DELETE, "删除" }, { 240, UI_HIT_GAL_SEND, "寄给克" }, { 400, UI_HIT_CAM_BACK, "返回" },
    };
    for (unsigned i = 0; i < sizeof gal_taps / sizeof gal_taps[0]; i++) {
        reset_counts();
        press_release(gal_taps[i].x, 295, 90, true);
        CHECK(n_taps == 1 && taps[0] == gal_taps[i].hit, "gallery: %s button -> hit %d (got %d)", gal_taps[i].name, gal_taps[i].hit, n_taps ? taps[0] : -1);
    }
    reset_counts();
    host_now_us += 30000; ui_touch(true, 350, 150);
    for (int x = 350; x >= 250; x -= 20) { host_now_us += 30000; ui_touch(true, x, 150); }
    host_now_us += 30000; ui_touch(false, 0, 0);
    CHECK(n_swipes == 1 && last_swipe == 1 && n_taps == 0, "swipe left in the gallery = next photo (swipes=%d dir=%d)", n_swipes, last_swipe);
    reset_counts();
    host_now_us += 30000; ui_touch(true, 150, 150);
    for (int x = 150; x <= 250; x += 20) { host_now_us += 30000; ui_touch(true, x, 150); }
    host_now_us += 30000; ui_touch(false, 0, 0);
    CHECK(n_swipes == 1 && last_swipe == -1 && n_taps == 0, "swipe right in the gallery = previous photo (swipes=%d dir=%d)", n_swipes, last_swipe);
    reset_counts();
    press_release(240, 150, 90, true);
    CHECK(n_swipes == 0 && n_taps == 1 && taps[0] == UI_HIT_GAL_VIEW, "plain tap on the photo is a tap, not a swipe");

    free(st);
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
