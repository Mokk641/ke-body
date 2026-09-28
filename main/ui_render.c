#include "ui_render.h"
#include "gfx.h"
#include "fonts/fonts.h"
#include <string.h>

#define COL_BG        GFX_RGB(0xF7, 0xF2, 0xEA)  /* warm off-white */
#define COL_FACE      GFX_RGB(0x2B, 0x2B, 0x33)
#define COL_CORNER    GFX_RGB(0x8A, 0x86, 0x80)
#define COL_BUBBLE    GFX_RGB(0xFF, 0xFF, 0xFF)
#define COL_BUBBLE_BD GFX_RGB(0xD8, 0xD2, 0xC8)
#define COL_SAY       GFX_RGB(0x33, 0x33, 0x3A)

#define SCREEN_W 320
#define SCREEN_H 480
#define MARGIN   12

#define BUBBLE_MAX_LINES 6
#define BUBBLE_PAD       12
#define BUBBLE_BOTTOM    (SCREEN_H - 16)

static void draw_face(const char *face, int center_y)
{
    const kb_font_t *cands[] = { &kb_font_face64, &kb_font_face44, &kb_font_face30 };
    const int max_w = SCREEN_W - 2 * MARGIN;
    const kb_font_t *f = cands[2];
    for (unsigned i = 0; i < sizeof(cands) / sizeof(cands[0]); i++) {
        if (gfx_text_width(cands[i], face) <= max_w) { f = cands[i]; break; }
    }
    gfx_line_t lines[2];
    int n = gfx_wrap(f, face, max_w, lines, 2);
    if (n <= 0) return;
    int lh = f->line_height;
    int top = center_y - (n * lh) / 2;
    for (int i = 0; i < n; i++) {
        char buf[UI_FACE_BUF];
        int len = lines[i].len < (int)sizeof(buf) - 1 ? lines[i].len : (int)sizeof(buf) - 1;
        memcpy(buf, lines[i].start, len);
        buf[len] = 0;
        gfx_draw_text_centered(f, SCREEN_W / 2, top + i * lh + f->ascent, buf, COL_FACE);
    }
}

static void draw_bubble(const char *say)
{
    const kb_font_t *f = &kb_font_text22;
    const int box_x = MARGIN, box_w = SCREEN_W - 2 * MARGIN;
    const int text_w = box_w - 2 * BUBBLE_PAD;
    gfx_line_t lines[BUBBLE_MAX_LINES];
    int n = gfx_wrap(f, say, text_w, lines, BUBBLE_MAX_LINES);
    if (n <= 0) return;
    int lh = f->line_height + 2;
    int box_h = n * lh + 2 * BUBBLE_PAD;
    int box_y = BUBBLE_BOTTOM - box_h;

    /* little tail pointing up toward the face */
    gfx_fill_round_rect(box_x, box_y, box_w, box_h, 14, COL_BUBBLE_BD);
    gfx_fill_round_rect(box_x + 2, box_y + 2, box_w - 4, box_h - 4, 12, COL_BUBBLE);
    for (int i = 0; i < 8; i++) {
        gfx_fill_rect(SCREEN_W / 2 - i, box_y - 8 + i, 2 * i + 1, 1, COL_BUBBLE_BD);
    }
    for (int i = 0; i < 6; i++) {
        gfx_fill_rect(SCREEN_W / 2 - i, box_y - 5 + i, 2 * i + 1, 1, COL_BUBBLE);
    }
    gfx_fill_rect(SCREEN_W / 2 - 6, box_y + 1, 13, 2, COL_BUBBLE);

    for (int i = 0; i < n; i++) {
        char buf[UI_SAY_BUF + 8];
        int len = lines[i].len < (int)sizeof(buf) - 4 ? lines[i].len : (int)sizeof(buf) - 4;
        memcpy(buf, lines[i].start, len);
        buf[len] = 0;
        /* if wrapping was cut off, mark the last line */
        if (i == n - 1 && n == BUBBLE_MAX_LINES && lines[i].start[lines[i].len] != 0) {
            strcat(buf, "…");
        }
        gfx_draw_text(f, box_x + BUBBLE_PAD, box_y + BUBBLE_PAD + i * lh + f->ascent, buf, COL_SAY);
    }
}

void ui_render(const ui_state_t *s)
{
    gfx_fill(COL_BG);

    /* corner status / IP */
    if (s->corner[0]) {
        const kb_font_t *f = &kb_font_small14;
        int w = gfx_text_width(f, s->corner);
        gfx_draw_text(f, SCREEN_W - 8 - w, 8 + f->ascent, s->corner, COL_CORNER);
    }

    /* face sits in the upper part of the screen; the bubble takes the bottom */
    draw_face(s->face[0] ? s->face : "(—_—)", s->say[0] ? 170 : 220);

    if (s->say[0]) draw_bubble(s->say);
}
