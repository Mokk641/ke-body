/* Camera / gallery screens and the remote snapshot. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

void camui_init(void);                 /* loads the "peek" setting */
void camui_enter(void);                /* chat -> camera screen (starts the live view) */
void camui_leave(void);                /* -> chat screen, camera off */
void camui_shoot(void);                /* take a photo, save, toast */
void camui_gallery_enter(void);
void camui_gallery_step(int dir);      /* +1 next, -1 previous */
void camui_gallery_delete(void);
void camui_gallery_send(void);         /* POST /photo to the bridge */

/* "让克看看": when on, GET/POST /snap takes a photo and returns it. Saved to NVS. */
bool camui_peek(void);
void camui_set_peek(bool on);
esp_err_t camui_remote_snap(uint8_t **jpeg, size_t *len);   /* ESP_ERR_NOT_ALLOWED when peek is off */

void camui_print_photos(void);         /* console: list files */
