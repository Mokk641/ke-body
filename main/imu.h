/* QMI8658 6-axis IMU: shake detection, face-down detection, optional auto-rotate.
 * Registers from the official SensorLib (components/sensorlib/src/REG/QMI8658Constants.h). */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

typedef enum {
    IMU_EVT_SHAKE,        /* shaken twice within a second */
    IMU_EVT_FACE_DOWN,    /* lying screen-down for 1.5 s */
    IMU_EVT_FACE_UP,      /* picked up again */
    IMU_EVT_ORIENTATION,  /* auto-rotate: arg = 0/90/180/270 */
} imu_evt_t;

typedef void (*imu_cb_t)(imu_evt_t evt, int arg);

esp_err_t imu_init(i2c_master_bus_handle_t bus, imu_cb_t cb);
bool imu_ready(void);
bool imu_read(float *ax, float *ay, float *az);   /* in g */

void imu_set_invert(bool inv);      /* flip the face-down sign if the board's Z points the other way (NVS) */
bool imu_get_invert(void);
void imu_set_autorotate(bool on);   /* NVS; default off */
bool imu_get_autorotate(void);
void imu_print(void);               /* one line of live values for the console */
