/* Host test of the real main/ui.c touch state machine + main/ui_render.c hit testing (phase 5 UI).
 * ESP-IDF pieces are stubbed in tools/hoststubs. Run:
 *
 *   tools/run_host_tests.sh
 *
 * Covers: page swipes (face <-> chat), hint / chevron / top-bar, panel (+, blank tap, phrases,
 * emoji, paging), camera icon, long-press talk, chat scrolling, camera/gallery screens, the new-message
 * notice face, the animation switches, the default emoji font coverage.
 * Regression from v4: the touch controller reports nothing on release, so the task called
 * ui_touch(false, 0, 0); the tap must be hit-tested at the last position seen while down.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "ui.h"
#include "ui_render.h"
#include "pics.h"
#include "gfx.h"
#include "fonts/fonts.h"
#include "app_actions.h"

/* ---- stubs the linker needs ---------------------------------------------- */
int64_t host_now_us;
void (*host_tick_cb)(void *);

static int s_rot_w = 480, s_rot_h = 320;
esp_err_t board_lcd_set_rotation(int r) { bool sw = r == 90 || r == 270; s_rot_w = sw ? 480 : 320; s_rot_h = sw ? 320 : 480; return ESP_OK; }
int board_lcd_width(void) { return s_rot_w; }
int board_lcd_height(void) { return s_rot_h; }
void board_lcd_flush(const uint16_t *fb) { (void)fb; }
void board_lcd_flush_rect(const uint16_t *fb, int x0, int y0, int x1, int y1) { (void)fb; (void)x0; (void)y0; (void)x1; (void)y1; }
static char s_rotate[8] = "270";
static char s_saved_anim[8][8];
bool settings_get_str(const char *k, char *out, unsigned n)
{
    if (strcmp(k, "rotate") == 0) { snprintf(out, n, "%s", s_rotate); return true; }
    return false;
}
esp_err_t settings_erase(const char *k) { (void)k; return ESP_OK; }
esp_err_t settings_set_str(const char *k, const char *v)
{
    if (!strcmp(k, "an_blush")) snprintf(s_saved_anim[0], 8, "%s", v);
    if (!strcmp(k, "an_blink")) snprintf(s_saved_anim[1], 8, "%s", v);
    if (!strcmp(k, "an_flash")) snprintf(s_saved_anim[2], 8, "%s", v);
    return ESP_OK;
}
static int n_new_msg;
void app_on_new_message(void) { n_new_msg++; }
static bool s_night;
bool light_night_active(void) { return s_night; }

/* ---- app callbacks recorded ----------------------------------------------- */
static int taps[16], n_taps, n_long_press, n_long_release, n_swipes, last_swipe;
void app_on_tap(int hit) { if (n_taps < 16) taps[n_taps++] = hit; }
void app_on_long_press(void) { n_long_press++; }
void app_on_long_release(void) { n_long_release++; }
void app_on_swipe(int dir) { n_swipes++; last_swipe = dir; }
void app_on_touch_activity(void) {}
static int n_ink_sent; static size_t last_ink_len; static uint8_t last_ink_sig[8];
void app_send_ink(const uint8_t *png, size_t len) { n_ink_sent++; last_ink_len = len; memcpy(last_ink_sig, png, 8); }

static void reset_counts(void) { n_taps = n_long_press = n_long_release = n_swipes = 0; }

/* ---- helpers ------------------------------------------------------------------ */
static int fails;
#define CHECK(cond, ...) do { if (cond) { printf("ok:   "); } else { printf("FAIL: "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)

static ui_state_t *st;
static void snap(void) { ui_get_state_copy(st); }

/* advance the 50 ms animation timer */
static void ticks(int n)
{
    for (int i = 0; i < n; i++) { host_now_us += 50000; if (host_tick_cb) host_tick_cb(NULL); }
}
static void settle(void) { ticks(12); }

/* what the touch task does: touch samples while down, then a release with NO coordinates */
static void press_release(int x, int y, int down_ms)
{
    int steps = down_ms / 30;
    if (steps < 2) steps = 2;
    for (int i = 0; i < steps; i++) { host_now_us += 30000; ui_touch(true, x, y); }
    host_now_us += 30000;
    ui_touch(false, 0, 0);
}

static void swipe(int x0, int y0, int x1, int y1)
{
    host_now_us += 30000; ui_touch(true, x0, y0);
    for (int i = 1; i <= 6; i++) { host_now_us += 30000; ui_touch(true, x0 + (x1 - x0) * i / 6, y0 + (y1 - y0) * i / 6); }
    host_now_us += 30000; ui_touch(false, 0, 0);
}

/* first point on the screen (coarse scan) whose hit id is `id` */
static bool find_hit(int id, int *ox, int *oy)
{
    snap();
    for (int y = 2; y < gfx_height(); y += 3)
        for (int x = 2; x < gfx_width(); x += 3)
            if (ui_hit_test(st, x, y) == id) { *ox = x; *oy = y; return true; }
    return false;
}

static bool tap_hit(int id)
{
    int x, y;
    if (!find_hit(id, &x, &y)) return false;
    host_now_us += 400000;                    /* a person does not tap the same thing twice within 250 ms */
    reset_counts();
    press_release(x, y, 90);
    return true;
}

static void to_face(void) { ui_set_screen(UI_SCREEN_FACE); ui_panel_set(false); settle(); }
static void to_chat(void) { ui_set_screen(UI_SCREEN_CHAT); settle(); }

int main(void)
{
    st = calloc(1, sizeof *st);
    ui_start();
    printf("screen %dx%d\n", gfx_width(), gfx_height());
    CHECK(gfx_width() == 480 && gfx_height() == 320, "landscape 480x320 after ui_start");

    snap();
    CHECK(st->screen == UI_SCREEN_FACE && st->page_pos == 0, "boots on the face page");
    CHECK(st->text_btn_n == 4 && st->emoji_btn_n == 10, "default buttons: %d phrases, %d emoji", st->text_btn_n, st->emoji_btn_n);
    CHECK(!strcmp(st->text_btn[0].text, "想你了") && !strcmp(st->text_btn[3].text, "晚安"), "phrases in the specified order");
    CHECK(!strcmp(st->emoji_btn[0].text, "(—ω—)") && !strcmp(st->emoji_btn[2].text, "(—▽—)♡") &&
          !strcmp(st->emoji_btn[6].text, "V(—ω—)V") && !strcmp(st->emoji_btn[9].text, "(—∀-)"), "emoji defaults in the specified order");
    {
        bool a, b, c, d, e;
        ui_anim_get("blink", &a); ui_anim_get("blush", &b); ui_anim_get("zzz", &c); ui_anim_get("shake", &d); ui_anim_get("flash", &e);
        CHECK(!a && b && c && d && !e, "animation defaults: blink off, blush on, zzz on, shake on, flash off");
    }

    /* every character of the default emoji has a glyph in the smallest emoji font, and in every face font used for them */
    {
        char buf[1024] = "";
        for (int i = 0; i < st->emoji_btn_n; i++) strcat(buf, st->emoji_btn[i].text);
        const kb_font_t *fs[] = { &kb_font_face96, &kb_font_face64, &kb_font_face44, &kb_font_face30, &kb_font_face18, &kb_font_face13 };
        const char *nm[] = { "96", "64", "44", "30", "18", "13" };
        for (int i = 0; i < 6; i++) CHECK(gfx_missing_glyphs(fs[i], buf) == 0, "default emoji fully covered by face%s", nm[i]);
        CHECK(gfx_missing_glyphs(&kb_font_face96, "(—_—)") == 0 && gfx_missing_glyphs(&kb_font_face96, "(—o—)") == 0, "base + notice face covered by face96");
        CHECK(gfx_missing_glyphs(&kb_font_small14, "上滑聊天 克") == 0, "hint / title text covered by small14");
    }

    /* ---- face page ---- */
    to_face();
    int fx, fy;
    reset_counts();
    press_release(240, 100, 90);
    CHECK(n_taps == 1 && taps[0] == UI_HIT_FACE, "tap on the face -> face hit (blush)");

    reset_counts();
    swipe(240, 260, 240, 150);
    settle();
    snap();
    CHECK(st->screen == UI_SCREEN_CHAT && st->page_pos == 255 && n_taps == 0, "swipe up on the face page -> chat page (screen=%d pos=%d)", st->screen, st->page_pos);

    to_face();
    CHECK(find_hit(UI_HIT_HINT, &fx, &fy), "hint hit area exists on the face page");
    tap_hit(UI_HIT_HINT);
    settle(); snap();
    CHECK(st->screen == UI_SCREEN_CHAT && n_taps == 0, "tap on 「⌃ 上滑聊天」-> chat page");

    to_face();
    swipe(240, 150, 240, 250);
    snap();
    CHECK(st->screen == UI_SCREEN_FACE, "swipe down on the face page does nothing");

    to_face();
    reset_counts();
    for (int i = 0; i < 25; i++) { host_now_us += 30000; ui_touch(true, 240, 100); }
    host_now_us += 30000; ui_touch(false, 0, 0);
    CHECK(n_long_press == 1 && n_long_release == 1 && n_taps == 0, "long press on the face -> talk start/stop");

    /* ---- chat page ---- */
    to_chat();
    snap();
    CHECK(tap_hit(UI_HIT_TOP_BACK), "⌄ hit area exists");
    settle(); snap();
    CHECK(st->screen == UI_SCREEN_FACE, "tap ⌄ -> back to the face page");

    to_chat();
    find_hit(UI_HIT_TOPBAR, &fx, &fy);
    swipe(fx, fy, fx, fy + 90);
    settle(); snap();
    CHECK(st->screen == UI_SCREEN_FACE, "swipe down from the top bar -> face page");

    to_chat();
    swipe(240, 120, 240, 220);
    settle(); snap();
    CHECK(st->screen == UI_SCREEN_FACE, "swipe down on a short (empty) chat -> face page");

    /* messages: mine on the left with the face at send time, hers on the right */
    to_chat();
    ui_set_face("(—▽—)");
    ui_chat_add(CHAT_KE, "早安");
    ui_set_face("(—_—)");
    ui_chat_add(CHAT_HER, "嗯");
    snap();
    CHECK(st->msg_count == 2 && st->msgs[0].who == CHAT_KE && !strcmp(st->msgs[0].face, "(—▽—)") && st->msgs[1].who == CHAT_HER,
          "Ke's message carries the face at that moment as avatar");

    /* + button and panel */
    CHECK(tap_hit(UI_HIT_PLUS), "+ hit area exists");
    ticks(3);
    snap();
    CHECK(st->panel_open && n_taps == 0, "tap + opens the panel");
    settle(); snap();
    CHECK(st->panel_pos == 255, "panel slides fully out");
    tap_hit(UI_HIT_PLUS);
    settle(); snap();
    CHECK(!st->panel_open && st->panel_pos == 0, "tap + again collapses the panel");

    tap_hit(UI_HIT_PLUS); settle();
    CHECK(tap_hit(UI_HIT_CHAT), "message area still hittable above the open panel");
    settle(); snap();
    CHECK(!st->panel_open && n_taps == 0, "tap on blank area above the panel collapses it");

    /* phrases and emoji */
    tap_hit(UI_HIT_PLUS); settle();
    for (int i = 0; i < 4; i++) {
        CHECK(tap_hit(UI_HIT_TEXT_BTN0 + i) && n_taps == 1 && taps[0] == UI_HIT_TEXT_BTN0 + i, "phrase button %d taps correctly", i);
    }
    snap();
    CHECK(st->panel_open, "panel stays open after a phrase tap");
    int per = ui_emoji_per_page();
    printf("      %d emoji per page, %d page(s)\n", per, ui_emoji_pages(st));
    for (int i = 0; i < per && i < st->emoji_btn_n; i++) {
        CHECK(tap_hit(UI_HIT_EMOJI_BTN0 + i) && n_taps == 1 && taps[0] == UI_HIT_EMOJI_BTN0 + i, "emoji button %d taps correctly", i);
    }
    /* release at (0,0): the v4 regression */
    {
        int x, y;
        find_hit(UI_HIT_TEXT_BTN0, &x, &y);
        reset_counts();
        press_release(x, y, 120);
        CHECK(n_taps == 1 && taps[0] == UI_HIT_TEXT_BTN0, "release reported at (0,0) still fires the pressed button");
        reset_counts();
        host_now_us += 30000; ui_touch(true, x, y);
        host_now_us += 30000; ui_touch(true, x + 60, y);
        host_now_us += 30000; ui_touch(false, 0, 0);
        CHECK(n_taps == 0, "dragging off a button cancels the tap");
    }

    /* paging with many emoji */
    {
        char json[1200] = "{\"text\":[\"a\"],\"emoji\":[";
        for (int i = 0; i < 25; i++) { char t[24]; snprintf(t, sizeof t, "%s\"(e%d)\"", i ? "," : "", i); strcat(json, t); }
        strcat(json, "]}");
        CHECK(ui_set_buttons_json(json) == ESP_OK, "25 emoji accepted");
        snap();
        per = ui_emoji_per_page();
        int pages = ui_emoji_pages(st);
        CHECK(pages >= 2, "25 emoji need %d pages (%d per page)", pages, per);
        CHECK(st->emoji_page == 0, "starts on page 0");
        int px, py;
        find_hit(UI_HIT_PANEL, &px, &py);
        swipe(400, py, 100, py);
        settle(); snap();
        CHECK(st->emoji_page == 1, "swipe left on the panel -> next emoji page");
        CHECK(tap_hit(UI_HIT_EMOJI_BTN0 + per) && taps[0] == UI_HIT_EMOJI_BTN0 + per, "first emoji of page 2 has absolute index %d", per);
        swipe(100, py, 400, py);
        snap();
        CHECK(st->emoji_page == 0, "swipe right -> previous page");
        swipe(100, py, 400, py);
        snap();
        CHECK(st->emoji_page == 0, "cannot page before the first");
        ui_reset_buttons();
        snap();
        CHECK(st->emoji_btn_n == 10 && st->text_btn_n == 4, "ui_reset_buttons restores the defaults");
    }
    ui_panel_set(false); settle();

    /* camera icon */
    CHECK(tap_hit(UI_HIT_CAM_BTN) && n_taps == 1 && taps[0] == UI_HIT_CAM_BTN, "camera icon in the bottom bar taps");

    /* long press in the message area: talk */
    reset_counts();
    for (int i = 0; i < 25; i++) { host_now_us += 30000; ui_touch(true, 240, 150); }
    host_now_us += 30000; ui_touch(false, 0, 0);
    CHECK(n_long_press == 1 && n_long_release == 1, "long press in the chat area -> talk");

    /* chat scrolling with a long log; swipe down in an overflowing chat scrolls, does not leave */
    for (int i = 0; i < 12; i++) ui_chat_add(i % 2 ? CHAT_KE : CHAT_HER, "一条比较长的消息，用来把聊天区撑满，这样才能滚动，看看效果怎么样。");
    settle();
    reset_counts();
    swipe(240, 100, 240, 200);
    settle(); snap();
    CHECK(st->screen == UI_SCREEN_CHAT && st->scroll > 0 && n_taps == 0, "drag down in a long chat scrolls to older messages (scroll=%d)", st->scroll);
    int scrolled = st->scroll;
    swipe(240, 200, 240, 150);
    snap();
    CHECK(st->scroll < scrolled && st->scroll >= 0, "drag up scrolls back (%d -> %d)", scrolled, st->scroll);
    for (int r = 0; r < 20; r++) swipe(240, 70, 240, 240);
    snap();
    int max_scroll = ui_chat_content_height(st) - ui_chat_area_height(st);
    CHECK(st->scroll == max_scroll && max_scroll > 0, "scroll stops exactly at the oldest message (scroll=%d, max=%d)", st->scroll, max_scroll);
    ui_chat_add(CHAT_HER, "新消息");
    snap();
    CHECK(st->scroll == 0, "a new message jumps back to the bottom");
    CHECK(st->slide_dy > 0, "new bubble starts below its place (slide_dy=%d)", st->slide_dy);
    ticks(8); snap();
    CHECK(st->slide_dy == 0, "bubble slide-in finishes");
    /* top bar swipe still returns from a long chat */
    find_hit(UI_HIT_TOPBAR, &fx, &fy);
    swipe(fx, fy, fx, fy + 90);
    settle(); snap();
    CHECK(st->screen == UI_SCREEN_FACE, "top-bar swipe returns from a long chat");

    /* ---- new-message behaviour ---- */
    to_face();
    ui_set_face("(—_—)");
    int before = n_new_msg;
    ui_set_say("想你了呀");
    snap();
    CHECK(!strcmp(st->face, "(—o—)"), "new message on the face page: face becomes (—o—) (%s)", st->face);
    CHECK(!strcmp(ui_latest_ke_text(st), "想你了呀") && st->line_alpha < 255, "latest line set and starts fading in (alpha %d)", st->line_alpha);
    CHECK(n_new_msg == before + 1, "app notified (chime hook)");
    ticks(8); snap();
    CHECK(!strcmp(st->face, "(—o—)"), "still (—o—) before ~1 s");
    ticks(16); snap();
    CHECK(!strcmp(st->face, "(—_—)") && st->line_alpha == 255, "face reverts after ~1 s, line fully faded in");

    to_chat();
    ui_set_say("在吗");
    snap();
    CHECK(strcmp(st->face, "(—o—)") != 0 && st->flash == 0, "new message on the chat page: no face change, no border flash");

    to_face();
    ui_set_sleeping(true);
    ui_set_say("睡了吗");
    snap();
    CHECK(strstr(st->face, "zzz") != NULL || st->sleeping, "sleeping face is kept when a message arrives");
    ui_set_sleeping(false);

    /* ---- animation switches ---- */
    {
        ui_state_t *a = st;
        bool on;
        CHECK(ui_anim_set("flash", true) && ui_anim_get("flash", &on) && on && !strcmp(s_saved_anim[2], "on"), "anim flash on (saved)");
        to_face();
        ui_set_say("闪一下");
        ticks(1);
        snap();
        CHECK(a->flash > 0, "flash on -> border flashes on a new message");
        ui_anim_set("flash", false);
        ticks(4);
        to_face();
        ui_set_say("不闪");
        snap();
        CHECK(a->flash == 0, "flash off -> no border");
        CHECK(!ui_anim_set("nope", true), "unknown animation name rejected");
        CHECK(ui_anim_set("blush", false) && !strcmp(s_saved_anim[0], "off"), "anim blush off (saved)");
        ui_anim_set("blush", true);
        ui_anim_set("blink", true);
        ticks(200);
        ui_anim_set("blink", false);
        snap();
        CHECK(!st->blink, "blink off leaves the eyes open");
    }

    /* ---- theme: default auto = white by day, dark during the night schedule ---- */
    snap();
    CHECK(!strcmp(ui_get_theme(), "auto") && st->theme == UI_THEME_LIGHT, "default theme mode is auto and it is light by day");
    s_night = true; ticks(25); snap();
    CHECK(st->theme == UI_THEME_DARK, "night schedule active -> dark");
    s_night = false; ticks(25); snap();
    CHECK(st->theme == UI_THEME_LIGHT, "morning -> light again");
    ui_set_theme("dark"); s_night = false; ticks(25); snap();
    CHECK(!strcmp(ui_get_theme(), "dark") && st->theme == UI_THEME_DARK, "theme dark stays dark by day");
    ui_set_theme("light"); s_night = true; ticks(25); snap();
    CHECK(st->theme == UI_THEME_LIGHT, "theme light stays light at night");
    CHECK(ui_set_theme("purple") != ESP_OK, "unknown theme rejected");
    ui_set_theme("auto"); s_night = false; ticks(25);

    /* ---- transparent bottom strip: chat underneath, only the two icons are buttons ---- */
    to_chat();
    snap();
    CHECK(ui_hit_test(st, 240, gfx_height() - 10) == UI_HIT_CHAT, "bottom strip between the icons belongs to the chat");
    CHECK(ui_hit_test(st, 20, gfx_height() - 10) == UI_HIT_PLUS && ui_hit_test(st, gfx_width() - 20, gfx_height() - 10) == UI_HIT_CAM_BTN,
          "+ at the bottom left, camera at the bottom right");
    CHECK(ui_hit_test(st, 20, 20) == UI_HIT_TOP_BACK && ui_hit_test(st, 70, 20) == UI_HIT_TOPBAR, "top bar: ⌄ at the far left, face + name are part of the bar");

    /* ---- handwriting ---- */
    to_chat();
    tap_hit(UI_HIT_PLUS); settle();
    CHECK(tap_hit(UI_HIT_INK_OPEN) && n_taps == 0, "the 手写 capsule in the + panel is a button");
    snap();
    CHECK(st->screen == UI_SCREEN_INK && st->ink != NULL, "tap 手写 -> handwriting page");
    int padx, pady, pads;
    ui_ink_pad_rect(&padx, &pady, &pads);
    CHECK(pads >= 250, "the writing square is big (%d px)", pads);
    /* write one stroke: finger down, slide, up; the controller reports no coordinates on release */
    host_now_us += 30000; ui_touch(true, padx + pads / 4, pady + pads / 2);
    for (int i = 1; i <= 12; i++) { host_now_us += 8000; ui_touch(true, padx + pads / 4 + i * pads / 24, pady + pads / 2 + (i % 3) * 6); }
    host_now_us += 8000; ui_touch(false, 0, 0);
    snap();
    CHECK(st->ink->cur.nstrokes == 1 && st->ink->cur.npts >= 8 && n_taps == 0 && !st->ink->pen_down, "one stroke drawn (%d points), no tap, pen lifted", st->ink->cur.npts);
    CHECK(st->ink->cur.pts[0].x < 300 && st->ink->cur.pts[0].y > 400 && st->ink->cur.pts[0].y < 600, "points are normalised to the pad (first at %d,%d)", st->ink->cur.pts[0].x, st->ink->cur.pts[0].y);
    /* a long slow press does not turn into a voice recording */
    reset_counts();
    for (int i = 0; i < 25; i++) { host_now_us += 30000; ui_touch(true, padx + pads / 2, pady + pads / 2); }
    host_now_us += 30000; ui_touch(false, 0, 0);
    snap();
    CHECK(n_long_press == 0 && st->ink->cur.nstrokes == 2, "holding on the pad makes a dot, not a recording");
    /* dragging off the pad keeps writing but clamped to the edge */
    host_now_us += 30000; ui_touch(true, padx + pads / 2, pady + pads / 2);
    host_now_us += 30000; ui_touch(true, padx + pads + 50, pady + pads / 2);
    host_now_us += 30000; ui_touch(false, 0, 0);
    snap();
    CHECK(st->ink->cur.nstrokes == 3 && st->ink->cur.pts[st->ink->cur.npts - 1].x == INK_RANGE - 1, "a stroke that leaves the pad is clamped to its edge");

    CHECK(tap_hit(UI_HIT_INK_UNDO), "撤销一笔 button");
    snap();
    CHECK(st->ink->cur.nstrokes == 2, "撤销一笔 removed the last stroke");
    tap_hit(UI_HIT_INK_NEXT);
    snap();
    CHECK(st->ink->ndone == 1 && st->ink->cur.nstrokes == 0, "下一个 moved the character up into the strip");
    tap_hit(UI_HIT_INK_SEND);
    CHECK(n_ink_sent == 1, "寄 sent the committed character as one PNG");
    n_ink_sent = 0;
    snap();
    CHECK(st->screen == UI_SCREEN_CHAT && st->ink->ndone == 0, "after 寄: back in the chat, paper cleared");
    CHECK(st->msg_count > 0 && st->msgs[st->msg_count - 1].ink_id > 0 && st->msgs[st->msg_count - 1].who == CHAT_HER, "her side shows the handwriting thumbnail");
    {
        int tw2, th2;
        CHECK(ink_thumb_get(st->msgs[st->msg_count - 1].ink_id, &tw2, &th2) != NULL, "thumbnail is in the pool (%dx%d)", tw2, th2);
    }
    /* second round: write, tap 寄 directly (auto-commit), then empty 寄 */
    ui_ink_open();
    ui_ink_pad_rect(&padx, &pady, &pads);
    host_now_us += 30000; ui_touch(true, padx + 30, pady + 30);
    host_now_us += 8000; ui_touch(true, padx + 200, pady + 200);
    host_now_us += 8000; ui_touch(false, 0, 0);
    reset_counts();
    tap_hit(UI_HIT_INK_SEND);
    CHECK(n_ink_sent == 1 && last_ink_len > 100 && !memcmp(last_ink_sig, "\x89PNG\r\n\x1a\n", 8), "寄 commits the character on the pad and sends one PNG (%u bytes)", (unsigned)last_ink_len);
    ui_ink_open();
    n_ink_sent = 0;
    tap_hit(UI_HIT_INK_SEND);
    snap();
    CHECK(n_ink_sent == 0 && st->screen == UI_SCREEN_INK, "寄 on an empty page sends nothing and stays");
    /* greyed while a send is in progress */
    host_now_us += 30000; ui_touch(true, padx + 30, pady + 30);
    host_now_us += 8000; ui_touch(true, padx + 200, pady + 200);
    host_now_us += 8000; ui_touch(false, 0, 0);
    ui_set_sending(true);
    tap_hit(UI_HIT_INK_SEND);
    CHECK(n_ink_sent == 0, "寄 is ignored while another send is in progress");
    ui_set_sending(false);
    CHECK(tap_hit(UI_HIT_INK_BACK), "back button");
    snap();
    CHECK(st->screen == UI_SCREEN_CHAT && st->ink->cur.nstrokes == 1, "back keeps the writing");
    ui_ink_open();
    tap_hit(UI_HIT_INK_CLEAR);
    snap();
    CHECK(st->ink->cur.nstrokes == 0, "清空 clears the pad");
    ui_set_screen(UI_SCREEN_CHAT);
    settle();

    /* ---- handwriting: long sentences, scrollable strip ---- */
    ui_ink_open();
    ui_ink_pad_rect(&padx, &pady, &pads);
    for (int i = 0; i < 25; i++) {
        host_now_us += 400000; ui_touch(true, padx + 20 + i * 5, pady + 30);
        host_now_us += 8000;   ui_touch(true, padx + 120 + i * 3, pady + 150);
        host_now_us += 8000;   ui_touch(false, 0, 0);
        tap_hit(UI_HIT_INK_NEXT);
    }
    snap();
    CHECK(st->ink->ndone == 25, "25 characters in one sentence (limit is %d)", INK_MAX_CHARS);
    int cap = ui_ink_strip_cap(), cell = ui_ink_strip_cell();
    CHECK(cap < 25 && cap >= 6, "the strip shows %d at a time, so it has to scroll", cap);
    CHECK(st->ink_scroll == 0, "the strip follows the newest character");
    {
        int stx = 100, sty = 20;
        CHECK(ui_hit_test(st, stx, sty) == UI_HIT_INK_STRIP, "the strip is touchable");
        host_now_us += 400000;
        ui_touch(true, stx, sty);
        for (int i = 1; i <= 8; i++) { host_now_us += 8000; ui_touch(true, stx + i * cell / 2, sty); }
        host_now_us += 8000; ui_touch(false, 0, 0);
        snap();
        CHECK(st->ink_scroll >= 3 && st->ink_scroll <= 5, "dragging the strip right scrolls back to earlier characters (%d)", st->ink_scroll);
        host_now_us += 400000;
        ui_touch(true, stx + 4 * cell, sty);
        for (int i = 1; i <= 40; i++) { host_now_us += 8000; ui_touch(true, stx + 4 * cell - i * cell / 2, sty); }
        host_now_us += 8000; ui_touch(false, 0, 0);
        snap();
        CHECK(st->ink_scroll == 0, "dragging left brings it back to the newest, and not beyond");
        host_now_us += 400000;
        ui_touch(true, stx, sty);
        for (int i = 1; i <= 200; i++) { host_now_us += 8000; ui_touch(true, stx + i * cell, sty); }
        host_now_us += 8000; ui_touch(false, 0, 0);
        snap();
        CHECK(st->ink_scroll == st->ink->ndone - cap, "the strip stops at the first character (%d)", st->ink_scroll);
    }
    for (int i = 0; i < 20; i++) {                        /* fill it up: 40 and no more */
        host_now_us += 400000; ui_touch(true, padx + 30, pady + 30);
        host_now_us += 8000;   ui_touch(true, padx + 90, pady + 90);
        host_now_us += 8000;   ui_touch(false, 0, 0);
        tap_hit(UI_HIT_INK_NEXT);
    }
    snap();
    CHECK(st->ink->ndone == INK_MAX_CHARS, "the sentence stops at %d characters (%d)", INK_MAX_CHARS, st->ink->ndone);
    n_ink_sent = 0;
    tap_hit(UI_HIT_INK_SEND);
    CHECK(n_ink_sent == 1 && last_ink_len > 1000, "40 characters go out as one PNG (%u bytes)", (unsigned)last_ink_len);
    snap();
    CHECK(st->screen == UI_SCREEN_CHAT && st->ink->ndone == 0 && st->ink_scroll == 0, "and the page is clean afterwards");
    {
        int before = st->msg_count;
        int nm = n_new_msg;
        uint8_t m[64 * 32];
        memset(m, 255, sizeof m);
        int slot = ink_thumb_store(m, 64, 32);
        ui_ke_ink(slot);
        snap();
        CHECK(st->msg_count == before + 1 && st->msgs[st->msg_count - 1].who == CHAT_KE && st->msgs[st->msg_count - 1].ink_id == slot,
              "handwriting from Ke arrives as a picture bubble on his side");
        CHECK(n_new_msg == nm + 1, "and raises the usual new-message alert");
    }
    to_chat();

    /* ---- pictures from Ke: bubble, tap for full screen, tap to leave ---- */
    to_chat();
    {
        uint16_t *px = malloc(120 * 80 * 2);
        for (int i = 0; i < 120 * 80; i++) px[i] = 0x1234;
        int id = pics_store(px, 120, 80);
        ui_ke_picture(id);
        ticks(8);
        snap();
        int last = st->msg_count - 1;
        CHECK(st->msgs[last].who == CHAT_KE && st->msgs[last].pic_id == id, "a picture from Ke is a bubble on his side");
        int hx = -1, hy = -1;
        for (int y = 60; y < gfx_height() - 50 && hx < 0; y += 3)
            for (int x = 4; x < gfx_width() / 2; x += 3)
                if (ui_hit_test(st, x, y) == UI_HIT_PIC0 + last) { hx = x; hy = y; break; }
        CHECK(hx >= 0, "the bubble can be tapped (hit id %d)", UI_HIT_PIC0 + last);
        reset_counts();
        host_now_us += 400000;
        press_release(hx, hy, 60);
        CHECK(n_taps == 1 && taps[0] == UI_HIT_PIC0 + last, "tap on the picture -> the app is asked to show it");
        /* dragging on a picture scrolls the chat instead */
        reset_counts();
        for (int i = 0; i < 12; i++) ui_chat_add(CHAT_HER, "填满聊天记录，好让它能上下滚动。填满聊天记录，好让它能上下滚动。");
        ticks(8); snap();
        int hx2 = -1, hy2 = -1;
        int pl = st->msg_count - 1;
        (void)pl;
        ui_set_screen(UI_SCREEN_VIEWER);
        snap();
        CHECK(ui_hit_test(st, 5, 5) == UI_HIT_VIEW_EXIT && ui_hit_test(st, 200, 100) == UI_HIT_VIEW_EXIT, "in the viewer every spot is 'back'");
        reset_counts();
        host_now_us += 400000;
        press_release(200, 100, 60);
        snap();
        CHECK(st->screen == UI_SCREEN_CHAT && n_taps == 0, "tap in the viewer returns to the chat");
        (void)hx2; (void)hy2;
    }

    /* ---- bigger touch areas near the bottom ---- */
    {
        ui_set_screen(UI_SCREEN_INK);
        snap();
        int W = gfx_width(), H = gfx_height();
        int pxx, pyy, ss;
        ui_ink_pad_rect(&pxx, &pyy, &ss);
        int seek_y_send = -1, seek_y_low = -1;
        for (int y = H - 1; y > 0; y--) if (ui_hit_test(st, W - 20, y) == UI_HIT_INK_SEND) { seek_y_send = y; break; }
        for (int y = 0; y < H; y++) if (ui_hit_test(st, W - 20, y) == UI_HIT_INK_SEND) { seek_y_low = y; break; }
        CHECK(seek_y_send > 0 && seek_y_send - seek_y_low >= 46 + 12, "寄 touch area is taller than the 40-46 px button (%d px)", seek_y_send - seek_y_low + 1);
        /* landscape column: every button has a touch area and none overlaps another */
        int prev = -1, changes = 0;
        for (int y = pyy; y < H; y++) {
            int h = ui_hit_test(st, W - 20, y);
            if (h != prev) { changes++; prev = h; }
        }
        CHECK(changes <= 9, "landscape button column has clean, non-overlapping touch areas (%d transitions)", changes);
        to_chat();
        snap();
        CHECK(ui_hit_test(st, 20, gfx_height() - 44 - 8) == UI_HIT_PLUS && ui_hit_test(st, gfx_width() - 20, gfx_height() - 44 - 8) == UI_HIT_CAM_BTN,
              "the + and camera touch areas reach 10 px above the bottom strip");
        ui_set_screen(UI_SCREEN_CAMERA);
        snap();
        int cam_lo = -1, cam_hi = -1;
        for (int y = 0; y < gfx_height(); y++) if (ui_hit_test(st, 80, y) == UI_HIT_CAM_VIEW && y > gfx_height() / 2) { if (cam_lo < 0) cam_lo = y; cam_hi = y; }
        CHECK(cam_hi - cam_lo + 1 >= 30 + 16, "camera buttons have 8 px more touch area above and below (%d px)", cam_hi - cam_lo + 1);
        to_chat();
    }

    /* ---- edge touches: a rolling finger must still count as a tap (v6.4) ---- */
    to_chat();
    snap();
    CHECK(ui_hit_test(st, 10, 8) == UI_HIT_TOP_BACK, "top-left corner is the ⌄ button");
    reset_counts();
    host_now_us += 30000; ui_touch(true, 10, 8);
    host_now_us += 30000; ui_touch(true, 14, 20);                 /* the finger rolls 12 px, then 20 px from where it landed */
    host_now_us += 30000; ui_touch(true, 12, 28);
    host_now_us += 30000; ui_touch(false, 0, 0);
    settle(); snap();
    CHECK(st->screen == UI_SCREEN_FACE, "⌄ at the very top edge works although the finger moved 20 px (no drag)");

    to_chat();
    /* first sample lands beside the button, the finger ends on it */
    snap();
    CHECK(ui_hit_test(st, 45, 20) == UI_HIT_TOPBAR && ui_hit_test(st, 30, 20) == UI_HIT_TOP_BACK, "the ⌄ button ends at x=36");
    host_now_us += 30000; ui_touch(true, 45, 20);
    host_now_us += 30000; ui_touch(true, 30, 22);
    host_now_us += 30000; ui_touch(false, 0, 0);
    settle(); snap();
    CHECK(st->screen == UI_SCREEN_FACE, "a touch that starts next to a button and lifts on it counts for the button");

    /* one contact split in two by the controller: the second half is ignored */
    to_chat();
    reset_counts();
    press_release(20, gfx_height() - 6, 60);                        /* + at the very bottom row */
    host_now_us += 90000;
    press_release(20, gfx_height() - 6, 60);
    snap();
    CHECK(st->panel_open, "+ at the bottom row opens the panel, and a second tap 90 ms later is ignored (one contact)");
    host_now_us += 400000;
    press_release(20, gfx_height() - 6, 60);
    snap();
    CHECK(!st->panel_open, "a deliberate second tap later closes it again");
    ui_panel_set(false); settle();

    /* the log lines: they must not crash and each release says why */
    ui_set_touchlog(true);
    press_release(240, 100, 60);
    press_release(20, 8, 60);
    to_chat();
    ui_set_touchlog(false);

    /* ---- touchlog: a marker follows the finger ---- */
    ui_set_touchlog(true);
    host_now_us += 30000; ui_touch(true, 200, 100);
    host_now_us += 30000; ui_touch(true, 210, 110);
    snap();
    CHECK(st->dot_ms > 0 && st->dot_x == 210 && st->dot_y == 110, "touchlog on: marker at the touch (%d,%d)", st->dot_x, st->dot_y);
    host_now_us += 30000; ui_touch(false, 0, 0);
    ticks(25); snap();
    CHECK(st->dot_ms <= 0, "marker disappears about a second after release");
    ui_set_touchlog(false);

    /* ---- sending: buttons are ignored while a message / photo is on its way ---- */
    to_chat();
    tap_hit(UI_HIT_PLUS); settle();
    ui_set_sending(true);
    snap();
    CHECK(st->sending, "sending flag set");
    CHECK(tap_hit(UI_HIT_TEXT_BTN0) && n_taps == 0, "phrase tap ignored while sending");
    CHECK(tap_hit(UI_HIT_EMOJI_BTN0) && n_taps == 0, "emoji tap ignored while sending");
    ui_set_sending(false);
    CHECK(tap_hit(UI_HIT_TEXT_BTN0) && n_taps == 1, "phrase tap works again afterwards");
    ui_panel_set(false); settle();

    /* ---- colour test pattern: tap anywhere leaves it ---- */
    ui_set_screen(UI_SCREEN_COLORTEST);
    reset_counts();
    press_release(200, 150, 90);
    snap();
    CHECK(st->screen == UI_SCREEN_FACE && n_taps == 0, "tap on the colour test returns to the face page");

    /* ---- camera screen ---- */
    ui_set_screen(UI_SCREEN_CAMERA);
    snap();
    static const struct { int x; int hit; const char *name; } cam_taps[] = {
        { 80, UI_HIT_CAM_VIEW, "拍照 button" }, { 240, UI_HIT_CAM_GALLERY, "相册 button" }, { 400, UI_HIT_CAM_BACK, "返回 button" },
        { 240, UI_HIT_CAM_VIEW, "viewfinder" },
    };
    for (unsigned i = 0; i < sizeof cam_taps / sizeof cam_taps[0]; i++) {
        int y = (i == 3) ? 150 : 295;
        reset_counts();
        press_release(cam_taps[i].x, y, 90);
        CHECK(n_taps == 1 && taps[0] == cam_taps[i].hit, "camera screen: %s -> hit %d (got %d)", cam_taps[i].name, cam_taps[i].hit, n_taps ? taps[0] : -1);
    }
    reset_counts();
    for (int i = 0; i < 25; i++) { host_now_us += 30000; ui_touch(true, 240, 150); }
    host_now_us += 30000; ui_touch(false, 0, 0);
    CHECK(n_long_press == 0 && n_taps == 1, "holding the viewfinder does not start talking");

    /* ---- gallery ---- */
    ui_set_screen(UI_SCREEN_GALLERY);
    static const struct { int x; int hit; const char *name; } gal_taps[] = {
        { 80, UI_HIT_GAL_DELETE, "删除" }, { 240, UI_HIT_GAL_SEND, "寄给克" }, { 400, UI_HIT_CAM_BACK, "返回" },
    };
    for (unsigned i = 0; i < sizeof gal_taps / sizeof gal_taps[0]; i++) {
        reset_counts();
        press_release(gal_taps[i].x, 295, 90);
        CHECK(n_taps == 1 && taps[0] == gal_taps[i].hit, "gallery: %s -> hit %d (got %d)", gal_taps[i].name, gal_taps[i].hit, n_taps ? taps[0] : -1);
    }
    reset_counts();
    swipe(350, 150, 250, 150);
    CHECK(n_swipes == 1 && last_swipe == 1 && n_taps == 0, "swipe left in the gallery = next photo");
    reset_counts();
    swipe(150, 150, 250, 150);
    CHECK(n_swipes == 1 && last_swipe == -1 && n_taps == 0, "swipe right in the gallery = previous photo");
    reset_counts();
    press_release(240, 150, 90);
    CHECK(n_swipes == 0 && n_taps == 1 && taps[0] == UI_HIT_GAL_VIEW, "plain tap on the photo is a tap");

    /* ---- portrait (auto-rotate / rotate 0) still lays out ---- */
    ui_set_screen(UI_SCREEN_CHAT);
    board_lcd_set_rotation(0);
    ui_refresh_after_rotation();
    settle();
    CHECK(gfx_width() == 320 && gfx_height() == 480, "portrait 320x480");
    CHECK(tap_hit(UI_HIT_PLUS), "portrait: + hit exists");
    settle();
    tap_hit(UI_HIT_PLUS);
    settle();
    CHECK(find_hit(UI_HIT_CAM_BTN, &fx, &fy) && fy > 400, "portrait: camera icon in the bottom bar (y=%d)", fy);

    free(st);
    printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
