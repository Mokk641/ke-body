/* Thread-safe UI state + render task. */
#pragma once
#include <stdbool.h>
#include "esp_err.h"

void ui_start(void);                   /* loads rotation/theme from NVS, draws the first frame */
void ui_set_face(const char *utf8);    /* base expression, truncated to UI_FACE_MAX_CHARS */
void ui_set_say(const char *utf8);     /* bubble text, "" hides it; truncated to UI_SAY_MAX_CHARS */
void ui_set_corner(const char *utf8);  /* top-right status text (base: IP / wifi state) */
void ui_blush(void);                   /* show (—//—) for 2 s, then restore the base face */

/* Temporary overrides used while recording / playing. NULL restores the base value. */
void ui_override_face(const char *utf8);
void ui_override_corner(const char *utf8);

/* Rotation 0/90/180/270 and theme "dark"/"light"; both saved to NVS. */
esp_err_t ui_set_rotation(int rotation);
int ui_get_rotation(void);
esp_err_t ui_set_theme(const char *name);
const char *ui_get_theme(void);
