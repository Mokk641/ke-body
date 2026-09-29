/* Backlight brightness (NVS) + night-time auto dimming (needs NTP time). */
#pragma once
#include <stdbool.h>
#include "esp_err.h"

#define LIGHT_MIN_BRIGHT     5
#define LIGHT_DEFAULT_BRIGHT 80
#define LIGHT_DEFAULT_NIGHT  "23:00 07:00 20"   /* start end level; "off" disables */

void light_init(void);                       /* load settings, apply backlight, start the 20 s timer */
esp_err_t light_set_bright(int percent);     /* clamps to LIGHT_MIN_BRIGHT..100, saves to NVS */
int light_get_bright(void);

/* "HH:MM HH:MM level" or "off". Saves to NVS. */
esp_err_t light_set_night(const char *spec);
const char *light_get_night(void);
bool light_night_active(void);               /* currently dimmed by the night schedule */

/* Temporary override (sleep): level 5..100, or -1 to clear. Not saved. */
void light_set_override(int level);

void light_start_sntp(void);                 /* call once when Wi-Fi is up */
bool light_time_valid(void);
void light_print_status(void);
