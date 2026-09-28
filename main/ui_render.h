/* Screen layout for the "face" UI. Pure C (no ESP-IDF), shared with tools/host_preview.c */
#pragma once
#include <stdbool.h>

#define UI_FACE_MAX_CHARS 20   /* code points accepted by POST /face */
#define UI_SAY_MAX_CHARS  60   /* code points accepted by POST /say  */
#define UI_FACE_BUF       (UI_FACE_MAX_CHARS * 4 + 1)
#define UI_SAY_BUF        (UI_SAY_MAX_CHARS * 4 + 1)

typedef struct {
    char face[UI_FACE_BUF];   /* kaomoji shown in the middle of the screen */
    char say[UI_SAY_BUF];     /* speech bubble text; "" hides the bubble */
    char corner[48];          /* small status text in the top-right corner (IP or state) */
} ui_state_t;

/* Draw the whole screen into the gfx framebuffer (gfx_init must have been called). */
void ui_render(const ui_state_t *s);
