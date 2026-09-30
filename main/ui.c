#include "ui.h"
#include "ui_render.h"
#include "gfx.h"
#include "board.h"
#include "settings.h"
#include "light.h"
#include "app_actions.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "ui";

#define BLUSH_FACE     "(—//—)"
#define SLEEP_FACE     "(—_—)"
#define NOTICE_FACE    "(—o—)"      /* the face flips to this for a second when a message arrives */
#define KEY_ROTATE     "rotate"
#define MUSIC_FACE "(—ω—)♪"
#define KEY_THEME      "theme_mode"   /* light | dark | auto (v6.1: new key, so an old saved "dark" does not stick) */
#define KEY_BUTTONS    "buttons"
#define KEY_ANIM       "anim"       /* master switch; per-animation keys are an_<name> */
#define DEFAULT_ROTATION 90
#define DEFAULT_BUTTONS  "{\"text\":[\"想你了\",\"抱抱\",\"在干嘛\",\"晚安\"]," \
    "\"emoji\":[\"(—ω—)\",\"(—//—)\",\"(—▽—)♡\",\"(—ε—)\",\"(—_—)♡\",\"(—︵—)\",\"V(—ω—)V\",\"(—o—)\",\"(=ω=)\",\"(—∀-)\"]," \
    "\"shake\":\"想你了\"}"
#define TICK_MS        50
#define LONG_PRESS_MS  500
#define DRAG_PX        10
#define SWIPE_PX       40
#define PAGE_STEP      58           /* page_pos per tick: ~220 ms for a full slide */
#define PANEL_STEP     51           /* ~250 ms */
#define NOTICE_MS      1000
#define SLIDE_MS       250
#define SLIDE_PX       44
#define BLUSH_MS       2200

enum { AN_BLINK, AN_BLUSH, AN_ZZZ, AN_SHAKE, AN_FLASH, AN_N };
static const char *const AN_NAME[AN_N] = { "blink", "blush", "zzz", "shake", "flash" };
static const bool AN_DEFAULT[AN_N] = { false, true, true, true, false };   /* no random blinking, no border flash */

static SemaphoreHandle_t s_lock;      /* protects s_state and friends */
static ink_t *s_ink;                  /* handwriting strokes (PSRAM) */
static SemaphoreHandle_t s_fb_lock;   /* protects framebuffer + gfx dimensions */
static SemaphoreHandle_t s_dirty;
static ui_state_t *s_state;           /* what is drawn (PSRAM) */
static ui_state_t *s_snap;            /* copy for the render task (PSRAM) */
static char s_base_face[UI_FACE_BUF];
static char s_override_face[UI_FACE_BUF];
static char s_base_corner[48];
static char s_override_corner[48];
static char s_shake_text[UI_BTN_TEXT_LEN] = "想你了";
static char s_buttons_json[BUTTONS_JSON_MAX] = DEFAULT_BUTTONS;
static uint16_t *s_fb;
static int s_rotation = DEFAULT_ROTATION;
static bool s_anim_master = true;
static bool s_an[AN_N];
static esp_timer_handle_t s_tick_timer;

/* animation state (ms countdowns, driven by the 50 ms tick) */
static int s_blush_ms;          /* > 0 while blushing */
static int s_blink_ms;          /* > 0 while eyes closed */
static int s_next_blink_ms;     /* countdown to next blink */
static int s_shake_ms;
static int s_flash_ms;
static int s_toast_ms;
static int s_notice_ms;         /* > 0 while the "message arrived" face is shown */
static int s_slide_ms;          /* > 0 while the newest bubble slides in */
static bool s_sleeping;

/* touch state */
static bool s_down;
static int s_down_x, s_down_y, s_last_y, s_last_x;
static int64_t s_down_t;
static int s_hit;               /* hit id at touch down */
static bool s_dragging, s_long_fired, s_axis_v;

/* ---- helpers --------------------------------------------------------------- */

static void copy_limited(char *dst, size_t dst_size, const char *src, int max_chars)
{
    size_t out = 0;
    int n = 0;
    const char *p = src;
    while (*p && n < max_chars) {
        const char *q = p;
        uint32_t cp = gfx_utf8_next(&q);
        size_t len = (size_t)(q - p);
        if (cp == 0xFFFD && len == 1) { p = q; continue; }
        if (cp == '\r') { p = q; continue; }
        if (out + len >= dst_size) break;
        memcpy(dst + out, p, len);
        out += len;
        n++;
        p = q;
    }
    dst[out] = 0;
}

static float ease01(float t)
{
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    return t * t * (3.f - 2.f * t);
}

static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }
static volatile int s_full_dirty = 1;   /* something besides new ink points changed: redraw the whole screen */
static int s_inc_n;                     /* render task: points of the current handwriting character already on the screen */
static bool s_touchlog;
static void mark_dirty(void) { __atomic_store_n(&s_full_dirty, 1, __ATOMIC_SEQ_CST); xSemaphoreGive(s_dirty); }
static void mark_ink_dirty(void) { xSemaphoreGive(s_dirty); }      /* only new points: the render task draws just those */

static bool s_music_face;                /* a song is playing: the face hums along */

static bool an_on(int k) { return s_anim_master && s_an[k]; }

/* recompute drawn face/corner from base + overrides; lock held */
static void recompute(void)
{
    const char *face = s_override_face[0] ? s_override_face
                     : (s_notice_ms > 0 ? NOTICE_FACE : (s_sleeping ? SLEEP_FACE : (s_music_face ? MUSIC_FACE : s_base_face)));
    strcpy(s_state->face, face);
    strcpy(s_state->corner, s_override_corner[0] ? s_override_corner : s_base_corner);
    s_state->sleeping = s_sleeping;
}

static void clamp_scroll(void)
{
    int max = ui_chat_content_height(s_state) - ui_chat_area_height(s_state);
    if (max < 0) max = 0;
    if (s_state->scroll > max) s_state->scroll = max;
    if (s_state->scroll < 0) s_state->scroll = 0;
}

enum { THEME_LIGHT = 0, THEME_DARK, THEME_AUTO };
static int s_theme_mode = THEME_AUTO;      /* default: white by day, dark during the night schedule */

/* palette for the current mode; `auto` follows the night schedule of light.c (light until the clock is known) */
static int theme_for_mode(void)
{
    if (s_theme_mode == THEME_DARK) return UI_THEME_DARK;
    if (s_theme_mode == THEME_LIGHT) return UI_THEME_LIGHT;
    return light_night_active() ? UI_THEME_DARK : UI_THEME_LIGHT;
}

static void render_task(void *arg)
{
    for (;;) {
        xSemaphoreTake(s_dirty, portMAX_DELAY);
        int full = __atomic_exchange_n(&s_full_dirty, 0, __ATOMIC_SEQ_CST);
        lock();
        memcpy(s_snap, s_state, sizeof(ui_state_t));
        unlock();
        xSemaphoreTake(s_fb_lock, portMAX_DELAY);
        const bool ink_page = s_snap->screen == UI_SCREEN_INK && s_snap->ink;
        int npts = ink_page ? s_snap->ink->cur.npts : 0;
        int x0, y0, x1, y1;
        if (!full && ink_page && npts >= s_inc_n) {
            /* only new pen points: put them on the screen as they are and push just that rectangle */
            if (ui_render_ink_incremental(s_snap, s_inc_n, npts, &x0, &y0, &x1, &y1)) board_lcd_flush_rect(s_fb, x0, y0, x1, y1);
            s_inc_n = npts;
        } else {
            ui_render(s_snap);
            board_lcd_flush(s_fb);
            s_inc_n = npts;                   /* read before drawing: points that arrive meanwhile are drawn twice, never lost */
        }
        xSemaphoreGive(s_fb_lock);
    }
}

static int step_toward(int cur, int target, int step)
{
    if (cur < target) { cur += step; if (cur > target) cur = target; }
    else if (cur > target) { cur -= step; if (cur < target) cur = target; }
    return cur;
}

static int s_theme_tick;

/* 50 ms tick: page / panel slides, bubble slide-in, fades, blush, shake, z's, blink, flash, toast */
static void tick_cb(void *arg)
{
    bool redraw = false;
    lock();

    if (s_theme_mode == THEME_AUTO && ++s_theme_tick >= 20) {          /* once a second */
        s_theme_tick = 0;
        int t = theme_for_mode();
        if (t != s_state->theme) { s_state->theme = t; redraw = true; }
    }

    if (s_state->dot_ms > 0) {
        s_state->dot_ms -= TICK_MS;
        if (s_state->dot_ms <= 0) redraw = true;
    }

    if (s_toast_ms > 0) {
        s_toast_ms -= TICK_MS;
        if (s_toast_ms <= 0) { s_state->toast[0] = 0; redraw = true; }
    }

    /* page slide: FACE <-> CHAT (the camera and gallery screens sit "after" the chat page) */
    int page_target = s_state->screen == UI_SCREEN_FACE ? 0 : 255;
    if (s_state->page_pos != page_target) {
        s_state->page_pos = step_toward(s_state->page_pos, page_target, PAGE_STEP);
        redraw = true;
    }
    int panel_target = s_state->panel_open ? 255 : 0;
    if (s_state->panel_pos != panel_target) {
        s_state->panel_pos = step_toward(s_state->panel_pos, panel_target, PANEL_STEP);
        clamp_scroll();
        redraw = true;
    }

    /* newest bubble slides in from below */
    if (s_slide_ms > 0) {
        s_slide_ms -= TICK_MS;
        float prog = 1.f - (float)(s_slide_ms > 0 ? s_slide_ms : 0) / SLIDE_MS;
        s_state->slide_dy = s_slide_ms > 0 ? (int)((1.f - ease01(prog)) * SLIDE_PX + 0.5f) : 0;
        redraw = true;
    }

    /* the latest sentence on the face page fades in */
    if (s_state->line_alpha < 255) {
        s_state->line_alpha += 40;
        if (s_state->line_alpha > 255) s_state->line_alpha = 255;
        redraw = true;
    }

    /* "message arrived" face */
    if (s_notice_ms > 0) {
        s_notice_ms -= TICK_MS;
        if (s_notice_ms <= 0) { recompute(); redraw = true; }
    }

    if (s_flash_ms > 0) {
        s_flash_ms -= TICK_MS;
        int t = 600 - s_flash_ms;                 /* two 150 ms pulses within 600 ms */
        int in_pulse = (t < 150) ? t : (t >= 300 && t < 450) ? t - 300 : -1;
        s_state->flash = in_pulse < 0 ? 0 : (in_pulse < 75 ? in_pulse * 255 / 75 : (150 - in_pulse) * 255 / 75);
        if (s_flash_ms <= 0) s_state->flash = 0;
        redraw = true;
    }

    if (s_blush_ms > 0) {
        s_blush_ms -= TICK_MS;
        int t = BLUSH_MS - s_blush_ms;            /* 0..BLUSH_MS */
        int a = t < 500 ? t * 255 / 500 : (t > 1500 ? (BLUSH_MS - t) * 255 / 700 : 255);
        if (a < 0) a = 0;
        s_state->blush_alpha = an_on(AN_BLUSH) ? a : 255;
        if (s_blush_ms <= 0 && strcmp(s_override_face, BLUSH_FACE) == 0) {
            s_override_face[0] = 0;
            s_state->blush_alpha = 255;
            recompute();
        }
        redraw = an_on(AN_BLUSH) || s_blush_ms <= 0;
    }

    if (s_shake_ms > 0) {
        s_shake_ms -= TICK_MS;
        int t = 600 - s_shake_ms;
        static const int pattern[] = { 8, -8, 6, -6, 4, -4, 2, -2, 0, 0, 0, 0 };
        s_state->face_dx = s_shake_ms > 0 ? pattern[(t / TICK_MS) % 12] : 0;
        redraw = true;
    }

    /* random blink: off by default (she took it for a frozen screen) */
    if (s_blink_ms > 0) {
        s_blink_ms -= TICK_MS;
        if (s_blink_ms <= 0) { s_state->blink = false; redraw = true; }
    } else if (an_on(AN_BLINK) && s_state->screen == UI_SCREEN_FACE && s_state->page_pos == 0 &&
               !s_sleeping && !s_override_face[0] && s_notice_ms <= 0) {
        s_next_blink_ms -= TICK_MS;
        if (s_next_blink_ms <= 0) {
            s_state->blink = true;
            s_blink_ms = 150;
            s_next_blink_ms = 3000 + (int)(esp_random() % 10000);
            redraw = true;
        }
    }

    if (s_sleeping && s_state->anim_tick >= 0 && s_state->screen == UI_SCREEN_FACE) {
        s_state->anim_tick++;
        if (s_state->anim_tick % 2 == 0) redraw = true;      /* 10 fps is plenty for z's */
    }

    unlock();
    if (redraw) mark_dirty();
}

static esp_err_t apply_rotation(int rotation)
{
    xSemaphoreTake(s_fb_lock, portMAX_DELAY);
    esp_err_t err = board_lcd_set_rotation(rotation);
    if (err == ESP_OK) {
        s_rotation = rotation;
        gfx_init(s_fb, board_lcd_width(), board_lcd_height());
    }
    xSemaphoreGive(s_fb_lock);
    return err;
}

/* ---- buttons config --------------------------------------------------------- */

static void copy_btn_array(cJSON *arr, ui_button_t *out, int *n, int max_n, int max_chars)
{
    *n = 0;
    if (!cJSON_IsArray(arr)) return;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (*n >= max_n) break;
        if (cJSON_IsString(it) && it->valuestring[0]) {
            copy_limited(out[*n].text, UI_BTN_TEXT_LEN, it->valuestring, max_chars);
            if (out[*n].text[0]) (*n)++;
        }
    }
}

static bool parse_buttons(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;
    ui_button_t *tb = calloc(UI_MAX_TEXT_BTN, sizeof(ui_button_t));
    ui_button_t *eb = calloc(UI_MAX_EMOJI_BTN, sizeof(ui_button_t));
    if (!tb || !eb) { free(tb); free(eb); cJSON_Delete(root); return false; }
    int tn = 0, en = 0;
    char shake[UI_BTN_TEXT_LEN] = "想你了";
    bool ok = true;
    if (cJSON_IsArray(root)) {
        copy_btn_array(root, tb, &tn, UI_MAX_TEXT_BTN, UI_TEXT_BTN_CHARS);
    } else if (cJSON_IsObject(root)) {
        copy_btn_array(cJSON_GetObjectItem(root, "text"), tb, &tn, UI_MAX_TEXT_BTN, UI_TEXT_BTN_CHARS);
        copy_btn_array(cJSON_GetObjectItem(root, "emoji"), eb, &en, UI_MAX_EMOJI_BTN, UI_EMOJI_BTN_CHARS);
        cJSON *sh = cJSON_GetObjectItem(root, "shake");
        if (cJSON_IsString(sh) && sh->valuestring[0]) copy_limited(shake, sizeof shake, sh->valuestring, UI_TEXT_BTN_CHARS);
    } else {
        ok = false;
    }
    cJSON_Delete(root);
    if (ok && tn == 0 && en == 0) ok = false;
    if (ok) {
        lock();
        memset(s_state->text_btn, 0, sizeof s_state->text_btn);
        memset(s_state->emoji_btn, 0, sizeof s_state->emoji_btn);
        memcpy(s_state->text_btn, tb, sizeof(ui_button_t) * (size_t)tn);
        memcpy(s_state->emoji_btn, eb, sizeof(ui_button_t) * (size_t)en);
        s_state->text_btn_n = tn;
        s_state->emoji_btn_n = en;
        if (s_state->emoji_page * ui_emoji_per_page() >= en) s_state->emoji_page = 0;
        strcpy(s_shake_text, shake);
        unlock();
    }
    free(tb);
    free(eb);
    return ok;
}

esp_err_t ui_set_buttons_json(const char *json)
{
    if (strlen(json) >= sizeof s_buttons_json) return ESP_ERR_INVALID_SIZE;
    if (!parse_buttons(json)) return ESP_ERR_INVALID_ARG;
    strcpy(s_buttons_json, json);
    settings_set_str(KEY_BUTTONS, json);
    mark_dirty();
    return ESP_OK;
}

esp_err_t ui_reset_buttons(void)
{
    strcpy(s_buttons_json, DEFAULT_BUTTONS);
    if (!parse_buttons(s_buttons_json)) return ESP_FAIL;
    settings_erase(KEY_BUTTONS);
    mark_dirty();
    return ESP_OK;
}

const char *ui_get_buttons_json(void) { return s_buttons_json; }
const char *ui_shake_text(void) { return s_shake_text; }

bool ui_button_text(bool emoji, int index, char *out, size_t out_len)
{
    bool ok = false;
    lock();
    int n = emoji ? s_state->emoji_btn_n : s_state->text_btn_n;
    if (index >= 0 && index < n) {
        const char *t = emoji ? s_state->emoji_btn[index].text : s_state->text_btn[index].text;
        snprintf(out, out_len, "%s", t);
        ok = true;
    }
    unlock();
    return ok;
}

/* ---- start ------------------------------------------------------------------ */

void ui_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_fb_lock = xSemaphoreCreateMutex();
    s_dirty = xSemaphoreCreateBinary();
    s_fb = heap_caps_malloc(BOARD_LCD_W * BOARD_LCD_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    s_state = heap_caps_calloc(1, sizeof(ui_state_t), MALLOC_CAP_SPIRAM);
    s_snap = heap_caps_calloc(1, sizeof(ui_state_t), MALLOC_CAP_SPIRAM);
    s_ink = heap_caps_calloc(1, sizeof(ink_t), MALLOC_CAP_SPIRAM);
    s_state->ink = s_ink;
    assert(s_fb && s_state && s_snap);
    s_state->pressed = UI_HIT_NONE;
    s_state->screen = UI_SCREEN_FACE;
    s_state->line_alpha = 255;
    s_state->blush_alpha = 255;

    char buf[16];
    int rot = DEFAULT_ROTATION;
    if (settings_get_str(KEY_ROTATE, buf, sizeof buf)) {
        int v = atoi(buf);
        if (v == 0 || v == 90 || v == 180 || v == 270) rot = v;
    }
    if (settings_get_str(KEY_THEME, buf, sizeof buf)) {
        if (strcmp(buf, "light") == 0) s_theme_mode = THEME_LIGHT;
        else if (strcmp(buf, "dark") == 0) s_theme_mode = THEME_DARK;
    }
    s_state->theme = theme_for_mode();
    if (settings_get_str(KEY_ANIM, buf, sizeof buf)) s_anim_master = strcmp(buf, "off") != 0;
    for (int k = 0; k < AN_N; k++) {
        char key[16];
        snprintf(key, sizeof key, "an_%s", AN_NAME[k]);
        s_an[k] = AN_DEFAULT[k];
        if (settings_get_str(key, buf, sizeof buf)) s_an[k] = strcmp(buf, "on") == 0;
    }
    s_state->anim_tick = an_on(AN_ZZZ) ? 0 : -1;
    if (!settings_get_str(KEY_BUTTONS, s_buttons_json, sizeof s_buttons_json) || !parse_buttons(s_buttons_json)) {
        strcpy(s_buttons_json, DEFAULT_BUTTONS);
        parse_buttons(s_buttons_json);
    }
    if (apply_rotation(rot) != ESP_OK) apply_rotation(0);
    ESP_LOGI(TAG, "rotation %d, theme %s, anim %s", s_rotation, ui_get_theme(), s_anim_master ? "on" : "off");

    strcpy(s_base_face, "(—_—)");
    recompute();
    s_next_blink_ms = 4000;

    const esp_timer_create_args_t targs = { .callback = tick_cb, .name = "ui_tick" };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_tick_timer, (uint64_t)TICK_MS * 1000));

    xTaskCreatePinnedToCore(render_task, "ui_render", 8192, NULL, 5, NULL, 1);
    mark_dirty();
}

/* ---- face ------------------------------------------------------------------- */

void ui_set_face(const char *utf8)
{
    lock();
    copy_limited(s_base_face, sizeof(s_base_face), utf8, UI_FACE_MAX_CHARS);
    if (!s_base_face[0]) strcpy(s_base_face, "(—_—)");
    recompute();
    unlock();
    mark_dirty();
}

void ui_set_corner(const char *utf8)
{
    lock();
    copy_limited(s_base_corner, sizeof(s_base_corner), utf8, 40);
    recompute();
    unlock();
    mark_dirty();
}

void ui_override_face(const char *utf8)
{
    lock();
    if (utf8) copy_limited(s_override_face, sizeof(s_override_face), utf8, UI_FACE_MAX_CHARS);
    else s_override_face[0] = 0;
    s_blush_ms = 0;
    s_state->blush_alpha = 255;
    recompute();
    unlock();
    mark_dirty();
}

void ui_override_corner(const char *utf8)
{
    lock();
    if (utf8) copy_limited(s_override_corner, sizeof(s_override_corner), utf8, 40);
    else s_override_corner[0] = 0;
    recompute();
    unlock();
    mark_dirty();
}

void ui_blush(void)
{
    lock();
    if (s_override_face[0] && strcmp(s_override_face, BLUSH_FACE) != 0) { unlock(); return; }
    strcpy(s_override_face, BLUSH_FACE);
    s_blush_ms = BLUSH_MS;
    s_state->blush_alpha = an_on(AN_BLUSH) ? 0 : 255;
    recompute();
    unlock();
    mark_dirty();
}

/* ---- chat -------------------------------------------------------------------- */

void ui_chat_add(chat_who_t who, const char *utf8)
{
    lock();
    if (s_state->msg_count == UI_CHAT_MAX) {
        memmove(&s_state->msgs[0], &s_state->msgs[1], sizeof(chat_msg_t) * (UI_CHAT_MAX - 1));
        s_state->msg_count--;
    }
    chat_msg_t *m = &s_state->msgs[s_state->msg_count++];
    memset(m, 0, sizeof *m);
    m->who = who;
    if (who == CHAT_KE) strcpy(m->face, s_base_face);          /* the expression at the moment of sending */
    copy_limited(m->text, sizeof m->text, utf8, UI_SAY_MAX_CHARS);
    s_state->scroll = 0;
    s_state->slide_dy = SLIDE_PX;                               /* slides in from below */
    s_slide_ms = SLIDE_MS;
    unlock();
    mark_dirty();
}

static void chat_add_ink(chat_who_t who, int thumb_id)
{
    lock();
    if (s_state->msg_count == UI_CHAT_MAX) {
        memmove(&s_state->msgs[0], &s_state->msgs[1], sizeof(chat_msg_t) * (UI_CHAT_MAX - 1));
        s_state->msg_count--;
    }
    chat_msg_t *m = &s_state->msgs[s_state->msg_count++];
    memset(m, 0, sizeof *m);
    m->who = who;
    strcpy(m->text, "[手写]");
    m->ink_id = thumb_id > 0 ? (uint16_t)thumb_id : 0;
    s_state->scroll = 0;
    s_state->slide_dy = SLIDE_PX;
    s_slide_ms = SLIDE_MS;
    unlock();
    mark_dirty();
}

void ui_chat_add_ink(int thumb_id) { chat_add_ink(CHAT_HER, thumb_id); }

void app_on_new_message(void);

/* Ke sent something (not plain text): same reactions as ui_set_say - the face looks up on the face page, alert */
static void ke_arrived(void)
{
    lock();
    s_state->line_alpha = 0;
    if (s_state->screen == UI_SCREEN_FACE && !s_sleeping) {
        s_notice_ms = NOTICE_MS;
        recompute();
    }
    unlock();
    mark_dirty();
    ui_flash_border();
    app_on_new_message();
}

void ui_set_music(const music_info_t *info, const char (*names)[UI_MUSIC_TITLE])
{
    lock();
    s_state->music = *info;
    s_state->music_names = names;
    int max = ui_music_list_max_scroll(s_state);
    if (s_state->music_scroll > max) s_state->music_scroll = max;
    unlock();
    mark_dirty();
}

void ui_set_music_playing(bool playing)
{
    lock();
    if (s_music_face != playing) {
        s_music_face = playing;
        recompute();
    }
    unlock();
    mark_dirty();
}

void ui_ke_picture(int pic_id)
{
    lock();
    if (s_state->msg_count == UI_CHAT_MAX) {
        memmove(&s_state->msgs[0], &s_state->msgs[1], sizeof(chat_msg_t) * (UI_CHAT_MAX - 1));
        s_state->msg_count--;
    }
    chat_msg_t *m = &s_state->msgs[s_state->msg_count++];
    memset(m, 0, sizeof *m);
    m->who = CHAT_KE;
    strcpy(m->text, "[图片]");
    m->pic_id = pic_id > 0 ? (uint16_t)pic_id : 0;
    s_state->scroll = 0;
    s_state->slide_dy = SLIDE_PX;
    s_slide_ms = SLIDE_MS;
    unlock();
    mark_dirty();
    ke_arrived();
}

int ui_msg_pic_id(int index)
{
    int id = 0;
    lock();
    if (index >= 0 && index < s_state->msg_count) id = s_state->msgs[index].pic_id;
    unlock();
    return id;
}

void ui_screen_size(int *w, int *h)
{
    *w = gfx_width();
    *h = gfx_height();
}

void ui_ke_ink(int thumb_id)
{
    chat_add_ink(CHAT_KE, thumb_id);
    ke_arrived();
}

void ui_set_say(const char *utf8)
{
    if (!utf8 || !utf8[0]) return;
    ui_chat_add(CHAT_KE, utf8);
    lock();
    s_state->line_alpha = 0;                                    /* the line under the face fades in */
    if (s_state->screen == UI_SCREEN_FACE && !s_sleeping) {     /* on the face page: the face looks up for a second */
        s_notice_ms = NOTICE_MS;
        recompute();
    }
    unlock();
    mark_dirty();
    ui_flash_border();                                          /* only does anything if the flash switch is on */
    app_on_new_message();
}

void ui_toast(const char *utf8, int ms)
{
    lock();
    copy_limited(s_state->toast, sizeof s_state->toast, utf8, UI_SAY_MAX_CHARS);
    s_toast_ms = ms;
    unlock();
    mark_dirty();
}

void ui_scroll_by(int dy)
{
    lock();
    s_state->scroll += dy;
    clamp_scroll();
    unlock();
    mark_dirty();
}

void ui_set_online(bool online)
{
    lock();
    bool changed = s_state->online != online;
    s_state->online = online;
    unlock();
    if (changed) mark_dirty();
}

/* ---- pages and panel ---------------------------------------------------------- */

void ui_go_page(ui_screen_t page)
{
    if (page != UI_SCREEN_FACE && page != UI_SCREEN_CHAT) return;
    lock();
    if (s_state->screen != page) {
        s_state->screen = page;
        s_state->pressed = UI_HIT_NONE;
        if (page == UI_SCREEN_FACE) { s_state->panel_open = false; s_state->panel_pos = 0; }
    }
    unlock();
    mark_dirty();
}

void ui_panel_set(bool open)
{
    lock();
    if (s_state->screen == UI_SCREEN_CHAT) s_state->panel_open = open;
    unlock();
    mark_dirty();
}

bool ui_panel_is_open(void) { return s_state->panel_open; }

void ui_emoji_page_step(int dir)
{
    lock();
    int pages = ui_emoji_pages(s_state);
    int p = s_state->emoji_page + dir;
    if (p >= 0 && p < pages) s_state->emoji_page = p;
    unlock();
    mark_dirty();
}

/* ---- settings ---------------------------------------------------------------- */

esp_err_t ui_set_rotation(int rotation)
{
    if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) return ESP_ERR_INVALID_ARG;
    esp_err_t err = apply_rotation(rotation);
    if (err != ESP_OK) return err;
    char buf[8];
    snprintf(buf, sizeof buf, "%d", rotation);
    settings_set_str(KEY_ROTATE, buf);
    mark_dirty();
    return ESP_OK;
}

int ui_get_rotation(void) { return s_rotation; }

void ui_refresh_after_rotation(void)
{
    xSemaphoreTake(s_fb_lock, portMAX_DELAY);
    gfx_init(s_fb, board_lcd_width(), board_lcd_height());
    xSemaphoreGive(s_fb_lock);
    mark_dirty();
}

esp_err_t ui_set_theme(const char *name)
{
    int m;
    if (strcmp(name, "dark") == 0) m = THEME_DARK;
    else if (strcmp(name, "light") == 0) m = THEME_LIGHT;
    else if (strcmp(name, "auto") == 0) m = THEME_AUTO;
    else return ESP_ERR_INVALID_ARG;
    lock();
    s_theme_mode = m;
    s_state->theme = theme_for_mode();
    unlock();
    settings_set_str(KEY_THEME, name);
    mark_dirty();
    return ESP_OK;
}

const char *ui_get_theme(void)
{
    return s_theme_mode == THEME_DARK ? "dark" : s_theme_mode == THEME_LIGHT ? "light" : "auto";
}

/* ---- animation ------------------------------------------------------------------ */

static void anim_after_change(void)
{
    /* lock held */
    if (!an_on(AN_BLINK)) s_state->blink = false;
    if (!an_on(AN_SHAKE)) { s_state->face_dx = 0; s_shake_ms = 0; }
    if (!an_on(AN_FLASH)) { s_state->flash = 0; s_flash_ms = 0; }
    s_state->anim_tick = an_on(AN_ZZZ) ? (s_state->anim_tick < 0 ? 0 : s_state->anim_tick) : -1;
    if (!an_on(AN_BLUSH)) s_state->blush_alpha = 255;
}

bool ui_anim_set(const char *name, bool on)
{
    int k = -1;
    bool master = !name || !strcmp(name, "all");
    if (!master) {
        for (int i = 0; i < AN_N; i++) if (!strcmp(name, AN_NAME[i])) k = i;
        if (k < 0) return false;
    }
    lock();
    if (master) s_anim_master = on; else s_an[k] = on;
    anim_after_change();
    unlock();
    if (master) {
        settings_set_str(KEY_ANIM, on ? "on" : "off");
    } else {
        char key[16];
        snprintf(key, sizeof key, "an_%s", name);
        settings_set_str(key, on ? "on" : "off");
    }
    mark_dirty();
    return true;
}

bool ui_anim_get(const char *name, bool *on)
{
    if (!name || !strcmp(name, "all")) { *on = s_anim_master; return true; }
    for (int i = 0; i < AN_N; i++) if (!strcmp(name, AN_NAME[i])) { *on = s_an[i]; return true; }
    return false;
}

void ui_anim_status(char *out, size_t out_len)
{
    size_t n = (size_t)snprintf(out, out_len, "all %s", s_anim_master ? "on" : "off");
    for (int i = 0; i < AN_N && n < out_len; i++) {
        n += (size_t)snprintf(out + n, out_len - n, ", %s %s", AN_NAME[i], s_an[i] ? "on" : "off");
    }
}

void ui_set_anim(bool on) { ui_anim_set("all", on); }
bool ui_get_anim(void) { return s_anim_master; }

void ui_set_sleeping(bool on)
{
    lock();
    s_sleeping = on;
    if (!an_on(AN_ZZZ)) s_state->anim_tick = -1;
    else if (s_state->anim_tick < 0) s_state->anim_tick = 0;
    recompute();
    unlock();
    mark_dirty();
}

bool ui_is_sleeping(void) { return s_sleeping; }

void ui_shake(void)
{
    lock();
    if (an_on(AN_SHAKE)) s_shake_ms = 600;
    unlock();
}

void ui_flash_border(void)
{
    lock();
    if (an_on(AN_FLASH)) s_flash_ms = 600;
    unlock();
}

void ui_set_sending(bool on)
{
    lock();
    s_state->sending = on;
    unlock();
    mark_dirty();
}

void ui_set_review(bool on)
{
    lock();
    s_state->review = on;
    unlock();
    mark_dirty();
}

void ui_display_hold(bool hold)
{
    /* the render task keeps the framebuffer lock while it draws and pushes a frame: holding it here means no
     * LCD traffic (and no PSRAM reads for it) while the camera DMA writes a frame into PSRAM */
    if (hold) xSemaphoreTake(s_fb_lock, portMAX_DELAY);
    else xSemaphoreGive(s_fb_lock);
}

void ui_set_peek_icon(bool on)
{
    lock();
    s_state->peek_on = on;
    unlock();
    mark_dirty();
}

/* ---- screens --------------------------------------------------------------------- */

void ui_set_screen(ui_screen_t screen)
{
    lock();
    s_state->screen = screen;
    s_state->pressed = UI_HIT_NONE;
    s_state->frame = NULL;
    s_state->review = false;
    s_state->cam_text[0] = 0;
    /* immediate: no slide when coming back from the camera */
    s_state->page_pos = screen == UI_SCREEN_FACE ? 0 : 255;
    if (screen != UI_SCREEN_CHAT) { s_state->panel_open = false; s_state->panel_pos = 0; }
    unlock();
    mark_dirty();
}

ui_screen_t ui_get_screen(void) { return s_state->screen; }

void ui_set_frame(const uint16_t *frame, int w, int h)
{
    lock();
    s_state->frame = frame;
    s_state->frame_w = w;
    s_state->frame_h = h;
    unlock();
    mark_dirty();
}

void ui_set_cam_text(const char *utf8)
{
    lock();
    copy_limited(s_state->cam_text, sizeof s_state->cam_text, utf8 ? utf8 : "", 20);
    unlock();
    mark_dirty();
}

void ui_set_gallery_pos(int index, int count)
{
    lock();
    s_state->gal_index = index;
    s_state->gal_count = count;
    unlock();
    mark_dirty();
}

bool ui_is_sending(void) { return s_state->sending; }

void ui_get_state_copy(ui_state_t *out)
{
    lock();
    memcpy(out, s_state, sizeof(ui_state_t));
    unlock();
}

/* ---- touch ------------------------------------------------------------------------- */

static void set_pressed(int id)
{
    lock();
    bool changed = s_state->pressed != id;
    s_state->pressed = id;
    unlock();
    if (changed) mark_dirty();
}

/* Taps that only change the UI itself. Returns false if the app has to handle the tap. */
/* ---- handwriting ------------------------------------------------------------------------------------- */

static bool s_ink_stroke;          /* a finger is drawing on the pad */
static bool s_vol_drag;            /* a finger is on the music volume slider */

void ui_ink_open(void)
{
    ui_set_screen(UI_SCREEN_INK);
}

static bool ink_norm(int x, int y, int *nx, int *ny)
{
    int px, py, side;
    ui_ink_pad_rect(&px, &py, &side);
    int ax = (x - px) * INK_RANGE / side, ay = (y - py) * INK_RANGE / side;
    *nx = ax < 0 ? 0 : (ax > INK_RANGE - 1 ? INK_RANGE - 1 : ax);
    *ny = ay < 0 ? 0 : (ay > INK_RANGE - 1 ? INK_RANGE - 1 : ay);
    return true;
}

static void ink_send_now(void)
{
    if (ui_is_sending()) return;
    if (!ink_finish(s_ink)) { ui_toast("先写几个字，再点「寄」", 1800); return; }
    uint8_t *png = NULL;
    size_t len = 0;
    int w, h;
    if (!ink_make_png(s_ink, &png, &len, &w, &h)) { ui_toast("生成图片失败", 2000); return; }
    uint8_t *mask = heap_caps_malloc(INK_HAND_W * INK_HAND_H, MALLOC_CAP_SPIRAM);
    int tw, th, slot = 0;
    if (mask && ink_make_thumb(s_ink, mask, &tw, &th)) slot = ink_thumb_store(mask, tw, th);
    free(mask);
    app_send_ink(png, len);
    free(png);
    ink_reset(s_ink);
    lock(); s_state->ink_scroll = 0; unlock();
    ui_chat_add_ink(slot);
    ui_set_screen(UI_SCREEN_CHAT);
}

static bool handle_tap_in_ui(int hit)
{
    switch (hit) {
    case UI_HIT_VIEW_EXIT: ui_set_screen(UI_SCREEN_CHAT); return true;
    case UI_HIT_MUSIC_BACK: ui_set_screen(UI_SCREEN_CHAT); return true;
    case UI_HIT_MUSIC_LIST: ui_set_screen(UI_SCREEN_MUSIC_LIST); return true;
    case UI_HIT_MUSIC_LIST_BACK: ui_set_screen(UI_SCREEN_MUSIC); return true;
    case UI_HIT_MUSIC_LIST_BG: return true;
    case UI_HIT_MUSIC_VOL: return true;
    case UI_HIT_INK_OPEN:  ui_ink_open(); return true;
    case UI_HIT_INK_BACK:  ui_set_screen(UI_SCREEN_CHAT); return true;
    case UI_HIT_INK_PAD:   return true;
    case UI_HIT_INK_STRIP: return true;
    case UI_HIT_INK_NEXT:
        lock(); s_state->ink_scroll = 0; unlock();                     /* the strip follows the newest character again */
        if (!ink_next(s_ink) && !ink_is_empty(&s_ink->cur)) ui_toast("最多写 40 个字", 1500);
        mark_dirty();
        return true;
    case UI_HIT_INK_UNDO:  ink_undo(s_ink); mark_dirty(); return true;
    case UI_HIT_INK_CLEAR: ink_clear(s_ink); mark_dirty(); return true;
    case UI_HIT_INK_SEND:  ink_send_now(); return true;
    case UI_HIT_HINT:     ui_go_page(UI_SCREEN_CHAT); return true;
    case UI_HIT_TEST_EXIT: ui_set_screen(UI_SCREEN_FACE); return true;
    case UI_HIT_TOP_BACK: ui_go_page(UI_SCREEN_FACE); return true;
    case UI_HIT_PLUS:     ui_panel_set(!ui_panel_is_open()); return true;
    case UI_HIT_PANEL:
    case UI_HIT_TOPBAR:   return true;
    case UI_HIT_CHAT:                                            /* blank space above an open panel closes it */
        if (ui_panel_is_open()) ui_panel_set(false);
        return true;
    default:              return false;
    }
}

/* the message list: the empty area and the picture bubbles in it */
static bool in_chat_area(int hit) { return hit == UI_HIT_CHAT || (hit >= UI_HIT_PIC0 && hit < UI_HIT_PIC0 + UI_CHAT_MAX); }

static bool is_pressable(int hit)
{
    if (hit >= UI_HIT_TEXT_BTN0 && hit < UI_HIT_MUSIC_ROW0) return true;
    switch (hit) {
    case UI_HIT_CAM_BTN: case UI_HIT_CAM_GALLERY: case UI_HIT_CAM_BACK: case UI_HIT_GAL_DELETE: case UI_HIT_GAL_SEND:
    case UI_HIT_HINT: case UI_HIT_TOP_BACK: case UI_HIT_PLUS:
    case UI_HIT_INK_OPEN: case UI_HIT_INK_BACK: case UI_HIT_INK_NEXT: case UI_HIT_INK_UNDO: case UI_HIT_INK_CLEAR: case UI_HIT_INK_SEND:
    case UI_HIT_MUSIC_OPEN: case UI_HIT_GAME_OPEN: case UI_HIT_MUSIC_BACK: case UI_HIT_MUSIC_LIST: case UI_HIT_MUSIC_PREV:
    case UI_HIT_MUSIC_PLAY: case UI_HIT_MUSIC_NEXT: case UI_HIT_MUSIC_LIST_BACK:
        return true;
    default:
        return false;
    }
}

static void handle_swipe(bool vertical, int total_dx, int total_dy)
{
    ui_screen_t scr;
    bool overflow;
    lock();
    scr = s_state->screen;
    overflow = ui_chat_content_height(s_state) > ui_chat_area_height(s_state);
    unlock();

    if (vertical) {
        if (scr == UI_SCREEN_FACE && total_dy <= -SWIPE_PX) {
            ui_go_page(UI_SCREEN_CHAT);                          /* swipe up: open the chat */
        } else if (scr == UI_SCREEN_CHAT && total_dy >= SWIPE_PX &&
                   (s_hit == UI_HIT_TOPBAR || s_hit == UI_HIT_TOP_BACK || (in_chat_area(s_hit) && !overflow))) {
            ui_go_page(UI_SCREEN_FACE);                          /* swipe down from the top bar (or on a short chat) */
        }
    } else if (abs(total_dx) > SWIPE_PX) {
        int dir = total_dx < 0 ? 1 : -1;
        if (scr == UI_SCREEN_GALLERY && s_hit == UI_HIT_GAL_VIEW) app_on_swipe(dir);
        else if (scr == UI_SCREEN_CHAT && (s_hit == UI_HIT_PANEL || s_hit >= UI_HIT_EMOJI_BTN0)) ui_emoji_page_step(dir);
    }
}

void ui_set_touchlog(bool on)
{
    s_touchlog = on;
    lock();
    s_state->dot_ms = 0;
    unlock();
    mark_dirty();
}

static void touch_dot(int x, int y)
{
    lock();
    bool moved = abs(x - s_state->dot_x) > 3 || abs(y - s_state->dot_y) > 3 || s_state->dot_ms <= 0;
    s_state->dot_x = x;
    s_state->dot_y = y;
    s_state->dot_ms = 900;
    unlock();
    if (moved) mark_dirty();
}

#define BTN_SLOP_PX   28    /* a finger on a button may wander this far (rolling at the screen edge, fat fingers) and still tap */
#define EDGE_ZONE_PX  40    /* touches this close to a screen edge get the wider drag threshold */
#define TLOG(...) do { if (s_touchlog) { printf("ui: " __VA_ARGS__); printf("\n"); } } while (0)

static int64_t s_last_tap_t;
static int s_last_tap_hit = UI_HIT_NONE;
static int s_drag_px = DRAG_PX;        /* movement that turns a touch into a drag, chosen when the finger goes down */

void ui_touch(bool down, int x, int y)
{
    if (s_touchlog && (down || s_down)) touch_dot(down ? x : s_last_x, down ? y : s_last_y);
    int64_t now = esp_timer_get_time();
    if (down && !s_down) {
        s_down = true;
        s_down_x = s_last_x = x;
        s_down_y = s_last_y = y;
        s_down_t = now;
        s_dragging = false;
        s_long_fired = false;
        lock();
        s_hit = ui_hit_test(s_state, x, y);
        unlock();
        if (is_pressable(s_hit)) set_pressed(s_hit);
        {
            /* buttons: never a drag until the finger has really left; near the edges the controller's coordinates
             * jump around as the finger rolls, so everything else gets a wider threshold there */
            const bool edge = x < EDGE_ZONE_PX || y < EDGE_ZONE_PX || x >= gfx_width() - EDGE_ZONE_PX || y >= gfx_height() - EDGE_ZONE_PX;
            s_drag_px = is_pressable(s_hit) ? BTN_SLOP_PX : (edge ? 2 * DRAG_PX : DRAG_PX + 4);
            TLOG("down (%d,%d) hit=%d%s drag>%dpx", x, y, s_hit, edge ? " edge" : "", s_drag_px);
        }
        app_on_touch_activity();
        if (s_hit == UI_HIT_MUSIC_VOL) {                           /* the volume slider follows the finger from the first touch */
            s_vol_drag = true;
            app_set_volume(ui_music_vol_from_x(x));
        }
        if (s_hit == UI_HIT_INK_PAD && s_ink) {                    /* start of a stroke */
            int nx, ny;
            ink_norm(x, y, &nx, &ny);
            s_ink_stroke = ink_pen_down(s_ink, nx, ny);
            mark_ink_dirty();
        }
        return;
    }
    if (down && s_down && s_vol_drag) {
        s_last_x = x;
        s_last_y = y;
        app_set_volume(ui_music_vol_from_x(x));
        return;
    }
    if (!down && s_down && s_vol_drag) {
        s_down = false;
        s_vol_drag = false;
        return;
    }
    if (down && s_down && s_ink_stroke) {                          /* the pen follows the finger */
        int nx, ny;
        ink_norm(x, y, &nx, &ny);
        s_last_x = x;
        s_last_y = y;
        if (ink_pen_move(s_ink, nx, ny)) mark_ink_dirty();
        return;
    }
    if (!down && s_down && s_ink_stroke) {
        s_down = false;
        s_ink_stroke = false;
        ink_pen_up(s_ink);
        /* a stroke drawn as straight pieces is redrawn as a smooth curve; a dot or a short tick needs no redraw */
        if (s_ink->cur.nstrokes > 0 && s_ink->cur.npts - s_ink->cur.start[s_ink->cur.nstrokes - 1] >= 3) mark_dirty();
        return;
    }
    if (down && s_down) {
        int dx = x - s_down_x, dy = y - s_down_y;
        if (!s_dragging && (abs(dx) > s_drag_px || abs(dy) > s_drag_px)) {
            s_dragging = true;
            TLOG("drag started at (%d,%d), moved %d,%d from hit=%d", x, y, dx, dy, s_hit);
            s_axis_v = abs(dy) >= abs(dx);
            set_pressed(UI_HIT_NONE);
        }
        if (s_dragging && !s_axis_v && s_hit == UI_HIT_INK_STRIP && s_ink) {
            /* the strip of characters slides with the finger: dragging right shows earlier characters */
            static int accum;
            accum += x - s_last_x;
            int cell = ui_ink_strip_cell(), steps = accum / cell;
            if (steps) {
                accum -= steps * cell;
                int max = s_ink->ndone - ui_ink_strip_cap();
                lock();
                int v = s_state->ink_scroll + steps;
                s_state->ink_scroll = v < 0 ? 0 : (v > max ? (max < 0 ? 0 : max) : v);
                unlock();
                mark_dirty();
            }
        }
        if (s_dragging && s_axis_v && (s_hit == UI_HIT_MUSIC_LIST_BG || s_hit >= UI_HIT_MUSIC_ROW0) && s_hit < UI_HIT_PIC0) {
            /* the song list follows the finger */
            lock();
            int v = s_state->music_scroll - (y - s_last_y), max = ui_music_list_max_scroll(s_state);
            s_state->music_scroll = v < 0 ? 0 : (v > max ? max : v);
            unlock();
            mark_dirty();
        }
        if (s_dragging && s_axis_v && in_chat_area(s_hit)) {
            /* content follows the finger: dragging down reveals older messages */
            ui_scroll_by(y - s_last_y);
        }
        s_last_x = x;
        s_last_y = y;
        /* long press on the face / message area: talk */
        if (!s_dragging && !s_long_fired && (s_hit == UI_HIT_FACE || in_chat_area(s_hit)) &&
            now - s_down_t >= (int64_t)LONG_PRESS_MS * 1000) {
            s_long_fired = true;
            app_on_long_press();
        }
        return;
    }
    if (!down && s_down) {
        s_down = false;
        set_pressed(UI_HIT_NONE);
        if (s_long_fired) {
            TLOG("up: long press ended (talk)");
            app_on_long_release();
            return;
        }
        if (s_dragging) {
            TLOG("up: drag (%d,%d) from hit=%d -> swipe handling", s_last_x - s_down_x, s_last_y - s_down_y, s_hit);
            handle_swipe(s_axis_v, s_last_x - s_down_x, s_last_y - s_down_y);
            return;
        }
        /* tap. The touch controller reports no coordinates once the finger is up, so the caller's
         * x,y are meaningless here: test with the last position seen while it was down. */
        lock();
        int hit_up = ui_hit_test(s_state, s_last_x, s_last_y);
        unlock();
        /* which element was meant: a button under the finger at touch-down or at lift-off (the first sample of a
         * rolling finger can land next to it); otherwise both ends must agree */
        int target = UI_HIT_NONE;
        if (is_pressable(s_hit)) target = s_hit;
        else if (is_pressable(hit_up)) target = hit_up;
        else if (hit_up == s_hit) target = s_hit;
        if (target == UI_HIT_NONE) {
            TLOG("up: ignored (down hit=%d, up hit=%d at (%d,%d) - not the same element)", s_hit, hit_up, s_last_x, s_last_y);
            return;
        }
        if (target == s_last_tap_hit && now - s_last_tap_t < 250000) { TLOG("up: ignored (hit=%d again within 250 ms: one contact that the controller split in two)", target); return; }
        s_last_tap_hit = target;
        s_last_tap_t = now;
        if (handle_tap_in_ui(target)) { TLOG("up: tap hit=%d handled by the UI", target); return; }
        bool send_btn = (target >= UI_HIT_TEXT_BTN0 && target < UI_HIT_MUSIC_ROW0) || target == UI_HIT_GAL_SEND || target == UI_HIT_GAL_DELETE;
        if (send_btn && ui_is_sending()) { TLOG("up: ignored (hit=%d, a send is in progress)", target); return; }   /* greyed out: no double taps */
        TLOG("up: tap hit=%d -> app", target);
        app_on_tap(target);
    }
}
