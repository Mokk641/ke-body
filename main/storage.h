/* Photo storage: Micro SD (FAT, /sdcard/DCIM) when a card is present, otherwise the
 * 4 MB "storage" flash partition (/flash/DCIM, at most STORAGE_FLASH_MAX photos). */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#define STORAGE_FLASH_MAX 12
#define STORAGE_MAX_FILES 200
#define STORAGE_NAME_LEN  64

esp_err_t storage_init(void);
esp_err_t storage_rescan(void);   /* probe the SD slot again (card inserted after boot) */          /* mounts what it can; safe to call again */
bool storage_is_sd(void);
const char *storage_dir(void);         /* ".../DCIM" or NULL if nothing is mounted */

/* Save a JPEG; name gets "YYYYMMDD-HHMMSS.jpg" (or IMG_nnnnn.jpg without valid time).
 * ESP_ERR_NO_MEM when the flash fallback is full. */
esp_err_t storage_save_jpeg(const uint8_t *jpeg, size_t len, char *name, size_t name_len);

/* Save under an explicit file name (used by `cam sweep`). Same flash limit as above. */
esp_err_t storage_save_named(const char *name, const uint8_t *data, size_t len);

/* Directory listing (sorted by name). Returns count; names is STORAGE_MAX_FILES x STORAGE_NAME_LEN. */
int storage_list(char (*names)[STORAGE_NAME_LEN]);
/* Other pictures on the SD card: JPEG/PNG files in its root and in /FROMKE, appended to `names` from index `start`
 * as "/<file>" or "/FROMKE/<file>" (sorted). Returns how many were added. Nothing without a card. */
int storage_list_extra(char (*names)[STORAGE_NAME_LEN], int start, int max);
bool storage_name_readonly(const char *name);   /* true for the card's own files (viewing only); DCIM and /FROMKE can be deleted */

/* Keep a copy of a picture Ke sent, on the SD card (/sdcard/FROMKE/<date>.<ext>). ESP_ERR_NOT_FOUND without a card. */
esp_err_t storage_save_fromke(const uint8_t *data, size_t len, const char *ext, char *name, size_t name_len);

esp_err_t storage_read(const char *name, uint8_t **data, size_t *len);   /* malloc'd (PSRAM) */
esp_err_t storage_delete(const char *name);
