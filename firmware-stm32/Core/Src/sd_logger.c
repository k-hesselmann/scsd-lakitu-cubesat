#include "sd_logger.h"

#include "cdh/coral.h"
#include "debug_log.h"
#include "fatfs.h"
#include "fdir/fdir.h"
#include "sd_spi.h"
#include "user_diskio.h"

#include <stdio.h>
#include <string.h>

#define SD_LOG_NAME_SIZE  SD_LOGGER_OPEN_NAME_SIZE
#define SD_LOG_LINE_SIZE  768U
#define SD_LOG_BATCH_ROWS 50U
#define SD_LOG_BUFFER_SIZE (SD_LOG_LINE_SIZE * SD_LOG_BATCH_ROWS)
#define SD_LOG_FILES_PER_DIRECTORY 32U
#define SD_LOG_DIRECTORY_SIZE 64U
#define SD_LOG_ROOT_DIRECTORY "LOGS"

volatile SD_LoggerState_t sd_logger_state = SD_LOGGER_OFF;
volatile FRESULT sd_logger_last_error = FR_OK;
volatile uint32_t sd_logger_session = 0U;
volatile uint32_t sd_logger_rows_in_file = 0U;

static FIL s_file;
static uint8_t s_file_open;
static uint8_t s_fatfs_linked;
static uint8_t s_consecutive_faults;
static uint32_t s_file_start_s;
static uint32_t s_last_flush_ms;
static uint32_t s_last_success_ms;
static char s_open_name[SD_LOG_NAME_SIZE];
static char s_boot_directory[SD_LOG_DIRECTORY_SIZE];
static char s_file_directory[SD_LOG_DIRECTORY_SIZE];
static char s_log_buffer[SD_LOG_BUFFER_SIZE];
static size_t s_log_buffer_used;
static SD_LoggerOperation_t s_last_operation;
static SD_LoggerOperation_t s_last_fault_operation;
static FRESULT s_last_fault_result = FR_OK;
static uint32_t s_total_faults;
static uint32_t s_last_fault_ms;
static uint32_t s_total_bytes_committed;
static uint32_t s_successful_flushes;
static uint32_t s_last_flush_duration_ms;
static uint32_t s_max_flush_duration_ms;
static uint32_t s_recovery_attempts;
static uint32_t s_last_recovery_duration_ms;
static uint32_t s_rotation_count;
static uint32_t s_discarded_buffer_bytes;
static uint32_t s_last_requested_bytes;
static uint32_t s_last_written_bytes;

/* Rotation is due (SD_LOG_ROTATE_SECONDS elapsed) but deferred until either
 * Coral_IsQuiescent() or the SD_LOG_ROTATE_MAX_DEFER_MS bounded fallback --
 * see SD_Logger_Update(). */
static uint8_t  s_rotation_pending;
static uint32_t s_rotation_due_ms;

static FRESULT ensure_directory(const char *path)
{
    FRESULT result = f_mkdir(path);
    return (result == FR_EXIST) ? FR_OK : result;
}

static FRESULT ensure_boot_directory(void)
{
    FRESULT result = ensure_directory(SD_LOG_ROOT_DIRECTORY);
    if (result != FR_OK)
        return result;
    return ensure_directory(s_boot_directory);
}

static FRESULT select_file_directory(uint32_t session)
{
    const uint32_t batch = session / SD_LOG_FILES_PER_DIRECTORY;
    int length = snprintf(s_file_directory, sizeof(s_file_directory),
                          "%s/D%04lu", s_boot_directory,
                          (unsigned long)batch);
    if (length < 0 || (size_t)length >= sizeof(s_file_directory))
        return FR_INVALID_NAME;
    return ensure_directory(s_file_directory);
}

static const char s_csv_header[] =
    "session,record_timestamp_ms,"
    "gps_lat_e7,gps_lon_e7,gps_alt_cm,gps_speed_cms,gps_vel_down_cms,"
    "gps_heading_cdeg,gps_utc_time,gps_satellites,gps_fix_type,gps_valid,"
    "imu_accel_x_mg,imu_accel_y_mg,imu_accel_z_mg,imu_accel_mag_mg,"
    "imu_gyro_x_mdps,imu_gyro_y_mdps,imu_gyro_z_mdps,imu_valid,"
    "baro_pressure_pa,baro_alt_cm,baro_temp_centi_c,baro_valid,i2c_bus_state,"
    "batt_voltage_mv,batt_valid,coral_block_hex,coral_valid,"
    "scv_magic,scv_boot_count,scv_mission_elapsed_ms,scv_flight_phase,scv_reset_reason,"
    "scv_equipment_enabled,scv_equipment_faults,scv_gps_timeout_count,scv_imu_timeout_count,"
    "scv_baro_timeout_count,scv_coral_timeout_count,scv_lora_timeout_count,"
    "scv_lora_tx_fault_counter,scv_sd_fault_count,"
    "scv_watchdog_reset_count,scv_last_batt_mv,scv_baro_ground_alt_cm,scv_crc16\r\n";

const char *SD_Logger_StateName(SD_LoggerState_t state)
{
    switch (state)
    {
        case SD_LOGGER_OFF: return "OFF";
        case SD_LOGGER_ACTIVE: return "ACTIVE";
        case SD_LOGGER_WAITING_RETRY: return "WAIT_RETRY";
        default: return "UNKNOWN";
    }
}

const char *SD_Logger_OperationName(SD_LoggerOperation_t operation)
{
    switch (operation)
    {
        case SD_LOG_OP_NONE: return "NONE";
        case SD_LOG_OP_CARD_INIT: return "CARD_INIT";
        case SD_LOG_OP_MOUNT: return "MOUNT";
        case SD_LOG_OP_OPEN: return "OPEN";
        case SD_LOG_OP_HEADER_WRITE: return "HEADER_WRITE";
        case SD_LOG_OP_DATA_WRITE: return "DATA_WRITE";
        case SD_LOG_OP_SYNC: return "SYNC";
        case SD_LOG_OP_CLOSE: return "CLOSE";
        case SD_LOG_OP_FORMAT_ROW: return "FORMAT_ROW";
        default: return "UNKNOWN";
    }
}

const char *SD_Logger_ResultName(FRESULT result)
{
    switch (result)
    {
        case FR_OK: return "FR_OK";
        case FR_DISK_ERR: return "FR_DISK_ERR";
        case FR_INT_ERR: return "FR_INT_ERR";
        case FR_NOT_READY: return "FR_NOT_READY";
        case FR_NO_FILE: return "FR_NO_FILE";
        case FR_NO_PATH: return "FR_NO_PATH";
        case FR_INVALID_NAME: return "FR_INVALID_NAME";
        case FR_DENIED: return "FR_DENIED";
        case FR_EXIST: return "FR_EXIST";
        case FR_INVALID_OBJECT: return "FR_INVALID_OBJECT";
        case FR_WRITE_PROTECTED: return "FR_WRITE_PROTECTED";
        case FR_INVALID_DRIVE: return "FR_INVALID_DRIVE";
        case FR_NOT_ENABLED: return "FR_NOT_ENABLED";
        case FR_NO_FILESYSTEM: return "FR_NO_FILESYSTEM";
        case FR_MKFS_ABORTED: return "FR_MKFS_ABORTED";
        case FR_TIMEOUT: return "FR_TIMEOUT";
        case FR_LOCKED: return "FR_LOCKED";
        case FR_NOT_ENOUGH_CORE: return "FR_NOT_ENOUGH_CORE";
        case FR_TOO_MANY_OPEN_FILES: return "FR_TOO_MANY_OPEN_FILES";
        case FR_INVALID_PARAMETER: return "FR_INVALID_PARAMETER";
        default: return "FR_UNKNOWN";
    }
}

static int32_t scale_float(float value, float scale)
{
    float scaled = value * scale;
    return (int32_t)(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
}

static void sd_note_successful_sync(void)
{
    s_consecutive_faults = 0U;
    s_last_success_ms = HAL_GetTick();
}

static void sd_drop_filesystem_state(void)
{
    if (s_file_open)
    {
        (void)f_close(&s_file);
        s_file_open = 0U;
    }
    if (s_log_buffer_used > 0U)
        s_discarded_buffer_bytes += (uint32_t)s_log_buffer_used;
    s_log_buffer_used = 0U;
    (void)f_mount(NULL, USERPath, 0U);
    USER_force_reinitialize();
}

static void sd_record_fault(FRESULT result)
{
    SD_SPIDiagnostics_t spi = {0};
    uint32_t buffered_bytes = (uint32_t)s_log_buffer_used;
    char line[384];
    int length;

    SD_SPI_GetDiagnostics(&spi);
    s_last_fault_operation =
        (s_last_operation == SD_LOG_OP_MOUNT && !spi.initialized) ?
        SD_LOG_OP_CARD_INIT : s_last_operation;
    s_last_fault_result = result;
    s_last_fault_ms = HAL_GetTick();
    s_total_faults++;
    sd_logger_last_error = result;
    sd_logger_state = SD_LOGGER_WAITING_RETRY;

    if (s_consecutive_faults < UINT8_MAX)
        s_consecutive_faults++;

    sd_drop_filesystem_state();

    length = snprintf(
        line, sizeof(line),
        "[SD_FAULT] op=%s fatfs=%s(%u) req=%lu wrote=%lu buf_drop=%lu consecutive=%u total=%lu spi_err=%lu hal_err=0x%08lX hal_state=%lu cmd=%u r1=0x%02X rx=0x%02X sector=%lu count=%lu\r\n",
        SD_Logger_OperationName(s_last_fault_operation),
        SD_Logger_ResultName(result),
        (unsigned int)result,
        (unsigned long)s_last_requested_bytes,
        (unsigned long)s_last_written_bytes,
        (unsigned long)buffered_bytes,
        (unsigned int)s_consecutive_faults,
        (unsigned long)s_total_faults,
        (unsigned long)spi.spi_error_count,
        (unsigned long)spi.hal_error_last,
        (unsigned long)spi.hal_state_last,
        (unsigned int)spi.last_command,
        (unsigned int)spi.last_response,
        (unsigned int)spi.last_rx,
        (unsigned long)spi.last_sector,
        (unsigned long)spi.last_sector_count);
    if (length > 0)
    {
        if ((size_t)length >= sizeof(line))
            length = (int)(sizeof(line) - 1U);
        DebugLog_WriteN(line, length);
    }
}

static void sd_set_healthy(void)
{
    sd_logger_last_error = FR_OK;
    sd_logger_state = SD_LOGGER_ACTIVE;
}

static uint32_t find_next_session(void)
{
    DIR boot_directory;
    DIR file_directory;
    FILINFO info;
    uint32_t maximum = 0U;

    if (f_opendir(&boot_directory, s_boot_directory) == FR_OK)
    {
        for (;;)
        {
            unsigned long batch = 0UL;
            if (f_readdir(&boot_directory, &info) != FR_OK || info.fname[0] == '\0')
                break;

            if ((info.fattrib & AM_DIR) != 0U &&
                sscanf(info.fname, "D%lu", &batch) == 1)
            {
                char directory_name[SD_LOG_DIRECTORY_SIZE];
                int length = snprintf(directory_name, sizeof(directory_name),
                                      "%s/%s", s_boot_directory, info.fname);
                if (length < 0 || (size_t)length >= sizeof(directory_name) ||
                    f_opendir(&file_directory, directory_name) != FR_OK)
                    continue;

                for (;;)
                {
                    unsigned long value = 0UL;
                    if (f_readdir(&file_directory, &info) != FR_OK ||
                        info.fname[0] == '\0')
                        break;
                    if (sscanf(info.fname, "LOG_%lu_", &value) == 1 &&
                        value > maximum)
                        maximum = (uint32_t)value;
                }
                (void)f_closedir(&file_directory);
            }
        }
        (void)f_closedir(&boot_directory);
    }

    return (maximum == UINT32_MAX) ? 1U : maximum + 1U;
}

static FRESULT write_all(const void *data, UINT length)
{
    UINT written = 0U;
    FRESULT result = f_write(&s_file, data, length, &written);

    s_last_requested_bytes = length;
    s_last_written_bytes = written;
    if (result != FR_OK)
        return result;
    return (written == length) ? FR_OK : FR_DISK_ERR;
}

static FRESULT flush_buffer(void)
{
    FRESULT result;
    uint32_t started_ms;
    uint32_t duration_ms;
    uint32_t flush_bytes;

    if (s_log_buffer_used == 0U)
        return FR_OK;

    started_ms = HAL_GetTick();
    flush_bytes = (uint32_t)s_log_buffer_used;
    s_last_operation = SD_LOG_OP_DATA_WRITE;
    result = write_all(s_log_buffer, (UINT)s_log_buffer_used);
    if (result == FR_OK)
    {
        s_last_operation = SD_LOG_OP_SYNC;
        result = f_sync(&s_file);
    }
    duration_ms = (uint32_t)(HAL_GetTick() - started_ms);
    s_last_flush_duration_ms = duration_ms;
    if (duration_ms > s_max_flush_duration_ms)
        s_max_flush_duration_ms = duration_ms;
    if (result == FR_OK)
    {
        s_log_buffer_used = 0U;
        s_last_flush_ms = HAL_GetTick();
        s_total_bytes_committed += flush_bytes;
        s_successful_flushes++;
        sd_note_successful_sync();
    }
    return result;
}

static FRESULT open_new_file(void)
{
    FRESULT result;
    uint32_t attempts = 0U;

    s_file_start_s = HAL_GetTick() / 1000U;
    s_last_flush_ms = HAL_GetTick();
    s_log_buffer_used = 0U;
    sd_logger_rows_in_file = 0U;
    s_rotation_pending = 0U;   /* this file is fresh; any prior pending rotation is moot */

    do
    {
        int length;
        result = select_file_directory(sd_logger_session);
        if (result != FR_OK)
            break;
        length = snprintf(s_open_name, sizeof(s_open_name),
                          "%s/LOG_%06lu_START_%010lu.CSV",
                          s_file_directory,
                          (unsigned long)sd_logger_session,
                          (unsigned long)s_file_start_s);
        if (length < 0 || (size_t)length >= sizeof(s_open_name))
        {
            result = FR_INVALID_NAME;
            break;
        }
        s_last_operation = SD_LOG_OP_OPEN;
        result = f_open(&s_file, s_open_name, FA_CREATE_NEW | FA_WRITE);
        if (result == FR_EXIST)
            sd_logger_session++;
        attempts++;
    } while (result == FR_EXIST && attempts < 1000U);

    if (result != FR_OK)
        return result;

    s_file_open = 1U;
    s_last_operation = SD_LOG_OP_HEADER_WRITE;
    result = write_all(s_csv_header, (UINT)(sizeof(s_csv_header) - 1U));
    if (result == FR_OK)
    {
        s_last_operation = SD_LOG_OP_SYNC;
        result = f_sync(&s_file);
    }
    if (result == FR_OK)
    {
        s_total_bytes_committed += (uint32_t)(sizeof(s_csv_header) - 1U);
        sd_note_successful_sync();
    }
    return result;
}

static FRESULT close_file(void)
{
    FRESULT result;

    if (!s_file_open)
        return FR_OK;

    result = flush_buffer();
    if (result == FR_OK)
    {
        s_last_operation = SD_LOG_OP_SYNC;
        result = f_sync(&s_file);
    }
    if (result == FR_OK)
    {
        s_last_operation = SD_LOG_OP_CLOSE;
        result = f_close(&s_file);
    }
    if (result != FR_OK)
        return result;
    s_file_open = 0U;
    return FR_OK;
}

static FRESULT mount_and_open(uint8_t discover_session)
{
    FRESULT result;

    s_last_operation = SD_LOG_OP_MOUNT;
    result = f_mount(&USERFatFS, USERPath, 1U);
    if (result != FR_OK)
        return result;

    result = ensure_boot_directory();
    if (result != FR_OK)
        return result;

    if (discover_session)
        sd_logger_session = find_next_session();
    result = open_new_file();
    if (result == FR_OK)
        sd_set_healthy();
    return result;
}

void SD_Logger_Init(SCV_t *scv)
{
    SD_SPIDiagnostics_t spi = {0};
    FRESULT result;
    uint32_t started_ms = HAL_GetTick();
    char line[320];
    int length;

    DebugLog_Write("[SD_INIT] phase=start flush_ms=5000 rotate_s=60 batch_rows=50\r\n");

    if (!s_fatfs_linked)
    {
        MX_FATFS_Init();
        s_fatfs_linked = 1U;
    }

    {
        uint32_t boot_count = (scv != NULL) ? scv->boot_count : 0U;
        int directory_length = snprintf(s_boot_directory,
                                        sizeof(s_boot_directory),
                                        "%s/B%08lu",
                                        SD_LOG_ROOT_DIRECTORY,
                                        (unsigned long)boot_count);
        if (directory_length < 0 ||
            (size_t)directory_length >= sizeof(s_boot_directory))
        {
            sd_record_fault(FR_INVALID_NAME);
            return;
        }
    }

    result = mount_and_open(1U);
    if (result != FR_OK)
        sd_record_fault(result);

    SD_SPI_GetDiagnostics(&spi);
    if (result == FR_OK && spi.sector_count == 0U)
    {
        uint32_t sectors;
        (void)SD_SPI_GetSectorCount(&sectors);
        SD_SPI_GetDiagnostics(&spi);
    }
    length = snprintf(
        line, sizeof(line),
        "[SD_INIT] result=%s fatfs=%s(%u) op=%s duration=%lums card=%s sectors=%lu capacity_mib=%lu session=%lu file=%s\r\n",
        (result == FR_OK) ? "OK" : "FAIL",
        SD_Logger_ResultName(result),
        (unsigned int)result,
        SD_Logger_OperationName((result == FR_OK) ? s_last_operation :
                               s_last_fault_operation),
        (unsigned long)(HAL_GetTick() - started_ms),
        SD_SPI_GetCardTypeName(),
        (unsigned long)spi.sector_count,
        (unsigned long)(spi.sector_count / 2048U),
        (unsigned long)sd_logger_session,
        s_file_open ? s_open_name : "NONE");
    if (length > 0)
    {
        if ((size_t)length >= sizeof(line))
            length = (int)(sizeof(line) - 1U);
        DebugLog_WriteN(line, length);
    }
}

void SD_Logger_Update(const SensorData_t *dp, SCV_t *scv)
{
    static char coral_hex[33];
    static char line[SD_LOG_LINE_SIZE];
    int length;
    FRESULT result;
    uint32_t now_ms = HAL_GetTick();

    if (dp == NULL || scv == NULL)
        return;

    if ((FDIR_GetReinitRequests() & EQUIPMENT_SD) != 0U)
    {
        FRESULT previous_error = sd_logger_last_error;
        SD_LoggerOperation_t previous_operation = s_last_fault_operation;
        uint32_t recovery_started_ms = HAL_GetTick();
        char recovery_line[320];
        int recovery_length;

        /* SD recovery can mount the card, create directories and allocate a
         * new file. Those operations may block long enough to overrun the
         * Coral UART ring, so leave FDIR's request pending until the same
         * quiet window used for CSV rotation is available. */
        if (!Coral_IsQuiescent())
            return;

        s_recovery_attempts++;
        recovery_length = snprintf(
            recovery_line, sizeof(recovery_line),
            "[SD_RECOVERY] attempt=%lu phase=start previous_op=%s previous=%s(%u)\r\n",
            (unsigned long)s_recovery_attempts,
            SD_Logger_OperationName(previous_operation),
            SD_Logger_ResultName(previous_error),
            (unsigned int)previous_error);
        if (recovery_length > 0)
        {
            if ((size_t)recovery_length >= sizeof(recovery_line))
                recovery_length = (int)(sizeof(recovery_line) - 1U);
            DebugLog_WriteN(recovery_line, recovery_length);
        }

        sd_drop_filesystem_state();
        /* Keep the in-RAM session and let FA_CREATE_NEW probe forward. A full
         * directory rescan here would block the live Coral receive path. */
        result = mount_and_open(0U);
        if (result != FR_OK)
            sd_record_fault(result);
        s_last_recovery_duration_ms = (uint32_t)(HAL_GetTick() - recovery_started_ms);

        recovery_length = snprintf(
            recovery_line, sizeof(recovery_line),
            "[SD_RECOVERY] attempt=%lu result=%s fatfs=%s(%u) op=%s duration=%lums session=%lu file=%s\r\n",
            (unsigned long)s_recovery_attempts,
            (result == FR_OK) ? "OK" : "FAIL",
            SD_Logger_ResultName(result),
            (unsigned int)result,
            SD_Logger_OperationName((result == FR_OK) ? s_last_operation :
                                   s_last_fault_operation),
            (unsigned long)s_last_recovery_duration_ms,
            (unsigned long)sd_logger_session,
            s_file_open ? s_open_name : "NONE");
        if (recovery_length > 0)
        {
            if ((size_t)recovery_length >= sizeof(recovery_line))
                recovery_length = (int)(sizeof(recovery_line) - 1U);
            DebugLog_WriteN(recovery_line, recovery_length);
        }
        FDIR_AcknowledgeReinit(EQUIPMENT_SD);
    }

    if (sd_logger_state != SD_LOGGER_ACTIVE)
        return;

    /* Rotation (close/open) can still contain slow FatFs calls
     * on this hardware -- doing it unconditionally here can stall the loop
     * long enough to overflow Coral's RX ring mid-transfer (same class of
     * bug as the per-frame f_open() this module's Coral side already works
     * around). Defer to Coral_IsQuiescent(), with a bounded fallback so a
     * disconnected/faulted Coral -- which never goes idle in the way this
     * checks for -- can't defer rotation forever. */
    if (!s_rotation_pending &&
        (now_ms / 1000U) - s_file_start_s >= SD_LOG_ROTATE_SECONDS)
    {
        s_rotation_pending = 1U;
        s_rotation_due_ms  = now_ms;
    }

    if (s_rotation_pending &&
        (Coral_IsQuiescent() ||
         (uint32_t)(now_ms - s_rotation_due_ms) >= SD_LOG_ROTATE_MAX_DEFER_MS))
    {
        s_rotation_pending = 0U;

        result = close_file();
        if (result != FR_OK)
        {
            sd_record_fault(result);
            return;
        }
        sd_logger_session++;
        result = open_new_file();
        if (result != FR_OK)
        {
            sd_record_fault(result);
            return;
        }
        s_rotation_count++;
    }

    for (uint32_t i = 0U; i < sizeof(dp->coral_block); i++)
        (void)snprintf(&coral_hex[i * 2U], 3U, "%02X", dp->coral_block[i]);
    coral_hex[32] = '\0';

    length = snprintf(
        line, sizeof(line),
        "%lu,%lu,"
        "%ld,%ld,%ld,%ld,%ld,%ld,%lu,%u,%u,%u,"
        "%ld,%ld,%ld,%ld,%ld,%ld,%ld,%u,"
        "%ld,%ld,%ld,%u,%u,%u,%u,%s,%u,"
        "%u,%lu,%lu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%ld,%u\r\n",
        (unsigned long)sd_logger_session,
        (unsigned long)dp->timestamp_ms,
        (long)scale_float(dp->gps_lat_deg, 10000000.0f),
        (long)scale_float(dp->gps_lon_deg, 10000000.0f),
        (long)scale_float(dp->gps_alt_m, 100.0f),
        (long)scale_float(dp->gps_speed_mps, 100.0f),
        (long)scale_float(dp->gps_vel_down_mps, 100.0f),
        (long)scale_float(dp->gps_heading_deg, 100.0f),
        (unsigned long)dp->gps_utc_time,
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
        (unsigned int)scv->lora_timeout_count,
        (unsigned int)scv->lora_tx_fault_counter,
        (unsigned int)scv->sd_fault_count,
        (unsigned int)scv->watchdog_reset_count,
        (unsigned int)scv->last_batt_mv,
        (long)scv->baro_ground_alt_cm,
        (unsigned int)scv->crc16);

    if (length <= 0 || (size_t)length >= sizeof(line))
    {
        s_last_operation = SD_LOG_OP_FORMAT_ROW;
        sd_record_fault(FR_INVALID_PARAMETER);
        return;
    }

    if (s_log_buffer_used + (size_t)length > sizeof(s_log_buffer))
    {
        result = flush_buffer();
        if (result != FR_OK)
        {
            sd_record_fault(result);
            return;
        }
    }

    memcpy(&s_log_buffer[s_log_buffer_used], line, (size_t)length);
    s_log_buffer_used += (size_t)length;
    sd_logger_rows_in_file++;

    if ((uint32_t)(now_ms - s_last_flush_ms) >= SD_LOG_FLUSH_PERIOD_MS)
    {
        result = flush_buffer();
        if (result != FR_OK)
        {
            sd_record_fault(result);
            return;
        }
    }
}

void SD_Logger_Close(void)
{
    FRESULT result = close_file();
    if (result != FR_OK)
        sd_record_fault(result);
    (void)f_mount(NULL, USERPath, 0U);
    sd_logger_state = SD_LOGGER_OFF;
}

void SD_Logger_GetHealth(SD_LoggerHealth_t *health)
{
    if (health == NULL)
        return;

    health->state = sd_logger_state;
    health->last_error = sd_logger_last_error;
    health->consecutive_faults = s_consecutive_faults;
    health->last_success_ms = s_last_success_ms;
    health->file_open = s_file_open;
}

void SD_Logger_GetDiagnostics(SD_LoggerDiagnostics_t *diagnostics)
{
    if (diagnostics == NULL)
        return;

    diagnostics->state = sd_logger_state;
    diagnostics->last_error = sd_logger_last_error;
    diagnostics->last_operation = s_last_operation;
    diagnostics->last_fault_operation = s_last_fault_operation;
    diagnostics->last_fault_result = s_last_fault_result;
    diagnostics->consecutive_faults = s_consecutive_faults;
    diagnostics->file_open = s_file_open;
    diagnostics->total_faults = s_total_faults;
    diagnostics->last_fault_ms = s_last_fault_ms;
    diagnostics->last_success_ms = s_last_success_ms;
    diagnostics->session = sd_logger_session;
    diagnostics->rows_in_file = sd_logger_rows_in_file;
    diagnostics->buffer_used = (uint32_t)s_log_buffer_used;
    diagnostics->buffer_capacity = SD_LOG_BUFFER_SIZE;
    diagnostics->total_bytes_committed = s_total_bytes_committed;
    diagnostics->successful_flushes = s_successful_flushes;
    diagnostics->last_flush_duration_ms = s_last_flush_duration_ms;
    diagnostics->max_flush_duration_ms = s_max_flush_duration_ms;
    diagnostics->recovery_attempts = s_recovery_attempts;
    diagnostics->last_recovery_duration_ms = s_last_recovery_duration_ms;
    diagnostics->rotation_count = s_rotation_count;
    diagnostics->discarded_buffer_bytes = s_discarded_buffer_bytes;
    diagnostics->last_requested_bytes = s_last_requested_bytes;
    diagnostics->last_written_bytes = s_last_written_bytes;
    (void)strncpy(diagnostics->open_name, s_open_name,
                  sizeof(diagnostics->open_name) - 1U);
    diagnostics->open_name[sizeof(diagnostics->open_name) - 1U] = '\0';
}
