/* Thread-safe UI state + render task + touch dispatch. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "ui_render.h"

void ui_start(void);                   /* loads rotation/theme/buttons from NVS, draws the first frame */

/* face */
void ui_set_face(const char *utf8);    /* base expression, truncated to UI_FACE_MAX_CHARS */
void ui_set_corner(const char *utf8);  /* top-right status text (base: IP / wifi state) */
void ui_blush(void);                   /* "(—//—)" fading in and out for ~2 s */
void ui_override_face(const char *utf8);    /* recording / playing; NULL restores */
void ui_override_corner(const char *utf8);

/* chat */
void ui_chat_add(chat_who_t who, const char *utf8);   /* appends, scrolls to bottom */
void ui_set_say(const char *utf8);     /* = ui_chat_add(CHAT_KE, ...) + silent alert; "" is ignored */
void ui_toast(const char *utf8, int ms);
void ui_scroll_by(int dy);

/* buttons: JSON {"text":[...],"emoji":[...],"shake":"..."} or a plain array (text row) */
esp_err_t ui_set_buttons_json(const char *json);
const char *ui_get_buttons_json(void);          /* current config as JSON (static buffer) */
const char *ui_shake_text(void);

/* display settings (saved to NVS) */
esp_err_t ui_set_rotation(int rotation);
int ui_get_rotation(void);
esp_err_t ui_set_theme(const char *name);
const char *ui_get_theme(void);

/* animation (phase 4.8) */
void ui_set_anim(bool on);
bool ui_get_anim(void);
void ui_set_sleeping(bool on);         /* face-down: sleep face + z's */
bool ui_is_sleeping(void);
void ui_shake(void);                   /* face wobbles left/right briefly */
void ui_flash_border(void);            /* silent alert: two soft flashes */
void ui_set_peek_icon(bool on);

/* screens (camera / gallery) */
void ui_set_screen(ui_screen_t screen);
ui_screen_t ui_get_screen(void);
void ui_set_frame(const uint16_t *frame, int w, int h);   /* image shown on camera/gallery screen */
void ui_set_cam_text(const char *utf8);
void ui_set_gallery_pos(int index, int count);

/* touch dispatch (from the touch task): x,y in current rotation frame */
void ui_touch(bool down, int x, int y);

/* snapshot for the host preview / tests */
void ui_get_state_copy(ui_state_t *out);
