/* Photo storage: Micro SD (FAT, /sdcard/DCIM) when a card is present, otherwise the
 * 4 MB "storage" flash partition (/flash/DCIM, at most STORAGE_FLASH_MAX photos). */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#define STORAGE_FLASH_MAX 12
#define STORAGE_MAX_FILES 200
#define STORAGE_NAME_LEN  40

esp_err_t storage_init(void);          /* mounts what it can; safe to call again */
bool storage_is_sd(void);
const char *storage_dir(void);         /* ".../DCIM" or NULL if nothing is mounted */

/* Save a JPEG; name gets "YYYYMMDD-HHMMSS.jpg" (or IMG_nnnnn.jpg without valid time).
 * ESP_ERR_NO_MEM when the flash fallback is full. */
esp_err_t storage_save_jpeg(const uint8_t *jpeg, size_t len, char *name, size_t name_len);

/* Directory listing (sorted by name). Returns count; names is STORAGE_MAX_FILES x STORAGE_NAME_LEN. */
int storage_list(char (*names)[STORAGE_NAME_LEN]);
esp_err_t storage_read(const char *name, uint8_t **data, size_t *len);   /* malloc'd (PSRAM) */
esp_err_t storage_delete(const char *name);
