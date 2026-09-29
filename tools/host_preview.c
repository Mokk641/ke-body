/* Host-side preview of the screen layout. Compiles gfx.c + ui_render.c + fonts
 * with a normal C compiler and writes a PPM image, so layout and font rendering
 * can be checked without the board.
 *
 *   gcc -O1 -Imain -o /tmp/preview tools/host_preview.c main/gfx.c main/ui_render.c main/fonts/font_*.c
 *   /tmp/preview out.ppm "(—_—)" "你好，我是小身体" "192.168.1.23" [portrait|landscape] [dark|light]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "ui_render.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s out.ppm [face] [say] [corner] [portrait|landscape] [dark|light]\n", argv[0]);
        return 1;
    }
    int w = 320, h = 480;
    if (argc > 5 && strcmp(argv[5], "landscape") == 0) { w = 480; h = 320; }
    static uint16_t fb[320 * 480];
    gfx_init(fb, w, h);
    ui_state_t s = {0};
    if (argc > 2) snprintf(s.face, sizeof s.face, "%s", argv[2]);
    if (argc > 3) snprintf(s.say, sizeof s.say, "%s", argv[3]);
    if (argc > 4) snprintf(s.corner, sizeof s.corner, "%s", argv[4]);
    s.theme = (argc > 6 && strcmp(argv[6], "light") == 0) ? UI_THEME_LIGHT : UI_THEME_DARK;
    ui_render(&s);

    FILE *o = fopen(argv[1], "wb");
    if (!o) { perror("fopen"); return 1; }
    fprintf(o, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        uint16_t v = (uint16_t)((fb[i] << 8) | (fb[i] >> 8)); /* undo byte swap */
        unsigned char px[3] = {
            (unsigned char)(((v >> 11) & 31) * 255 / 31),
            (unsigned char)(((v >> 5) & 63) * 255 / 63),
            (unsigned char)((v & 31) * 255 / 31),
        };
        fwrite(px, 1, 3, o);
    }
    fclose(o);
    return 0;
}
