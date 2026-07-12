#include "cdh/cdh.h"
#include "fsw/fsm.h"
#include "fatfs.h"
#include "sd_spi.h"
#include <stdio.h>
#include <string.h>
#include "cdh/mpu6050_equipment_handler.h"
#include "cdh/ms5607_equipment_handler.h"
#include "cdh/gps_equipment_handler.h"
#include "cdh/cdh_debug.h"
#include "cdh/cdh_fdir.h"

#include "main.h"
#include <math.h>

extern I2C_HandleTypeDef hi2c1;

static MPU6050_EquipmentHandler s_imu;
static MS5607_EquipmentHandler s_baro;
static GPS_EquipmentHandler s_gps;
static CDH_FDIR_Context s_fdir;
static float s_ground_baro_alt_m = 0.0f;

/* ------------------------------------------------------------------ */
/*  SD logging — internal state                                        */
/* ------------------------------------------------------------------ */
#define LOG_BUFFER_SIZE  4096U   /* bytes buffered before a write     */
#define LOG_SYNC_ROWS      64U   /* f_sync() every N rows (~64 s)     */

static char     s_log_buf[LOG_BUFFER_SIZE];
static uint32_t s_log_used  = 0;
static uint32_t s_row_count = 0;
static FIL      s_log_file;
static uint8_t  s_sd_ready  = 0;  /* 0 = fault/not init, 1 = logging */

static const char *CSV_HEADER =
    "timestamp_ms,flight_phase,"
    "baro_pressure_pa,baro_temp_c,baro_alt_m,baro_valid,"
    "imu_accel_x_g,imu_accel_y_g,imu_accel_z_g,imu_accel_mag_g,"
    "imu_gyro_x_dps,imu_gyro_y_dps,imu_gyro_z_dps,imu_valid,"
    "gps_lat_deg,gps_lon_deg,gps_alt_m,gps_vvel_mps,gps_speed_mps,gps_valid,"
    "batt_voltage_mv,batt_valid,coral_valid\r\n";

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                   */
/* ------------------------------------------------------------------ */
static FRESULT sd_flush(void)
{
    UINT written;
    if (s_log_used == 0) { return FR_OK; }
    FRESULT r = f_write(&s_log_file, s_log_buf, s_log_used, &written);
    if (r != FR_OK || written != s_log_used) { return FR_DISK_ERR; }
    s_log_used = 0;
    return FR_OK;
}

static FRESULT sd_append(const char *text)
{
    uint32_t len = (uint32_t)strlen(text);
    if ((s_log_used + len) > LOG_BUFFER_SIZE)
    {
        FRESULT r = sd_flush();
        if (r != FR_OK) { return r; }
    }
    memcpy(&s_log_buf[s_log_used], text, len);
    s_log_used += len;
    return FR_OK;
}

/* ------------------------------------------------------------------ */
/*  CDH_Init                                                           */
/* ------------------------------------------------------------------ */
void CDH_Init(void)
{
    s_imu = MPU6050_EquipmentHandler_Init(&hi2c1);
    s_baro = MS5607_EquipmentHandler_Init(&hi2c1);
    s_gps = GPS_EquipmentHandler_Init(&hi2c1);
    CDH_FDIR_Init(&s_fdir);

    if (s_baro.baro_valid)
        s_ground_baro_alt_m = s_baro.data.altitude;

    /* SCV health/identity fields (magic, timeout counts, faults) are owned and
     * initialised by FDIR_Init(); CDH only fills SensorData_t. */

    /* TODO: initialise GPS (NEO-M8N) over UART, set Airborne mode (CR-006) */
    /* TODO: initialise Coral UART (115200 baud, FR-027) */
    /* TODO: read and store ground pressure baseline for baro_alt_m reference */

    /* ---- SD card + FatFS init ---- */
    MX_FATFS_Init();

    if (SD_SPI_Init() != 0)
    {
        /* SD hardware not responding — s_sd_ready stays 0 */
        return;
    }

    if (f_mount(&USERFatFS, USERPath, 1) != FR_OK)
    {
        return;
    }

    if (f_open(&s_log_file, "FLIGHT.CSV", FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
    {
        f_mount(NULL, USERPath, 0);
        return;
    }

    /* Write CSV header immediately and sync so it survives a power cut */
    UINT written;
    f_write(&s_log_file, CSV_HEADER, strlen(CSV_HEADER), &written);
    f_sync(&s_log_file);

    s_sd_ready  = 1;
    s_log_used  = 0;
    s_row_count = 0;
}

/* ------------------------------------------------------------------ */
/*  CDH_Update  (called at 1 Hz from the superloop)                   */
/* ------------------------------------------------------------------ */
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

    s_gps = GPS_EquipmentHandler_Update(s_gps, &hi2c1);
    if (s_gps.gps_valid)
    {
        dp->gps_lat_deg = s_gps.data.latitude;
        dp->gps_lon_deg = s_gps.data.longitude;
        dp->gps_alt_m = s_gps.data.altitude;
        dp->gps_speed_mps = s_gps.data.speed;
        dp->gps_valid = 1U;
    }
    else
    {
        dp->gps_valid = 0U;
    }

    CDH_Debug_PrintGPS(&s_gps);
    CDH_Debug_PrintBaro(&s_baro);

    dp->i2c_bus_state = s_fdir.bus_state;

    /* Equipment faults, timeout counts and SCV bookkeeping are owned by
     * FDIR_Update(); CDH only publishes SensorData_t here. */

    /* TODO: read GPS — fill gps_* fields, set gps_valid */
    /* TODO: read ADC for battery voltage — fill batt_voltage_mv, set batt_valid */
    /* TODO: read Coral block over UART — fill coral_block[16], set coral_valid */
    /* TODO: write updated scv to NVM */

    /* ---- SD logging ---- */
    if (!s_sd_ready) { (void)scv; return; }

    char row[300];
    snprintf(row, sizeof(row),
        "%lu,%u,"
        "%.2f,%.2f,%.2f,%u,"
        "%.4f,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f,%u,"
        "%.7f,%.7f,%.2f,%.2f,%.2f,%u,"
        "%u,%u,%u\r\n",
        (unsigned long)dp->timestamp_ms,
        (unsigned int)FSW_GetPhase(),
        /* barometer */
        dp->baro_pressure_pa, dp->baro_temp_c, dp->baro_alt_m, (unsigned)dp->baro_valid,
        /* IMU */
        dp->imu_accel_x_g, dp->imu_accel_y_g, dp->imu_accel_z_g, dp->imu_accel_mag_g,
        dp->imu_gyro_x_dps, dp->imu_gyro_y_dps, dp->imu_gyro_z_dps, (unsigned)dp->imu_valid,
        /* GPS */
        dp->gps_lat_deg, dp->gps_lon_deg, dp->gps_alt_m,
        dp->gps_vvel_mps, dp->gps_speed_mps, (unsigned)dp->gps_valid,
        /* EPS + payload */
        (unsigned)dp->batt_voltage_mv, (unsigned)dp->batt_valid, (unsigned)dp->coral_valid);

    if (sd_append(row) != FR_OK)
    {
        s_sd_ready = 0;   /* log the fault; sensor reads continue */
        (void)scv;
        return;
    }

    s_row_count++;

    /* Flush buffer and sync to card every 64 rows (~64 s at 1 Hz) */
    if ((s_row_count % LOG_SYNC_ROWS) == 0)
    {
        if (sd_flush()              != FR_OK) { s_sd_ready = 0; }
        if (f_sync(&s_log_file)     != FR_OK) { s_sd_ready = 0; }
    }

    (void)scv;
}
