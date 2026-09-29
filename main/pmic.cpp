/* AXP2101 setup, copied from the official example
 * waveshareteam/ESP32-S3-Touch-LCD-3.5 ESP-IDF/07_lvgl_wifi/components/esp_port/esp_axp2101_port.cpp
 * (esp_axp2101_port_init): same voltages, same rails enabled. Left out on purpose:
 * the PMU watchdog, IRQ setup and the debug printf flood.
 *
 * Cross-check: the xiaozhi-esp32 board file for this board enables only ALDO1 (3.3 V),
 * BLDO1 (1.5 V) and BLDO2 (2.8 V) and disables everything else; the official example
 * enables all rails. We follow the official example; `pmic <rail> on|off` on the
 * serial console lets you narrow it down on the bench. */
#include "pmic.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"

static const char *TAG = "pmic";
static XPowersPMU s_pmu;
static bool s_ready;

struct rail_t {
    const char *name;
    bool (XPowersPMU::*enable)(void);
    bool (XPowersPMU::*disable)(void);
    bool (XPowersPMU::*is_enabled)(void);
    uint16_t (XPowersPMU::*voltage)(void);
};

static const rail_t RAILS[] = {
    { "dc1",     &XPowersPMU::enableDC1,     &XPowersPMU::disableDC1,     &XPowersPMU::isEnableDC1,     &XPowersPMU::getDC1Voltage },
    { "dc2",     &XPowersPMU::enableDC2,     &XPowersPMU::disableDC2,     &XPowersPMU::isEnableDC2,     &XPowersPMU::getDC2Voltage },
    { "dc3",     &XPowersPMU::enableDC3,     &XPowersPMU::disableDC3,     &XPowersPMU::isEnableDC3,     &XPowersPMU::getDC3Voltage },
    { "dc4",     &XPowersPMU::enableDC4,     &XPowersPMU::disableDC4,     &XPowersPMU::isEnableDC4,     &XPowersPMU::getDC4Voltage },
    { "dc5",     &XPowersPMU::enableDC5,     &XPowersPMU::disableDC5,     &XPowersPMU::isEnableDC5,     &XPowersPMU::getDC5Voltage },
    { "aldo1",   &XPowersPMU::enableALDO1,   &XPowersPMU::disableALDO1,   &XPowersPMU::isEnableALDO1,   &XPowersPMU::getALDO1Voltage },
    { "aldo2",   &XPowersPMU::enableALDO2,   &XPowersPMU::disableALDO2,   &XPowersPMU::isEnableALDO2,   &XPowersPMU::getALDO2Voltage },
    { "aldo3",   &XPowersPMU::enableALDO3,   &XPowersPMU::disableALDO3,   &XPowersPMU::isEnableALDO3,   &XPowersPMU::getALDO3Voltage },
    { "aldo4",   &XPowersPMU::enableALDO4,   &XPowersPMU::disableALDO4,   &XPowersPMU::isEnableALDO4,   &XPowersPMU::getALDO4Voltage },
    { "bldo1",   &XPowersPMU::enableBLDO1,   &XPowersPMU::disableBLDO1,   &XPowersPMU::isEnableBLDO1,   &XPowersPMU::getBLDO1Voltage },
    { "bldo2",   &XPowersPMU::enableBLDO2,   &XPowersPMU::disableBLDO2,   &XPowersPMU::isEnableBLDO2,   &XPowersPMU::getBLDO2Voltage },
    { "dldo1",   &XPowersPMU::enableDLDO1,   &XPowersPMU::disableDLDO1,   &XPowersPMU::isEnableDLDO1,   &XPowersPMU::getDLDO1Voltage },
    { "dldo2",   &XPowersPMU::enableDLDO2,   &XPowersPMU::disableDLDO2,   &XPowersPMU::isEnableDLDO2,   &XPowersPMU::getDLDO2Voltage },
    { "cpusldo", &XPowersPMU::enableCPUSLDO, &XPowersPMU::disableCPUSLDO, &XPowersPMU::isEnableCPUSLDO, &XPowersPMU::getCPUSLDOVoltage },
};

extern "C" esp_err_t pmic_init(i2c_master_bus_handle_t bus)
{
    if (!s_pmu.begin(bus, AXP2101_SLAVE_ADDRESS)) {
        ESP_LOGE(TAG, "AXP2101 not answering on I2C 0x%02x", AXP2101_SLAVE_ADDRESS);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "AXP2101 chip id 0x%02x", s_pmu.getChipID());

    /* --- verbatim from the official esp_axp2101_port_init() --- */
    s_pmu.setVbusVoltageLimit(XPOWERS_AXP2101_VBUS_VOL_LIM_4V36);
    s_pmu.setVbusCurrentLimit(XPOWERS_AXP2101_VBUS_CUR_LIM_1500MA);
    s_pmu.setSysPowerDownVoltage(2600);

    s_pmu.setDC1Voltage(3300);
    s_pmu.setDC2Voltage(1000);
    s_pmu.setDC3Voltage(3300);
    s_pmu.setDC4Voltage(1000);
    s_pmu.setDC5Voltage(3300);
    s_pmu.setALDO1Voltage(3300);
    s_pmu.setALDO2Voltage(3300);
    s_pmu.setALDO3Voltage(3300);
    s_pmu.setALDO4Voltage(3300);
    s_pmu.setBLDO1Voltage(1500);
    s_pmu.setBLDO2Voltage(2800);
    s_pmu.setCPUSLDOVoltage(1000);
    s_pmu.setDLDO1Voltage(3300);
    s_pmu.setDLDO2Voltage(3300);

    // s_pmu.enableDC1();   (official leaves DC1 as is: it feeds the ESP32 itself)
    s_pmu.enableDC2();
    s_pmu.enableDC3();
    s_pmu.enableDC4();
    s_pmu.enableDC5();
    s_pmu.enableALDO1();
    s_pmu.enableALDO2();
    s_pmu.enableALDO3();
    s_pmu.enableALDO4();
    s_pmu.enableBLDO1();
    s_pmu.enableBLDO2();
    s_pmu.enableCPUSLDO();
    s_pmu.enableDLDO1();
    s_pmu.enableDLDO2();

    s_pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
    s_pmu.setPowerKeyPressOnTime(XPOWERS_POWERON_128MS);

    s_pmu.disableTSPinMeasure();      /* no battery NTC on this board (official comment) */
    s_pmu.enableBattDetection();
    s_pmu.enableVbusVoltageMeasure();
    s_pmu.enableBattVoltageMeasure();
    s_pmu.enableSystemVoltageMeasure();
    s_pmu.setChargingLedMode(XPOWERS_CHG_LED_OFF);

    s_pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    s_pmu.clearIrqStatus();

    s_pmu.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_50MA);
    s_pmu.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_200MA);
    s_pmu.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_25MA);
    s_pmu.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V1);
    s_pmu.enableButtonBatteryCharge();
    s_pmu.setButtonBatteryChargeVoltage(3300);
    /* --- end of official sequence (watchdog + IRQ enable omitted) --- */

    s_ready = true;
    pmic_dump();
    return ESP_OK;
}

extern "C" bool pmic_ready(void) { return s_ready; }

extern "C" void pmic_dump(void)
{
    if (!s_ready) { printf("pmic: not initialised\n"); return; }
    printf("AXP2101 id=0x%02x  vbus=%s %umV  vsys=%umV  batt=%s %umV %u%%\n",
           s_pmu.getChipID(),
           s_pmu.isVbusIn() ? "in" : "out", s_pmu.getVbusVoltage(),
           s_pmu.getSystemVoltage(),
           s_pmu.isBatteryConnect() ? "yes" : "no", s_pmu.getBattVoltage(), s_pmu.getBatteryPercent());
    for (const rail_t &r : RAILS) {
        printf("  %-8s %s  %4u mV\n", r.name, (s_pmu.*r.is_enabled)() ? "ON " : "off", (s_pmu.*r.voltage)());
    }
}

extern "C" esp_err_t pmic_set_rail(const char *rail, bool on)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    for (const rail_t &r : RAILS) {
        if (strcmp(r.name, rail) == 0) {
            bool ok = on ? (s_pmu.*r.enable)() : (s_pmu.*r.disable)();
            ESP_LOGI(TAG, "%s -> %s (%s)", r.name, on ? "on" : "off", ok ? "ok" : "failed");
            return ok ? ESP_OK : ESP_FAIL;
        }
    }
    return ESP_ERR_NOT_FOUND;
}
