#include "cdh/cdh.h"

void CDH_Init(void)
{
    /* TODO: initialise IMU (MPU-6050) over I2C */
    /* TODO: initialise barometer (MS5607) over I2C */
    /* TODO: initialise GPS (NEO-M8N) over UART, set Airborne mode (CR-006) */
    /* TODO: initialise SD card over SPI */
    /* TODO: initialise Coral UART (115200 baud, FR-027) */
    /* TODO: read and store ground pressure baseline for baro_alt_m reference */
}

void CDH_Update(SensorData_t *dp, SCV_t *scv)
{
    dp->timestamp_ms = 0; /* TODO: HAL_GetTick() */

    /* TODO: read GPS — fill gps_* fields, set gps_valid */
    /* TODO: read IMU — fill imu_* fields, compute imu_accel_mag_g, set imu_valid */
    /* TODO: read barometer — fill baro_* fields, compute baro_alt_m relative to
             ground baseline, set baro_valid */
    /* TODO: read ADC for battery voltage — fill batt_voltage_mv, set batt_valid */
    /* TODO: read Coral block over UART — fill coral_block[16], set coral_valid */
    /* TODO: update scv->sensor_faults and timeout counters */
    /* TODO: write updated scv to NVM */
    /* TODO: write log record to SD card (FR-016, FR-017) */

    (void)scv;
}
