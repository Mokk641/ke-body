#include "ui_render.h"
#include "ui_colors.h"
#include "gfx.h"
#include "fonts/fonts.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    uint16_t bg, face, dim, line_text;
    uint16_t bar, sep, icon, icon_pressed;
    uint16_t ke_bub, ke_txt, her_bub, her_txt, av_bg, av_txt;
    uint16_t cap, cap_txt, cell, cell_txt, pressed;
    uint16_t online, offline, accent, blush, toast_txt, disabled_txt;
} palette_t;

#define PAL(X) { \
    .bg = COL_##X##_BG, .face = COL_##X##_FACE, .dim = COL_##X##_TEXT_DIM, .line_text = COL_##X##_LINE_TEXT, \
    .bar = COL_##X##_BAR, .sep = COL_##X##_SEP, .icon = COL_##X##_ICON, .icon_pressed = COL_##X##_ICON_PRESSED, \
    .ke_bub = COL_##X##_KE_BUBBLE, .ke_txt = COL_##X##_KE_TEXT, .her_bub = COL_##X##_HER_BUBBLE, .her_txt = COL_##X##_HER_TEXT, \
    .av_bg = COL_##X##_AVATAR_BG, .av_txt = COL_##X##_AVATAR_TEXT, \
    .cap = COL_##X##_CAP, .cap_txt = COL_##X##_CAP_TEXT, .cell = COL_##X##_CELL, .cell_txt = COL_##X##_CELL_TEXT, \
    .pressed = COL_##X##_PRESSED, .online = COL_##X##_ONLINE, .offline = COL_##X##_OFFLINE, \
    .accent = COL_##X##_ACCENT, .blush = COL_##X##_BLUSH, .toast_txt = COL_##X##_TOAST_TEXT, \
    .disabled_txt = COL_##X##_DISABLED_TEXT }

static const palette_t PAL_DARK = PAL(D);
static const palette_t PAL_LIGHT = PAL(L);

/* ---- metrics ---------------------------------------------------------------------- */
#define MARGIN      10
#define TOP_H       48      /* chat page top bar: small face circle + name */
#define BOT_H       44      /* chat page bottom strip: only the + and camera icons float here (no background) */
#define AV_D        36      /* diameter of the face circle in the top bar */
#define BUB_MARGIN  12      /* bubble distance from the screen edge */
#define BUB_R       18      /* bubble corner radius */
#define BUB_PADX    13
#define BUB_PADY    8
#define MSG_GAP     10      /* between different senders */
#define MSG_GAP_SAME 3      /* between consecutive messages of the same sender */
#define CAP_H       30      /* quick-phrase capsule */
#define CAP_GAP     8
#define CELL_GAP    6
#define BTN_H       30      /* camera / gallery button row */
#define BTN_GAP     4

static float ease(int v255)
{
    float t = (float)v255 / 255.f;
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    return t * t * (3.f - 2.f * t);
}

/* ---- geometry (shared by drawing and hit testing) ------------------------------------ */

typedef struct {
    int W, H;
    bool land;
    int top_h, bot_h, bar_y;
    int phrase_rows, phrases_h;
    int cols, rows, cell_w, cell_h, per_page, pages;
    int grid_h, panel_h;        /* panel_h: full-open height */
    int panel_shown, panel_y;   /* currently visible height / top edge */
    int chat_y0, chat_y1;       /* message viewport: newest message ends at chat_y1 */
    int chat_clip_y1;           /* messages are drawn (and scroll) down to here: under the floating icons when the panel is closed */
} geo_t;

int ui_emoji_per_page(void) { return gfx_width() > gfx_height() ? 8 : 9; }

int ui_emoji_pages(const ui_state_t *s)
{
    int pp = ui_emoji_per_page();
    return s->emoji_btn_n <= 0 ? 0 : (s->emoji_btn_n + pp - 1) / pp;
}

/* Where quick-phrase capsule idx sits: x, row (0/1), width. false if it does not fit in two rows. */
static bool phrase_pos(const ui_state_t *s, int W, int idx, int *x, int *row, int *w)
{
    const kb_font_t *f = &kb_font_text22;
    int cx = MARGIN, r = 0;
    for (int i = 0; i <= idx && i < s->text_btn_n; i++) {
        int cw = gfx_text_width(f, s->text_btn[i].text) + 28;
        if (cw < 56) cw = 56;
        if (cw > W - 2 * MARGIN) cw = W - 2 * MARGIN;
        if (cx > MARGIN && cx + cw > W - MARGIN) { r++; cx = MARGIN; }
        if (r >= 2) return false;
        if (i == idx) { *x = cx; *row = r; *w = cw; return true; }
        cx += cw + CAP_GAP;
    }
    return false;
}

static void calc_geo(const ui_state_t *s, geo_t *g)
{
    g->W = gfx_width();
    g->H = gfx_height();
    g->land = g->W > g->H;
    g->top_h = TOP_H;
    g->bot_h = BOT_H;
    g->bar_y = g->H - g->bot_h;

    int rows = 0;
    for (int i = 0; i < s->text_btn_n; i++) {
        int x, r, w;
        if (phrase_pos(s, g->W, i, &x, &r, &w) && r + 1 > rows) rows = r + 1;
    }
    g->phrase_rows = rows;
    g->phrases_h = rows ? rows * CAP_H + (rows - 1) * CAP_GAP + 8 : 0;

    g->cols = g->land ? 4 : 3;
    g->rows = g->land ? 2 : 3;
    g->cell_h = g->land ? 44 : 50;
    g->cell_w = (g->W - 2 * MARGIN - (g->cols - 1) * CELL_GAP) / g->cols;
    g->per_page = g->cols * g->rows;
    g->pages = s->emoji_btn_n <= 0 ? 0 : (s->emoji_btn_n + g->per_page - 1) / g->per_page;
    g->grid_h = g->rows * g->cell_h + (g->rows - 1) * CELL_GAP;

    int emoji_h = s->emoji_btn_n > 0 ? g->grid_h + (g->pages > 1 ? 18 : 6) : 0;
    g->panel_h = 8 + g->phrases_h + emoji_h + 4;
    g->panel_shown = (int)((float)g->panel_h * ease(s->panel_pos) + 0.5f);
    g->panel_y = g->bar_y - g->panel_shown;
    g->chat_y0 = g->top_h;
    g->chat_y1 = g->panel_y;
    g->chat_clip_y1 = g->panel_shown > 0 ? g->panel_y : g->H;
}

/* emoji cell rect for local index (0..per_page-1) in full-open panel coordinates */
static void cell_rect(const geo_t *g, int local, int *x, int *y, int *w, int *h)
{
    int py = g->bar_y - g->panel_h;
    int gy = py + 8 + g->phrases_h;
    int col = local % g->cols, row = local / g->cols;
    *x = MARGIN + col * (g->cell_w + CELL_GAP);
    *y = gy + row * (g->cell_h + CELL_GAP);
    *w = g->cell_w;
    *h = g->cell_h;
}

/* ---- small helpers ------------------------------------------------------------------------ */

static const kb_font_t *pick_font(const kb_font_t *const *cands, int n, const char *s, int max_w)
{
    for (int i = 0; i < n; i++) {
        if (gfx_text_width(cands[i], s) <= max_w) return cands[i];
    }
    return cands[n - 1];
}

/* baseline that visually centres a kaomoji (its dash line sits ~0.3 em above the baseline) on cy */
static int face_baseline(const kb_font_t *f, int cy) { return cy + f->size * 32 / 100; }

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

static void draw_chevron(float cx, float cy, float hw, float hh, bool up, float w, uint16_t c)
{
    if (up) {
        gfx_line(cx - hw, cy + hh, cx, cy - hh, w, c);
        gfx_line(cx, cy - hh, cx + hw, cy + hh, w, c);
    } else {
        gfx_line(cx - hw, cy - hh, cx, cy + hh, w, c);
        gfx_line(cx, cy + hh, cx + hw, cy - hh, w, c);
    }
}

static void draw_eye_icon(int x, int y, uint16_t c)
{
    gfx_draw_round_rect(x, y, 20, 12, 6, 2, c);
    gfx_fill_circle(x + 10, y + 6, 3, c);
}

/* corner status text (right aligned, ending at right_x) and the remote-peek eye; returns the left edge used */
static int draw_corner(const palette_t *p, const ui_state_t *s, int right_x, int y_mid)
{
    int left = right_x;
    if (s->corner[0]) {
        const kb_font_t *cf = &kb_font_small14;
        int cw = gfx_text_width(cf, s->corner);
        gfx_draw_text(cf, right_x - cw, y_mid - cf->line_height / 2 + cf->ascent, s->corner, p->dim);
        left = right_x - cw - 6;
    }
    if (s->peek_on) {
        draw_eye_icon(left - 22, y_mid - 6, p->dim);
        left -= 28;
    }
    return left;
}

/* ---- face page ------------------------------------------------------------------------------ */

static void draw_face_page(const palette_t *p, const ui_state_t *s, const geo_t *g)
{
    const int W = g->W, H = g->H;
    gfx_fill_rect(0, 0, W, H, p->bg);
    draw_corner(p, s, W - 10, 16);

    char shown[UI_FACE_BUF + 8];
    const char *face = s->face[0] ? s->face : "(—_—)";
    if (s->blink && blink_face(face, shown, sizeof shown)) face = shown;
    else if (s->sleeping && s->anim_tick < 0) {           /* z animation off: static zzz */
        snprintf(shown, sizeof shown, "%.80szzz", face);
        face = shown;
    }

    static const kb_font_t *const cands[] = { &kb_font_face96, &kb_font_face64, &kb_font_face44, &kb_font_face30 };
    const kb_font_t *f = pick_font(cands, 4, face, W - 40);
    const int cy = g->land ? 112 : H * 36 / 100;
    const int base = face_baseline(f, cy);
    const int cx = W / 2 + s->face_dx;
    int fw = gfx_text_width(f, face);

    if (fw > W - 20) {                                    /* even the smallest is too wide: two lines */
        gfx_line_t lines[2];
        int n = gfx_wrap(f, face, W - 30, lines, 2);
        for (int i = 0; i < n; i++) {
            int lw = gfx_text_width_n(f, lines[i].start, lines[i].len);
            gfx_draw_text_n(f, cx - lw / 2, base + (i - (n - 1) / 2) * f->line_height, lines[i].start, lines[i].len, p->face);
        }
    } else {
        /* the "//" of a blush face is drawn in its own colour, fading in and out */
        const char *slash = strstr(face, "//");
        if (slash) {
            int x = cx - fw / 2, pre = (int)(slash - face);
            gfx_draw_text_n(f, x, base, face, pre, p->face);
            x += gfx_text_width_n(f, face, pre);
            uint16_t c = gfx_mix(p->face, p->blush, s->blush_alpha);
            gfx_draw_text_n(f, x, base, slash, 2, c);
            x += gfx_text_width_n(f, slash, 2);
            gfx_draw_text(f, x, base, slash + 2, p->face);
        } else {
            gfx_draw_text_centered(f, cx, base, face, p->face);
        }
    }

    /* floating z's while sleeping: three staggered letters rising from the top right of the face */
    if (s->sleeping && s->anim_tick >= 0) {
        int x0 = cx + fw / 2 + 4, y0 = base - f->size * 55 / 100;
        const kb_font_t *zf = f->size >= 64 ? &kb_font_text22 : &kb_font_small14;
        for (int k = 0; k < 3; k++) {
            int ph = (s->anim_tick * 3 + k * 40) % 120;        /* 0..119, 6 s cycle */
            int rise = ph * 44 / 120;
            int alpha = ph < 20 ? ph * 255 / 20 : (ph > 90 ? (120 - ph) * 255 / 30 : 255);
            int sway = (ph % 24 < 12 ? ph % 12 : 12 - ph % 12) / 3;
            gfx_draw_text(zf, x0 + k * 14 + sway, y0 - rise + k * 6, k == 1 ? "Z" : "z", gfx_mix(p->bg, p->face, alpha));
        }
    }

    /* the latest sentence from Ke, one faint line under the face */
    const char *t = ui_latest_ke_text(s);
    if (t[0]) {
        const kb_font_t *lf = &kb_font_text22;
        char buf[UI_SAY_BUF + 4];
        const char *nl = strchr(t, '\n');
        int len = nl ? (int)(nl - t) : (int)strlen(t);
        int max_w = W - 60;
        int fit = gfx_fit_len(lf, t, max_w);
        if (fit < len) {                                       /* too long: cut and add an ellipsis */
            fit = gfx_fit_len(lf, t, max_w - gfx_text_width(lf, "…"));
            memcpy(buf, t, (size_t)fit);
            strcpy(buf + fit, "…");
        } else {
            memcpy(buf, t, (size_t)len);
            buf[len] = 0;
        }
        int ly = base + f->descent + 34;
        gfx_draw_text_centered(lf, W / 2, ly, buf, gfx_mix(p->bg, p->line_text, s->line_alpha));
    }

    /* bottom hint: "^ 上滑聊天" */
    {
        const kb_font_t *hf = &kb_font_small14;
        const char *txt = "上滑聊天";
        int tw = gfx_text_width(hf, txt), total = 16 + 8 + tw;
        int x0 = (W - total) / 2, my = H - 18;
        uint16_t c = s->pressed == UI_HIT_HINT ? p->icon_pressed : p->dim;
        draw_chevron((float)x0 + 8.f, (float)my + 1.f, 7.f, 4.f, true, 1.8f, c);
        gfx_draw_text(hf, x0 + 24, my - hf->line_height / 2 + hf->ascent, txt, c);
    }
}

/* ---- chat page --------------------------------------------------------------------------------- */

typedef struct { int lines; int w, h; gfx_line_t ln[8]; } msg_layout_t;

static int bubble_max_w(int W) { return W * 70 / 100; }

static void measure_msg(const chat_msg_t *m, int W, msg_layout_t *out)
{
    const kb_font_t *f = &kb_font_text22;
    out->lines = gfx_wrap(f, m->text, bubble_max_w(W) - 2 * BUB_PADX, out->ln, 8);
    int w = 0;
    for (int i = 0; i < out->lines; i++) {
        int lw = gfx_text_width_n(f, out->ln[i].start, out->ln[i].len);
        if (lw > w) w = lw;
    }
    out->w = w + 2 * BUB_PADX;
    if (out->w < 2 * BUB_R + 8) out->w = 2 * BUB_R + 8;
    out->h = out->lines * (f->line_height + 2) + 2 * BUB_PADY;
    if (out->h < 2 * BUB_R) out->h = 2 * BUB_R;
}

/* space above message i: small inside a run of one sender, larger when the sender changes */
static int gap_above(const ui_state_t *s, int i)
{
    return (i > 0 && s->msgs[i - 1].who == s->msgs[i].who) ? MSG_GAP_SAME : MSG_GAP;
}

int ui_chat_content_height(const ui_state_t *s)
{
    int W = gfx_width();
    int total = 8;
    msg_layout_t ml;
    for (int i = 0; i < s->msg_count; i++) {
        measure_msg(&s->msgs[i], W, &ml);
        total += ml.h + (i > 0 ? gap_above(s, i) : 0);
    }
    return total;
}

int ui_chat_area_height(const ui_state_t *s)
{
    geo_t g;
    calc_geo(s, &g);
    int h = g.chat_y1 - g.chat_y0;
    return h > 0 ? h : 0;
}

const char *ui_latest_ke_text(const ui_state_t *s)
{
    for (int i = s->msg_count - 1; i >= 0; i--) {
        if (s->msgs[i].who == CHAT_KE) return s->msgs[i].text;
    }
    return "";
}

static void draw_chat_area(const palette_t *p, const ui_state_t *s, const geo_t *g)
{
    const kb_font_t *f = &kb_font_text22;
    const int lh = f->line_height + 2;
    gfx_set_clip(0, g->chat_y0, g->W, g->chat_clip_y1 - g->chat_y0);

    int y_bottom = g->chat_y1 - 8 + s->scroll;
    msg_layout_t ml;
    for (int i = s->msg_count - 1; i >= 0; i--) {
        const chat_msg_t *m = &s->msgs[i];
        measure_msg(m, g->W, &ml);
        int y0 = y_bottom - ml.h;
        int y = y0 + (i == s->msg_count - 1 ? s->slide_dy : 0);          /* newest bubble slides in from below */
        if (y + ml.h > g->chat_y0 && y < g->chat_clip_y1) {
            bool her = m->who == CHAT_HER;
            int x = her ? g->W - BUB_MARGIN - ml.w : BUB_MARGIN;
            gfx_fill_round_rect(x, y, ml.w, ml.h, BUB_R, her ? p->her_bub : p->ke_bub);
            for (int k = 0; k < ml.lines; k++) {
                gfx_draw_text_n(f, x + BUB_PADX, y + BUB_PADY + k * lh + f->ascent, ml.ln[k].start, ml.ln[k].len,
                                her ? p->her_txt : p->ke_txt);
            }
        }
        y_bottom = y0 - (i > 0 ? gap_above(s, i) : 0);
        if (y_bottom < g->chat_y0 - 400) break;
    }
    if (s->msg_count == 0) {
        gfx_draw_text_centered(&kb_font_small14, g->W / 2, (g->chat_y0 + g->chat_y1) / 2, "还没有消息，点 + 或按住说话", p->dim);
    }
    gfx_clear_clip();
}

/* my face inside a small circle: brackets dropped and the text shrunk so it fits */
static void draw_mini_face(const palette_t *p, const char *face, int cx, int cy)
{
    gfx_fill_circle(cx, cy, AV_D / 2, p->av_bg);
    char core[UI_FACE_BUF];
    snprintf(core, sizeof core, "%s", face[0] ? face : "(—_—)");
    size_t n = strlen(core);
    if (core[0] == '(' && n > 1 && core[n - 1] == ')') { memmove(core, core + 1, n - 2); core[n - 2] = 0; }
    const kb_font_t *f = &kb_font_face18;
    int inner = AV_D - 6, num = 10, den = 10;             /* the 18 px face font, shrunk in 10% steps until it fits */
    while (num > 4 && gfx_text_width_scaled(f, core, num, den) > inner) num--;
    if (gfx_text_width_scaled(f, core, num, den) > inner) {   /* still too wide: cut */
        int len = gfx_fit_len(f, core, inner * den / num);
        core[len] = 0;
    }
    int tw = gfx_text_width_scaled(f, core, num, den);
    gfx_draw_text_scaled(f, cx - tw / 2, cy + f->size * 32 / 100 * num / den, core, p->av_txt, num, den);
}

static void draw_top_bar(const palette_t *p, const ui_state_t *s, const geo_t *g)
{
    const int W = g->W, h = g->top_h, mid = h / 2;
    gfx_fill_rect(0, 0, W, h, p->bar);
    gfx_fill_rect(0, h - 1, W, 1, p->sep);

    draw_chevron(18.f, (float)mid, 7.f, 4.f, false, 1.6f, s->pressed == UI_HIT_TOP_BACK ? p->icon_pressed : p->icon);

    /* on the left: my face in a small circle, then the name, then a green dot while the bridge answers */
    const int cx = 38 + AV_D / 2;
    draw_mini_face(p, s->face, cx, mid - 1);
    const kb_font_t *nf = &kb_font_text22;
    const char *name = "克";
    int nx = cx + AV_D / 2 + 8, nw = gfx_text_width(nf, name);
    gfx_draw_text(nf, nx, mid - 1 - nf->line_height / 2 + nf->ascent, name, p->cap_txt);
    if (s->online) gfx_fill_circle(nx + nw + 8, mid, 3, p->online);

    if (s->peek_on) draw_eye_icon(W - 36, mid - 6, p->dim);         /* remote snapshots allowed */
}

static void draw_bottom_bar(const palette_t *p, const ui_state_t *s, const geo_t *g)
{
    const int W = g->W, y0 = g->bar_y, mid = y0 + g->bot_h / 2;
    /* no background of its own; once the panel is out the strip gets the panel colour so it reads as one sheet */
    if (s->panel_pos > 0) gfx_fill_rect(0, y0, W, g->bot_h, gfx_mix(p->bg, p->bar, (int)(ease(s->panel_pos) * 255.f)));
    const uint16_t halo = s->panel_pos > 0 ? gfx_mix(p->bg, p->bar, (int)(ease(s->panel_pos) * 255.f)) : p->bg;

    /* + (turns into an x while the panel is out): thin lines */
    uint16_t ic = s->pressed == UI_HIT_PLUS ? p->icon_pressed : p->icon;
    float cx = (float)(MARGIN + 21), cy = (float)mid, ang = ease(s->panel_pos) * 0.7853982f;
    float ca = cosf(ang) * 8.f, sa = sinf(ang) * 8.f;
    gfx_line(cx - ca, cy - sa, cx + ca, cy + sa, 4.8f, halo);        /* both halos first, then both strokes */
    gfx_line(cx + sa, cy - ca, cx - sa, cy + ca, 4.8f, halo);
    gfx_line(cx - ca, cy - sa, cx + ca, cy + sa, 1.8f, ic);
    gfx_line(cx + sa, cy - ca, cx - sa, cy + ca, 1.8f, ic);

    /* camera, thin lines with a halo */
    uint16_t cc = s->pressed == UI_HIT_CAM_BTN ? p->icon_pressed : p->icon;
    int px = W - MARGIN - 22;
    gfx_draw_round_rect(px - 14, mid - 9, 28, 20, 6, 4, halo);
    gfx_draw_round_rect(px - 6, mid - 12, 12, 8, 3, 3, halo);
    gfx_ring((float)px, (float)mid + 1.f, 4.5f, 4.6f, halo);
    gfx_draw_round_rect(px - 12, mid - 7, 24, 16, 4, 2, cc);
    gfx_draw_round_rect(px - 4, mid - 10, 8, 4, 2, 1, cc);
    gfx_ring((float)px, (float)mid + 1.f, 4.5f, 1.6f, cc);
}

/* index of the largest face font that fits a grid cell on one line (n-1 if none does) */
static int cell_font_index(const kb_font_t *const *cands, int n, const char *text, int w, int h)
{
    for (int i = 0; i < n; i++) {
        if (gfx_text_width(cands[i], text) <= w - 8 && cands[i]->line_height <= h) return i;
    }
    return n - 1;
}

/* draw at font index >= first (so a whole page can share one size); else two lines at the smallest size */
static void draw_cell_face(const kb_font_t *const *cands, int n, int first, const char *text, int x, int y, int w, int h, uint16_t color)
{
    int inner = w - 8;
    for (int i = first; i < n; i++) {
        const kb_font_t *f = cands[i];
        if (gfx_text_width(f, text) <= inner && f->line_height <= h) {
            gfx_draw_text_centered(f, x + w / 2, face_baseline(f, y + h / 2), text, color);
            return;
        }
    }
    const kb_font_t *f = cands[n - 1];
    gfx_line_t ln[3];
    int lines = gfx_wrap(f, text, inner, ln, 3);
    if (lines > 2) lines = 2;
    int lh = f->line_height;
    for (int i = 0; i < lines; i++) {
        int lw = gfx_text_width_n(f, ln[i].start, ln[i].len);
        int line_cy = y + h / 2 - (lines - 1) * lh / 2 + i * lh;
        gfx_draw_text_n(f, x + (w - lw) / 2, face_baseline(f, line_cy), ln[i].start, ln[i].len, color);
    }
}

static void draw_panel(const palette_t *p, const ui_state_t *s, const geo_t *g)
{
    if (g->panel_shown <= 0) return;
    const int py = g->bar_y - g->panel_h;
    gfx_set_origin(0, g->panel_h - g->panel_shown);           /* slide up from the bottom bar */
    gfx_set_clip(0, py, g->W, g->panel_shown);
    gfx_fill_rect(0, py, g->W, g->panel_h, p->bar);
    gfx_fill_rect(0, py, g->W, 1, p->sep);

    /* quick phrases: rounded capsules */
    const kb_font_t *tf = &kb_font_text22;
    for (int i = 0; i < s->text_btn_n; i++) {
        int x, row, w;
        if (!phrase_pos(s, g->W, i, &x, &row, &w)) continue;
        int y = py + 8 + row * (CAP_H + CAP_GAP);
        gfx_fill_round_rect(x, y, w, CAP_H, CAP_H / 2, s->pressed == UI_HIT_TEXT_BTN0 + i && !s->sending ? p->pressed : p->cap);
        int inner = w - 16, len = gfx_fit_len(tf, s->text_btn[i].text, inner);
        int tw = gfx_text_width_n(tf, s->text_btn[i].text, len);
        gfx_draw_text_n(tf, x + (w - tw) / 2, y + (CAP_H - tf->line_height) / 2 + tf->ascent, s->text_btn[i].text, len, s->sending ? p->disabled_txt : p->cap_txt);
    }

    /* emoji grid, one page at a time */
    static const kb_font_t *const cands[] = { &kb_font_face44, &kb_font_face30, &kb_font_face18, &kb_font_face13 };
    int page = s->emoji_page;
    if (page >= g->pages) page = g->pages - 1;
    /* one common size per page (so the grid looks even), unless a cell needs to go below face18 */
    int common = 0;
    for (int local = 0; local < g->per_page; local++) {
        int idx = page * g->per_page + local;
        if (idx >= s->emoji_btn_n) break;
        int x, y, w, h;
        cell_rect(g, local, &x, &y, &w, &h);
        int fi = cell_font_index(cands, 4, s->emoji_btn[idx].text, w, h);
        if (fi > 2) fi = 2;
        if (fi > common) common = fi;
    }
    for (int local = 0; local < g->per_page; local++) {
        int idx = page * g->per_page + local;
        if (idx >= s->emoji_btn_n) break;
        int x, y, w, h;
        cell_rect(g, local, &x, &y, &w, &h);
        gfx_fill_round_rect(x, y, w, h, 12, s->pressed == UI_HIT_EMOJI_BTN0 + idx && !s->sending ? p->pressed : p->cell);
        draw_cell_face(cands, 4, common, s->emoji_btn[idx].text, x, y, w, h, s->sending ? p->disabled_txt : p->cell_txt);
    }
    if (g->pages > 1) {
        int gy = py + 8 + g->phrases_h + g->grid_h + 9;
        int total = g->pages * 12 - 4, x0 = (g->W - total) / 2;
        for (int i = 0; i < g->pages; i++) {
            gfx_fill_circle(x0 + i * 12 + 4, gy, i == page ? 3 : 2, i == page ? p->icon : p->sep);
        }
    }
    gfx_set_origin(0, 0);
    gfx_clear_clip();
}

static void draw_chat_page(const palette_t *p, const ui_state_t *s, const geo_t *g)
{
    gfx_fill_rect(0, 0, g->W, g->H, p->bg);
    draw_chat_area(p, s, g);
    draw_panel(p, s, g);
    draw_top_bar(p, s, g);
    draw_bottom_bar(p, s, g);
}

/* ---- camera / gallery (unchanged layout from phase 4) ---------------------------------------------- */

static void btn_rect3(const geo_t *g, int i, int *x, int *y, int *w)
{
    int total = g->W - 2 * MARGIN;
    int bw = (total - 2 * BTN_GAP) / 3;
    *x = MARGIN + i * (bw + BTN_GAP);
    *y = g->H - MARGIN + 2 - BTN_H;
    *w = bw;
}

static void draw_button(const palette_t *p, int x, int y, int w, const char *label, bool pressed, bool disabled)
{
    const kb_font_t *f = &kb_font_text22;
    gfx_fill_round_rect(x, y, w, BTN_H, BTN_H / 2, pressed && !disabled ? p->pressed : p->cap);
    int tw = gfx_text_width(f, label);
    gfx_draw_text(f, x + (w - tw) / 2, y + (BTN_H - f->line_height) / 2 + f->ascent, label, disabled ? p->disabled_txt : p->cap_txt);
}

static void draw_frame_fit(const ui_state_t *s, const geo_t *g, int area_h)
{
    if (!s->frame) return;
    int x = (g->W - s->frame_w) / 2, y = (area_h - s->frame_h) / 2;
    gfx_set_clip(0, 0, g->W, area_h);
    gfx_blit(x, y, s->frame, s->frame_w, s->frame_h);
    gfx_clear_clip();
}

static void draw_camera_screen(const palette_t *p, const ui_state_t *s, const geo_t *g)
{
    int x, y, w;
    btn_rect3(g, 0, &x, &y, &w);
    int area_h = y - BTN_GAP;
    draw_frame_fit(s, g, area_h);
    if (!s->frame) gfx_draw_text_centered(&kb_font_text22, g->W / 2, area_h / 2, "相机启动中", p->dim);
    if (s->cam_text[0]) {
        const kb_font_t *f = &kb_font_text22;
        int tw = gfx_text_width(f, s->cam_text) + 2 * BUB_PADX;
        int bx = (g->W - tw) / 2, by = area_h - 44;
        gfx_fill_round_rect(bx, by, tw, f->line_height + 8, 12, p->accent);
        gfx_draw_text(f, bx + BUB_PADX, by + 4 + f->ascent, s->cam_text, p->toast_txt);
    }
    btn_rect3(g, 0, &x, &y, &w); draw_button(p, x, y, w, "拍照", false, false);
    btn_rect3(g, 1, &x, &y, &w); draw_button(p, x, y, w, "相册", s->pressed == UI_HIT_CAM_GALLERY, false);
    btn_rect3(g, 2, &x, &y, &w); draw_button(p, x, y, w, "返回", s->pressed == UI_HIT_CAM_BACK, false);
}

static void draw_gallery_screen(const palette_t *p, const ui_state_t *s, const geo_t *g)
{
    int x, y, w;
    btn_rect3(g, 0, &x, &y, &w);
    int area_h = y - BTN_GAP;
    draw_frame_fit(s, g, area_h);
    const kb_font_t *f = &kb_font_small14;
    char hdr[160];
    if (s->gal_count > 0) snprintf(hdr, sizeof hdr, "%d / %d   %.60s", s->gal_index + 1, s->gal_count, s->cam_text);
    else snprintf(hdr, sizeof hdr, "%.60s", s->cam_text[0] ? s->cam_text : "没有照片");
    gfx_draw_text(f, MARGIN, 6 + f->ascent, hdr, p->dim);
    if (!s->frame && s->gal_count == 0) gfx_draw_text_centered(&kb_font_text22, g->W / 2, area_h / 2, "没有照片", p->dim);
    /* review = the photo just taken: retake / send / keep */
    btn_rect3(g, 0, &x, &y, &w); draw_button(p, x, y, w, s->review ? "重拍" : "删除", s->pressed == UI_HIT_GAL_DELETE, s->sending);
    btn_rect3(g, 1, &x, &y, &w); draw_button(p, x, y, w, s->sending ? "发送中" : "寄给克", s->pressed == UI_HIT_GAL_SEND, s->sending);
    btn_rect3(g, 2, &x, &y, &w); draw_button(p, x, y, w, s->review ? "保留" : "返回", s->pressed == UI_HIT_CAM_BACK, false);
}

/* ---- colour test pattern: swatches with their #RRGGBB, to compare with a phone ----------------------------- */

static void draw_colortest(const ui_state_t *s, const geo_t *g)
{
    /* the colours the UI really uses (through the same macros), then a grey ramp and the primaries; the label is
     * the intended #RRGGBB, so the screen can be compared with the same hex value on a phone */
    static const struct { uint16_t col; const char *label; } sw[] = {
        { COL_D_BG, "000000" }, { COL_D_BAR, "1C1C1E" }, { COL_D_KE_BUBBLE, "262628" }, { COL_D_CELL, "2C2C2E" },
        { COL_D_HER_BUBBLE, "0A84FF" }, { COL_D_FACE, "FFFFFF" }, { COL_D_ONLINE, "30D158" }, { GFX_RGB(0x2F, 0x4A, 0x3A), "2F4A3A" },
        { GFX_RGB(0xEF, 0xE3, 0xCF), "EFE3CF" }, { GFX_RGB(0xFF, 0x00, 0x00), "FF0000" }, { GFX_RGB(0x00, 0xFF, 0x00), "00FF00" },
        { GFX_RGB(0x00, 0x00, 0xFF), "0000FF" }, { GFX_GREY(0x10), "101010" }, { GFX_GREY(0x30), "303030" }, { GFX_GREY(0x60), "606060" },
        { GFX_GREY(0x90), "909090" }, { GFX_GREY(0xC0), "C0C0C0" }, { GFX_GREY(0xE0), "E0E0E0" },
    };
    const int n = (int)(sizeof sw / sizeof sw[0]);
    const int cols = g->land ? 6 : 4;
    const int rows = (n + cols - 1) / cols;
    const int cw = g->W / cols, ch = (g->H - 20) / rows;
    const kb_font_t *f = &kb_font_small14;
    for (int i = 0; i < n; i++) {
        int x = (i % cols) * cw, y = (i / cols) * ch;
        gfx_fill_rect(x, y, cw, ch, sw[i].col);
        int lum = (((sw[i].col >> 11) & 31) * 30 + ((sw[i].col >> 5) & 63) * 29 + (sw[i].col & 31) * 11) / 100 * 4;   /* rough 0..~255 */
        gfx_draw_text_centered(f, x + cw / 2, y + ch / 2 + 4, sw[i].label, lum < 110 ? GFX_GREY(0xFF) : GFX_GREY(0x00));
    }
    gfx_draw_text_centered(f, g->W / 2, g->H - 5, "colour test - tap to leave", GFX_GREY(0xA0));
}

/* ---- toast and top level ------------------------------------------------------------------------------- */

static void draw_toast(const palette_t *p, const ui_state_t *s, const geo_t *g)
{
    if (!s->toast[0]) return;
    const kb_font_t *f = &kb_font_text22;
    gfx_line_t ln[3];
    int max_w = g->W - 4 * MARGIN - 2 * BUB_PADX;
    int n = gfx_wrap(f, s->toast, max_w, ln, 3);
    int lh = f->line_height + 2, w = 0;
    for (int i = 0; i < n; i++) {
        int lw = gfx_text_width_n(f, ln[i].start, ln[i].len);
        if (lw > w) w = lw;
    }
    w += 2 * BUB_PADX;
    int h = n * lh + 2 * BUB_PADY;
    int cy = s->screen == UI_SCREEN_CHAT ? (g->chat_y0 + g->chat_y1) / 2 : g->H * 62 / 100;
    int x = (g->W - w) / 2, y = cy - h / 2;
    gfx_fill_round_rect(x, y, w, h, BUB_R, p->accent);
    for (int i = 0; i < n; i++) {
        gfx_draw_text_n(f, x + BUB_PADX, y + BUB_PADY + i * lh + f->ascent, ln[i].start, ln[i].len, p->toast_txt);
    }
}

void ui_render(const ui_state_t *s)
{
    const palette_t *p = (s->theme == UI_THEME_LIGHT) ? &PAL_LIGHT : &PAL_DARK;
    geo_t g;
    calc_geo(s, &g);
    gfx_set_origin(0, 0);
    gfx_clear_clip();
    gfx_fill(p->bg);

    if (s->screen == UI_SCREEN_COLORTEST) {
        draw_colortest(s, &g);
    } else if (s->screen == UI_SCREEN_CAMERA) {
        draw_camera_screen(p, s, &g);
    } else if (s->screen == UI_SCREEN_GALLERY) {
        draw_gallery_screen(p, s, &g);
    } else if (s->page_pos <= 0) {
        draw_face_page(p, s, &g);
    } else if (s->page_pos >= 255) {
        draw_chat_page(p, s, &g);
    } else {                                              /* sliding between the two pages */
        int off = (int)(ease(s->page_pos) * (float)g.H + 0.5f);
        gfx_set_origin(0, -off);
        draw_face_page(p, s, &g);
        gfx_set_origin(0, g.H - off);
        draw_chat_page(p, s, &g);
        gfx_set_origin(0, 0);
    }
    draw_toast(p, s, &g);

    if (s->flash > 0) {                                   /* optional silent alert: soft border (off by default) */
        uint16_t c = gfx_mix(p->bg, p->accent, s->flash);
        gfx_fill_rect(0, 0, g.W, 4, c);
        gfx_fill_rect(0, g.H - 4, g.W, 4, c);
        gfx_fill_rect(0, 0, 4, g.H, c);
        gfx_fill_rect(g.W - 4, 0, 4, g.H, c);
    }
}

/* ---- hit testing ------------------------------------------------------------------------------------------ */

static bool in_rect(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

int ui_hit_test(const ui_state_t *s, int px, int py)
{
    geo_t g;
    calc_geo(s, &g);
    int x, y, w;

    if (s->screen == UI_SCREEN_CAMERA || s->screen == UI_SCREEN_GALLERY) {
        bool cam = s->screen == UI_SCREEN_CAMERA;
        btn_rect3(&g, 0, &x, &y, &w);
        if (in_rect(px, py, x, y, w, BTN_H)) return cam ? UI_HIT_CAM_VIEW : UI_HIT_GAL_DELETE;
        btn_rect3(&g, 1, &x, &y, &w);
        if (in_rect(px, py, x, y, w, BTN_H)) return cam ? UI_HIT_CAM_GALLERY : UI_HIT_GAL_SEND;
        btn_rect3(&g, 2, &x, &y, &w);
        if (in_rect(px, py, x, y, w, BTN_H)) return UI_HIT_CAM_BACK;
        if (py < y - BTN_GAP) return cam ? UI_HIT_CAM_VIEW : UI_HIT_GAL_VIEW;
        return UI_HIT_NONE;
    }

    if (s->screen == UI_SCREEN_COLORTEST) return UI_HIT_TEST_EXIT;

    if (s->screen == UI_SCREEN_FACE) {
        if (py >= g.H - 44 && px >= g.W / 2 - 110 && px < g.W / 2 + 110) return UI_HIT_HINT;
        return UI_HIT_FACE;
    }

    /* chat page */
    if (py < g.top_h) return px < 36 ? UI_HIT_TOP_BACK : UI_HIT_TOPBAR;
    if (py >= g.bar_y) {
        if (px < 64) return UI_HIT_PLUS;
        if (px >= g.W - 64) return UI_HIT_CAM_BTN;
        return s->panel_open ? UI_HIT_NONE : UI_HIT_CHAT;      /* transparent strip: the chat is underneath */
    }
    if (s->panel_open && py >= g.panel_y) {
        if (s->panel_pos < 255) return UI_HIT_PANEL;                 /* still sliding: no button presses */
        for (int i = 0; i < s->text_btn_n; i++) {
            int cx, row, cw;
            if (!phrase_pos(s, g.W, i, &cx, &row, &cw)) continue;
            int cyy = g.bar_y - g.panel_h + 8 + row * (CAP_H + CAP_GAP);
            if (in_rect(px, py, cx, cyy, cw, CAP_H)) return UI_HIT_TEXT_BTN0 + i;
        }
        int page = s->emoji_page < g.pages ? s->emoji_page : (g.pages > 0 ? g.pages - 1 : 0);
        for (int local = 0; local < g.per_page; local++) {
            int idx = page * g.per_page + local;
            if (idx >= s->emoji_btn_n) break;
            int ch;
            cell_rect(&g, local, &x, &y, &w, &ch);
            if (in_rect(px, py, x, y, w, ch)) return UI_HIT_EMOJI_BTN0 + idx;
        }
        return UI_HIT_PANEL;
    }
    return UI_HIT_CHAT;
}
