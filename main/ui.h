/* Thread-safe UI state + render task + touch dispatch. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "ui_render.h"

void ui_start(void);                   /* loads rotation/theme/buttons/animation switches from NVS, draws the first frame */

/* face */
void ui_set_face(const char *utf8);    /* base expression, truncated to UI_FACE_MAX_CHARS */
void ui_set_corner(const char *utf8);  /* small status text (base: IP / wifi state) */
void ui_blush(void);                   /* "(—//—)" with a pink // fading in and out for ~2 s */
void ui_override_face(const char *utf8);    /* recording / playing; NULL restores */
void ui_override_corner(const char *utf8);

/* chat */
void ui_chat_add(chat_who_t who, const char *utf8);
void ui_chat_add_ink(int thumb_slot);   /* her handwriting: a small picture bubble on her side */
void ui_ke_ink(int thumb_slot);         /* Ke's handwriting (POST /ink): a picture bubble on his side + the usual alert */   /* appends (Ke's carry the current face as avatar), scrolls to bottom */
void ui_set_say(const char *utf8);     /* Ke's message: chat + face-page line + "(—o—)" notice; "" is ignored */
void ui_toast(const char *utf8, int ms);
void ui_scroll_by(int dy);
void ui_set_online(bool online);       /* bridge reachable: green dot in the chat top bar */

/* pages */
void ui_go_page(ui_screen_t page);     /* UI_SCREEN_FACE or UI_SCREEN_CHAT, with the slide animation */
void ui_set_screen(ui_screen_t screen);/* immediate switch (camera / gallery flows) */
ui_screen_t ui_get_screen(void);
void ui_panel_set(bool open);          /* slide-up panel on the chat page */
bool ui_panel_is_open(void);
void ui_emoji_page_step(int dir);      /* +1 / -1 */

#define BUTTONS_JSON_MAX 2048

/* buttons: JSON {"text":[...],"emoji":[...],"shake":"..."} or a plain array (= the phrase row) */
esp_err_t ui_set_buttons_json(const char *json);
esp_err_t ui_reset_buttons(void);                 /* back to the built-in defaults (erases the saved config) */
const char *ui_get_buttons_json(void);            /* current config as JSON (static buffer) */
const char *ui_shake_text(void);
bool ui_button_text(bool emoji, int index, char *out, size_t out_len);   /* false if no such button */

/* display settings (saved to NVS) */
esp_err_t ui_set_rotation(int rotation);
void ui_refresh_after_rotation(void);   /* after board_lcd_set_rotation() by auto-rotate */
int ui_get_rotation(void);
esp_err_t ui_set_theme(const char *name);
const char *ui_get_theme(void);          /* "light" | "dark" | "auto" (the mode; auto = dark during the night schedule) */

/* animations. Each one has its own switch (saved to NVS); "all" is the master switch.
 * names: blink (default off), blush, zzz, shake (default on), flash = border flash (default off) */
bool ui_anim_set(const char *name, bool on);      /* false if the name is unknown */
bool ui_anim_get(const char *name, bool *on);     /* false if the name is unknown */
void ui_anim_status(char *out, size_t out_len);
void ui_set_anim(bool on);                        /* = ui_anim_set("all", on) */
bool ui_get_anim(void);
void ui_set_sleeping(bool on);         /* face-down: sleep face + z's */
bool ui_is_sleeping(void);
void ui_shake(void);                   /* face wobbles left/right briefly */
void ui_flash_border(void);            /* optional silent alert: two soft border flashes (only if the flash switch is on) */
void ui_set_peek_icon(bool on);
void ui_set_sending(bool on);          /* a message / photo is on its way: quick buttons and "send" greyed, taps ignored */
bool ui_is_sending(void);
void ui_set_review(bool on);           /* gallery screen = the photo just taken: retake / send / keep */
void ui_display_hold(bool hold);       /* stop / resume LCD refresh (while the camera grabs a photo); same task must pair the calls */

/* camera / gallery screens */
void ui_set_frame(const uint16_t *frame, int w, int h);   /* image shown on camera/gallery screen */
void ui_set_cam_text(const char *utf8);
void ui_set_gallery_pos(int index, int count);

/* handwriting page (the strokes are kept while you go back to the chat) */
void ui_ink_open(void);

/* debugging aid: draw a ring where the finger is (`touchlog on`) */
void ui_set_touchlog(bool on);

/* touch dispatch (from the touch task): x,y in current rotation frame.
 * x,y are ignored when down == false; the last position seen while down is used. */
void ui_touch(bool down, int x, int y);

/* snapshot for the host preview / tests */
void ui_get_state_copy(ui_state_t *out);
