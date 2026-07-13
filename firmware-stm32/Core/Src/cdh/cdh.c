#include "cdh/cdh.h"
#include "cdh/mpu6050_equipment_handler.h"
#include "cdh/ms5607_equipment_handler.h"
#include "cdh/gps_equipment_handler.h"
#include "cdh/cdh_debug.h"
#include "cdh/cdh_fdir.h"

#include "main.h"
#include <math.h>

#define SCV_MAGIC          0xCAFEU
#define SENSOR_FAULT_GPS   0x01U
#define SENSOR_FAULT_IMU   0x02U
#define SENSOR_FAULT_BARO  0x04U
#define SENSOR_FAULT_CORAL 0x10U

extern I2C_HandleTypeDef hi2c1;

static MPU6050_EquipmentHandler s_imu;
static MS5607_EquipmentHandler s_baro;
static GPS_EquipmentHandler s_gps;
static CDH_FDIR_Context s_fdir;
static float s_ground_baro_alt_m = 0.0f;

static void CDH_SetFault(SCV_t *scv, uint8_t mask, uint8_t active)
{
    if (active)
        scv->sensor_faults |= mask;
    else
        scv->sensor_faults &= (uint8_t)~mask;
}

void CDH_Init(void)
{
    s_imu = MPU6050_EquipmentHandler_Init(&hi2c1);
    s_baro = MS5607_EquipmentHandler_Init(&hi2c1);
    s_gps = GPS_EquipmentHandler_Init(&hi2c1);
    CDH_FDIR_Init(&s_fdir);

    if (s_baro.baro_valid)
        s_ground_baro_alt_m = s_baro.data.altitude;

    g_scv.magic = SCV_MAGIC;
    g_scv.last_update_ms = HAL_GetTick();

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
        scv->imu_timeout_count = 0U;
    }
    else
    {
        dp->imu_valid = 0U;
        if (scv->imu_timeout_count < UINT8_MAX)
            scv->imu_timeout_count++;
    }

    s_baro = MS5607_EquipmentHandler_Update(s_baro, &hi2c1);
    if (s_baro.baro_valid)
    {
        dp->baro_pressure_pa = s_baro.data.pressure * 100.0f;
        dp->baro_alt_m = s_baro.data.altitude - s_ground_baro_alt_m;
        dp->baro_temp_c = s_baro.data.temperature;
        dp->baro_valid = 1U;
        scv->baro_timeout_count = 0U;
    }
    else
    {
        dp->baro_valid = 0U;
        if (scv->baro_timeout_count < UINT8_MAX)
            scv->baro_timeout_count++;
    }

    s_gps = GPS_EquipmentHandler_Update(s_gps, &hi2c1);
    if (s_gps.gps_valid)
    {
        dp->gps_lat_deg = s_gps.data.latitude;
        dp->gps_lon_deg = s_gps.data.longitude;
        dp->gps_alt_m = s_gps.data.altitude;
        dp->gps_speed_mps = s_gps.data.speed;
        dp->gps_vel_north_mps = s_gps.data.vel_north;
        dp->gps_vel_east_mps = s_gps.data.vel_east;
        dp->gps_vel_down_mps = s_gps.data.vel_down;
        dp->gps_vvel_mps = s_gps.data.vel_down;
        dp->gps_heading_deg = s_gps.data.heading;
        dp->gps_num_satellites = s_gps.data.num_satellites;
        dp->gps_fix_type = s_gps.data.fix_type;
        dp->gps_valid = 1U;
    }
    else
    {
        dp->gps_valid = 0U;
    }

    CDH_Debug_PrintDatapool(dp);
    CDH_Debug_PrintBaro(&s_baro);

    dp->i2c_bus_state = s_fdir.bus_state;

    CDH_SetFault(scv, SENSOR_FAULT_IMU, dp->imu_valid == 0U);
    CDH_SetFault(scv, SENSOR_FAULT_BARO, dp->baro_valid == 0U);
    CDH_SetFault(scv, SENSOR_FAULT_GPS, dp->gps_valid == 0U);
    CDH_SetFault(scv, SENSOR_FAULT_CORAL, dp->coral_valid == 0U);

    scv->last_update_ms = dp->timestamp_ms;
    if (dp->batt_valid)
        scv->last_batt_mv = dp->batt_voltage_mv;

    /* TODO: read GPS — fill gps_* fields, set gps_valid */
    /* TODO: read ADC for battery voltage — fill batt_voltage_mv, set batt_valid */
    /* TODO: read Coral block over UART — fill coral_block[16], set coral_valid */
    /* TODO: write updated scv to NVM */
    /* TODO: write log record to SD card (FR-016, FR-017) */
}
