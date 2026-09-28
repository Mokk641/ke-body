/* Small string settings in NVS (namespace "kebody"). Nothing here is in the repo. */
#pragma once
#include <stdbool.h>
#include "esp_err.h"

#define SETTINGS_KEY_SERVER_URL "server_url"   /* e.g. http://192.168.1.5:8770/hear */

bool settings_get_str(const char *key, char *out, unsigned out_len);   /* false if unset */
esp_err_t settings_set_str(const char *key, const char *value);
esp_err_t settings_erase(const char *key);
