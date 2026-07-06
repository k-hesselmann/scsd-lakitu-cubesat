#include "cdh/cdh.h"
#include "cdh/mpu6050_equipment_handler.h"
#include "cdh/ms5607_equipment_handler.h"

#include "main.h"
#include <math.h>

extern I2C_HandleTypeDef hi2c1;

static MPU6050_EquipmentHandler s_imu;
static MS5607_EquipmentHandler s_baro;
static float s_ground_baro_alt_m = 0.0f;

void CDH_Init(void)
{
    s_imu = MPU6050_EquipmentHandler_Init(&hi2c1);
    s_baro = MS5607_EquipmentHandler_Init(&hi2c1);

    if (s_baro.baro_valid)
        s_ground_baro_alt_m = s_baro.data.altitude;

    /* TODO: initialise GPS (NEO-M8N) over UART, set Airborne mode (CR-006) */
    /* TODO: initialise SD card over SPI */
    /* TODO: initialise Coral UART (115200 baud, FR-027) */
}

void CDH_Update(SensorData_t *dp, SCV_t *scv)
{
    dp->timestamp_ms = HAL_GetTick();

    s_imu = MPU6050_EquipmentHandler_Update(s_imu, &hi2c1);
    if (s_imu.imu_valid)
    {
        dp->imu_accel_x_g = s_imu.data.accel_x;
        dp->imu_accel_y_g = s_imu.data.accel_y;
        dp->imu_accel_z_g = s_imu.data.accel_z;
        dp->imu_accel_mag_g = sqrtf((s_imu.data.accel_x * s_imu.data.accel_x) +
                                    (s_imu.data.accel_y * s_imu.data.accel_y) +
                                    (s_imu.data.accel_z * s_imu.data.accel_z));
        dp->imu_gyro_x_dps = s_imu.data.gyro_x;
        dp->imu_gyro_y_dps = s_imu.data.gyro_y;
        dp->imu_gyro_z_dps = s_imu.data.gyro_z;
        dp->imu_valid = 1U;
    }
    else
    {
        dp->imu_valid = 0U;
    }

    s_baro = MS5607_EquipmentHandler_Update(s_baro, &hi2c1);
    if (s_baro.baro_valid)
    {
        dp->baro_pressure_pa = s_baro.data.pressure * 100.0f;
        dp->baro_alt_m = s_baro.data.altitude - s_ground_baro_alt_m;
        dp->baro_temp_c = s_baro.data.temperature;
        dp->baro_valid = 1U;
    }
    else
    {
        dp->baro_valid = 0U;
    }

    /* TODO: read GPS — fill gps_* fields, set gps_valid */
    /* TODO: read ADC for battery voltage — fill batt_voltage_mv, set batt_valid */
    /* TODO: read Coral block over UART — fill coral_block[16], set coral_valid */
    /* TODO: write log record to SD card (FR-016, FR-017) */

    (void)scv;
}
