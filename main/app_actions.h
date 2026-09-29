/* Callbacks from the UI (touch) into the application (main.c). */
#pragma once

void app_on_tap(int hit);          /* a tap on a UI element (hit id from ui_render.h) */
void app_on_long_press(void);      /* held >= 0.5 s on the face / chat area: start talking */
void app_on_long_release(void);    /* released after a long press: stop talking */
void app_on_swipe(int dir);        /* gallery: +1 next, -1 previous */
void app_on_touch_activity(void);  /* any touch (wake from sleep etc.) */
