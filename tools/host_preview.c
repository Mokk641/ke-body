/* Host-side preview of the screen layout. Compiles gfx.c + ui_render.c + fonts
 * with a normal C compiler and writes a PPM image, so layout and font rendering
 * can be checked without the board.
 *
 *   gcc -O1 -Imain -o /tmp/preview tools/host_preview.c main/gfx.c main/ui_render.c main/fonts/font_*.c
 *   /tmp/preview out.ppm "(—_—)" "corner" [portrait|landscape] [dark|light] [chat|camera|gallery] [toast]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "ui_render.h"

static void add(ui_state_t *s, int who, const char *t)
{
    if (s->msg_count >= UI_CHAT_MAX) return;
    s->msgs[s->msg_count].who = who;
    snprintf(s->msgs[s->msg_count].text, UI_SAY_BUF, "%s", t);
    s->msg_count++;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s out.ppm [face] [corner] [portrait|landscape] [dark|light] [chat|camera|gallery] [toast]\n", argv[0]);
        return 1;
    }
    int w = 320, h = 480;
    if (argc > 4 && strcmp(argv[4], "landscape") == 0) { w = 480; h = 320; }
    static uint16_t fb[320 * 480];
    gfx_init(fb, w, h);
    ui_state_t *s = calloc(1, sizeof(ui_state_t));
    s->pressed = UI_HIT_NONE;
    if (argc > 2) snprintf(s->face, sizeof s->face, "%s", argv[2]);
    if (argc > 3) snprintf(s->corner, sizeof s->corner, "%s", argv[3]);
    s->theme = (argc > 5 && strcmp(argv[5], "light") == 0) ? UI_THEME_LIGHT : UI_THEME_DARK;
    if (argc > 6 && strcmp(argv[6], "camera") == 0) s->screen = UI_SCREEN_CAMERA;
    if (argc > 6 && strcmp(argv[6], "gallery") == 0) { s->screen = UI_SCREEN_GALLERY; s->gal_count = 3; s->gal_index = 1; snprintf(s->cam_text, sizeof s->cam_text, "20260929-121500.jpg"); }
    if (argc > 7 && strcmp(argv[7], "sleep") == 0) { s->sleeping = true; s->anim_tick = 37; }
    else if (argc > 7 && strcmp(argv[7], "blink") == 0) s->blink = true;
    else if (argc > 7) snprintf(s->toast, sizeof s->toast, "%s", argv[7]);

    const char *tb[] = { "想你了", "抱抱", "在干嘛", "晚安" };
    const char *eb[] = { "(´ω`)", "(≧▽≦)", "♡", "💧" };
    for (int i = 0; i < 4; i++) { snprintf(s->text_btn[i].text, UI_BTN_TEXT_LEN, "%s", tb[i]); snprintf(s->emoji_btn[i].text, UI_BTN_TEXT_LEN, "%s", eb[i]); }
    s->text_btn_n = 4;
    s->emoji_btn_n = 4;
    s->pressed = UI_HIT_TEXT_BTN0 + 1;

    add(s, CHAT_KE, "早呀，今天天气不错。");
    add(s, CHAT_HER, "想你了");
    add(s, CHAT_KE, "我也想你。中午吃了什么？要不要一起出去散散步，公园的花开了。");
    add(s, CHAT_HER, "(≧▽≦)");
    add(s, CHAT_KE, "晚上给你打电话。");
    s->blush_alpha = 255;
    if (s->screen == UI_SCREEN_CAMERA) snprintf(s->cam_text, sizeof s->cam_text, "已保存 20260929-121500.jpg");

    ui_render(s);

    FILE *o = fopen(argv[1], "wb");
    if (!o) { perror("fopen"); return 1; }
    fprintf(o, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        uint16_t v = (uint16_t)((fb[i] << 8) | (fb[i] >> 8));
        unsigned char px[3] = {
            (unsigned char)(((v >> 11) & 31) * 255 / 31),
            (unsigned char)(((v >> 5) & 63) * 255 / 63),
            (unsigned char)((v & 31) * 255 / 31),
        };
        fwrite(px, 1, 3, o);
    }
    fclose(o);
    free(s);
    return 0;
}
