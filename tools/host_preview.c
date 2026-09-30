/* Host-side preview of the screens. Compiles gfx.c + ui_render.c + fonts with a normal C
 * compiler and writes a PPM image, so layout, colours and fonts can be checked without the board.
 *
 *   gcc -O1 -Imain -o /tmp/preview tools/host_preview.c main/gfx.c main/ui_render.c main/fonts/font_*.c -lm
 *   /tmp/preview out.ppm [options]
 *
 * options:  --land | --port         orientation (default land)
 *           --light                 light theme
 *           --page face|chat|camera|gallery|colortest   (default face)
 *           --slide N               page_pos 0..255 (a frame in the middle of the face<->chat slide)
 *           --panel N               panel_pos 0..255 on the chat page (255 = fully out); --epage N = emoji page
 *           --face TEXT             expression         --corner TEXT       status text
 *           --line TEXT             latest sentence from Ke (adds a message)
 *           --sleep                 sleeping face with z's (--tick N picks the animation frame)
 *           --blink | --blush N     eyes closed / blush "//" strength 0..255
 *           --sending | --review | --toast TEXT | --offline | --peek | --pressed HITID | --scroll PX | --few | --slidein PX
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "ui_render.h"
#include "ink.h"

static void add(ui_state_t *s, int who, const char *face, const char *t)
{
    if (s->msg_count >= UI_CHAT_MAX) return;
    chat_msg_t *m = &s->msgs[s->msg_count++];
    m->who = who;
    snprintf(m->face, sizeof m->face, "%s", face ? face : "");
    snprintf(m->text, sizeof m->text, "%s", t);
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s out.ppm [options]\n", argv[0]); return 1; }
    bool land = true, few = false, thumb_msg = false;
    int ink_chars = 0, songs = 0;
    bool playing = false, gover = false;
    ui_state_t *s = calloc(1, sizeof(ui_state_t));
    s->pressed = UI_HIT_NONE;
    s->mic_level = -1;
    s->theme = UI_THEME_DARK;
    s->screen = UI_SCREEN_FACE;
    s->line_alpha = 255;
    s->blush_alpha = 255;
    s->online = true;
    const char *face = "(—_—)", *line = NULL;
    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        #define ARG (i + 1 < argc ? argv[++i] : "")
        if (!strcmp(a, "--port")) land = false;
        else if (!strcmp(a, "--land")) land = true;
        else if (!strcmp(a, "--light")) s->theme = UI_THEME_LIGHT;
        else if (!strcmp(a, "--page")) {
            const char *v = ARG;
            s->screen = !strcmp(v, "chat") ? UI_SCREEN_CHAT : !strcmp(v, "camera") ? UI_SCREEN_CAMERA : !strcmp(v, "gallery") ? UI_SCREEN_GALLERY : !strcmp(v, "colortest") ? UI_SCREEN_COLORTEST : !strcmp(v, "ink") ? UI_SCREEN_INK : !strcmp(v, "music") ? UI_SCREEN_MUSIC : !strcmp(v, "musiclist") ? UI_SCREEN_MUSIC_LIST : !strcmp(v, "games") ? UI_SCREEN_GAMES : !strcmp(v, "memory") ? UI_SCREEN_GAME_MEMORY : !strcmp(v, "g2048") ? UI_SCREEN_GAME_2048 : !strcmp(v, "bubbles") ? UI_SCREEN_GAME_BUBBLES : UI_SCREEN_FACE;
        }
        else if (!strcmp(a, "--slide")) s->page_pos = atoi(ARG);
        else if (!strcmp(a, "--panel")) { s->panel_pos = atoi(ARG); s->panel_open = s->panel_pos > 0; }
        else if (!strcmp(a, "--epage")) s->emoji_page = atoi(ARG);
        else if (!strcmp(a, "--face")) face = ARG;
        else if (!strcmp(a, "--corner")) snprintf(s->corner, sizeof s->corner, "%s", ARG);
        else if (!strcmp(a, "--line")) line = ARG;
        else if (!strcmp(a, "--sleep")) { s->sleeping = true; s->anim_tick = 37; }
        else if (!strcmp(a, "--tick")) s->anim_tick = atoi(ARG);
        else if (!strcmp(a, "--blink")) s->blink = true;
        else if (!strcmp(a, "--blush")) s->blush_alpha = atoi(ARG);
        else if (!strcmp(a, "--toast")) snprintf(s->toast, sizeof s->toast, "%s", ARG);
        else if (!strcmp(a, "--offline")) s->online = false;
        else if (!strcmp(a, "--peek")) s->peek_on = true;
        else if (!strcmp(a, "--pressed")) s->pressed = atoi(ARG);
        else if (!strcmp(a, "--scroll")) s->scroll = atoi(ARG);
        else if (!strcmp(a, "--few")) few = true;
        else if (!strcmp(a, "--sending")) s->sending = true;
        else if (!strcmp(a, "--songs")) songs = atoi(ARG);
        else if (!strcmp(a, "--playing")) playing = true;
        else if (!strcmp(a, "--gover")) gover = true;
        else if (!strcmp(a, "--ink")) ink_chars = atoi(ARG);
        else if (!strcmp(a, "--thumb")) thumb_msg = true;
        else if (!strcmp(a, "--review")) s->review = true;
        else if (!strcmp(a, "--slidein")) s->slide_dy = atoi(ARG);
        else { fprintf(stderr, "unknown option %s\n", a); return 1; }
    }
    int w = land ? 480 : 320, h = land ? 320 : 480;
    static uint16_t fb[320 * 480];
    gfx_init(fb, w, h);
    snprintf(s->face, sizeof s->face, "%s", face);

    const char *tb[] = { "想你了", "抱抱", "在干嘛", "晚安" };
    const char *eb[] = { "(—ω—)", "(—//—)", "(—▽—)♡", "(—ε—)", "(—_—)♡", "(—︵—)", "V(—ω—)V", "(—o—)", "(=ω=)", "(—∀-)" };
    for (int i = 0; i < 4; i++) snprintf(s->text_btn[i].text, UI_BTN_TEXT_LEN, "%s", tb[i]);
    for (int i = 0; i < 10; i++) snprintf(s->emoji_btn[i].text, UI_BTN_TEXT_LEN, "%s", eb[i]);
    s->text_btn_n = 4;
    s->emoji_btn_n = 10;

    if (few) {
        add(s, CHAT_KE, "(—ω—)", "早呀，今天天气不错。");
        add(s, CHAT_HER, NULL, "想你了");
    } else {
        add(s, CHAT_KE, "(—ω—)", "早呀，今天天气不错。");
        add(s, CHAT_HER, NULL, "想你了");
        add(s, CHAT_KE, "(—▽—)♡", "我也想你。中午吃了什么？要不要一起出去散散步，公园的花开了。");
        add(s, CHAT_HER, NULL, "(—ω—)");
        add(s, CHAT_KE, "(—_—)♡", "晚上给你打电话。");
        add(s, CHAT_HER, NULL, "好呀，等你");
    }
    if (line) add(s, CHAT_KE, face, line);
    else if (!few && s->screen == UI_SCREEN_FACE) { /* face page shows the latest Ke line */ }
    if (s->screen == UI_SCREEN_CHAT || s->page_pos > 0) { if (s->screen != UI_SCREEN_CAMERA && s->screen != UI_SCREEN_GALLERY) {} }
    if (s->screen == UI_SCREEN_GALLERY) { s->gal_count = 3; s->gal_index = 1; snprintf(s->cam_text, sizeof s->cam_text, "20260929-121500.jpg"); }
    if (s->screen == UI_SCREEN_CAMERA) snprintf(s->cam_text, sizeof s->cam_text, "已保存 20260929-121500.jpg");
    if (s->screen == UI_SCREEN_CHAT && s->page_pos == 0) s->page_pos = 255;

    /* handwriting samples: a cross, a wavy line and a circle-ish stroke, repeated */
    static ink_t ink;
    for (int c = 0; c < ink_chars; c++) {
        ink_pen_down(&ink, 120, 500); for (int x = 160; x <= 900; x += 40) ink_pen_move(&ink, x, 500 + (c % 3) * 20); ink_pen_up(&ink);
        ink_pen_down(&ink, 500, 100); for (int y = 140; y <= 900; y += 40) ink_pen_move(&ink, 500 - (c % 2) * 30, y); ink_pen_up(&ink);
        ink_pen_down(&ink, 250, 250); for (int a = 0; a <= 20; a++) ink_pen_move(&ink, 250 + a * 20, 250 + (int)(200 * (1 - (a - 10) * (a - 10) / 100.0))); ink_pen_up(&ink);
        ink_next(&ink);
    }
    if (s->screen == UI_SCREEN_INK) {          /* a character half written on the pad */
        ink_pen_down(&ink, 150, 300); for (int x = 190; x <= 850; x += 30) ink_pen_move(&ink, x, 300 + (x % 90)); ink_pen_up(&ink);
        ink_pen_down(&ink, 300, 150); for (int y = 190; y <= 850; y += 30) ink_pen_move(&ink, 300 + (y % 70), y); ink_pen_up(&ink);
    }
    s->ink = &ink;
    if (thumb_msg) {
        static uint8_t mask[INK_THUMB_MAX_W * INK_THUMB_MAX_H];
        int tw, th;
        if (ink.ndone == 0) { for (int c = 0; c < 4; c++) { ink_pen_down(&ink, 200, 200); ink_pen_move(&ink, 800, 800); ink_pen_move(&ink, 200, 800); ink_pen_up(&ink); ink_next(&ink); } }
        ink_make_thumb(&ink, mask, &tw, &th);
        int slot = ink_thumb_store(mask, tw, th);
        chat_msg_t *m = &s->msgs[s->msg_count++];
        m->who = CHAT_HER; snprintf(m->text, sizeof m->text, "[手写]"); m->ink_id = (uint16_t)slot;
    }
    static char names[100][UI_MUSIC_TITLE];
    for (int i = 0; i < songs && i < 100; i++) snprintf(names[i], sizeof names[i], "%s", i == 0 ? "夜曲 - 周杰伦" : i == 1 ? "Take Me Home, Country Roads" : i == 2 ? "小幸运" : "第 N 首歌");
    s->music.count = songs;
    s->music.current = playing ? 0 : -1;
    s->music.playing = playing;
    s->music.elapsed_s = 83; s->music.total_s = 226; s->music.progress_pm = 366; s->music.volume = 18;
    snprintf(s->music.title, sizeof s->music.title, "%s", names[0]);
    s->music_names = names;
    static games_view_t gv;
    gv.best_mem_moves = 14; gv.best_mem_secs = 38; gv.best_2048 = 2312; gv.best_bub = 31;
    memory_new(&gv.mem, 5);
    memory_tap(&gv.mem, 0); for (int i = 1; i < MEM_CARDS; i++) if (gv.mem.kind[i] == gv.mem.kind[0]) { memory_tap(&gv.mem, i); break; }
    memory_tap(&gv.mem, 4); memory_tap(&gv.mem, 5); gv.mem.elapsed_ms = 27000; gv.mem.hide_ms = 0;
    g2048_new(&gv.g2048, 9);
    { static const int b[4][4] = { {2,4,8,16}, {0,2,64,32}, {4,128,256,8}, {2,512,1024,2048} }; memcpy(gv.g2048.cell, b, sizeof b); gv.g2048.score = 3120; }
    bubbles_new(&gv.bub, s->screen == UI_SCREEN_GAME_BUBBLES ? (land ? 480 : 320) : 480, land ? 268 : 428, 3);
    for (int i = 0; i < 60; i++) bubbles_tick(&gv.bub, 50);
    gv.bub.b[1].crab = true; gv.bub.score = 12;
    if (gover) { gv.mem.won = true; gv.g2048.over = true; gv.bub.over = true; gv.bub.time_left_ms = 0; gv.record = true; }
    snprintf(gv.note, sizeof gv.note, "克帮你撤销了一步");
    s->games = &gv;
    ui_render(s);

    FILE *o = fopen(argv[1], "wb");
    if (!o) { perror("fopen"); return 1; }
    fprintf(o, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        uint16_t v = (uint16_t)((fb[i] << 8) | (fb[i] >> 8));
        unsigned char px[3] = { (unsigned char)(((v >> 11) & 31) * 255 / 31), (unsigned char)(((v >> 5) & 63) * 255 / 63), (unsigned char)((v & 31) * 255 / 31) };
        fwrite(px, 1, 3, o);
    }
    fclose(o);
    free(s);
    return 0;
}
