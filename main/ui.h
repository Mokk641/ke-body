/* Thread-safe UI state + render task. */
#pragma once
#include <stdbool.h>

void ui_start(void);
void ui_set_face(const char *utf8);    /* base expression, truncated to UI_FACE_MAX_CHARS */
void ui_set_say(const char *utf8);     /* bubble text, "" hides it; truncated to UI_SAY_MAX_CHARS */
void ui_set_corner(const char *utf8);  /* top-right status text (base: IP / wifi state) */
void ui_blush(void);                   /* show (—//—) for 2 s, then restore the base face */

/* Temporary overrides used while recording / playing. NULL restores the base value. */
void ui_override_face(const char *utf8);
void ui_override_corner(const char *utf8);
