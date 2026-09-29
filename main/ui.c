#include "ui.h"
#include "ui_render.h"
#include "gfx.h"
#include "board.h"
#include "settings.h"
#include "audio.h"
#include "bridge.h"
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
#define KEY_ROTATE     "rotate"
#define KEY_THEME      "theme"
#define KEY_BUTTONS    "buttons"
#define KEY_ANIM       "anim"
#define DEFAULT_ROTATION 90
#define DEFAULT_THEME    UI_THEME_DARK
#define DEFAULT_BUTTONS  "{\"text\":[\"想你了\",\"抱抱\",\"在干嘛\",\"晚安\"],\"emoji\":[\"(´ω`)\",\"(≧▽≦)\",\"♡\",\"💧\"],\"shake\":\"想你了\"}"
#define TICK_MS        50
#define LONG_PRESS_MS  500
#define DRAG_PX        10

static SemaphoreHandle_t s_lock;      /* protects s_state and friends */
static SemaphoreHandle_t s_fb_lock;   /* protects framebuffer + gfx dimensions */
static SemaphoreHandle_t s_dirty;
static ui_state_t *s_state;           /* what is drawn (PSRAM) */
static ui_state_t *s_snap;            /* copy for the render task (PSRAM) */
static char s_base_face[UI_FACE_BUF];
static char s_override_face[UI_FACE_BUF];
static char s_base_corner[48];
static char s_override_corner[48];
static char s_shake_text[UI_BTN_TEXT_LEN] = "想你了";
static char s_buttons_json[1024] = DEFAULT_BUTTONS;
static uint16_t *s_fb;
static int s_rotation = DEFAULT_ROTATION;
static bool s_anim = true;
static esp_timer_handle_t s_tick_timer;

/* animation state (ms countdowns, driven by the 50 ms tick) */
static int s_blush_ms;          /* > 0 while blushing (total 2200 ms) */
static int s_blink_ms;          /* > 0 while eyes closed */
static int s_next_blink_ms;     /* countdown to next blink */
static int s_shake_ms;
static int s_flash_ms;
static int s_toast_ms;
static bool s_sleeping;

/* touch state */
static bool s_down;
static int s_down_x, s_down_y, s_last_y, s_last_x;
static int64_t s_down_t;
static int s_hit;               /* hit id at touch down */
static bool s_dragging, s_long_fired;

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

static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }
static void mark_dirty(void) { xSemaphoreGive(s_dirty); }

/* recompute drawn face/corner from base + overrides; lock held */
static void recompute(void)
{
    const char *face = s_override_face[0] ? s_override_face : (s_sleeping ? SLEEP_FACE : s_base_face);
    strcpy(s_state->face, face);
    strcpy(s_state->corner, s_override_corner[0] ? s_override_corner : s_base_corner);
    s_state->sleeping = s_sleeping;
}

static void clamp_scroll(void)
{
    int max = ui_chat_content_height(s_state) - ui_chat_area_height();
    if (max < 0) max = 0;
    if (s_state->scroll > max) s_state->scroll = max;
    if (s_state->scroll < 0) s_state->scroll = 0;
}

static void render_task(void *arg)
{
    for (;;) {
        xSemaphoreTake(s_dirty, portMAX_DELAY);
        lock();
        memcpy(s_snap, s_state, sizeof(ui_state_t));
        unlock();
        xSemaphoreTake(s_fb_lock, portMAX_DELAY);
        ui_render(s_snap);
        board_lcd_flush(s_fb);
        xSemaphoreGive(s_fb_lock);
    }
}

/* 50 ms tick: drives blink / blush / shake / flash / toast / z animation */
static void tick_cb(void *arg)
{
    bool redraw = false;
    lock();
    if (s_toast_ms > 0) {
        s_toast_ms -= TICK_MS;
        if (s_toast_ms <= 0) { s_state->toast[0] = 0; redraw = true; }
    }
    if (s_flash_ms > 0) {
        s_flash_ms -= TICK_MS;
        /* two 150 ms pulses within 600 ms */
        int t = 600 - s_flash_ms;
        int in_pulse = (t < 150) ? t : (t >= 300 && t < 450) ? t - 300 : -1;
        s_state->flash = in_pulse < 0 ? 0 : (in_pulse < 75 ? in_pulse * 255 / 75 : (150 - in_pulse) * 255 / 75);
        if (s_flash_ms <= 0) s_state->flash = 0;
        redraw = true;
    }
    if (s_anim) {
        if (s_blush_ms > 0) {
            s_blush_ms -= TICK_MS;
            int t = 2200 - s_blush_ms;            /* 0..2200 */
            int a = t < 500 ? t * 255 / 500 : (t > 1500 ? (2200 - t) * 255 / 700 : 255);
            if (a < 0) a = 0;
            s_state->blush_alpha = a;
            if (s_blush_ms <= 0 && strcmp(s_override_face, BLUSH_FACE) == 0) {
                s_override_face[0] = 0;
                recompute();
            }
            redraw = true;
        }
        if (s_shake_ms > 0) {
            s_shake_ms -= TICK_MS;
            int t = 600 - s_shake_ms;
            static const int pattern[] = { 8, -8, 6, -6, 4, -4, 2, -2, 0, 0, 0, 0 };
            s_state->face_dx = s_shake_ms > 0 ? pattern[(t / TICK_MS) % 12] : 0;
            redraw = true;
        }
        if (s_blink_ms > 0) {
            s_blink_ms -= TICK_MS;
            if (s_blink_ms <= 0) { s_state->blink = false; redraw = true; }
        } else if (s_state->screen == UI_SCREEN_CHAT && !s_sleeping && !s_override_face[0]) {
            s_next_blink_ms -= TICK_MS;
            if (s_next_blink_ms <= 0) {
                s_state->blink = true;
                s_blink_ms = 150;
                s_next_blink_ms = 3000 + (int)(esp_random() % 10000);
                redraw = true;
            }
        }
        if (s_sleeping && s_state->screen == UI_SCREEN_CHAT) {
            s_state->anim_tick++;
            if (s_state->anim_tick % 2 == 0) redraw = true;   /* 10 fps is plenty for z's */
        }
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

static void copy_btn_array(cJSON *arr, ui_button_t *out, int *n)
{
    *n = 0;
    if (!cJSON_IsArray(arr)) return;
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (*n >= UI_MAX_BUTTONS) break;
        if (cJSON_IsString(it) && it->valuestring[0]) {
            copy_limited(out[*n].text, UI_BTN_TEXT_LEN, it->valuestring, 12);
            (*n)++;
        }
    }
}

static bool parse_buttons(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;
    ui_button_t tb[UI_MAX_BUTTONS], eb[UI_MAX_BUTTONS];
    int tn = 0, en = 0;
    char shake[UI_BTN_TEXT_LEN] = "想你了";
    if (cJSON_IsArray(root)) {
        copy_btn_array(root, tb, &tn);
    } else if (cJSON_IsObject(root)) {
        copy_btn_array(cJSON_GetObjectItem(root, "text"), tb, &tn);
        copy_btn_array(cJSON_GetObjectItem(root, "emoji"), eb, &en);
        cJSON *sh = cJSON_GetObjectItem(root, "shake");
        if (cJSON_IsString(sh) && sh->valuestring[0]) copy_limited(shake, sizeof shake, sh->valuestring, 12);
    } else {
        cJSON_Delete(root);
        return false;
    }
    cJSON_Delete(root);
    if (tn == 0 && en == 0) return false;
    lock();
    memcpy(s_state->text_btn, tb, sizeof tb);
    memcpy(s_state->emoji_btn, eb, sizeof eb);
    s_state->text_btn_n = tn;
    s_state->emoji_btn_n = en;
    strcpy(s_shake_text, shake);
    unlock();
    return true;
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

const char *ui_get_buttons_json(void) { return s_buttons_json; }
const char *ui_shake_text(void) { return s_shake_text; }

/* ---- start ------------------------------------------------------------------ */

void ui_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_fb_lock = xSemaphoreCreateMutex();
    s_dirty = xSemaphoreCreateBinary();
    s_fb = heap_caps_malloc(BOARD_LCD_W * BOARD_LCD_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    s_state = heap_caps_calloc(1, sizeof(ui_state_t), MALLOC_CAP_SPIRAM);
    s_snap = heap_caps_calloc(1, sizeof(ui_state_t), MALLOC_CAP_SPIRAM);
    assert(s_fb && s_state && s_snap);
    s_state->pressed = UI_HIT_NONE;

    char buf[16];
    int rot = DEFAULT_ROTATION;
    if (settings_get_str(KEY_ROTATE, buf, sizeof buf)) {
        int v = atoi(buf);
        if (v == 0 || v == 90 || v == 180 || v == 270) rot = v;
    }
    s_state->theme = DEFAULT_THEME;
    if (settings_get_str(KEY_THEME, buf, sizeof buf)) {
        if (strcmp(buf, "light") == 0) s_state->theme = UI_THEME_LIGHT;
    }
    if (settings_get_str(KEY_ANIM, buf, sizeof buf)) s_anim = strcmp(buf, "off") != 0;
    if (!settings_get_str(KEY_BUTTONS, s_buttons_json, sizeof s_buttons_json) || !parse_buttons(s_buttons_json)) {
        strcpy(s_buttons_json, DEFAULT_BUTTONS);
        parse_buttons(s_buttons_json);
    }
    if (apply_rotation(rot) != ESP_OK) apply_rotation(0);
    ESP_LOGI(TAG, "rotation %d, theme %s, anim %s", s_rotation, ui_get_theme(), s_anim ? "on" : "off");

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
    s_blush_ms = 2200;
    s_state->blush_alpha = s_anim ? 0 : 255;
    recompute();
    unlock();
    if (!s_anim) {
        /* no fade: plain 2 s timer through the tick */
        lock(); s_blush_ms = 2200; unlock();
    }
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
    m->who = who;
    copy_limited(m->text, sizeof m->text, utf8, UI_SAY_MAX_CHARS);
    s_state->scroll = 0;
    unlock();
    mark_dirty();
}

void ui_set_say(const char *utf8)
{
    if (!utf8 || !utf8[0]) return;
    ui_chat_add(CHAT_KE, utf8);
    ui_flash_border();
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

esp_err_t ui_set_theme(const char *name)
{
    int t;
    if (strcmp(name, "dark") == 0) t = UI_THEME_DARK;
    else if (strcmp(name, "light") == 0) t = UI_THEME_LIGHT;
    else return ESP_ERR_INVALID_ARG;
    lock();
    s_state->theme = t;
    unlock();
    settings_set_str(KEY_THEME, name);
    mark_dirty();
    return ESP_OK;
}

const char *ui_get_theme(void) { return s_state->theme == UI_THEME_LIGHT ? "light" : "dark"; }

/* ---- animation ------------------------------------------------------------------ */

void ui_set_anim(bool on)
{
    lock();
    s_anim = on;
    if (!on) {
        s_state->blink = false;
        s_state->face_dx = 0;
        s_state->blush_alpha = 255;
        s_state->anim_tick = -1;
    } else if (s_state->anim_tick < 0) {
        s_state->anim_tick = 0;
    }
    unlock();
    settings_set_str(KEY_ANIM, on ? "on" : "off");
    mark_dirty();
}

bool ui_get_anim(void) { return s_anim; }

void ui_set_sleeping(bool on)
{
    lock();
    s_sleeping = on;
    if (!s_anim) s_state->anim_tick = -1;
    recompute();
    unlock();
    mark_dirty();
}

bool ui_is_sleeping(void) { return s_sleeping; }

void ui_shake(void)
{
    if (!s_anim) return;
    lock();
    s_shake_ms = 600;
    unlock();
}

void ui_flash_border(void)
{
    lock();
    s_flash_ms = 600;
    unlock();
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
    s_state->cam_text[0] = 0;
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
    if (s_state->pressed != id) { s_state->pressed = id; unlock(); mark_dirty(); }
    else unlock();
}

void ui_touch(bool down, int x, int y)
{
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
        if (s_hit >= UI_HIT_TEXT_BTN0 || s_hit == UI_HIT_CAM_BTN || s_hit == UI_HIT_CAM_GALLERY ||
            s_hit == UI_HIT_CAM_BACK || s_hit == UI_HIT_GAL_DELETE || s_hit == UI_HIT_GAL_SEND) {
            set_pressed(s_hit);
        }
        app_on_touch_activity();
        return;
    }
    if (down && s_down) {
        int dx = x - s_down_x, dy = y - s_down_y;
        if (!s_dragging && (abs(dx) > DRAG_PX || abs(dy) > DRAG_PX)) {
            s_dragging = true;
            set_pressed(UI_HIT_NONE);
        }
        if (s_dragging && s_hit == UI_HIT_CHAT) {
            ui_scroll_by(s_last_y - y);
        }
        s_last_x = x;
        s_last_y = y;
        /* long press on face / chat / camera view: talk */
        if (!s_dragging && !s_long_fired && (s_hit == UI_HIT_FACE || s_hit == UI_HIT_CHAT) &&
            now - s_down_t >= (int64_t)LONG_PRESS_MS * 1000) {
            s_long_fired = true;
            app_on_long_press();
        }
        return;
    }
    if (!down && s_down) {
        s_down = false;
        set_pressed(UI_HIT_NONE);
        int total_dx = s_last_x - s_down_x;
        if (s_long_fired) {
            app_on_long_release();
            return;
        }
        if (s_dragging) {
            if (s_hit == UI_HIT_GAL_VIEW && abs(total_dx) > 40) app_on_swipe(total_dx < 0 ? 1 : -1);
            return;
        }
        /* tap */
        lock();
        int hit_up = ui_hit_test(s_state, x, y);
        unlock();
        if (hit_up != s_hit) return;
        app_on_tap(s_hit);
    }
}
