/* Outbound HTTP to the PC bridge (ke_bridge.py). The base URL comes from the
 * NVS "server_url" setting (e.g. http://192.168.1.5:8770/hear): the last path
 * segment is replaced, so /hear -> /msg, /photo. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

/* Build the URL for `path` ("msg", "photo"). false when no server is configured. */
bool bridge_url(const char *path, char *out, size_t out_len);

/* Blocking POST. status receives the HTTP status; resp/resp_len (optional) get a
 * malloc'd copy of the response body up to resp_max bytes. */
esp_err_t bridge_post(const char *url, const char *content_type, const uint8_t *data, size_t len,
                      int *status, uint8_t **resp, size_t *resp_len, size_t resp_max);

/* Queue a text message for POST /msg from a worker task (non-blocking). */
void bridge_send_text(const char *text);

/* Queue a JPEG for POST /photo; the buffer is copied. */
void bridge_send_photo(const uint8_t *jpeg, size_t len);

void bridge_start(void);
