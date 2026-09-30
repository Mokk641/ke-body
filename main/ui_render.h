/* Screen layout / hit testing for the ke-body UI. Pure C (no ESP-IDF), shared
 * with the host tools. All coordinates are in the current rotation's frame
 * (gfx_width x gfx_height): 480x320 landscape or 320x480 portrait.
 *
 * Screens:
 *   FACE     big face, latest sentence underneath, "swipe up" hint       (default)
 *   CHAT     top bar (face + status), messages, bottom bar (+ / camera), slide-up panel
 *   CAMERA / GALLERY   unchanged from phase 4
 * FACE <-> CHAT slide vertically; page_pos animates 0 (FACE) .. 255 (CHAT). */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "ink.h"

#define UI_FACE_MAX_CHARS 20   /* code points accepted by POST /face */
#define UI_SAY_MAX_CHARS  60   /* code points accepted by POST /say  */
#define UI_FACE_BUF       (UI_FACE_MAX_CHARS * 4 + 1)
#define UI_SAY_BUF        (UI_SAY_MAX_CHARS * 4 + 1)

#define UI_THEME_DARK  0
#define UI_THEME_LIGHT 1

#define UI_CHAT_MAX        50    /* messages kept (ring buffer) */
#define UI_MAX_TEXT_BTN    8
#define UI_MAX_EMOJI_BTN   30
#define UI_MAX_BUTTONS     UI_MAX_EMOJI_BTN
#define UI_TEXT_BTN_CHARS  12
#define UI_EMOJI_BTN_CHARS 20
#define UI_BTN_TEXT_LEN    96

typedef enum { CHAT_KE = 0, CHAT_HER = 1 } chat_who_t;

typedef struct {
    uint8_t who;              /* chat_who_t */
    char face[UI_FACE_BUF];   /* CHAT_KE: the expression when this was sent (the avatar) */
    char text[UI_SAY_BUF];
    uint16_t ink_id;          /* 0 = a text message; else handwriting, its thumbnail in the ink pool (see ink_thumb_get) */
    uint16_t pic_id;          /* 0 = none; else a picture from Ke, its thumbnail in the picture pool (see pics_get) */
} chat_msg_t;

typedef enum { UI_SCREEN_FACE = 0, UI_SCREEN_CHAT, UI_SCREEN_CAMERA, UI_SCREEN_GALLERY, UI_SCREEN_COLORTEST, UI_SCREEN_INK, UI_SCREEN_VIEWER, UI_SCREEN_MUSIC, UI_SCREEN_MUSIC_LIST } ui_screen_t;

typedef struct { char text[UI_BTN_TEXT_LEN]; } ui_button_t;

#define UI_MUSIC_TITLE 64
/* what the music player page shows (filled by music.c through ui_set_music) */
typedef struct {
    int count, current;        /* songs in the list; the loaded one (-1 = none) */
    bool playing, paused;
    int elapsed_s, total_s;    /* total_s 0 = not known yet */
    int progress_pm;           /* 0..1000 */
    int volume;                /* 0..100 */
    char title[UI_MUSIC_TITLE];
} music_info_t;

typedef struct {
    /* face (shared by the face page and the chat top bar) */
    char face[UI_FACE_BUF];   /* what is drawn now: base face, or a temporary override */
    char corner[48];          /* small status text (IP / "在听" ...) */
    int theme;                /* UI_THEME_DARK / UI_THEME_LIGHT */
    ui_screen_t screen;       /* logical screen (the target while page_pos is still moving) */
    int page_pos;             /* 0 = face page fully shown ... 255 = chat page fully shown */
    bool online;              /* bridge reachable (green dot) */

    /* chat page */
    chat_msg_t msgs[UI_CHAT_MAX];
    int msg_count;            /* 0..UI_CHAT_MAX, oldest first */
    int scroll;               /* pixels scrolled up from the newest message */
    int slide_dy;             /* newest bubble slides in from this many px below its place */
    ui_button_t text_btn[UI_MAX_TEXT_BTN];
    int text_btn_n;
    ui_button_t emoji_btn[UI_MAX_EMOJI_BTN];
    int emoji_btn_n;
    bool panel_open;          /* target state of the slide-up panel */
    int panel_pos;            /* 0 = hidden ... 255 = fully out (animated) */
    int emoji_page;
    int pressed;              /* hit id currently pressed (visual), or UI_HIT_NONE */
    char toast[UI_SAY_BUF];   /* short notice over the middle of the screen */

    /* face page */
    int line_alpha;           /* 0..255 fade-in of the latest sentence */
    int face_dx;              /* horizontal shake offset in px */
    int blush_alpha;          /* 0..255: the "//" of a blush face fades with this */
    bool blink;               /* eyes closed this frame */
    bool sleeping;            /* z's floating above the face */
    int anim_tick;            /* free-running 50 ms counter for the z animation; < 0 = z's off (static "zzz") */
    int flash;                /* 0..255 border flash intensity (optional, off by default) */
    bool peek_on;             /* eye icon in the corner: remote snapshots allowed */

    /* camera / gallery screens */
    const uint16_t *frame;    /* byte-swapped RGB565 image to show (preview or photo), or NULL */
    int frame_w, frame_h;
    music_info_t music;
    const char (*music_names)[UI_MUSIC_TITLE];   /* song titles for the list page (owned by music.c) */
    int music_scroll;         /* list page: pixels scrolled */
    int ink_scroll;           /* characters the strip is scrolled back from the newest (0 = follow the newest) */
    const ink_t *ink;         /* handwriting page: the strokes (owned by ui.c, read by the renderer) */
    int dot_x, dot_y, dot_ms; /* touch marker (touchlog on): where, and how much longer it stays */
    bool review;              /* gallery screen shows the photo just taken (retake / send / keep) */
    bool sending;             /* a message/photo is on its way: buttons greyed, taps ignored */
    char cam_text[64];        /* status line on the camera/gallery screen */
    int gal_index, gal_count; /* "3 / 12" */
} ui_state_t;

/* Hit ids returned by ui_hit_test */
enum {
    UI_HIT_NONE = -1,
    UI_HIT_FACE = 0,       /* face page: anywhere except the hint */
    UI_HIT_CHAT,           /* chat page: the message area (and the area above an open panel) */
    UI_HIT_CAM_BTN,        /* chat page bottom bar: camera icon */
    UI_HIT_CAM_VIEW,       /* camera screen: viewfinder / "拍照" */
    UI_HIT_CAM_GALLERY,    /* camera screen: "相册" */
    UI_HIT_CAM_BACK,       /* camera / gallery: "返回" */
    UI_HIT_GAL_VIEW,       /* gallery: photo (swipe) */
    UI_HIT_GAL_DELETE,
    UI_HIT_GAL_SEND,       /* "寄给克" */
    UI_HIT_HINT,           /* face page: "上滑聊天" */
    UI_HIT_TOP_BACK,       /* chat page top bar: the down chevron */
    UI_HIT_TOPBAR,         /* chat page top bar: the rest (drag down = back to the face page) */
    UI_HIT_PLUS,           /* chat page bottom bar: + */
    UI_HIT_INK_OPEN,       /* the 手写 capsule in the + panel */
    UI_HIT_INK_BACK,       /* handwriting page: back to the chat */
    UI_HIT_INK_PAD,        /* handwriting page: the square you write in */
    UI_HIT_INK_NEXT,       /* 下一个 */
    UI_HIT_INK_UNDO,       /* 撤销一笔 */
    UI_HIT_INK_CLEAR,      /* 清空 */
    UI_HIT_INK_SEND,       /* 寄 */
    UI_HIT_INK_STRIP,      /* handwriting page: the strip of finished characters (drag sideways to scroll) */
    UI_HIT_MUSIC_OPEN,     /* the 音乐 capsule in the + panel */
    UI_HIT_GAME_OPEN,      /* the 游戏 capsule in the + panel */
    UI_HIT_MUSIC_BACK,     /* player: back to the chat */
    UI_HIT_MUSIC_LIST,     /* player: the song list button */
    UI_HIT_MUSIC_PREV,
    UI_HIT_MUSIC_PLAY,     /* play / pause */
    UI_HIT_MUSIC_NEXT,
    UI_HIT_MUSIC_VOL,      /* volume slider (drag) */
    UI_HIT_MUSIC_LIST_BACK,/* list: back to the player */
    UI_HIT_MUSIC_LIST_BG,  /* list: empty space between rows (drag scrolls) */
    UI_HIT_VIEW_EXIT,      /* picture viewer: tap anywhere to go back */
    UI_HIT_TEST_EXIT,      /* colour test pattern: tap anywhere to leave */
    UI_HIT_PANEL,          /* open panel: empty part (also the panel while it is still sliding) */
    UI_HIT_TEXT_BTN0 = 100,   /* + index into text_btn */
    UI_HIT_EMOJI_BTN0 = 200,  /* + index into emoji_btn (absolute, not per page) */
    UI_HIT_MUSIC_ROW0 = 300,  /* + index into the song list (music list page) */
    UI_HIT_PIC0 = 400,        /* + index of a chat message that is a picture from Ke (tap = full screen) */
};

/* Draw the whole screen into the gfx framebuffer (gfx_init must have been called). */
void ui_render(const ui_state_t *s);

/* Which element is at (x,y) for the given state. */
int ui_hit_test(const ui_state_t *s, int x, int y);

/* Chat scrolling: total content height and the visible viewport height (shrinks while the panel is out). */
int ui_chat_content_height(const ui_state_t *s);
int ui_chat_area_height(const ui_state_t *s);

/* Emoji grid paging for the current orientation. */
int ui_emoji_per_page(void);
int ui_emoji_pages(const ui_state_t *s);

/* Handwriting, incremental: draw the straight segments for points [from, to) of the current character on top of what
 * is already on the screen (same pen as the full render) and return the changed rectangle (screen px, x1/y1 exclusive).
 * false when nothing was drawn. Used while the pen is down so a line appears at once; the pen-up redraws it smooth. */
bool ui_render_ink_incremental(const ui_state_t *s, int from, int to, int *x0, int *y0, int *x1, int *y1);

/* The largest picture that fits a chat bubble in the current orientation (for POST /ink and /image thumbnails). */
void ui_chat_picture_box(int *w, int *h);

/* Music player page geometry: volume from a touch x (0..100), and list scrolling limits. */
int ui_music_vol_from_x(int x);
int ui_music_list_max_scroll(const ui_state_t *s);

/* Strip of finished characters: how many are visible at once and how wide each is (for scrolling). */
int ui_ink_strip_cap(void);
int ui_ink_strip_cell(void);

/* The handwriting square in the current orientation (screen pixels): left, top, side. */
void ui_ink_pad_rect(int *x, int *y, int *side);

/* Latest sentence from Ke (shown under the face); "" if none. */
const char *ui_latest_ke_text(const ui_state_t *s);
