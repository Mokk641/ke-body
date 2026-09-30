#include "picview.h"
#include "pics.h"
#include "ui.h"
#include <stdlib.h>

/* two buffers, used in turn: the renderer may still be drawing the previous one when a new picture opens */
static uint16_t *s_buf[2];
static int s_which;

void picview_open(int pic_id)
{
    const pic_t *p = pics_get(pic_id);
    if (!p) { ui_toast("这张图已经不在了", 1500); return; }
    int W, H, w, h;
    ui_screen_size(&W, &H);
    uint16_t *fit = NULL;
    if (!rgb565_fit(p->full, p->fw, p->fh, W, H, &fit, &w, &h)) { ui_toast("内存不够", 1500); return; }
    s_which ^= 1;
    free(s_buf[s_which]);            /* the one from two pictures ago: long off the screen */
    s_buf[s_which] = fit;
    ui_set_screen(UI_SCREEN_VIEWER);
    ui_set_frame(fit, w, h);
    ui_toast("点一下返回", 1200);
}
