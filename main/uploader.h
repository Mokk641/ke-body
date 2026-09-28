#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/* POST a WAV file (Content-Type: audio/wav) to url. Blocks up to ~15 s.
 * Returns ESP_OK when the request completed; *status receives the HTTP status. */
esp_err_t uploader_post_wav(const char *url, const uint8_t *data, size_t len, int *status);
