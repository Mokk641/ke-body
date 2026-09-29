#pragma once
#include "esp_err.h"

/* Serial REPL over USB-Serial/JTAG. Commands (see README):
 *   wifi <ssid> <password> | wifi | wifi clear
 *   server <url> | server | server clear
 *   volume <0-100>
 *   rotate 0|90|180|270          bright <5-100>          theme dark|light
 *   night HH:MM HH:MM <level> | night off | night
 *   touchlog on|off
 *   pmic | pmic init | pmic <rail> on|off      tca | tca <pin> 0|1|in
 *   audio test [ms] | audio regs | audio gain <0-7>
 *   face <text> / say <text> */
esp_err_t console_cmd_start(void);
