/* Handwriting: the board is only paper. Strokes are collected, drawn, and sent as one wide PNG of the whole
 * sentence; nothing is recognised here. Pure C (no ESP-IDF), shared with the host tests.
 *
 * Coordinates are normalised to 0..INK_RANGE-1 inside the square writing pad, so the same strokes can be drawn at
 * any size: on the pad, small in the preview strip, at 96 px in the PNG, at 32 px in a chat thumbnail. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INK_RANGE       1024
#define INK_MAX_CHARS   40      /* characters in one sentence */
#define INK_MAX_STROKES 32      /* per character */
#define INK_MAX_PTS     800     /* per character */

typedef struct { uint16_t x, y; } ink_pt_t;

typedef struct {
    uint8_t nstrokes;
    uint16_t npts;
    uint16_t start[INK_MAX_STROKES];   /* index of each stroke's first point; a stroke ends where the next begins */
    ink_pt_t pts[INK_MAX_PTS];
} ink_char_t;

typedef struct {
    ink_char_t cur;                    /* the character being written on the pad */
    ink_char_t done[INK_MAX_CHARS];    /* committed with 下一个, shown in the preview strip */
    int ndone;
    bool pen_down;                     /* a stroke is in progress */
} ink_t;

/* ---- editing ------------------------------------------------------------------------------------------ */
void ink_reset(ink_t *k);                                  /* everything gone */
bool ink_pen_down(ink_t *k, int x, int y);                 /* start a stroke at normalised x,y; false if full */
bool ink_pen_move(ink_t *k, int x, int y);                 /* extend it (points closer than ~2 px are skipped) */
void ink_pen_up(ink_t *k);
bool ink_is_empty(const ink_char_t *c);
bool ink_next(ink_t *k);                                   /* 下一个: current -> strip. false if empty or the strip is full */
void ink_undo(ink_t *k);                                   /* 撤销一笔: last stroke; on an empty pad the last strip character comes back first */
void ink_clear(ink_t *k);                                  /* 清空: the pad; on an empty pad, the whole strip */
bool ink_finish(ink_t *k);                                 /* before sending: commit a character still on the pad; true if there is anything to send */

/* ---- geometry: the smoothed path of a stroke -------------------------------------------------------------- */
/* Calls seg() with short straight segments approximating the stroke (midpoint quadratic smoothing), in output
 * coordinates ox + x*scale, oy + y*scale. A single-point stroke gives one zero-length segment (a round dot). */
typedef void (*ink_seg_fn)(void *ctx, float x0, float y0, float x1, float y1);
void ink_flatten(const ink_char_t *c, int stroke, float ox, float oy, float scale, ink_seg_fn seg, void *ctx);

/* ---- rendering for sending ------------------------------------------------------------------------------- */
#define INK_PNG_CELL   96       /* height of a PNG row and the width of every character */
#define INK_PNG_PER_ROW 16      /* more characters than this wrap onto further rows */
#define INK_HAND_W 192          /* the box for her own handwriting in the chat: characters at 32, 24, 20 or 16 px fit 18, 32, 36, 72 */
#define INK_HAND_H 96
#define INK_THUMB_MAX_W 320     /* thumbnail pool slots also hold handwriting from the PC (POST /ink), which may be bigger */
#define INK_THUMB_MAX_H 160

/* One sentence as an 8-bit grey PNG, black strokes on white, one INK_PNG_CELL square per character (the committed
 * ones), INK_PNG_PER_ROW to a row and further rows below (so a short sentence is one wide strip, a long one a block).
 * *png is malloc'd. Returns false when there is nothing or no memory. */
bool ink_make_png(const ink_t *k, uint8_t **png, size_t *len, int *w, int *h);

/* The same sentence small, as an 8-bit coverage mask (255 = ink), as many characters per row as fit the box. mask must
 * hold INK_HAND_W * INK_HAND_H bytes; the used size is *w x *h (tightly packed). */
bool ink_make_thumb(const ink_t *k, uint8_t *mask, int *w, int *h);

/* Wrap 8-bit grey pixels in a PNG using stored (uncompressed) deflate blocks. *out is malloc'd. */
bool ink_png_from_gray(const uint8_t *gray, int w, int h, uint8_t **out, size_t *len);

/* Chat thumbnails live in a small pool so chat messages only carry a slot number. */
#define INK_THUMB_SLOTS 8
int ink_thumb_store(const uint8_t *mask, int w, int h);              /* returns the slot (oldest one is reused) */
const uint8_t *ink_thumb_get(int slot, int *w, int *h);              /* NULL if the slot is empty */
