#include "ui_render.h"
#include "gfx.h"
#include "fonts/fonts.h"
#include <string.h>

typedef struct { uint16_t bg, face, corner, bubble, bubble_bd, say; } palette_t;

static const palette_t PAL_LIGHT = {
    .bg = GFX_RGB(0xF7, 0xF2, 0xEA),   /* warm off-white */
    .face = GFX_RGB(0x2B, 0x2B, 0x33),
    .corner = GFX_RGB(0x8A, 0x86, 0x80),
    .bubble = GFX_RGB(0xFF, 0xFF, 0xFF),
    .bubble_bd = GFX_RGB(0xD8, 0xD2, 0xC8),
    .say = GFX_RGB(0x33, 0x33, 0x3A),
};

static const palette_t PAL_DARK = {
    .bg = GFX_RGB(0x00, 0x00, 0x00),   /* pure black, matches the case */
    .face = GFX_RGB(0xF4, 0xF4, 0xF4),
    .corner = GFX_RGB(0x8C, 0x8C, 0x8C),
    .bubble = GFX_RGB(0x1C, 0x1C, 0x21),
    .bubble_bd = GFX_RGB(0x40, 0x40, 0x48),
    .say = GFX_RGB(0xEC, 0xEC, 0xEC),
};

#define MARGIN     12
#define BUBBLE_PAD 12

static void draw_face(const palette_t *p, const char *face, int center_y)
{
    const int W = gfx_width();
    const kb_font_t *cands[] = { &kb_font_face64, &kb_font_face44, &kb_font_face30 };
    const int max_w = W - 2 * MARGIN;
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
        gfx_draw_text_centered(f, W / 2, top + i * lh + f->ascent, buf, p->face);
    }
}

/* Returns the top y of the bubble (or the screen bottom when there is none). */
static void draw_bubble(const palette_t *p, const char *say, int max_lines)
{
    const int W = gfx_width(), H = gfx_height();
    const kb_font_t *f = &kb_font_text22;
    const int box_x = MARGIN, box_w = W - 2 * MARGIN;
    const int text_w = box_w - 2 * BUBBLE_PAD;
    gfx_line_t lines[6];
    if (max_lines > 6) max_lines = 6;
    int n = gfx_wrap(f, say, text_w, lines, max_lines);
    if (n <= 0) return;
    int lh = f->line_height + 2;
    int box_h = n * lh + 2 * BUBBLE_PAD;
    int box_y = (H - 16) - box_h;

    gfx_fill_round_rect(box_x, box_y, box_w, box_h, 14, p->bubble_bd);
    gfx_fill_round_rect(box_x + 2, box_y + 2, box_w - 4, box_h - 4, 12, p->bubble);
    /* little tail pointing up toward the face */
    for (int i = 0; i < 8; i++) {
        gfx_fill_rect(W / 2 - i, box_y - 8 + i, 2 * i + 1, 1, p->bubble_bd);
    }
    for (int i = 0; i < 6; i++) {
        gfx_fill_rect(W / 2 - i, box_y - 5 + i, 2 * i + 1, 1, p->bubble);
    }
    gfx_fill_rect(W / 2 - 6, box_y + 1, 13, 2, p->bubble);

    for (int i = 0; i < n; i++) {
        char buf[UI_SAY_BUF + 8];
        int len = lines[i].len < (int)sizeof(buf) - 4 ? lines[i].len : (int)sizeof(buf) - 4;
        memcpy(buf, lines[i].start, len);
        buf[len] = 0;
        /* if wrapping was cut off, mark the last line */
        if (i == n - 1 && n == max_lines && lines[i].start[lines[i].len] != 0) {
            strcat(buf, "…");
        }
        gfx_draw_text(f, box_x + BUBBLE_PAD, box_y + BUBBLE_PAD + i * lh + f->ascent, buf, p->say);
    }
}

void ui_render(const ui_state_t *s)
{
    const palette_t *p = (s->theme == UI_THEME_LIGHT) ? &PAL_LIGHT : &PAL_DARK;
    const int W = gfx_width(), H = gfx_height();
    const bool landscape = W > H;
    const bool has_say = s->say[0] != 0;

    gfx_fill(p->bg);

    /* corner status / IP */
    if (s->corner[0]) {
        const kb_font_t *f = &kb_font_small14;
        int w = gfx_text_width(f, s->corner);
        gfx_draw_text(f, W - 8 - w, 8 + f->ascent, s->corner, p->corner);
    }

    /* face sits in the upper part of the screen; the bubble takes the bottom.
     * landscape 480x320: bubble up to 4 lines (128 px) from y=176, face centred at 100
     * portrait 320x480: bubble up to 6 lines from y=260, face centred at 170 */
    int face_cy = landscape ? (has_say ? 100 : 150) : (has_say ? 170 : 220);
    draw_face(p, s->face[0] ? s->face : "(—_—)", face_cy);

    if (has_say) draw_bubble(p, s->say, landscape ? 4 : 6);
}
