#include "ttc/ttc.h"
#include "ttc/lora_driver.h"

#include "main.h"
#include "usbd_cdc_if.h"

#include <stdio.h>

_Static_assert(sizeof(TelemetryPacket_t) == 128U, "TelemetryPacket_t must match the raw v3 wire format");

#ifndef TTC_CDC_BINARY_MIRROR
#define TTC_CDC_BINARY_MIRROR 0
#endif

#ifndef TTC_CDC_DEBUG_LOGS
#define TTC_CDC_DEBUG_LOGS 0
#endif

#ifndef TTC_UART_DEBUG_LOGS
#define TTC_UART_DEBUG_LOGS 0
#endif

#ifndef TTC_DEBUG_INTERVAL_MS
#define TTC_DEBUG_INTERVAL_MS 0U
#endif

#ifndef TTC_DEBUG_FIXED_PAYLOAD
#define TTC_DEBUG_FIXED_PAYLOAD 0
#endif

static uint32_t s_last_tx_ms  = 0;
static uint8_t s_has_transmitted = 0U;
static uint8_t s_radio_ready = 0U;
static TTCDebugStatus_t s_debug = {0};

#if TTC_UART_DEBUG_LOGS
extern UART_HandleTypeDef huart2;
#endif

#if TTC_DEBUG_FIXED_PAYLOAD
static const uint8_t s_fixed_payload[] = {
    'T', 'T', 'C', '_', 'T', 'E', 'S', 'T',
    0x01U, 0x02U, 0x03U, 0x04U, 0xA5U, 0x5AU, 0xC3U, 0x3CU
};
#endif

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

static void TTC_SetLoRaFault(uint8_t active)
{
    if (active)
        g_scv.equipment_faults |= EQUIPMENT_LORA;
    else
        g_scv.equipment_faults &= (uint16_t)~EQUIPMENT_LORA;
}

#if TTC_CDC_DEBUG_LOGS || TTC_UART_DEBUG_LOGS
static void TTC_DebugLog(const char *line)
{
    size_t len = 0U;

    while (line[len] != '\0')
        len++;

#if TTC_CDC_DEBUG_LOGS
    (void)CDC_Transmit_FS((uint8_t *)line, (uint16_t)len);
#endif
#if TTC_UART_DEBUG_LOGS
    (void)HAL_UART_Transmit(&huart2, (uint8_t *)line, (uint16_t)len, 100U);
#endif
}

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

void TTC_Init(void)
{
    LoRaStatus_t init_status;
    LoRaDebugStatus_t lora = {0};

    s_has_transmitted = 0U;
    init_status = LoRa_Init();
    s_radio_ready = (init_status == LORA_OK) ? 1U : 0U;

    s_debug.radio_ready = s_radio_ready;
    s_debug.last_init_status = init_status;
    s_debug.last_send_status = LORA_ERROR;
    s_debug.tx_attempt_count = 0U;
    s_debug.tx_success_count = 0U;
    s_debug.tx_timeout_count = 0U;
    s_debug.tx_error_count = 0U;

    LoRa_GetDebugStatus(&lora);
    s_debug.last_lora_version = lora.version;
    s_debug.last_irq_flags = lora.irq_flags;

    TTC_SetLoRaFault(s_radio_ready ? 0U : 1U);
    TTC_LogInitStatus();
}

uint8_t TTC_TelemetryDue(void)
{
    return TTC_IntervalElapsed(HAL_GetTick());
}

void TTC_Transmit(const TelemetryPacket_t *pkt)
{
    uint32_t now;
    LoRaStatus_t send_status = LORA_ERROR;
    LoRaDebugStatus_t lora = {0};
#if TTC_DEBUG_FIXED_PAYLOAD
    const uint8_t *tx_data = s_fixed_payload;
    uint8_t tx_length = (uint8_t)sizeof(s_fixed_payload);
#else
    const uint8_t *tx_data = (const uint8_t *)pkt;
    uint8_t tx_length = (uint8_t)sizeof(*pkt);
#endif

    if (pkt == NULL)
        return;

    now = HAL_GetTick();
    if (!TTC_IntervalElapsed(now))
        return;

#if TTC_CDC_BINARY_MIRROR
    (void)CDC_Transmit_FS((uint8_t *)pkt, (uint16_t)sizeof(*pkt));
#endif

    s_debug.tx_attempt_count++;
    s_debug.last_sequence_number = pkt->sequence_number;
    s_debug.last_crc16 = pkt->crc16;
    s_debug.last_payload_length = tx_length;

    if (s_radio_ready)
    {
        send_status = LoRa_Send(tx_data, tx_length, 5000U);
        s_debug.last_send_status = send_status;

        if (send_status == LORA_OK)
        {
            s_debug.tx_success_count++;
            TTC_SetLoRaFault(0U);
        }
        else
        {
            if (send_status == LORA_TIMEOUT)
                s_debug.tx_timeout_count++;
            else
                s_debug.tx_error_count++;
            TTC_SetLoRaFault(1U);
        }
    }
    else
    {
        s_debug.last_send_status = LORA_ERROR;
        s_debug.tx_error_count++;
        TTC_SetLoRaFault(1U);
    }

    LoRa_GetDebugStatus(&lora);
    s_debug.last_lora_version = lora.version;
    s_debug.last_irq_flags = lora.irq_flags;
    s_debug.radio_ready = s_radio_ready;
    TTC_LogTxStatus();

    s_last_tx_ms = now;
    s_has_transmitted = 1U;
}

void TTC_GetDebugStatus(TTCDebugStatus_t *status)
{
    if (status == NULL)
        return;

    *status = s_debug;
}
