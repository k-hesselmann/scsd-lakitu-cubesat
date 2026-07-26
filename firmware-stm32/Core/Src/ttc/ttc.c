#include "ttc/ttc.h"
#include "ttc/lora_driver.h"
#include "fdir/fdir.h"

#include "main.h"
#include "usbd_cdc_if.h"

#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(TelemetryPacket_t) == TELEMETRY_PACKET_V8_SIZE,
               "TelemetryPacket_t must match the compact v8 wire format");

#ifndef TTC_CDC_BINARY_MIRROR
#define TTC_CDC_BINARY_MIRROR 0
#endif

#ifndef TTC_CDC_DEBUG_LOGS
#define TTC_CDC_DEBUG_LOGS 0
#endif

#ifndef TTC_UART_DEBUG_LOGS
#define TTC_UART_DEBUG_LOGS 0
#endif

#ifndef TTC_LORA_TRACE_LOGS
#define TTC_LORA_TRACE_LOGS 0
#endif

#ifndef TTC_DEBUG_INTERVAL_MS
#define TTC_DEBUG_INTERVAL_MS 0U
#endif

#ifndef TTC_DEBUG_FIXED_PAYLOAD
#define TTC_DEBUG_FIXED_PAYLOAD 0
#endif

#define TTC_TX_TIMEOUT_MS               5000U
#define TTC_ACK_TIMEOUT_MS              5000U
#define TTC_ACK_RX_RECOVERY_GRACE_MS   15000U
#define TTC_LORA_STATS_INTERVAL_MS      10000U
#define TTC_UPLINK_MAX_LENGTH              64U
#define TTC_COMMAND_REPLAY_WINDOW_SIZE     16U

typedef enum
{
    TTC_STATE_STARTUP_INIT = 0,
    TTC_STATE_STARTUP_RX,
    TTC_STATE_IDLE,
    TTC_STATE_TX_WAIT,
    TTC_STATE_TX_RX_START,
    TTC_STATE_ACTION_ISOLATION,
    TTC_STATE_ACTION_INIT,
    TTC_STATE_ACTION_RX_START
} TTC_State_t;

static uint32_t s_last_tx_ms;
static uint32_t s_pending_last_attempt_ms;
static uint32_t s_pending_tx_done_ms;
static uint8_t s_has_transmitted;
static uint8_t s_radio_ready;
static uint8_t s_radio_faulted;
static uint8_t s_immediate_telemetry;
static uint8_t s_pending_valid;
static uint8_t s_pending_awaiting_ack;
static uint8_t s_pending_waiting_for_rx;
static uint8_t s_driver_fault_observed;
#if TTC_CDC_BINARY_MIRROR
static uint8_t s_cdc_mirror_pending;
#endif
static TelemetryPacket_t s_pending_packet;
static LoRaHealth_t s_health;
static UplinkState_t s_uplink;
static TTCDebugStatus_t s_debug;
static TTC_FDIR_Health_t s_fdir_health;
static TTC_FDIR_ActionStatus_t s_action_status;
static TTC_FDIR_Action_t s_requested_action;
static uint16_t s_command_high_water;
static uint16_t s_command_seen_mask;
static uint8_t s_has_command_high_water;
static TTC_State_t s_state;

#if TTC_UART_DEBUG_LOGS || TTC_LORA_TRACE_LOGS
extern UART_HandleTypeDef huart2;
#endif

#if TTC_DEBUG_FIXED_PAYLOAD
static const uint8_t s_fixed_payload[] = {
    'T', 'T', 'C', '_', 'T', 'E', 'S', 'T',
    0x01U, 0x02U, 0x03U, 0x04U, 0xA5U, 0x5AU, 0xC3U, 0x3CU
};
#endif

static void TTC_IncrementU8(uint8_t *value)
{
    if (*value < UINT8_MAX)
        (*value)++;
}

static uint16_t TTC_IncrementU16(uint16_t value)
{
    return (value < UINT16_MAX) ? (uint16_t)(value + 1U) : value;
}

static uint8_t TTC_AcceptCommandId(uint16_t command_id)
{
    uint16_t forward;
    uint16_t age;
    uint16_t bit;

    if (!s_has_command_high_water)
    {
        s_command_high_water = command_id;
        s_command_seen_mask = 1U;
        s_has_command_high_water = 1U;
        return 1U;
    }

    forward = (uint16_t)(command_id - s_command_high_water);
    if (forward != 0U && forward < 0x8000U)
    {
        s_command_seen_mask = (forward >= TTC_COMMAND_REPLAY_WINDOW_SIZE) ? 1U :
                              (uint16_t)((s_command_seen_mask << forward) | 1U);
        s_command_high_water = command_id;
        return 1U;
    }

    age = (uint16_t)(s_command_high_water - command_id);
    if (age >= TTC_COMMAND_REPLAY_WINDOW_SIZE)
        return 0U;

    bit = (uint16_t)(1U << age);
    if ((s_command_seen_mask & bit) != 0U)
        return 0U;

    s_command_seen_mask |= bit;
    return 1U;
}

static uint32_t TTC_TelemetryIntervalMs(void)
{
#if TTC_DEBUG_INTERVAL_MS > 0U
    return TTC_DEBUG_INTERVAL_MS;
#else
    return TTC_TELEMETRY_INTERVAL_MS;
#endif
}

static uint8_t TTC_IntervalElapsed(uint32_t now)
{
    return (!s_has_transmitted ||
            (uint32_t)(now - s_last_tx_ms) >= TTC_TelemetryIntervalMs()) ? 1U : 0U;
}

#if TTC_CDC_DEBUG_LOGS || TTC_UART_DEBUG_LOGS || TTC_LORA_TRACE_LOGS
static uint32_t s_last_lora_stats_ms;
static LoRaRuntimeStats_t s_previous_lora_stats;
#if TTC_LORA_TRACE_LOGS
static uint8_t s_lora_trace_banner_logged;
#endif

static void TTC_DebugLog(const char *line)
{
    size_t len = 0U;

    while (line[len] != '\0')
        len++;

#if TTC_CDC_DEBUG_LOGS
    (void)CDC_Transmit_FS((uint8_t *)line, (uint16_t)len);
#endif
#if TTC_UART_DEBUG_LOGS || TTC_LORA_TRACE_LOGS
    (void)DebugLog_WriteN((uint8_t *)line, (uint16_t)len);
#endif
}

static const char *TTC_LoraPhaseName(uint8_t phase)
{
    switch ((LoRaOperationPhase_t)phase)
    {
        case LORA_PHASE_INIT: return "INIT";
        case LORA_PHASE_TX_SETUP: return "TX_SETUP";
        case LORA_PHASE_TX_POLL: return "TX_POLL";
        case LORA_PHASE_TX_FINISH: return "TX_FINISH";
        case LORA_PHASE_RX_START: return "RX_START";
        case LORA_PHASE_RX_POLL: return "RX_POLL";
        case LORA_PHASE_RX_PAYLOAD: return "RX_PAYLOAD";
        case LORA_PHASE_ISOLATION: return "ISOLATE";
        case LORA_PHASE_SPI_REINIT: return "SPI_REINIT";
        case LORA_PHASE_DEBUG_READ: return "DEBUG_READ";
        default: return "NONE";
    }
}

static const char *TTC_LoraFailureCauseName(uint8_t cause)
{
    switch ((LoRaSpiFailureCause_t)cause)
    {
        case LORA_SPI_FAIL_START_REJECTED: return "START_REJECT";
        case LORA_SPI_FAIL_IRQ_ERROR: return "IRQ_ERROR";
        case LORA_SPI_FAIL_TRANSFER_TIMEOUT: return "XFER_TIMEOUT";
        case LORA_SPI_FAIL_ABORT_REJECTED: return "ABORT_REJECT";
        case LORA_SPI_FAIL_ABORT_TIMEOUT: return "ABORT_TIMEOUT";
        case LORA_SPI_FAIL_DEINIT: return "DEINIT_FAIL";
        case LORA_SPI_FAIL_REINIT: return "REINIT_FAIL";
        case LORA_SPI_FAIL_VERIFY: return "VERIFY_FAIL";
        default: return "UNKNOWN";
    }
}

static const char *TTC_LoraOperationName(uint8_t operation)
{
    switch (operation)
    {
        case 1U: return "READ";
        case 2U: return "WRITE";
        case 3U: return "FIFO_WRITE";
        case 4U: return "SPI_REINIT";
        default: return "NONE";
    }
}

static const char *TTC_LoraRegisterName(uint8_t address)
{
    switch (address)
    {
        case 0x00U: return "FIFO";
        case 0x01U: return "OP_MODE";
        case 0x06U: return "FRF_MSB";
        case 0x07U: return "FRF_MID";
        case 0x08U: return "FRF_LSB";
        case 0x09U: return "PA_CONFIG";
        case 0x0DU: return "FIFO_ADDR_PTR";
        case 0x0EU: return "FIFO_TX_BASE";
        case 0x10U: return "FIFO_RX_CURRENT";
        case 0x12U: return "IRQ_FLAGS";
        case 0x13U: return "RX_NB_BYTES";
        case 0x1DU: return "MODEM_CONFIG_1";
        case 0x1EU: return "MODEM_CONFIG_2";
        case 0x26U: return "MODEM_CONFIG_3";
        case 0x39U: return "SYNC_WORD";
        case 0x42U: return "VERSION";
        case 0x4DU: return "PA_DAC";
        case 0xFFU: return "SPI1";
        default: return "UNKNOWN";
    }
}

static const char *TTC_HalStatusName(uint8_t status)
{
    switch ((HAL_StatusTypeDef)status)
    {
        case HAL_OK: return "OK";
        case HAL_ERROR: return "ERROR";
        case HAL_BUSY: return "BUSY";
        case HAL_TIMEOUT: return "TIMEOUT";
        default: return "UNKNOWN";
    }
}

static void TTC_LogLoRaFaults(void)
{
    LoRaFaultTrace_t trace;
    char line[256];

    while (LoRa_TakeFaultTrace(&trace))
    {
        int len = snprintf(
            line, sizeof(line),
            "[LORA_FAULT] n=%lu t=%lu cause=%s phase=%s state=%u reg=%s(0x%02X) op=%s hal=%s(%u) err=0x%08lX active=%u wait=%u abort=%u irq=0x%02X txdone=%u\r\n",
            (unsigned long)trace.occurrence,
            (unsigned long)trace.timestamp_ms,
            TTC_LoraFailureCauseName(trace.cause),
            TTC_LoraPhaseName(trace.phase),
            (unsigned int)trace.lora_state,
            TTC_LoraRegisterName(trace.register_address),
            (unsigned int)trace.register_address,
            TTC_LoraOperationName(trace.operation),
            TTC_HalStatusName(trace.hal_status),
            (unsigned int)trace.hal_status,
            (unsigned long)trace.hal_error,
            (unsigned int)trace.spi_active,
            (unsigned int)trace.action_waiting,
            (unsigned int)trace.abort_pending,
            (unsigned int)trace.irq_flags,
            (unsigned int)trace.tx_done_seen);
        if (len > 0)
            TTC_DebugLog(line);
    }
}

static void TTC_LogLoRaStats(uint32_t now)
{
    LoRaRuntimeStats_t stats;
    char line[224];
    int len;

    if ((uint32_t)(now - s_last_lora_stats_ms) < TTC_LORA_STATS_INTERVAL_MS)
        return;

    LoRa_GetRuntimeStats(&stats);
    len = snprintf(
        line, sizeof(line),
        "[LORA_STAT] t=%lu window_ms=%lu spi=%lu complete=%lu irqerr=%lu timeout=%lu abort=%lu txpoll=%lu rxpoll=%lu dropped=%lu\r\n",
        (unsigned long)now,
        (unsigned long)(now - s_last_lora_stats_ms),
        (unsigned long)(stats.spi_transfer_count - s_previous_lora_stats.spi_transfer_count),
        (unsigned long)(stats.spi_completion_count - s_previous_lora_stats.spi_completion_count),
        (unsigned long)(stats.spi_irq_error_count - s_previous_lora_stats.spi_irq_error_count),
        (unsigned long)(stats.spi_timeout_count - s_previous_lora_stats.spi_timeout_count),
        (unsigned long)(stats.spi_abort_count - s_previous_lora_stats.spi_abort_count),
        (unsigned long)(stats.tx_irq_poll_count - s_previous_lora_stats.tx_irq_poll_count),
        (unsigned long)(stats.rx_irq_poll_count - s_previous_lora_stats.rx_irq_poll_count),
        (unsigned long)(stats.dropped_fault_trace_count - s_previous_lora_stats.dropped_fault_trace_count));
    if (len > 0)
        TTC_DebugLog(line);

    s_previous_lora_stats = stats;
    s_last_lora_stats_ms = now;
}

static void TTC_LogLoRaDiagnostics(uint32_t now)
{
#if TTC_LORA_TRACE_LOGS
    if (!s_lora_trace_banner_logged)
    {
        TTC_DebugLog("[LORA_TRACE] enabled tx_poll_ms=2 rx_poll_ms=5 stats_ms=10000\r\n");
        s_lora_trace_banner_logged = 1U;
    }
#endif
    TTC_LogLoRaFaults();
    /* Periodic logging is intentionally deferred until normal RX is active,
     * so the bench trace cannot delay the TX-to-RX turnaround. */
    if (!LoRa_IsBusy() && LoRa_IsRxActive())
        TTC_LogLoRaStats(now);
}

#if TTC_CDC_DEBUG_LOGS || TTC_UART_DEBUG_LOGS
static void TTC_LogInitStatus(void)
{
    char line[160];
    LoRaDebugStatus_t lora = {0};
    int len;

    LoRa_GetDebugStatus(&lora);
    len = snprintf(line, sizeof(line),
                   "LORA_INIT status=%u ready=%u version=0x%02X op=0x%02X irq=0x%02X fail_reg=0x%02X fail_op=%u hal=%lu spierr=%lu\r\n",
                   (unsigned int)s_debug.last_init_status,
                   (unsigned int)s_debug.radio_ready,
                   (unsigned int)lora.version,
                   (unsigned int)lora.op_mode,
                   (unsigned int)lora.irq_flags,
                   (unsigned int)lora.last_failed_reg,
                   (unsigned int)lora.last_failed_op,
                   (unsigned long)lora.last_hal_status,
                   (unsigned long)lora.spi_error_code);
    if (len > 0)
        TTC_DebugLog(line);
}

static void TTC_LogTxStatus(void)
{
    char line[224];
    LoRaDebugStatus_t lora = {0};
    int len;

    LoRa_GetDebugStatus(&lora);
    len = snprintf(line, sizeof(line),
                   "TTC_TX seq=%u len=%u crc=0x%04X status=%u ready=%u version=0x%02X irq=0x%02X op=0x%02X tx_ok=%lu tx_timeout=%lu tx_error=%lu fail_reg=0x%02X fail_op=%u hal=%lu spierr=%lu\r\n",
                   (unsigned int)s_debug.last_sequence_number,
                   (unsigned int)s_debug.last_payload_length,
                   (unsigned int)s_debug.last_crc16,
                   (unsigned int)s_debug.last_send_status,
                   (unsigned int)s_debug.radio_ready,
                   (unsigned int)lora.version,
                   (unsigned int)lora.irq_flags,
                   (unsigned int)lora.op_mode,
                   (unsigned long)s_debug.tx_success_count,
                   (unsigned long)s_debug.tx_timeout_count,
                   (unsigned long)s_debug.tx_error_count,
                   (unsigned int)lora.last_failed_reg,
                   (unsigned int)lora.last_failed_op,
                   (unsigned long)lora.last_hal_status,
                   (unsigned long)lora.spi_error_code);
    if (len > 0)
        TTC_DebugLog(line);
}
#else
static void TTC_LogInitStatus(void) {}
static void TTC_LogTxStatus(void) {}
#endif
#else
static void TTC_LogInitStatus(void) {}
static void TTC_LogTxStatus(void) {}
static void TTC_LogLoRaDiagnostics(uint32_t now) { (void)now; }
#endif

static void TTC_RefreshDebugFromLora(void)
{
    LoRaDebugStatus_t lora = {0};

    LoRa_GetDebugStatus(&lora);
    s_debug.radio_ready = s_radio_ready;
    s_debug.last_lora_version = lora.version;
    s_debug.last_irq_flags = lora.irq_flags;
}

static void TTC_RecordDebugTxCompletion(LoRaStatus_t status)
{
    s_debug.last_send_status = status;
    if (status == LORA_OK)
    {
        s_debug.tx_success_count++;
    }
    else if (status == LORA_TIMEOUT)
    {
        s_debug.tx_timeout_count++;
    }
    else
    {
        s_debug.tx_error_count++;
    }

    /* The driver continuously caches the relevant registers. Do not add a
     * synchronous 11-register SPI snapshot to every TX completion. */
    TTC_RefreshDebugFromLora();
    TTC_LogTxStatus();
}

static uint8_t TTC_ActionIsActive(void)
{
    return (s_action_status.state == TTC_FDIR_ACTION_PENDING ||
            s_action_status.state == TTC_FDIR_ACTION_IN_PROGRESS) ? 1U : 0U;
}

static void TTC_SyncFdirHealth(void)
{
    uint8_t action_is_recovery =
        (s_action_status.action == TTC_FDIR_ACTION_RECOVERY ||
         s_action_status.action == TTC_FDIR_ACTION_RETURN_TO_SERVICE) ? 1U : 0U;

    s_fdir_health.radio_ready = (s_radio_ready && LoRa_IsReady()) ? 1U : 0U;
    s_fdir_health.rx_active = LoRa_IsRxActive();
    s_fdir_health.radio_busy = LoRa_IsBusy();
    s_fdir_health.recovery_in_progress =
        (action_is_recovery &&
         (s_action_status.state == TTC_FDIR_ACTION_PENDING ||
          s_action_status.state == TTC_FDIR_ACTION_IN_PROGRESS)) ? 1U : 0U;
    s_fdir_health.isolation_active = LoRa_IsIsolated();
}

static void TTC_SetActionResult(TTC_FDIR_ActionState_t state, LoRaStatus_t status)
{
    s_action_status.state = state;
    s_action_status.last_lora_status = (uint8_t)status;
    s_requested_action = TTC_FDIR_ACTION_NONE;
    TTC_SyncFdirHealth();
}

static void TTC_RecordInitResult(LoRaStatus_t status)
{
    s_radio_ready = (status == LORA_OK) ? 1U : 0U;
    s_debug.last_init_status = status;
    if (s_radio_ready)
    {
        s_driver_fault_observed = 0U;
        s_health.last_event = LORA_EVENT_INIT_OK;
        s_radio_faulted = 0U;
    }
    else
    {
        s_driver_fault_observed = 1U;
        s_health.last_event = (status == LORA_CONFIG_ERROR) ?
                              LORA_EVENT_CONFIG_FAIL : LORA_EVENT_INIT_FAIL;
        s_health.rx_mode_active = 0U;
        s_health.last_rx_status = (status == LORA_CONFIG_ERROR) ?
                                  LORA_RX_HEALTH_MODE_ERROR : LORA_RX_HEALTH_SPI_ERROR;
        TTC_IncrementU8(&s_health.consecutive_failures);
        s_radio_faulted = 1U;
    }
    /* Init read-verifies and caches the modem profile asynchronously. */
    TTC_RefreshDebugFromLora();
    TTC_LogInitStatus();
    TTC_SyncFdirHealth();
}

static void TTC_RecordRxModeResult(LoRaStatus_t status)
{
    if (status == LORA_OK)
    {
        s_driver_fault_observed = 0U;
        s_health.rx_mode_active = 1U;
        s_health.last_rx_status = LORA_RX_HEALTH_ACTIVE;
        s_radio_ready = 1U;
        s_radio_faulted = 0U;
    }
    else
    {
        s_driver_fault_observed = 1U;
        s_health.rx_mode_active = 0U;
        s_health.last_event = (status == LORA_CONFIG_ERROR) ?
                              LORA_EVENT_RX_MODE_FAIL : LORA_EVENT_RX_SPI_FAIL;
        s_health.last_rx_status = (status == LORA_CONFIG_ERROR) ?
                                  LORA_RX_HEALTH_MODE_ERROR : LORA_RX_HEALTH_SPI_ERROR;
        TTC_IncrementU8(&s_health.consecutive_failures);
        s_radio_ready = 0U;
        s_radio_faulted = 1U;
    }
    TTC_SyncFdirHealth();
}

static void TTC_ReconcileDriverFault(void)
{
    if (s_state != TTC_STATE_IDLE)
        return;

    if (LoRa_GetState() != LORA_STATE_FAULT)
    {
        s_driver_fault_observed = 0U;
        return;
    }

    if (!s_driver_fault_observed)
        TTC_RecordRxModeResult(LoRa_GetLastStatus());
}

static void TTC_RecordTxSuccess(uint32_t now)
{
    s_driver_fault_observed = 0U;
    s_health.last_event = LORA_EVENT_TX_OK;
    s_health.consecutive_failures = 0U;
    s_health.last_success_ms = now;
    s_fdir_health.consecutive_tx_failures = 0U;
    s_fdir_health.last_tx_success_ms = now;
    s_radio_ready = 1U;
    TTC_SyncFdirHealth();
    TTC_RecordDebugTxCompletion(LORA_OK);
}

static void TTC_RecordTxFailure(LoRaStatus_t status)
{
    if (status == LORA_LENGTH_ERROR)
    {
        s_health.last_event = LORA_EVENT_TX_BAD_LENGTH;
        TTC_RecordDebugTxCompletion(status);
        return;
    }

    s_driver_fault_observed = 1U;
    s_health.last_event = (status == LORA_TIMEOUT) ?
                          LORA_EVENT_TX_TIMEOUT : LORA_EVENT_TX_SPI_FAIL;
    TTC_IncrementU8(&s_health.consecutive_failures);
    TTC_IncrementU8(&s_fdir_health.consecutive_tx_failures);
    s_fdir_health.lora_tx_fault_counter =
        TTC_IncrementU16(s_fdir_health.lora_tx_fault_counter);
    s_radio_ready = 0U;
    s_radio_faulted = 1U;
    TTC_SyncFdirHealth();
    TTC_RecordDebugTxCompletion(status);
}

static void TTC_RecordRxPacket(uint32_t now)
{
    s_health.rx_mode_active = 1U;
    s_health.last_rx_status = LORA_RX_HEALTH_PACKET_OK;
    s_health.last_event = LORA_EVENT_RX_OK;
    s_health.last_rx_ms = now;
    s_health.rx_packet_count = TTC_IncrementU16(s_health.rx_packet_count);
    s_health.consecutive_failures = 0U;
    s_fdir_health.last_rx_success_ms = now;
    TTC_SyncFdirHealth();
}

static void TTC_RecordRxError(LoRaStatus_t status)
{
    if (status == LORA_RX_CRC_ERROR)
    {
        s_health.rx_mode_active = 1U;
        s_health.last_rx_status = LORA_RX_HEALTH_CRC_ERROR;
        s_health.last_event = LORA_EVENT_RX_CRC_ERROR;
        s_health.rx_crc_error_count = TTC_IncrementU16(s_health.rx_crc_error_count);
        return;
    }

    if (status == LORA_LENGTH_ERROR)
    {
        s_health.rx_mode_active = 1U;
        s_health.last_rx_status = LORA_RX_HEALTH_BAD_LENGTH;
        return;
    }

    TTC_RecordRxModeResult(status);
}

static uint8_t TTC_ParseAcknowledgement(const uint8_t *data, uint8_t length,
                                        uint16_t *sequence)
{
    uint32_t value = 0U;
    uint8_t i;

    if (length < 5U || length > 9U || memcmp(data, "ACK,", 4U) != 0)
        return 0U;

    for (i = 4U; i < length; i++)
    {
        if (data[i] < (uint8_t)'0' || data[i] > (uint8_t)'9')
            return 0U;
        value = (value * 10U) + (uint32_t)(data[i] - (uint8_t)'0');
        if (value > UINT16_MAX)
            return 0U;
    }

    *sequence = (uint16_t)value;
    return 1U;
}

static uint8_t TTC_ParseCommandEnvelope(const uint8_t *data, uint8_t length,
                                        uint16_t *command_id,
                                        const uint8_t **verb, uint8_t *verb_length)
{
    uint32_t value = 0U;
    uint8_t i = 4U;
    uint8_t digits = 0U;

    if (length < 7U || memcmp(data, "CMD,", 4U) != 0)
        return 0U;

    while (i < length && data[i] != (uint8_t)',')
    {
        if (data[i] < (uint8_t)'0' || data[i] > (uint8_t)'9' || digits >= 5U)
            return 0U;
        value = (value * 10U) + (uint32_t)(data[i] - (uint8_t)'0');
        if (value > UINT16_MAX)
            return 0U;
        digits++;
        i++;
    }

    if (digits == 0U || value == 0U || i >= (uint8_t)(length - 1U))
        return 0U;

    *command_id = (uint16_t)value;
    *verb = &data[i + 1U];
    *verb_length = (uint8_t)(length - i - 1U);
    return 1U;
}

static void TTC_ProcessAcknowledgement(uint16_t acknowledged_sequence)
{
    if (s_pending_valid && acknowledged_sequence == s_pending_packet.sequence_number)
    {
        s_uplink.last_ack_status = UPLINK_STATUS_ACCEPTED;
        s_uplink.last_ack_sequence = acknowledged_sequence;
        s_pending_valid = 0U;
        s_pending_awaiting_ack = 0U;
        s_pending_waiting_for_rx = 0U;
    }
    else if (acknowledged_sequence == s_uplink.last_ack_sequence)
    {
        s_uplink.last_ack_status = UPLINK_STATUS_DUPLICATE;
    }
    else
    {
        s_uplink.last_ack_status = UPLINK_STATUS_UNEXPECTED_ACK;
    }
}

static void TTC_ProcessUplink(const uint8_t *data, uint8_t length)
{
    uint16_t value;
    const uint8_t *verb;
    uint8_t verb_length;

    if (TTC_ParseAcknowledgement(data, length, &value))
    {
        TTC_ProcessAcknowledgement(value);
        return;
    }

    if (TTC_ParseCommandEnvelope(data, length, &value, &verb, &verb_length))
    {
        if (verb_length == 13U && memcmp(verb, "REQ_TELEMETRY", 13U) == 0)
        {
            s_uplink.last_command = UPLINK_COMMAND_REQUEST_TELEMETRY;
            s_uplink.last_command_id = value;
            if (!TTC_AcceptCommandId(value))
            {
                s_uplink.last_command_status = UPLINK_STATUS_DUPLICATE;
            }
            else
            {
                s_uplink.last_command_status = UPLINK_STATUS_ACCEPTED;
                s_uplink.command_count = TTC_IncrementU16(s_uplink.command_count);
            }
            TTC_RequestTelemetry();
            return;
        }

        s_uplink.last_command = UPLINK_COMMAND_NONE;
        s_uplink.last_command_id = value;
        s_uplink.last_command_status = UPLINK_STATUS_UNSUPPORTED;
        TTC_RequestTelemetry();
        return;
    }

    if (length >= 4U && memcmp(data, "ACK,", 4U) == 0)
    {
        s_uplink.last_ack_status = UPLINK_STATUS_INVALID_FORMAT;
    }
    else
    {
        s_uplink.last_command = UPLINK_COMMAND_NONE;
        s_uplink.last_command_id = 0U;
        s_uplink.last_command_status =
            (length >= 4U && memcmp(data, "CMD,", 4U) == 0) ?
            UPLINK_STATUS_INVALID_FORMAT : UPLINK_STATUS_UNSUPPORTED;
    }
    TTC_RequestTelemetry();
}

static void TTC_PollUplink(uint32_t now)
{
    uint8_t packet[TTC_UPLINK_MAX_LENGTH];
    uint8_t length = 0U;
    LoRaStatus_t status;

    if (!s_radio_ready || s_radio_faulted || !LoRa_IsRxActive())
        return;

    status = LoRa_Receive(packet, &length, sizeof(packet));
    if (status == LORA_OK)
    {
        TTC_RecordRxPacket(now);
        TTC_ProcessUplink(packet, length);
    }
    else if (status != LORA_NO_PACKET)
    {
        TTC_RecordRxError(status);
    }
}

static void TTC_StartPendingTransmit(uint32_t now)
{
    LoRaStatus_t status;
#if TTC_DEBUG_FIXED_PAYLOAD
    const uint8_t *tx_data = s_fixed_payload;
    uint8_t tx_length = (uint8_t)sizeof(s_fixed_payload);
#else
    const uint8_t *tx_data = (const uint8_t *)&s_pending_packet;
    uint8_t tx_length = (uint8_t)sizeof(s_pending_packet);
#endif

    if (!s_pending_valid || s_radio_faulted || !s_radio_ready ||
        !LoRa_IsRxActive() || LoRa_IsBusy())
        return;

    s_debug.tx_attempt_count++;
    s_debug.last_sequence_number = s_pending_packet.sequence_number;
    s_debug.last_crc16 = s_pending_packet.crc16;
    s_debug.last_payload_length = tx_length;

    status = LoRa_Send(tx_data, tx_length, TTC_TX_TIMEOUT_MS);
    s_debug.last_send_status = status;
    TTC_RefreshDebugFromLora();
    if (status != LORA_OK)
    {
        TTC_RecordTxFailure(status);
        s_pending_valid = 0U;
        s_last_tx_ms = now;
        s_has_transmitted = 1U;
        return;
    }

    s_pending_awaiting_ack = 0U;
    s_pending_waiting_for_rx = 0U;
    s_state = TTC_STATE_TX_WAIT;
}

static void TTC_ServicePendingTelemetry(uint32_t now)
{
    if (!s_pending_valid)
        return;

    if (s_pending_waiting_for_rx)
    {
        if ((uint32_t)(now - s_pending_tx_done_ms) <
            TTC_ACK_RX_RECOVERY_GRACE_MS)
            return;

        /* TxDone was observed, but TTC never regained RX. Do not attribute
         * this to a silent ground station: the ACK was unobservable. */
        s_health.last_event = LORA_EVENT_ACK_RX_UNAVAILABLE;
        s_health.ack_timeout_count = TTC_IncrementU16(s_health.ack_timeout_count);
        s_pending_valid = 0U;
        s_pending_waiting_for_rx = 0U;
        return;
    }

    if (!s_pending_awaiting_ack)
    {
        if (!s_radio_faulted)
            TTC_StartPendingTransmit(now);
        return;
    }

    if ((uint32_t)(now - s_pending_last_attempt_ms) < TTC_ACK_TIMEOUT_MS)
        return;

    /* A missing ground ACK is reported by the next fresh telemetry packet.
     * Do not retransmit this packet or reuse its sequence number. */
    s_health.last_event = LORA_EVENT_ACK_TIMEOUT;
    s_health.ack_timeout_count = TTC_IncrementU16(s_health.ack_timeout_count);
    s_fdir_health.nack_counter = TTC_IncrementU16(s_fdir_health.nack_counter);
    s_pending_valid = 0U;
    s_pending_awaiting_ack = 0U;
}

static void TTC_BeginRequestedAction(void)
{
    LoRaStatus_t status;

    if (s_requested_action == TTC_FDIR_ACTION_NONE)
        return;

    /* Isolation is allowed to preempt any in-flight TTC operation. */
    if (s_requested_action == TTC_FDIR_ACTION_ISOLATION)
    {
        s_action_status.state = TTC_FDIR_ACTION_IN_PROGRESS;
        status = LoRa_Isolate();
        s_radio_ready = 0U;
        s_radio_faulted = 1U;
        s_driver_fault_observed = 1U;
        if (status == LORA_BUSY)
        {
            s_state = TTC_STATE_ACTION_ISOLATION;
            TTC_SyncFdirHealth();
            return;
        }
        s_state = TTC_STATE_IDLE;
        TTC_SetActionResult((status == LORA_OK) ? TTC_FDIR_ACTION_SUCCEEDED :
                                                TTC_FDIR_ACTION_FAILED, status);
        return;
    }

    if (LoRa_IsBusy())
        return;

    if (s_requested_action == TTC_FDIR_ACTION_RECOVERY ||
        s_requested_action == TTC_FDIR_ACTION_RETURN_TO_SERVICE)
    {
        status = LoRa_Init();
        if (status == LORA_BUSY)
            return;
        if (status != LORA_OK)
        {
            s_debug.last_init_status = status;
            TTC_RefreshDebugFromLora();
            TTC_LogInitStatus();
            TTC_SetActionResult(TTC_FDIR_ACTION_FAILED, status);
            return;
        }
        s_debug.last_init_status = status;
        TTC_RefreshDebugFromLora();
        s_action_status.state = TTC_FDIR_ACTION_IN_PROGRESS;
        TTC_IncrementU8(&s_health.recovery_count);
        s_driver_fault_observed = 0U;
        s_radio_ready = 0U;
        s_radio_faulted = 0U;
        s_state = TTC_STATE_ACTION_INIT;
        TTC_SyncFdirHealth();
        return;
    }

    status = LoRa_StartReceive();
    if (status == LORA_BUSY)
        return;
    if (status != LORA_OK)
    {
        TTC_SetActionResult(TTC_FDIR_ACTION_FAILED, status);
        return;
    }
    s_action_status.state = TTC_FDIR_ACTION_IN_PROGRESS;
    s_state = TTC_STATE_ACTION_RX_START;
    TTC_SyncFdirHealth();
}

static void TTC_CompleteRxStart(TTC_State_t next_state, uint8_t action_completion)
{
    LoRaStatus_t status = LoRa_GetLastStatus();

    TTC_RecordRxModeResult(status);
    if (status == LORA_OK && s_pending_valid && s_pending_waiting_for_rx)
    {
        /* The ACK timer measures observable receive time, not modem cleanup
         * or FDIR recovery time following TxDone. */
        s_pending_last_attempt_ms = HAL_GetTick();
        s_pending_awaiting_ack = 1U;
        s_pending_waiting_for_rx = 0U;
    }
    if (action_completion)
        TTC_SetActionResult((status == LORA_OK) ? TTC_FDIR_ACTION_SUCCEEDED :
                                               TTC_FDIR_ACTION_FAILED, status);
    s_state = next_state;
}

void TTC_Init(void)
{
    memset(&s_pending_packet, 0, sizeof(s_pending_packet));
    memset(&s_health, 0, sizeof(s_health));
    memset(&s_uplink, 0, sizeof(s_uplink));
    memset(&s_debug, 0, sizeof(s_debug));
    memset(&s_fdir_health, 0, sizeof(s_fdir_health));
    memset(&s_action_status, 0, sizeof(s_action_status));
    s_last_tx_ms = 0U;
    s_pending_last_attempt_ms = 0U;
    s_pending_tx_done_ms = 0U;
    s_has_transmitted = 0U;
    s_radio_ready = 0U;
    s_radio_faulted = 0U;
    s_immediate_telemetry = 0U;
    s_pending_valid = 0U;
    s_pending_awaiting_ack = 0U;
    s_pending_waiting_for_rx = 0U;
    s_driver_fault_observed = 0U;
#if TTC_CDC_BINARY_MIRROR
    s_cdc_mirror_pending = 0U;
#endif
    s_requested_action = TTC_FDIR_ACTION_NONE;
    s_command_high_water = 0U;
    s_command_seen_mask = 0U;
    s_has_command_high_water = 0U;
    s_state = TTC_STATE_STARTUP_INIT;
    s_action_status.action = TTC_FDIR_ACTION_NONE;
    s_action_status.state = TTC_FDIR_ACTION_IDLE;
    s_action_status.last_lora_status = (uint8_t)LoRa_Init();
    s_debug.last_init_status = (LoRaStatus_t)s_action_status.last_lora_status;
    s_debug.last_send_status = LORA_NOT_READY;
    TTC_RefreshDebugFromLora();
    if (s_action_status.last_lora_status != (uint8_t)LORA_OK)
    {
        TTC_RecordInitResult((LoRaStatus_t)s_action_status.last_lora_status);
        s_state = TTC_STATE_IDLE;
    }
    TTC_SyncFdirHealth();
}

void TTC_Service(void)
{
    LoRaStatus_t status;
    uint32_t now = HAL_GetTick();

    /* Consume FDIR-requested LoRa recovery (FMECA T1). Ack only once TTC has
     * accepted it, found it already complete, or is already doing the same
     * action. A different active action leaves the request bit set. */
    if (FDIR_GetReinitRequests() & EQUIPMENT_LORA)
    {
        TTC_FDIR_Result_t result;

        /* Use the least disruptive recovery that matches the observation.
         * A configured modem that only lost RX gets an RX restart; SPI/modem
         * faults still receive the full reset, reconfigure and verify path. */
        TTC_SyncFdirHealth();
        if (s_fdir_health.radio_ready && !s_fdir_health.rx_active &&
            !s_fdir_health.isolation_active)
            result = TTC_FDIR_RequestRxRestart();
        else
            result = TTC_FDIR_RequestRecovery();
        if (result != TTC_FDIR_RESULT_REJECTED)
            FDIR_AcknowledgeReinit(EQUIPMENT_LORA);
    }

    LoRa_Service();
    TTC_LogLoRaDiagnostics(now);

#if TTC_CDC_BINARY_MIRROR
    if (s_cdc_mirror_pending)
    {
        (void)CDC_Transmit_FS((uint8_t *)&s_pending_packet,
                              (uint16_t)sizeof(s_pending_packet));
        s_cdc_mirror_pending = 0U;
    }
#endif

    if (s_requested_action == TTC_FDIR_ACTION_ISOLATION &&
        s_state != TTC_STATE_ACTION_ISOLATION)
    {
        TTC_BeginRequestedAction();
        goto done;
    }

    if (s_state == TTC_STATE_ACTION_ISOLATION)
    {
        if (LoRa_IsBusy())
            goto done;
        status = LoRa_GetLastStatus();
        s_state = TTC_STATE_IDLE;
        TTC_SetActionResult((status == LORA_OK) ? TTC_FDIR_ACTION_SUCCEEDED :
                                                TTC_FDIR_ACTION_FAILED, status);
        goto done;
    }

    if (s_state == TTC_STATE_STARTUP_INIT)
    {
        if (LoRa_IsBusy())
            goto done;
        status = LoRa_GetLastStatus();
        TTC_RecordInitResult(status);
        if (status == LORA_OK)
        {
            status = LoRa_StartReceive();
            if (status == LORA_OK)
                s_state = TTC_STATE_STARTUP_RX;
            else
            {
                TTC_RecordRxModeResult(status);
                s_state = TTC_STATE_IDLE;
            }
        }
        else
            s_state = TTC_STATE_IDLE;
        goto done;
    }

    if (s_state == TTC_STATE_STARTUP_RX)
    {
        if (LoRa_IsBusy())
            goto done;
        TTC_CompleteRxStart(TTC_STATE_IDLE, 0U);
        goto done;
    }

    if (s_state == TTC_STATE_TX_WAIT)
    {
        if (LoRa_IsBusy())
            goto done;
        status = LoRa_GetLastStatus();
        if (status == LORA_OK || LoRa_WasLastTxOnAir())
        {
            /* TxDone is the on-air success boundary. A later IRQ-clear or
             * standby-write failure must not be counted as a lost downlink. */
            TTC_RecordTxSuccess(now);
            s_pending_tx_done_ms = now;
            s_pending_awaiting_ack = 0U;
            s_pending_waiting_for_rx = 1U;
            s_last_tx_ms = now;
            s_has_transmitted = 1U;

            if (status != LORA_OK)
            {
                /* The packet was radiated, but the local modem/SPI cleanup
                 * failed. Preserve the packet until FDIR restores RX. */
                TTC_RecordRxModeResult(status);
                s_state = TTC_STATE_IDLE;
                goto done;
            }

            status = LoRa_StartReceive();
            if (status == LORA_OK)
                s_state = TTC_STATE_TX_RX_START;
            else
            {
                TTC_RecordRxModeResult(status);
                s_state = TTC_STATE_IDLE;
            }
        }
        else
        {
            TTC_RecordTxFailure(status);
            s_pending_valid = 0U;
            s_pending_awaiting_ack = 0U;
            s_pending_waiting_for_rx = 0U;
            s_last_tx_ms = now;
            s_has_transmitted = 1U;
            s_state = TTC_STATE_IDLE;
        }
        goto done;
    }

    if (s_state == TTC_STATE_TX_RX_START)
    {
        if (LoRa_IsBusy())
            goto done;
        TTC_CompleteRxStart(TTC_STATE_IDLE, 0U);
        goto done;
    }

    if (s_state == TTC_STATE_ACTION_INIT)
    {
        if (LoRa_IsBusy())
            goto done;
        status = LoRa_GetLastStatus();
        TTC_RecordInitResult(status);
        if (status == LORA_OK)
        {
            status = LoRa_StartReceive();
            if (status == LORA_OK)
                s_state = TTC_STATE_ACTION_RX_START;
            else
            {
                TTC_RecordRxModeResult(status);
                TTC_SetActionResult(TTC_FDIR_ACTION_FAILED, status);
                s_state = TTC_STATE_IDLE;
            }
        }
        else
        {
            TTC_SetActionResult(TTC_FDIR_ACTION_FAILED, status);
            s_state = TTC_STATE_IDLE;
        }
        goto done;
    }

    if (s_state == TTC_STATE_ACTION_RX_START)
    {
        if (LoRa_IsBusy())
            goto done;
        TTC_CompleteRxStart(TTC_STATE_IDLE, 1U);
        goto done;
    }

    TTC_BeginRequestedAction();
    if (s_state != TTC_STATE_IDLE)
        goto done;
    TTC_ReconcileDriverFault();
    TTC_PollUplink(now);
    TTC_ServicePendingTelemetry(now);

done:
    TTC_SyncFdirHealth();
}

void TTC_RequestTelemetry(void)
{
    s_immediate_telemetry = 1U;
}

uint8_t TTC_TelemetryDue(void)
{
    if (s_pending_valid)
        return 0U;

    return s_immediate_telemetry || TTC_IntervalElapsed(HAL_GetTick());
}

const LoRaHealth_t *TTC_GetHealth(void)
{
    return &s_health;
}

const UplinkState_t *TTC_GetUplinkState(void)
{
    return &s_uplink;
}

void TTC_GetDebugStatus(TTCDebugStatus_t *status)
{
    if (status == NULL)
        return;

    TTC_RefreshDebugFromLora();
    *status = s_debug;
}

void TTC_Transmit(const TelemetryPacket_t *pkt)
{
    uint32_t now;

    if (pkt == NULL || s_pending_valid)
        return;

    now = HAL_GetTick();
    if (!s_immediate_telemetry && !TTC_IntervalElapsed(now))
        return;

    s_pending_packet = *pkt;
    s_pending_valid = 1U;
    s_pending_awaiting_ack = 0U;
    s_pending_waiting_for_rx = 0U;
    s_immediate_telemetry = 0U;
#if TTC_CDC_BINARY_MIRROR
    s_cdc_mirror_pending = 1U;
#endif
}

void TTC_FDIR_GetHealth(TTC_FDIR_Health_t *health)
{
    if (health == NULL)
        return;

    TTC_SyncFdirHealth();
    *health = s_fdir_health;
}

static TTC_FDIR_Result_t TTC_RequestAction(TTC_FDIR_Action_t action)
{
    if (TTC_ActionIsActive())
        return (s_action_status.action == action) ? TTC_FDIR_RESULT_IN_PROGRESS :
                                                   TTC_FDIR_RESULT_REJECTED;

    s_requested_action = action;
    s_action_status.action = action;
    s_action_status.state = TTC_FDIR_ACTION_PENDING;
    s_action_status.last_lora_status = (uint8_t)LORA_BUSY;
    return TTC_FDIR_RESULT_ACCEPTED;
}

TTC_FDIR_Result_t TTC_FDIR_RequestIsolation(void)
{
    if (LoRa_IsIsolated())
        return TTC_FDIR_RESULT_ALREADY_COMPLETE;
    return TTC_RequestAction(TTC_FDIR_ACTION_ISOLATION);
}

TTC_FDIR_Result_t TTC_FDIR_RequestRecovery(void)
{
    if (!LoRa_IsIsolated() && s_radio_ready && !s_radio_faulted &&
        LoRa_IsReady() && LoRa_IsRxActive())
        return TTC_FDIR_RESULT_ALREADY_COMPLETE;
    return TTC_RequestAction(TTC_FDIR_ACTION_RECOVERY);
}

TTC_FDIR_Result_t TTC_FDIR_RequestRxRestart(void)
{
    if (LoRa_IsIsolated() || !s_radio_ready || s_radio_faulted)
        return TTC_FDIR_RESULT_REJECTED;
    return TTC_RequestAction(TTC_FDIR_ACTION_RX_RESTART);
}

TTC_FDIR_Result_t TTC_FDIR_RequestReturnToService(void)
{
    if (!LoRa_IsIsolated() && s_radio_ready && !s_radio_faulted && LoRa_IsRxActive())
        return TTC_FDIR_RESULT_ALREADY_COMPLETE;
    return TTC_RequestAction(TTC_FDIR_ACTION_RETURN_TO_SERVICE);
}

TTC_FDIR_ActionStatus_t TTC_FDIR_GetActionStatus(void)
{
    TTC_SyncFdirHealth();
    return s_action_status;
}
