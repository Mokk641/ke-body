/* Screen layout / hit testing for the ke-body UI. Pure C (no ESP-IDF), shared
 * with tools/host_preview.c. All coordinates are in the current rotation's
 * frame (gfx_width x gfx_height): 480x320 landscape or 320x480 portrait. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define UI_FACE_MAX_CHARS 20   /* code points accepted by POST /face */
#define UI_SAY_MAX_CHARS  60   /* code points accepted by POST /say  */
#define UI_FACE_BUF       (UI_FACE_MAX_CHARS * 4 + 1)
#define UI_SAY_BUF        (UI_SAY_MAX_CHARS * 4 + 1)

#define UI_THEME_DARK  0
#define UI_THEME_LIGHT 1

#define UI_CHAT_MAX     50     /* messages kept (ring buffer) */
#define UI_MAX_BUTTONS  8
#define UI_BTN_TEXT_LEN 48

typedef enum { CHAT_KE = 0, CHAT_HER = 1 } chat_who_t;

typedef struct {
    uint8_t who;              /* chat_who_t */
    char text[UI_SAY_BUF];
} chat_msg_t;

typedef enum { UI_SCREEN_CHAT = 0, UI_SCREEN_CAMERA, UI_SCREEN_GALLERY } ui_screen_t;

typedef struct { char text[UI_BTN_TEXT_LEN]; } ui_button_t;

typedef struct {
    /* face band */
    char face[UI_FACE_BUF];   /* kaomoji shown at the top */
    char corner[48];          /* small status text in the top-right corner (IP or state) */
    int theme;                /* UI_THEME_DARK / UI_THEME_LIGHT */
    ui_screen_t screen;

    /* chat screen */
    chat_msg_t msgs[UI_CHAT_MAX];
    int msg_count;            /* 0..UI_CHAT_MAX, oldest first */
    int scroll;               /* pixels scrolled up from the newest message */
    ui_button_t text_btn[UI_MAX_BUTTONS];
    int text_btn_n;
    ui_button_t emoji_btn[UI_MAX_BUTTONS];
    int emoji_btn_n;
    int pressed;              /* hit id currently pressed (visual), or UI_HIT_NONE */
    char toast[UI_SAY_BUF];   /* short notice drawn over the chat area */

    /* animation (phase 4.8) */
    int face_dx;              /* horizontal shake offset in px */
    int blush_alpha;          /* 0..255: the "//" of a blush face fades with this */
    bool blink;               /* eyes closed this frame */
    bool sleeping;            /* z's floating above the face */
    int anim_tick;            /* free-running 50 ms counter for the z animation */
    int flash;                /* 0..255 border flash intensity */
    bool peek_on;             /* eye icon in the corner: remote snapshots allowed */

    /* camera / gallery screens */
    const uint16_t *frame;    /* byte-swapped RGB565 image to show (preview or photo), or NULL */
    int frame_w, frame_h;
    char cam_text[64];        /* status line on the camera/gallery screen */
    int gal_index, gal_count; /* "3 / 12" */
} ui_state_t;

/* Hit ids returned by ui_hit_test */
enum {
    UI_HIT_NONE = -1,
    UI_HIT_FACE = 0,
    UI_HIT_CHAT,
    UI_HIT_CAM_BTN,        /* chat screen: "相机" button */
    UI_HIT_CAM_VIEW,       /* camera screen: viewfinder (tap = shoot) */
    UI_HIT_CAM_GALLERY,    /* camera screen: "相册" */
    UI_HIT_CAM_BACK,       /* camera / gallery: "返回" */
    UI_HIT_GAL_VIEW,       /* gallery: photo (swipe) */
    UI_HIT_GAL_DELETE,
    UI_HIT_GAL_SEND,       /* "寄给克" */
    UI_HIT_TEXT_BTN0 = 100,   /* + index */
    UI_HIT_EMOJI_BTN0 = 200,  /* + index */
};

/* Draw the whole screen into the gfx framebuffer (gfx_init must have been called). */
void ui_render(const ui_state_t *s);

/* Which element is at (x,y) for the given state. */
int ui_hit_test(const ui_state_t *s, int x, int y);

/* Total chat content height in px (for scroll clamping) and the chat area height. */
int ui_chat_content_height(const ui_state_t *s);
int ui_chat_area_height(void);
