#include "cdh/cdh.h"
#include "fsw/fsm.h"
#include "cdh/mpu6050_equipment_handler.h"
#include "cdh/ms5607_equipment_handler.h"
#include "cdh/gps_equipment_handler.h"
#include "cdh/cdh_debug.h"
#include "cdh/cdh_fdir.h"
#include "cdh/coral.h"
#include "cdh/sensor_validation.h"

#include "main.h"
#include <math.h>

extern I2C_HandleTypeDef hi2c1;

static MPU6050_EquipmentHandler s_imu;
static MS5607_EquipmentHandler  s_baro;
static GPS_EquipmentHandler     s_gps;
static CDH_FDIR_Context         s_fdir;
static float                    s_ground_baro_alt_m = 0.0f;

/*  CDH_Init                                                           */
/* ------------------------------------------------------------------ */
void CDH_Init(void)
{
    s_imu  = MPU6050_EquipmentHandler_Init(&hi2c1);
    s_baro = MS5607_EquipmentHandler_Init(&hi2c1);
    s_gps  = GPS_EquipmentHandler_Init(&hi2c1);
    CDH_FDIR_Init(&s_fdir);
    SensorValidation_Init();

    if (s_baro.baro_valid)
        s_ground_baro_alt_m = s_baro.data.altitude;

    Coral_Init();   /* UART5 already init by MX_UART5_Init() in main.c */

}

/* ------------------------------------------------------------------ */
/*  CDH_Update  (called at 1 Hz from the superloop)                   */
/* ------------------------------------------------------------------ */
void CDH_Update(SensorData_t *dp, SCV_t *scv)
{
    dp->timestamp_ms = HAL_GetTick();

    /* ---- IMU ---- */
    s_imu = MPU6050_EquipmentHandler_Update(s_imu, &hi2c1);
    if (s_imu.imu_valid)
    {
        dp->imu_accel_x_g   = s_imu.data.accel_x;
        dp->imu_accel_y_g   = s_imu.data.accel_y;
        dp->imu_accel_z_g   = s_imu.data.accel_z;
        dp->imu_accel_mag_g = sqrtf((s_imu.data.accel_x * s_imu.data.accel_x) +
                                    (s_imu.data.accel_y * s_imu.data.accel_y) +
                                    (s_imu.data.accel_z * s_imu.data.accel_z));
        dp->imu_gyro_x_dps  = s_imu.data.gyro_x;
        dp->imu_gyro_y_dps  = s_imu.data.gyro_y;
        dp->imu_gyro_z_dps  = s_imu.data.gyro_z;
        dp->imu_valid       = 1U;
    }
    else { dp->imu_valid = 0U; }

    /* ---- Barometer ---- */
    s_baro = MS5607_EquipmentHandler_Update(s_baro, &hi2c1);
    if (s_baro.baro_valid)
    {
        dp->baro_pressure_pa = s_baro.data.pressure * 100.0f;
        dp->baro_alt_m       = s_baro.data.altitude - s_ground_baro_alt_m;
        dp->baro_temp_c      = s_baro.data.temperature;
        dp->baro_valid       = 1U;
    }
    else { dp->baro_valid = 0U; }

    /* ---- GPS ---- */
    s_gps = GPS_EquipmentHandler_Update(s_gps, &hi2c1);
    /* Always copy GPS data to datapool (for logging), but validation sets VALID flag */
    dp->gps_lat_deg        = s_gps.data.latitude;
    dp->gps_lon_deg        = s_gps.data.longitude;
    dp->gps_alt_m          = s_gps.data.altitude;
    dp->gps_speed_mps      = s_gps.data.speed;
    dp->gps_vel_down_mps   = s_gps.data.vel_down;
    dp->gps_heading_deg    = s_gps.data.heading;
    dp->gps_utc_time       = s_gps.data.utc_time;
    dp->gps_num_satellites = s_gps.data.num_satellites;
    dp->gps_fix_type       = s_gps.data.fix_type;
    dp->gps_valid          = s_gps.gps_valid ? 1U : 0U;

    /* Validate sensor data and handle faults (before printing so validation matches printed values) */
    SensorValidation_Update(dp, scv);

    CDH_Debug_PrintDatapool(dp);
    CDH_Debug_PrintBaro(&s_baro);

    /* CDH owns the I2C bus: advance any pending restart requested by FDIR and
     * publish the resulting bus state. CDH is the sole writer of i2c_bus_state. */
    CDH_FDIR_BusRestart_Process(&s_fdir, &hi2c1);
    dp->i2c_bus_state = s_fdir.bus_state;

    /* TODO: read ADC for battery voltage — fill batt_voltage_mv, set batt_valid */
    Coral_Update(dp);
    /* TODO: write updated scv to NVM */

    (void)scv;
}

/* ------------------------------------------------------------------ */
/*  CDH_RequestBusRestart                                              */
/* ------------------------------------------------------------------ */
void CDH_RequestBusRestart(void)
{
    CDH_FDIR_BusRestart_Start(&s_fdir);
}
