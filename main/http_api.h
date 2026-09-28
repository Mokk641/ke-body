#pragma once
#include "esp_err.h"

/* Starts the HTTP server on port 80 (idempotent). Routes:
 *   GET  /ping  -> "ok"
 *   POST /face  -> body = kaomoji line, shown in the middle of the screen
 *   POST /say   -> body = text for the speech bubble ("" clears it) */
esp_err_t http_api_start(void);
