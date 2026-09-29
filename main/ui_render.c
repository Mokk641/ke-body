#include "ui_render.h"
#include "gfx.h"
#include "fonts/fonts.h"
#include <string.h>
#include <stdio.h>

typedef struct {
    uint16_t bg, face, corner, bubble, bubble_bd, say;   /* Ke's bubbles (left) */
    uint16_t her_bubble, her_bd, her_text;                /* her bubbles (right) */
    uint16_t btn, btn_bd, btn_text, btn_pressed, accent, blush;
} palette_t;

static const palette_t PAL_LIGHT = {
    .bg = GFX_RGB(0xF7, 0xF2, 0xEA), .face = GFX_RGB(0x2B, 0x2B, 0x33), .corner = GFX_RGB(0x8A, 0x86, 0x80),
    .bubble = GFX_RGB(0xFF, 0xFF, 0xFF), .bubble_bd = GFX_RGB(0xD8, 0xD2, 0xC8), .say = GFX_RGB(0x33, 0x33, 0x3A),
    .her_bubble = GFX_RGB(0xDD, 0xEF, 0xD0), .her_bd = GFX_RGB(0xB8, 0xD4, 0xA6), .her_text = GFX_RGB(0x24, 0x30, 0x20),
    .btn = GFX_RGB(0xFF, 0xFF, 0xFF), .btn_bd = GFX_RGB(0xC8, 0xC2, 0xB8), .btn_text = GFX_RGB(0x33, 0x33, 0x3A),
    .btn_pressed = GFX_RGB(0xD0, 0xCA, 0xC0), .accent = GFX_RGB(0xF0, 0x8A, 0x9A), .blush = GFX_RGB(0xF0, 0x70, 0x90),
};

static const palette_t PAL_DARK = {
    .bg = GFX_RGB(0x00, 0x00, 0x00), .face = GFX_RGB(0xF4, 0xF4, 0xF4), .corner = GFX_RGB(0x8C, 0x8C, 0x8C),
    .bubble = GFX_RGB(0x1C, 0x1C, 0x21), .bubble_bd = GFX_RGB(0x40, 0x40, 0x48), .say = GFX_RGB(0xEC, 0xEC, 0xEC),
    .her_bubble = GFX_RGB(0x1E, 0x33, 0x25), .her_bd = GFX_RGB(0x3A, 0x5A, 0x44), .her_text = GFX_RGB(0xE6, 0xF2, 0xE6),
    .btn = GFX_RGB(0x16, 0x16, 0x1A), .btn_bd = GFX_RGB(0x48, 0x48, 0x50), .btn_text = GFX_RGB(0xE8, 0xE8, 0xE8),
    .btn_pressed = GFX_RGB(0x50, 0x50, 0x58), .accent = GFX_RGB(0xF0, 0x8A, 0x9A), .blush = GFX_RGB(0xFF, 0x80, 0xA0),
};

#define MARGIN      8
#define BTN_H       30
#define BTN_GAP     4
#define BUBBLE_PAD  8
#define MSG_GAP     6
#define CAM_LABEL   "相机"

/* ---- layout ------------------------------------------------------------- */

typedef struct {
    int W, H;
    bool landscape;
    int face_h;                 /* face band height */
    int chat_y, chat_h;         /* chat area */
    int row1_y, row2_y;         /* button rows (y of top edge) */
} layout_t;

static layout_t layout(void)
{
    layout_t L;
    L.W = gfx_width();
    L.H = gfx_height();
    L.landscape = L.W > L.H;
    L.face_h = L.landscape ? 66 : 84;
    L.row2_y = L.H - MARGIN - BTN_H;
    L.row1_y = L.row2_y - BTN_GAP - BTN_H;
    L.chat_y = L.face_h;
    L.chat_h = L.row1_y - BTN_GAP - L.chat_y;
    return L;
}

/* button i of n in a row (the text row also has the camera button at the end) */
static void btn_rect(const layout_t *L, int row, int i, int n, int *x, int *y, int *w)
{
    int total = L->W - 2 * MARGIN;
    int bw = (total - (n - 1) * BTN_GAP) / n;
    *x = MARGIN + i * (bw + BTN_GAP);
    *y = row == 0 ? L->row1_y : L->row2_y;
    *w = bw;
}

int ui_chat_area_height(void) { return layout().chat_h; }

/* ---- face -------------------------------------------------------------- */

static const kb_font_t *pick_face_font(const char *face, int max_w, bool small)
{
    const kb_font_t *cands[] = { &kb_font_face64, &kb_font_face44, &kb_font_face30 };
    for (unsigned i = small ? 1 : 0; i < 3; i++) {
        if (gfx_text_width(cands[i], face) <= max_w) return cands[i];
    }
    return cands[2];
}

/* Blink: replace the eye characters (2nd and 2nd-to-last code points inside a
 * "(...)" face) with "-". Returns false if the face has no such structure. */
static bool blink_face(const char *face, char *out, size_t out_size)
{
    int n = gfx_utf8_count(face);
    if (n < 5 || face[0] != '(' || face[strlen(face) - 1] != ')') return false;
    const char *p = face;
    size_t o = 0;
    for (int i = 0; i < n; i++) {
        const char *q = p;
        gfx_utf8_next(&q);
        const char *rep = (i == 1 || i == n - 2) ? "-" : NULL;
        size_t len = rep ? 1 : (size_t)(q - p);
        if (o + len >= out_size) return false;
        memcpy(out + o, rep ? rep : p, len);
        o += len;
        p = q;
    }
    out[o] = 0;
    return true;
}

static void draw_face_band(const palette_t *p, const ui_state_t *s, const layout_t *L)
{
    char shown[UI_FACE_BUF + 8];
    const char *face = s->face[0] ? s->face : "(—_—)";
    if (s->blink && blink_face(face, shown, sizeof shown)) face = shown;
    else if (s->sleeping && s->anim_tick < 0) {          /* animation off: static zzz */
        snprintf(shown, sizeof shown, "%szzz", face);
        face = shown;
    }
    const kb_font_t *f = pick_face_font(face, L->W - 2 * MARGIN - 60, L->landscape);
    int cx = L->W / 2 + s->face_dx;
    int base = (L->face_h - f->line_height) / 2 + f->ascent;

    /* blush: draw the "//" part with its own colour/alpha */
    const char *slash = strstr(face, "//");
    if (slash && s->blush_alpha < 255) {
        int total = gfx_text_width(f, face);
        int x = cx - total / 2;
        int pre = (int)(slash - face);
        gfx_draw_text_n(f, x, base, face, pre, p->face);
        x += gfx_text_width_n(f, face, pre);
        uint16_t c = gfx_mix(p->bg, gfx_mix(p->face, p->blush, s->blush_alpha), s->blush_alpha);
        gfx_draw_text_n(f, x, base, slash, 2, c);
        x += gfx_text_width_n(f, slash, 2);
        gfx_draw_text(f, x, base, slash + 2, p->face);
    } else if (slash) {
        int total = gfx_text_width(f, face);
        int x = cx - total / 2;
        int pre = (int)(slash - face);
        gfx_draw_text_n(f, x, base, face, pre, p->face);
        x += gfx_text_width_n(f, face, pre);
        gfx_draw_text_n(f, x, base, slash, 2, p->blush);
        x += gfx_text_width_n(f, slash, 2);
        gfx_draw_text(f, x, base, slash + 2, p->face);
    } else {
        gfx_draw_text_centered(f, cx, base, face, p->face);
    }

    /* floating z's while sleeping (three of them, staggered, rising and fading) */
    if (s->sleeping && s->anim_tick >= 0) {
        int fw = gfx_text_width(f, face);
        int x0 = cx + fw / 2 + 6;
        int y0 = base - 2;                       /* start at the baseline, rise ~30 px inside the band */
        const kb_font_t *zf = &kb_font_text22;
        for (int k = 0; k < 3; k++) {
            int ph = (s->anim_tick * 3 + k * 40) % 120;   /* 0..119, 6 s cycle */
            int rise = ph * 30 / 120;
            int alpha = ph < 20 ? ph * 255 / 20 : (ph > 90 ? (120 - ph) * 255 / 30 : 255);
            uint16_t c = gfx_mix(p->bg, p->face, alpha);
            gfx_draw_text(zf, x0 + k * 12 + (ph % 24 < 12 ? ph % 12 : 12 - ph % 12) / 3, y0 - rise + k * 4,
                          k == 1 ? "Z" : "z", c);
        }
    }

    /* corner: eye icon (remote peek allowed) + status text */
    int cw = 0;
    if (s->corner[0]) {
        const kb_font_t *cf = &kb_font_small14;
        cw = gfx_text_width(cf, s->corner);
        gfx_draw_text(cf, L->W - 8 - cw, 8 + cf->ascent, s->corner, p->corner);
    }
    if (s->peek_on) {
        int ex = L->W - 8 - cw - 26, ey = 9;
        gfx_draw_round_rect(ex, ey, 20, 12, 6, 2, p->corner);
        gfx_fill_circle(ex + 10, ey + 6, 3, p->corner);
    }
}

/* ---- chat ---------------------------------------------------------------- */

typedef struct { int lines; int h; int w; gfx_line_t ln[8]; } msg_layout_t;

static void measure_msg(const kb_font_t *f, const chat_msg_t *m, int max_text_w, msg_layout_t *out)
{
    out->lines = gfx_wrap(f, m->text, max_text_w, out->ln, 8);
    int lh = f->line_height + 2;
    out->h = out->lines * lh + 2 * BUBBLE_PAD;
    int w = 0;
    for (int i = 0; i < out->lines; i++) {
        int lw = gfx_text_width_n(f, out->ln[i].start, out->ln[i].len);
        if (lw > w) w = lw;
    }
    out->w = w + 2 * BUBBLE_PAD;
}

int ui_chat_content_height(const ui_state_t *s)
{
    layout_t L = layout();
    const kb_font_t *f = &kb_font_text22;
    int max_text_w = L.W * 7 / 10 - 2 * BUBBLE_PAD;
    int total = 0;
    msg_layout_t ml;
    for (int i = 0; i < s->msg_count; i++) {
        measure_msg(f, &s->msgs[i], max_text_w, &ml);
        total += ml.h + MSG_GAP;
    }
    return total;
}

static void draw_chat(const palette_t *p, const ui_state_t *s, const layout_t *L)
{
    const kb_font_t *f = &kb_font_text22;
    int max_text_w = L->W * 7 / 10 - 2 * BUBBLE_PAD;
    int lh = f->line_height + 2;
    gfx_set_clip(0, L->chat_y, L->W, L->chat_h);
    /* lay out from the bottom: newest message at the bottom, shifted by scroll */
    int y_bottom = L->chat_y + L->chat_h - 2 + s->scroll;
    msg_layout_t ml;
    for (int i = s->msg_count - 1; i >= 0; i--) {
        const chat_msg_t *m = &s->msgs[i];
        measure_msg(f, m, max_text_w, &ml);
        int y = y_bottom - ml.h;
        if (y + ml.h > L->chat_y && y < L->chat_y + L->chat_h) {
            bool her = m->who == CHAT_HER;
            int x = her ? L->W - MARGIN - ml.w : MARGIN;
            gfx_fill_round_rect(x, y, ml.w, ml.h, 10, her ? p->her_bd : p->bubble_bd);
            gfx_fill_round_rect(x + 1, y + 1, ml.w - 2, ml.h - 2, 9, her ? p->her_bubble : p->bubble);
            for (int k = 0; k < ml.lines; k++) {
                gfx_draw_text_n(f, x + BUBBLE_PAD, y + BUBBLE_PAD + k * lh + f->ascent, ml.ln[k].start, ml.ln[k].len,
                                her ? p->her_text : p->say);
            }
        }
        y_bottom = y - MSG_GAP;
        if (y_bottom < L->chat_y) break;
    }
    if (s->msg_count == 0 && !s->toast[0]) {
        const kb_font_t *sf = &kb_font_small14;
        gfx_draw_text_centered(sf, L->W / 2, L->chat_y + L->chat_h / 2, "按下面的按钮，或者按住屏幕说话", p->corner);
    }
    gfx_clear_clip();
}

static void draw_button(const palette_t *p, int x, int y, int w, const char *label, bool pressed)
{
    const kb_font_t *f = &kb_font_text22;
    gfx_fill_round_rect(x, y, w, BTN_H, 8, p->btn_bd);
    gfx_fill_round_rect(x + 1, y + 1, w - 2, BTN_H - 2, 7, pressed ? p->btn_pressed : p->btn);
    gfx_set_clip(x + 3, y, w - 6, BTN_H);
    int tw = gfx_text_width(f, label);
    int tx = tw < w - 6 ? x + (w - tw) / 2 : x + 3;
    gfx_draw_text(f, tx, y + (BTN_H - f->line_height) / 2 + f->ascent, label, p->btn_text);
    gfx_clear_clip();
}

static void draw_buttons(const palette_t *p, const ui_state_t *s, const layout_t *L)
{
    int n1 = s->text_btn_n + 1;   /* + camera */
    for (int i = 0; i < n1; i++) {
        int x, y, w;
        btn_rect(L, 0, i, n1, &x, &y, &w);
        bool is_cam = i == s->text_btn_n;
        int id = is_cam ? UI_HIT_CAM_BTN : UI_HIT_TEXT_BTN0 + i;
        draw_button(p, x, y, w, is_cam ? CAM_LABEL : s->text_btn[i].text, s->pressed == id);
    }
    int n2 = s->emoji_btn_n;
    for (int i = 0; i < n2; i++) {
        int x, y, w;
        btn_rect(L, 1, i, n2, &x, &y, &w);
        draw_button(p, x, y, w, s->emoji_btn[i].text, s->pressed == UI_HIT_EMOJI_BTN0 + i);
    }
}

static void draw_toast(const palette_t *p, const ui_state_t *s, const layout_t *L)
{
    if (!s->toast[0]) return;
    const kb_font_t *f = &kb_font_text22;
    gfx_line_t ln[3];
    int max_w = L->W - 4 * MARGIN - 2 * BUBBLE_PAD;
    int n = gfx_wrap(f, s->toast, max_w, ln, 3);
    int lh = f->line_height + 2;
    int w = 0;
    for (int i = 0; i < n; i++) {
        int lw = gfx_text_width_n(f, ln[i].start, ln[i].len);
        if (lw > w) w = lw;
    }
    w += 2 * BUBBLE_PAD;
    int h = n * lh + 2 * BUBBLE_PAD;
    int x = (L->W - w) / 2, y = L->chat_y + (L->chat_h - h) / 2;
    gfx_fill_round_rect(x, y, w, h, 10, p->accent);
    for (int i = 0; i < n; i++) {
        gfx_draw_text_n(f, x + BUBBLE_PAD, y + BUBBLE_PAD + i * lh + f->ascent, ln[i].start, ln[i].len, GFX_RGB(0x20, 0x10, 0x14));
    }
}

/* ---- camera / gallery ------------------------------------------------------ */

static void draw_frame_fit(const ui_state_t *s, const layout_t *L, int area_y, int area_h)
{
    if (!s->frame) return;
    int x = (L->W - s->frame_w) / 2;
    int y = area_y + (area_h - s->frame_h) / 2;
    gfx_set_clip(0, area_y, L->W, area_h);
    gfx_blit(x, y, s->frame, s->frame_w, s->frame_h);
    gfx_clear_clip();
}

static void draw_camera_screen(const palette_t *p, const ui_state_t *s, const layout_t *L)
{
    int area_h = L->row2_y - BTN_GAP;
    draw_frame_fit(s, L, 0, area_h);
    if (!s->frame) {
        gfx_draw_text_centered(&kb_font_text22, L->W / 2, area_h / 2, "相机启动中", p->corner);
    }
    if (s->cam_text[0]) {
        const kb_font_t *f = &kb_font_text22;
        int w = gfx_text_width(f, s->cam_text) + 2 * BUBBLE_PAD;
        int x = (L->W - w) / 2, y = area_h - 44;
        gfx_fill_round_rect(x, y, w, f->line_height + 8, 8, p->accent);
        gfx_draw_text(f, x + BUBBLE_PAD, y + 4 + f->ascent, s->cam_text, GFX_RGB(0x20, 0x10, 0x14));
    }
    int x, y, w;
    btn_rect(L, 1, 0, 3, &x, &y, &w); draw_button(p, x, y, w, "拍照", false);
    btn_rect(L, 1, 1, 3, &x, &y, &w); draw_button(p, x, y, w, "相册", s->pressed == UI_HIT_CAM_GALLERY);
    btn_rect(L, 1, 2, 3, &x, &y, &w); draw_button(p, x, y, w, "返回", s->pressed == UI_HIT_CAM_BACK);
}

static void draw_gallery_screen(const palette_t *p, const ui_state_t *s, const layout_t *L)
{
    int area_h = L->row2_y - BTN_GAP;
    draw_frame_fit(s, L, 0, area_h);
    const kb_font_t *f = &kb_font_small14;
    char hdr[160];
    if (s->gal_count > 0) snprintf(hdr, sizeof hdr, "%d / %d   %.60s", s->gal_index + 1, s->gal_count, s->cam_text);
    else snprintf(hdr, sizeof hdr, "%.60s", s->cam_text[0] ? s->cam_text : "没有照片");
    gfx_draw_text(f, MARGIN, 6 + f->ascent, hdr, p->corner);
    if (!s->frame && s->gal_count == 0) {
        gfx_draw_text_centered(&kb_font_text22, L->W / 2, area_h / 2, "没有照片", p->corner);
    }
    int x, y, w;
    btn_rect(L, 1, 0, 3, &x, &y, &w); draw_button(p, x, y, w, "删除", s->pressed == UI_HIT_GAL_DELETE);
    btn_rect(L, 1, 1, 3, &x, &y, &w); draw_button(p, x, y, w, "寄给克", s->pressed == UI_HIT_GAL_SEND);
    btn_rect(L, 1, 2, 3, &x, &y, &w); draw_button(p, x, y, w, "返回", s->pressed == UI_HIT_CAM_BACK);
}

/* ---- top level -------------------------------------------------------------- */

void ui_render(const ui_state_t *s)
{
    const palette_t *p = (s->theme == UI_THEME_LIGHT) ? &PAL_LIGHT : &PAL_DARK;
    layout_t L = layout();
    gfx_fill(p->bg);

    if (s->screen == UI_SCREEN_CAMERA) {
        draw_camera_screen(p, s, &L);
    } else if (s->screen == UI_SCREEN_GALLERY) {
        draw_gallery_screen(p, s, &L);
    } else {
        draw_face_band(p, s, &L);
        draw_chat(p, s, &L);
        draw_buttons(p, s, &L);
        draw_toast(p, s, &L);
    }

    /* silent alert: border flash */
    if (s->flash > 0) {
        uint16_t c = gfx_mix(p->bg, p->accent, s->flash);
        gfx_fill_rect(0, 0, L.W, 4, c);
        gfx_fill_rect(0, L.H - 4, L.W, 4, c);
        gfx_fill_rect(0, 0, 4, L.H, c);
        gfx_fill_rect(L.W - 4, 0, 4, L.H, c);
    }
}

static bool in_rect(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

int ui_hit_test(const ui_state_t *s, int px, int py)
{
    layout_t L = layout();
    int x, y, w;
    if (s->screen == UI_SCREEN_CAMERA || s->screen == UI_SCREEN_GALLERY) {
        bool cam = s->screen == UI_SCREEN_CAMERA;
        btn_rect(&L, 1, 0, 3, &x, &y, &w);
        if (in_rect(px, py, x, y, w, BTN_H)) return cam ? UI_HIT_CAM_VIEW : UI_HIT_GAL_DELETE;
        btn_rect(&L, 1, 1, 3, &x, &y, &w);
        if (in_rect(px, py, x, y, w, BTN_H)) return cam ? UI_HIT_CAM_GALLERY : UI_HIT_GAL_SEND;
        btn_rect(&L, 1, 2, 3, &x, &y, &w);
        if (in_rect(px, py, x, y, w, BTN_H)) return UI_HIT_CAM_BACK;
        if (py < L.row2_y - BTN_GAP) return cam ? UI_HIT_CAM_VIEW : UI_HIT_GAL_VIEW;
        return UI_HIT_NONE;
    }
    int n1 = s->text_btn_n + 1;
    for (int i = 0; i < n1; i++) {
        btn_rect(&L, 0, i, n1, &x, &y, &w);
        if (in_rect(px, py, x, y, w, BTN_H)) return i == s->text_btn_n ? UI_HIT_CAM_BTN : UI_HIT_TEXT_BTN0 + i;
    }
    for (int i = 0; i < s->emoji_btn_n; i++) {
        btn_rect(&L, 1, i, s->emoji_btn_n, &x, &y, &w);
        if (in_rect(px, py, x, y, w, BTN_H)) return UI_HIT_EMOJI_BTN0 + i;
    }
    if (py < L.face_h) return UI_HIT_FACE;
    if (py < L.chat_y + L.chat_h) return UI_HIT_CHAT;
    return UI_HIT_NONE;
}
