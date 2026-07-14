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
#include "cdh/coral.h"

#include "main.h"
#include <math.h>

extern I2C_HandleTypeDef hi2c1;

static MPU6050_EquipmentHandler s_imu;
static MS5607_EquipmentHandler  s_baro;
static GPS_EquipmentHandler     s_gps;
static CDH_FDIR_Context         s_fdir;
static float                    s_ground_baro_alt_m = 0.0f;

/* ------------------------------------------------------------------ */
/*  SD logging — configuration                                         */
/* ------------------------------------------------------------------ */
/*
 * File rolling strategy:
 *   A new file is opened every LOG_ROWS_PER_FILE rows (~5 min at 1 Hz).
 *   When a file is closed it is renamed to include its actual duration:
 *
 *     L000060.CSV          <- currently open  (started at t=60 s since boot)
 *     L000060D0300.CSV     <- cleanly closed, ran for 300 s
 *     L000360D0300.CSV     <- next segment
 *     L000660.CSV          <- no duration = STM rebooted mid-segment (partial)
 *
 *   Mid-segment f_sync() every LOG_SYNC_ROWS limits worst-case data loss
 *   to ~64 rows if power is cut between rolls.
 */
#define LOG_ROWS_PER_FILE         300U   /* rows before rolling (~5 min)   */
#define LOG_SYNC_ROWS              64U   /* f_sync() within a file segment  */
#define LOG_BUFFER_SIZE          4096U   /* bytes buffered before f_write() */
#define SD_FAULT_REMOUNT_STRIKES    3U   /* consecutive faults before remount */

/* ------------------------------------------------------------------ */
/*  SD logging — internal state                                        */
/* ------------------------------------------------------------------ */
static char     s_log_buf[LOG_BUFFER_SIZE];
static uint32_t s_log_used        = 0;
static uint32_t s_row_count       = 0;   /* rows in current segment        */
static FIL      s_log_file;
static uint8_t  s_sd_ready        = 0;
static char     s_log_filename[24];      /* "B{boot}L{start_s}.CSV" while open */
static uint32_t s_file_open_tick  = 0;   /* HAL_GetTick() when opened      */
static uint8_t  s_fault_strikes   = 0;
static SCV_t   *s_scv             = NULL; /* captured on first CDH_Update() */

/* ------------------------------------------------------------------ */
/*  CSV layout — complete SensorData_t + SCV_t snapshot               */
/* ------------------------------------------------------------------ */
static const char *CSV_HEADER =
    /* timing */
    "timestamp_ms,flight_phase,"
    /* GPS */
    "gps_lat_deg,gps_lon_deg,gps_alt_m,"
    "gps_speed_mps,gps_vel_north_mps,gps_vel_east_mps,gps_vel_down_mps,"
    "gps_heading_deg,gps_num_satellites,gps_fix_type,gps_valid,"
    /* IMU */
    "imu_accel_x_g,imu_accel_y_g,imu_accel_z_g,imu_accel_mag_g,"
    "imu_gyro_x_dps,imu_gyro_y_dps,imu_gyro_z_dps,imu_valid,"
    /* Barometer */
    "baro_pressure_pa,baro_alt_m,baro_temp_c,baro_valid,"
    /* Bus + EPS + payload */
    "i2c_bus_state,batt_voltage_mv,batt_valid,coral_valid,"
    /* SCV health */
    "scv_boot_count,scv_mission_elapsed_ms,scv_reset_reason,"
    "scv_equipment_enabled,scv_equipment_faults,"
    "scv_gps_timeout,scv_imu_timeout,scv_baro_timeout,scv_coral_timeout,"
    "scv_sd_fault_count,scv_watchdog_resets,"
    "scv_last_batt_mv,scv_baro_ground_alt_cm"
    "\r\n";

/* ------------------------------------------------------------------ */
/*  Low-level buffer helpers                                           */
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

static void sd_record_fault(void)
{
    if (s_scv != NULL) { s_scv->sd_fault_count++; }
    s_fault_strikes++;
}

/* ------------------------------------------------------------------ */
/*  File management                                                    */
/* ------------------------------------------------------------------ */

/* Open a fresh log segment.
 * Name format: B{boot_count}L{seconds_since_boot}.CSV
 * boot_count comes from SCV (persisted in flash across reboots), so each
 * boot gets a unique prefix — no two boots can collide even at tick=0.
 * Example:  B001L000000.CSV → B001L000000D0300.CSV → B001L000300.CSV
 *           B002L000000.CSV  (next boot — different prefix)           */
static uint8_t CDH_SD_OpenNewFile(void)
{
    s_file_open_tick = HAL_GetTick();
    snprintf(s_log_filename, sizeof(s_log_filename),
             "B%03luL%05lu.CSV",
             (unsigned long)(g_scv.boot_count % 1000UL),
             (unsigned long)(s_file_open_tick / 1000UL));

    if (f_open(&s_log_file, s_log_filename, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
    {
        return 0;
    }

    UINT written;
    f_write(&s_log_file, CSV_HEADER, strlen(CSV_HEADER), &written);
    f_sync(&s_log_file);

    s_log_used  = 0;
    s_row_count = 0;
    return 1;
}

/* Flush + close current segment, rename it to include duration, then
 * open the next one.  Example: L000060.CSV → L000060D0300.CSV */
static uint8_t CDH_SD_RollFile(void)
{
    uint32_t dur_s = (HAL_GetTick() - s_file_open_tick) / 1000UL;
    char closed_name[28];
    snprintf(closed_name, sizeof(closed_name), "B%03luL%05luD%04lu.CSV",
             (unsigned long)(g_scv.boot_count % 1000UL),
             (unsigned long)(s_file_open_tick / 1000UL),
             (unsigned long)dur_s);

    sd_flush();
    f_sync(&s_log_file);
    f_close(&s_log_file);
    s_log_used = 0;

    /* Rename to add duration — data is already safe even if rename fails */
    f_rename(s_log_filename, closed_name);

    return CDH_SD_OpenNewFile();
}

/* Unmount, remount, restore high-speed clock, open a fresh segment.
 * A gap in filenames makes the reboot visible in post-flight analysis. */
static uint8_t CDH_SD_Remount(void)
{
    f_close(&s_log_file);
    f_mount(NULL, USERPath, 0);
    HAL_Delay(200);

    if (f_mount(&USERFatFS, USERPath, 1) != FR_OK) { return 0; }

    /* disk_initialize() runs SD_SPI_Init() at slow speed; bump back up */
    SD_SPI_SetHighSpeed();

    s_log_used      = 0;
    s_fault_strikes = 0;

    return CDH_SD_OpenNewFile();
}

/* ------------------------------------------------------------------ */
/*  CDH_Init                                                           */
/* ------------------------------------------------------------------ */
void CDH_Init(void)
{
    s_imu  = MPU6050_EquipmentHandler_Init(&hi2c1);
    s_baro = MS5607_EquipmentHandler_Init(&hi2c1);
    s_gps  = GPS_EquipmentHandler_Init(&hi2c1);
    CDH_FDIR_Init(&s_fdir);

    if (s_baro.baro_valid)
        s_ground_baro_alt_m = s_baro.data.altitude;

    Coral_Init();   /* UART5 already init by MX_UART5_Init() in main.c */

    /* ---- SD card + FatFS init ---- */
    MX_FATFS_Init();

    /* f_mount(opt=1) forces immediate disk_initialize() → SD_SPI_Init().
     * Do NOT call SD_SPI_Init() here — that would double-init the card. */
    if (f_mount(&USERFatFS, USERPath, 1) != FR_OK)
    {
        return;   /* s_sd_ready stays 0; sensor reads still run */
    }

    /* Card came up at /256 (312 kHz) init speed — switch to /8 (10 MHz) */
    SD_SPI_SetHighSpeed();

    /* Open first log segment for this boot */
    if (!CDH_SD_OpenNewFile())
    {
        f_mount(NULL, USERPath, 0);
        return;
    }

    s_sd_ready      = 1;
    s_fault_strikes = 0;
}

/* ------------------------------------------------------------------ */
/*  CDH_Update  (called at 1 Hz from the superloop)                   */
/* ------------------------------------------------------------------ */
void CDH_Update(SensorData_t *dp, SCV_t *scv)
{
    /* Capture scv pointer so fault handlers can update sd_fault_count */
    if (s_scv == NULL) { s_scv = scv; }

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
    if (s_gps.gps_valid)
    {
        dp->gps_lat_deg        = s_gps.data.latitude;
        dp->gps_lon_deg        = s_gps.data.longitude;
        dp->gps_alt_m          = s_gps.data.altitude;
        dp->gps_speed_mps      = s_gps.data.speed;
        dp->gps_vel_north_mps  = s_gps.data.vel_north;
        dp->gps_vel_east_mps   = s_gps.data.vel_east;
        dp->gps_vel_down_mps   = s_gps.data.vel_down;
        dp->gps_heading_deg    = s_gps.data.heading;
        dp->gps_num_satellites = s_gps.data.num_satellites;
        dp->gps_fix_type       = s_gps.data.fix_type;
        dp->gps_valid          = 1U;
    }
    else { dp->gps_valid = 0U; }

    CDH_Debug_PrintDatapool(dp);
    CDH_Debug_PrintBaro(&s_baro);

    /* CDH owns I2C bus: advance any pending FDIR restart */
    CDH_FDIR_BusRestart_Process(&s_fdir, &hi2c1);
    dp->i2c_bus_state = s_fdir.bus_state;

    /* TODO: read ADC for battery voltage — fill batt_voltage_mv, set batt_valid */
    Coral_Update(dp);
    /* TODO: write updated scv to NVM */

    /* ---- SD logging ---- */
    if (!s_sd_ready) { return; }

    /* Build a complete snapshot of SensorData_t + SCV_t */
    char row[512];
    snprintf(row, sizeof(row),
        /* timing */
        "%lu,%u,"
        /* GPS */
        "%.7f,%.7f,%.2f,%.2f,%.2f,%.2f,%.2f,%.1f,%u,%u,%u,"
        /* IMU */
        "%.4f,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f,%u,"
        /* Baro */
        "%.2f,%.2f,%.2f,%u,"
        /* bus + EPS + payload */
        "%u,%u,%u,%u,"
        /* SCV */
        "%lu,%lu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%ld"
        "\r\n",
        /* timing */
        (unsigned long)dp->timestamp_ms,
        (unsigned int)FSW_GetPhase(),
        /* GPS */
        dp->gps_lat_deg, dp->gps_lon_deg, dp->gps_alt_m,
        dp->gps_speed_mps, dp->gps_vel_north_mps, dp->gps_vel_east_mps, dp->gps_vel_down_mps,
        dp->gps_heading_deg,
        (unsigned)dp->gps_num_satellites, (unsigned)dp->gps_fix_type, (unsigned)dp->gps_valid,
        /* IMU */
        dp->imu_accel_x_g, dp->imu_accel_y_g, dp->imu_accel_z_g, dp->imu_accel_mag_g,
        dp->imu_gyro_x_dps, dp->imu_gyro_y_dps, dp->imu_gyro_z_dps, (unsigned)dp->imu_valid,
        /* Baro */
        dp->baro_pressure_pa, dp->baro_alt_m, dp->baro_temp_c, (unsigned)dp->baro_valid,
        /* bus + EPS + payload */
        (unsigned)dp->i2c_bus_state,
        (unsigned)dp->batt_voltage_mv, (unsigned)dp->batt_valid, (unsigned)dp->coral_valid,
        /* SCV — guarded in case pointer is still NULL on very first call */
        (unsigned long)(scv ? scv->boot_count           : 0UL),
        (unsigned long)(scv ? scv->mission_elapsed_ms   : 0UL),
        (unsigned)     (scv ? scv->reset_reason         : 0U),
        (unsigned)     (scv ? scv->equipment_enabled    : 0U),
        (unsigned)     (scv ? scv->equipment_faults     : 0U),
        (unsigned)     (scv ? scv->gps_timeout_count    : 0U),
        (unsigned)     (scv ? scv->imu_timeout_count    : 0U),
        (unsigned)     (scv ? scv->baro_timeout_count   : 0U),
        (unsigned)     (scv ? scv->coral_timeout_count  : 0U),
        (unsigned)     (scv ? scv->sd_fault_count       : 0U),
        (unsigned)     (scv ? scv->watchdog_reset_count : 0U),
        (unsigned)     (scv ? scv->last_batt_mv         : 0U),
        (long)         (scv ? scv->baro_ground_alt_cm   : 0L));

    if (sd_append(row) != FR_OK)
    {
        sd_record_fault();
        if (s_fault_strikes >= SD_FAULT_REMOUNT_STRIKES)
        {
            if (!CDH_SD_Remount()) { s_sd_ready = 0; }
        }
        return;
    }

    s_row_count++;

    if ((s_row_count % LOG_ROWS_PER_FILE) == 0)
    {
        /* Roll to a new file every ~5 minutes */
        if (!CDH_SD_RollFile())
        {
            sd_record_fault();
            if (!CDH_SD_Remount()) { s_sd_ready = 0; }
        }
    }
    else if ((s_row_count % LOG_SYNC_ROWS) == 0)
    {
        /* Mid-segment sync — don't also roll this tick */
        if (sd_flush()          != FR_OK) { sd_record_fault(); }
        if (f_sync(&s_log_file) != FR_OK) { sd_record_fault(); }

        if (s_fault_strikes >= SD_FAULT_REMOUNT_STRIKES)
        {
            if (!CDH_SD_Remount()) { s_sd_ready = 0; }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  CDH_RequestBusRestart                                              */
/* ------------------------------------------------------------------ */
void CDH_RequestBusRestart(void)
{
    CDH_FDIR_BusRestart_Start(&s_fdir);
}
