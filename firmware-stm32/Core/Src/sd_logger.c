#include "sd_logger.h"

#include "fatfs.h"
#include "user_diskio.h"

#include <stdio.h>
#include <string.h>

#define SD_LOG_NAME_SIZE  96U
#define SD_LOG_LINE_SIZE  768U

volatile SD_LoggerState_t sd_logger_state = SD_LOGGER_OFF;
volatile FRESULT sd_logger_last_error = FR_OK;
volatile uint32_t sd_logger_session = 0U;
volatile uint32_t sd_logger_rows_in_file = 0U;

static FIL s_file;
static uint8_t s_file_open;
static uint8_t s_fatfs_linked;
static uint8_t s_consecutive_faults;
static uint32_t s_file_start_s;
static uint32_t s_retry_at_ms;
static char s_open_name[SD_LOG_NAME_SIZE];

static const char s_csv_header[] =
    "session,record_timestamp_ms,"
    "gps_lat_e7,gps_lon_e7,gps_alt_cm,gps_speed_cms,gps_vel_north_cms,"
    "gps_vel_east_cms,gps_vel_down_cms,gps_heading_cdeg,gps_satellites,gps_fix_type,gps_valid,"
    "imu_accel_x_mg,imu_accel_y_mg,imu_accel_z_mg,imu_accel_mag_mg,"
    "imu_gyro_x_mdps,imu_gyro_y_mdps,imu_gyro_z_mdps,imu_valid,"
    "baro_pressure_pa,baro_alt_cm,baro_temp_centi_c,baro_valid,i2c_bus_state,"
    "batt_voltage_mv,batt_valid,coral_block_hex,coral_valid,"
    "scv_magic,scv_boot_count,scv_mission_elapsed_ms,scv_flight_phase,scv_reset_reason,"
    "scv_equipment_enabled,scv_equipment_faults,scv_gps_timeout_count,scv_imu_timeout_count,"
    "scv_baro_timeout_count,scv_coral_timeout_count,scv_sd_fault_count,"
    "scv_watchdog_reset_count,scv_last_batt_mv,scv_baro_ground_alt_cm,scv_crc16\r\n";

static int32_t scale_float(float value, float scale)
{
    float scaled = value * scale;
    return (int32_t)(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
}

static void sd_set_fault(SCV_t *scv, FRESULT result)
{
    sd_logger_last_error = result;
    sd_logger_state = SD_LOGGER_WAITING_RETRY;
    s_retry_at_ms = HAL_GetTick() + SD_LOG_RETRY_DELAY_MS;

    if (s_consecutive_faults < UINT8_MAX)
        s_consecutive_faults++;
    if (scv != NULL && scv->sd_fault_count < UINT8_MAX)
        scv->sd_fault_count++;
    if (scv != NULL)
        scv->equipment_faults |= EQUIPMENT_SD;

    if (s_file_open)
    {
        (void)f_close(&s_file);
        s_file_open = 0U;
    }
    (void)f_mount(NULL, USERPath, 0U);
}

static void sd_set_healthy(SCV_t *scv)
{
    sd_logger_last_error = FR_OK;
    sd_logger_state = SD_LOGGER_ACTIVE;
    if (scv != NULL)
        scv->equipment_faults &= (uint16_t)~EQUIPMENT_SD;
}

static uint32_t find_next_session(void)
{
    DIR directory;
    FILINFO info;
    uint32_t maximum = 0U;

    if (f_opendir(&directory, USERPath) == FR_OK)
    {
        for (;;)
        {
            unsigned long value = 0UL;
            if (f_readdir(&directory, &info) != FR_OK || info.fname[0] == '\0')
                break;
            if (sscanf(info.fname, "LOG_%lu_", &value) == 1 && value > maximum)
                maximum = (uint32_t)value;
        }
        (void)f_closedir(&directory);
    }

    return (maximum == UINT32_MAX) ? 1U : maximum + 1U;
}

static FRESULT write_all(const void *data, UINT length)
{
    UINT written = 0U;
    FRESULT result = f_write(&s_file, data, length, &written);
    return (result == FR_OK && written == length) ? FR_OK : FR_DISK_ERR;
}

static FRESULT open_new_file(void)
{
    FRESULT result;
    uint32_t attempts = 0U;

    s_file_start_s = HAL_GetTick() / 1000U;
    sd_logger_rows_in_file = 0U;

    do
    {
        (void)snprintf(s_open_name, sizeof(s_open_name),
                       "LOG_%06lu_START_%010lu_OPEN.CSV",
                       (unsigned long)sd_logger_session,
                       (unsigned long)s_file_start_s);
        result = f_open(&s_file, s_open_name, FA_CREATE_NEW | FA_WRITE);
        if (result == FR_EXIST)
            sd_logger_session++;
        attempts++;
    } while (result == FR_EXIST && attempts < 1000U);

    if (result != FR_OK)
        return result;

    s_file_open = 1U;
    result = write_all(s_csv_header, (UINT)(sizeof(s_csv_header) - 1U));
    if (result == FR_OK)
        result = f_sync(&s_file);
    return result;
}

static FRESULT close_and_name_file(void)
{
    char final_name[SD_LOG_NAME_SIZE];
    uint32_t end_s = HAL_GetTick() / 1000U;
    uint32_t duration_s = end_s - s_file_start_s;
    FRESULT result;

    if (!s_file_open)
        return FR_OK;

    result = f_sync(&s_file);
    if (result == FR_OK)
        result = f_close(&s_file);
    s_file_open = 0U;
    if (result != FR_OK)
        return result;

    (void)snprintf(final_name, sizeof(final_name),
                   "LOG_%06lu_START_%010lu_END_%010lu_DUR_%010lu.CSV",
                   (unsigned long)sd_logger_session,
                   (unsigned long)s_file_start_s,
                   (unsigned long)end_s,
                   (unsigned long)duration_s);
    return f_rename(s_open_name, final_name);
}

static FRESULT mount_and_open(SCV_t *scv)
{
    FRESULT result;

    if (s_consecutive_faults >= SD_LOG_REMOUNT_THRESHOLD)
        USER_force_reinitialize();

    result = f_mount(&USERFatFS, USERPath, 1U);
    if (result != FR_OK)
        return result;

    sd_logger_session = find_next_session();
    result = open_new_file();
    if (result == FR_OK)
        sd_set_healthy(scv);
    return result;
}

void SD_Logger_Init(SCV_t *scv)
{
    FRESULT result;

    if (!s_fatfs_linked)
    {
        MX_FATFS_Init();
        s_fatfs_linked = 1U;
    }

    result = mount_and_open(scv);
    if (result != FR_OK)
        sd_set_fault(scv, result);
}

void SD_Logger_Update(const SensorData_t *dp, SCV_t *scv)
{
    char coral_hex[33];
    char line[SD_LOG_LINE_SIZE];
    int length;
    FRESULT result;
    uint32_t now_ms = HAL_GetTick();

    if (dp == NULL || scv == NULL)
        return;

    if (sd_logger_state != SD_LOGGER_ACTIVE)
    {
        if ((int32_t)(now_ms - s_retry_at_ms) >= 0)
        {
            result = mount_and_open(scv);
            if (result != FR_OK)
                sd_set_fault(scv, result);
        }
        return;
    }

    if ((now_ms / 1000U) - s_file_start_s >= SD_LOG_ROTATE_SECONDS)
    {
        result = close_and_name_file();
        if (result != FR_OK)
        {
            sd_set_fault(scv, result);
            return;
        }
        sd_logger_session++;
        result = open_new_file();
        if (result != FR_OK)
        {
            sd_set_fault(scv, result);
            return;
        }
    }

    for (uint32_t i = 0U; i < sizeof(dp->coral_block); i++)
        (void)snprintf(&coral_hex[i * 2U], 3U, "%02X", dp->coral_block[i]);
    coral_hex[32] = '\0';

    length = snprintf(
        line, sizeof(line),
        "%lu,%lu,"
        "%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%u,%u,%u,"
        "%ld,%ld,%ld,%ld,%ld,%ld,%ld,%u,"
        "%ld,%ld,%ld,%u,%u,%u,%u,%s,%u,"
        "%u,%lu,%lu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%ld,%u\r\n",
        (unsigned long)sd_logger_session,
        (unsigned long)dp->timestamp_ms,
        (long)scale_float(dp->gps_lat_deg, 10000000.0f),
        (long)scale_float(dp->gps_lon_deg, 10000000.0f),
        (long)scale_float(dp->gps_alt_m, 100.0f),
        (long)scale_float(dp->gps_speed_mps, 100.0f),
        (long)scale_float(dp->gps_vel_north_mps, 100.0f),
        (long)scale_float(dp->gps_vel_east_mps, 100.0f),
        (long)scale_float(dp->gps_vel_down_mps, 100.0f),
        (long)scale_float(dp->gps_heading_deg, 100.0f),
        (unsigned int)dp->gps_num_satellites,
        (unsigned int)dp->gps_fix_type,
        (unsigned int)dp->gps_valid,
        (long)scale_float(dp->imu_accel_x_g, 1000.0f),
        (long)scale_float(dp->imu_accel_y_g, 1000.0f),
        (long)scale_float(dp->imu_accel_z_g, 1000.0f),
        (long)scale_float(dp->imu_accel_mag_g, 1000.0f),
        (long)scale_float(dp->imu_gyro_x_dps, 1000.0f),
        (long)scale_float(dp->imu_gyro_y_dps, 1000.0f),
        (long)scale_float(dp->imu_gyro_z_dps, 1000.0f),
        (unsigned int)dp->imu_valid,
        (long)scale_float(dp->baro_pressure_pa, 1.0f),
        (long)scale_float(dp->baro_alt_m, 100.0f),
        (long)scale_float(dp->baro_temp_c, 100.0f),
        (unsigned int)dp->baro_valid,
        (unsigned int)dp->i2c_bus_state,
        (unsigned int)dp->batt_voltage_mv,
        (unsigned int)dp->batt_valid,
        coral_hex,
        (unsigned int)dp->coral_valid,
        (unsigned int)scv->magic,
        (unsigned long)scv->boot_count,
        (unsigned long)scv->mission_elapsed_ms,
        (unsigned int)scv->flight_phase,
        (unsigned int)scv->reset_reason,
        (unsigned int)scv->equipment_enabled,
        (unsigned int)scv->equipment_faults,
        (unsigned int)scv->gps_timeout_count,
        (unsigned int)scv->imu_timeout_count,
        (unsigned int)scv->baro_timeout_count,
        (unsigned int)scv->coral_timeout_count,
        (unsigned int)scv->sd_fault_count,
        (unsigned int)scv->watchdog_reset_count,
        (unsigned int)scv->last_batt_mv,
        (long)scv->baro_ground_alt_cm,
        (unsigned int)scv->crc16);

    if (length <= 0 || (size_t)length >= sizeof(line))
    {
        sd_set_fault(scv, FR_INVALID_PARAMETER);
        return;
    }

    result = write_all(line, (UINT)length);
    if (result != FR_OK)
    {
        sd_set_fault(scv, result);
        return;
    }

    sd_logger_rows_in_file++;
    if ((sd_logger_rows_in_file % SD_LOG_SYNC_ROWS) == 0U)
    {
        result = f_sync(&s_file);
        if (result != FR_OK)
        {
            sd_set_fault(scv, result);
            return;
        }
    }

    /* Only a completed write/sync proves the recovered path is healthy. */
    s_consecutive_faults = 0U;
}

void SD_Logger_Close(void)
{
    (void)close_and_name_file();
    (void)f_mount(NULL, USERPath, 0U);
    sd_logger_state = SD_LOGGER_OFF;
}
