/* AXP2101 power management (via XPowersLib, as in the official Waveshare examples). */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Runs the same rail setup as the official esp_axp2101_port.cpp (watchdog/IRQ left out).
 * Logs chip id and every rail. Returns ESP_FAIL if the chip does not answer. */
esp_err_t pmic_init(i2c_master_bus_handle_t bus);
bool pmic_ready(void);

/* Print all rails (enabled + mV), VBUS/VSYS/battery voltages. */
void pmic_dump(void);

/* rail: dc1..dc5, aldo1..aldo4, bldo1, bldo2, dldo1, dldo2, cpusldo */
esp_err_t pmic_set_rail(const char *rail, bool on);

#ifdef __cplusplus
}
#endif
