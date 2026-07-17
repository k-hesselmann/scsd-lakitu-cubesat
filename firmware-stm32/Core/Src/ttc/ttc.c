#include "ttc/ttc.h"
#include "ttc/lora_driver.h"

#include "main.h"
#include "usbd_cdc_if.h"

#include <string.h>

_Static_assert(sizeof(TelemetryPacket_t) == 155U,
               "TelemetryPacket_t must match the raw v7 wire format");

#ifndef TTC_CDC_BINARY_MIRROR
#define TTC_CDC_BINARY_MIRROR 0
#endif

#define TTC_TX_TIMEOUT_MS             5000U
#define TTC_ACK_TIMEOUT_MS            5000U
#define TTC_MAX_TX_ATTEMPTS              3U
#define TTC_FAILURE_LIMIT                3U
#define TTC_RECOVERY_BACKOFF_MS       60000U
#define TTC_UPLINK_MAX_LENGTH            64U
#define TTC_COMMAND_REPLAY_WINDOW_SIZE   16U

static uint32_t s_last_tx_ms;
static uint32_t s_last_recovery_ms;
static uint32_t s_pending_last_attempt_ms;
static uint8_t s_has_transmitted;
static uint8_t s_has_recovery_timestamp;
static uint8_t s_radio_ready;
static uint8_t s_immediate_telemetry;
static uint8_t s_pending_valid;
static uint8_t s_pending_attempts;
static TelemetryPacket_t s_pending_packet;
static LoRaHealth_t s_health;
static UplinkState_t s_uplink;
static uint16_t s_command_high_water;
static uint16_t s_command_seen_mask;
static uint8_t s_has_command_high_water;

static void TTC_SetLoRaFault(uint8_t active)
{
    if (active)
        g_scv.equipment_faults |= EQUIPMENT_LORA;
    else
        g_scv.equipment_faults &= (uint16_t)~EQUIPMENT_LORA;
}

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

static uint8_t TTC_IntervalElapsed(uint32_t now)
{
    return (!s_has_transmitted ||
            (uint32_t)(now - s_last_tx_ms) >= TTC_TELEMETRY_INTERVAL_MS) ? 1U : 0U;
}

static uint8_t TTC_RecoveryDue(uint32_t now)
{
    return (!s_has_recovery_timestamp ||
            (uint32_t)(now - s_last_recovery_ms) >= TTC_RECOVERY_BACKOFF_MS) ? 1U : 0U;
}

static void TTC_RecordRxModeResult(LoRaStatus_t status)
{
    if (status == LORA_OK)
    {
        s_health.rx_mode_active = 1U;
        s_health.last_rx_status = LORA_RX_HEALTH_ACTIVE;
        return;
    }

    s_health.rx_mode_active = 0U;
    if (status == LORA_CONFIG_ERROR)
    {
        s_health.last_event = LORA_EVENT_RX_MODE_FAIL;
        s_health.last_rx_status = LORA_RX_HEALTH_MODE_ERROR;
    }
    else
    {
        s_health.last_event = LORA_EVENT_RX_SPI_FAIL;
        s_health.last_rx_status = LORA_RX_HEALTH_SPI_ERROR;
    }
    TTC_IncrementU8(&s_health.consecutive_failures);
    TTC_SetLoRaFault(1U);
}

static void TTC_RecordInitResult(LoRaStatus_t status, uint8_t recovery)
{
    if (recovery)
    {
        TTC_IncrementU8(&s_health.recovery_count);
        s_last_recovery_ms = HAL_GetTick();
        s_has_recovery_timestamp = 1U;
    }

    s_radio_ready = (status == LORA_OK) ? 1U : 0U;
    if (s_radio_ready)
        s_health.last_event = LORA_EVENT_INIT_OK;
    else if (status == LORA_CONFIG_ERROR)
        s_health.last_event = LORA_EVENT_CONFIG_FAIL;
    else
        s_health.last_event = LORA_EVENT_INIT_FAIL;

    if (s_radio_ready)
    {
        s_health.rx_mode_active = 1U;
        s_health.last_rx_status = LORA_RX_HEALTH_ACTIVE;
    }
    else
    {
        s_health.rx_mode_active = 0U;
        s_health.last_rx_status = (status == LORA_CONFIG_ERROR) ?
                                  LORA_RX_HEALTH_MODE_ERROR : LORA_RX_HEALTH_SPI_ERROR;
        TTC_IncrementU8(&s_health.consecutive_failures);
    }

    /* Recovery is considered complete only after a later successful packet or
     * TxDone proves that the modem is operational. */
    if (s_radio_ready && !recovery)
        TTC_SetLoRaFault(0U);
    else if (!s_radio_ready || recovery)
        TTC_SetLoRaFault(1U);
}

static void TTC_InitialiseRadio(uint8_t recovery)
{
    LoRaStatus_t status = LoRa_Init();

    if (status == LORA_OK)
        status = LoRa_StartReceive();

    TTC_RecordInitResult(status, recovery);
}

static void TTC_AttemptRecovery(void)
{
    TTC_InitialiseRadio(1U);
}

static void TTC_RecordTxFailure(LoRaStatus_t status)
{
    TTC_SetLoRaFault(1U);

    if (status == LORA_LENGTH_ERROR)
    {
        s_health.last_event = LORA_EVENT_TX_BAD_LENGTH;
        return;
    }

    s_health.last_event = (status == LORA_TIMEOUT) ?
                          LORA_EVENT_TX_TIMEOUT : LORA_EVENT_TX_SPI_FAIL;
    TTC_IncrementU8(&s_health.consecutive_failures);
}

static void TTC_RecordRxPacket(void)
{
    s_health.rx_mode_active = 1U;
    s_health.last_rx_status = LORA_RX_HEALTH_PACKET_OK;
    s_health.last_event = LORA_EVENT_RX_OK;
    s_health.last_rx_ms = HAL_GetTick();
    s_health.rx_packet_count = TTC_IncrementU16(s_health.rx_packet_count);
    s_health.consecutive_failures = 0U;
    TTC_SetLoRaFault(0U);
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

    s_radio_ready = 0U;
    TTC_RecordRxModeResult(status);
}

static LoRaStatus_t TTC_RestartReceive(void)
{
    LoRaStatus_t status = LoRa_StartReceive();

    TTC_RecordRxModeResult(status);
    if (status != LORA_OK)
        s_radio_ready = 0U;

    return status;
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
    s_uplink.last_command = UPLINK_COMMAND_ACKNOWLEDGE;
    s_uplink.last_command_id = 0U;

    if (s_pending_valid && acknowledged_sequence == s_pending_packet.sequence_number)
    {
        s_uplink.last_status = UPLINK_STATUS_ACCEPTED;
        s_uplink.last_ack_sequence = acknowledged_sequence;
        s_pending_valid = 0U;
        s_pending_attempts = 0U;
    }
    else if (acknowledged_sequence == s_uplink.last_ack_sequence)
    {
        s_uplink.last_status = UPLINK_STATUS_DUPLICATE;
    }
    else
    {
        s_uplink.last_status = UPLINK_STATUS_UNEXPECTED_ACK;
    }

    /* Do not transmit an immediate ACK-of-ACK packet; that would create an
     * acknowledgement loop. The result is piggybacked on later telemetry. */
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
                s_uplink.last_status = UPLINK_STATUS_DUPLICATE;
            }
            else
            {
                s_uplink.last_status = UPLINK_STATUS_ACCEPTED;
                s_uplink.command_count = TTC_IncrementU16(s_uplink.command_count);
            }
            TTC_RequestTelemetry();
            return;
        }

        s_uplink.last_command = UPLINK_COMMAND_NONE;
        s_uplink.last_command_id = value;
        s_uplink.last_status = UPLINK_STATUS_UNSUPPORTED;
        TTC_RequestTelemetry();
        return;
    }

    s_uplink.last_command = UPLINK_COMMAND_NONE;
    s_uplink.last_command_id = 0U;
    s_uplink.last_status = (length >= 4U &&
                            (memcmp(data, "ACK,", 4U) == 0 ||
                             memcmp(data, "CMD,", 4U) == 0)) ?
                           UPLINK_STATUS_INVALID_FORMAT : UPLINK_STATUS_UNSUPPORTED;
    TTC_RequestTelemetry();
}

static void TTC_SendPending(uint32_t now)
{
    LoRaStatus_t send_status;

    if (!s_pending_valid)
        return;

    s_pending_last_attempt_ms = now;
    if (!s_radio_ready)
    {
        s_health.last_event = LORA_EVENT_NOT_READY;
        TTC_SetLoRaFault(1U);
        return;
    }

    TTC_IncrementU8(&s_pending_attempts);
    send_status = LoRa_Send((const uint8_t *)&s_pending_packet,
                            (uint8_t)sizeof(s_pending_packet), TTC_TX_TIMEOUT_MS);
    if (send_status == LORA_OK)
    {
        s_health.last_event = LORA_EVENT_TX_OK;
        s_health.consecutive_failures = 0U;
        s_health.last_success_ms = HAL_GetTick();
        TTC_SetLoRaFault(0U);
    }
    else
    {
        TTC_RecordTxFailure(send_status);
        if (s_health.consecutive_failures >= TTC_FAILURE_LIMIT && TTC_RecoveryDue(now))
            TTC_AttemptRecovery();
    }

    if (s_radio_ready)
        (void)TTC_RestartReceive();

    s_last_tx_ms = now;
    s_has_transmitted = 1U;
}

static void TTC_ServicePendingRetry(uint32_t now)
{
    if (!s_pending_valid ||
        (uint32_t)(now - s_pending_last_attempt_ms) < TTC_ACK_TIMEOUT_MS)
        return;

    if (s_pending_attempts >= TTC_MAX_TX_ATTEMPTS)
    {
        /* Record the end-to-end delivery failure before releasing stop-and-wait. */
        s_health.last_event = LORA_EVENT_ACK_TIMEOUT;
        s_health.ack_timeout_count = TTC_IncrementU16(s_health.ack_timeout_count);
        s_pending_valid = 0U;
        s_pending_attempts = 0U;
        return;
    }

    TTC_SendPending(now);
}

void TTC_Init(void)
{
    s_last_tx_ms = 0U;
    s_last_recovery_ms = 0U;
    s_pending_last_attempt_ms = 0U;
    s_has_transmitted = 0U;
    s_has_recovery_timestamp = 0U;
    s_radio_ready = 0U;
    s_immediate_telemetry = 0U;
    s_pending_valid = 0U;
    s_pending_attempts = 0U;
    memset(&s_pending_packet, 0, sizeof(s_pending_packet));
    memset(&s_health, 0, sizeof(s_health));
    memset(&s_uplink, 0, sizeof(s_uplink));
    s_command_high_water = 0U;
    s_command_seen_mask = 0U;
    s_has_command_high_water = 0U;

    TTC_InitialiseRadio(0U);
    if (!s_radio_ready)
    {
        s_last_recovery_ms = HAL_GetTick();
        s_has_recovery_timestamp = 1U;
    }
}

void TTC_Service(void)
{
    uint8_t packet[TTC_UPLINK_MAX_LENGTH];
    uint8_t length = 0U;
    LoRaStatus_t status;
    uint32_t now = HAL_GetTick();

    if (!s_radio_ready)
    {
        if (TTC_RecoveryDue(now))
            TTC_AttemptRecovery();
    }
    else
    {
        status = LoRa_Receive(packet, &length, sizeof(packet));
        if (status == LORA_OK)
        {
            TTC_RecordRxPacket();
            TTC_ProcessUplink(packet, length);
        }
        else if (status != LORA_NO_PACKET)
        {
            TTC_RecordRxError(status);
            if (!s_radio_ready && TTC_RecoveryDue(now))
                TTC_AttemptRecovery();
        }
    }

    TTC_ServicePendingRetry(now);
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

void TTC_Transmit(const TelemetryPacket_t *pkt)
{
    uint32_t now;

    if (pkt == NULL || s_pending_valid)
        return;

    now = HAL_GetTick();
    if (!s_immediate_telemetry && !TTC_IntervalElapsed(now))
        return;

#if TTC_CDC_BINARY_MIRROR
    (void)CDC_Transmit_FS((uint8_t *)pkt, (uint16_t)sizeof(*pkt));
#endif

    s_pending_packet = *pkt;
    s_pending_valid = 1U;
    s_pending_attempts = 0U;
    s_pending_last_attempt_ms = now;
    s_immediate_telemetry = 0U;
    TTC_SendPending(now);
}