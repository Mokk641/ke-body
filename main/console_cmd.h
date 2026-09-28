#pragma once
#include "esp_err.h"

/* Serial REPL over USB-Serial/JTAG. Commands:
 *   wifi <ssid> <password>   save credentials to NVS and reboot
 *   wifi                     show current credentials / state
 *   wifi clear               erase credentials
 *   face <text> / say <text> local test of the display */
esp_err_t console_cmd_start(void);
