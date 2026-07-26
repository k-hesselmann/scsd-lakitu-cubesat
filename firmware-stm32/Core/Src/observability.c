#include "observability.h"

#include "cdh/coral.h"
#include "cdh/m10s.h"
#include "debug_log.h"
#include "sd_logger.h"
#include "sd_spi.h"

#include <stdio.h>

#define OBSERVABILITY_INTERVAL_MS 10000U
#define OBSERVABILITY_AGE_NEVER   UINT32_MAX

static uint32_t s_last_loop_ms;
static uint32_t s_last_report_ms;
static uint32_t s_max_loop_gap_ms;
static uint32_t s_previous_coral_overflow;

static uint32_t Observability_AgeMs(uint32_t now_ms, uint32_t timestamp_ms)
{
    return (timestamp_ms == 0U) ? OBSERVABILITY_AGE_NEVER :
                                 (uint32_t)(now_ms - timestamp_ms);
}

void Observability_Init(void)
{
    s_last_loop_ms = 0U;
    s_last_report_ms = HAL_GetTick();
    s_max_loop_gap_ms = 0U;
    s_previous_coral_overflow = 0U;
    DebugLog_Write("[OBS] enabled stats_ms=10000 age_ms=4294967295 means never-seen\r\n");
}

void Observability_Update(uint32_t now_ms, const SensorData_t *datapool)
{
    DebugLogStats_t debug = {0};
    CoralDiagnostics_t coral = {0};
    M10S_Diagnostics_t gps = {0};
    SD_LoggerDiagnostics_t sd = {0};
    SD_SPIDiagnostics_t sd_spi = {0};
    uint32_t loop_gap_ms;
    uint32_t window_ms;
    uint32_t overflow_delta;
    char line[512];
    char sd_line[384];
    char sd_spi_line[384];
    int length;

    if (s_last_loop_ms != 0U)
    {
        loop_gap_ms = (uint32_t)(now_ms - s_last_loop_ms);
        if (loop_gap_ms > s_max_loop_gap_ms)
            s_max_loop_gap_ms = loop_gap_ms;
    }
    s_last_loop_ms = now_ms;

    window_ms = (uint32_t)(now_ms - s_last_report_ms);
    if (window_ms < OBSERVABILITY_INTERVAL_MS)
        return;

    DebugLog_GetStats(&debug);
    Coral_GetDiagnostics(&coral);
    M10S_GetDiagnostics(&gps);
    SD_Logger_GetDiagnostics(&sd);
    SD_SPI_GetDiagnostics(&sd_spi);
    overflow_delta = coral.rx_overflow_count - s_previous_coral_overflow;

    length = snprintf(
        line, sizeof(line),
        "[SYS_STAT] t=%lu win=%lu loop_max=%lu dbg_q=%u dbg_high=%u dbg_drop=%lu/%lu dbg_start_err=%lu coral_q=%u coral_high=%u coral_ovf=%lu(+%lu) coral_ok=%lu coral_to=%lu coral_crc=%lu gps_bytes=%lu gps_nav=%lu gps_msg_age=%lu gps_fix_age=%lu fix=%u sv=%u\r\n",
        (unsigned long)now_ms,
        (unsigned long)window_ms,
        (unsigned long)s_max_loop_gap_ms,
        (unsigned int)debug.queued_bytes,
        (unsigned int)debug.queue_high_water,
        (unsigned long)debug.dropped_messages,
        (unsigned long)debug.dropped_bytes,
        (unsigned long)debug.tx_start_failures,
        (unsigned int)coral.rx_queued_bytes,
        (unsigned int)coral.rx_high_water,
        (unsigned long)coral.rx_overflow_count,
        (unsigned long)overflow_delta,
        (unsigned long)coral.good_frame_count,
        (unsigned long)coral.timeout_count,
        (unsigned long)coral.crc_error_count,
        (unsigned long)gps.i2c_bytes_received,
        (unsigned long)gps.nav_pvt_count,
        (unsigned long)Observability_AgeMs(now_ms, gps.last_nav_pvt_ms),
        (unsigned long)Observability_AgeMs(now_ms, gps.last_valid_fix_ms),
        (unsigned int)((datapool != NULL) ? datapool->gps_fix_type : 0U),
        (unsigned int)((datapool != NULL) ? datapool->gps_num_satellites : 0U));
    if (length > 0)
    {
        if ((size_t)length >= sizeof(line))
            length = (int)(sizeof(line) - 1U);
        DebugLog_WriteN(line, length);
    }

    length = snprintf(
        sd_line, sizeof(sd_line),
        "[SD_STAT] state=%s err=%s(%u) file=%u session=%lu rows=%lu buf=%lu/%lu bytes=%lu flush=%lu last_max_ms=%lu/%lu sync_age=%lu faults=%u/%lu recover=%lu/%lums rotate=%lu discard=%lu\r\n",
        SD_Logger_StateName(sd.state),
        SD_Logger_ResultName(sd.last_error),
        (unsigned int)sd.last_error,
        (unsigned int)sd.file_open,
        (unsigned long)sd.session,
        (unsigned long)sd.rows_in_file,
        (unsigned long)sd.buffer_used,
        (unsigned long)sd.buffer_capacity,
        (unsigned long)sd.total_bytes_committed,
        (unsigned long)sd.successful_flushes,
        (unsigned long)sd.last_flush_duration_ms,
        (unsigned long)sd.max_flush_duration_ms,
        (unsigned long)Observability_AgeMs(now_ms, sd.last_success_ms),
        (unsigned int)sd.consecutive_faults,
        (unsigned long)sd.total_faults,
        (unsigned long)sd.recovery_attempts,
        (unsigned long)sd.last_recovery_duration_ms,
        (unsigned long)sd.rotation_count,
        (unsigned long)sd.discarded_buffer_bytes);
    if (length > 0)
    {
        if ((size_t)length >= sizeof(sd_line))
            length = (int)(sizeof(sd_line) - 1U);
        DebugLog_WriteN(sd_line, length);
    }

    length = snprintf(
        sd_spi_line, sizeof(sd_spi_line),
        "[SD_SPI] init=%u card=%s sectors=%lu attempts=%lu spi_err=%lu hal_err=0x%08lX hal_state=%lu cmd=%u r1=0x%02X rx=0x%02X read=%lu/%lu write=%lu/%lu sync=%lu/%lu ready_to=%lu token_to=%lu reject=%lu fail_age=%lu coral_sd=%lu\r\n",
        (unsigned int)sd_spi.initialized,
        SD_SPI_GetCardTypeName(),
        (unsigned long)sd_spi.sector_count,
        (unsigned long)sd_spi.init_attempts,
        (unsigned long)sd_spi.spi_error_count,
        (unsigned long)sd_spi.hal_error_last,
        (unsigned long)sd_spi.hal_state_last,
        (unsigned int)sd_spi.last_command,
        (unsigned int)sd_spi.last_response,
        (unsigned int)sd_spi.last_rx,
        (unsigned long)sd_spi.read_operations,
        (unsigned long)sd_spi.read_failures,
        (unsigned long)sd_spi.write_operations,
        (unsigned long)sd_spi.write_failures,
        (unsigned long)sd_spi.sync_operations,
        (unsigned long)sd_spi.sync_failures,
        (unsigned long)sd_spi.ready_timeout_count,
        (unsigned long)sd_spi.data_token_timeout_count,
        (unsigned long)sd_spi.write_reject_count,
        (unsigned long)Observability_AgeMs(now_ms, sd_spi.last_failure_ms),
        (unsigned long)coral.sd_error_count);
    if (length > 0)
    {
        if ((size_t)length >= sizeof(sd_spi_line))
            length = (int)(sizeof(sd_spi_line) - 1U);
        DebugLog_WriteN(sd_spi_line, length);
    }

    s_previous_coral_overflow = coral.rx_overflow_count;
    s_max_loop_gap_ms = 0U;
    s_last_report_ms = now_ms;
}
