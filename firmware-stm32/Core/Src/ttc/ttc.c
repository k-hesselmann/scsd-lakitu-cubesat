#include "ttc/ttc.h"
#include "ttc/lora_driver.h"

#include "main.h"
#include "usbd_cdc_if.h"

_Static_assert(sizeof(TelemetryPacket_t) == 128U, "TelemetryPacket_t must match the raw v3 wire format");

#ifndef TTC_CDC_BINARY_MIRROR
#define TTC_CDC_BINARY_MIRROR 0
#endif

static uint32_t s_last_tx_ms  = 0;
static uint8_t s_has_transmitted = 0U;
static uint8_t s_radio_ready = 0U;

static void TTC_SetLoRaFault(uint8_t active)
{
    if (active)
        g_scv.equipment_faults |= EQUIPMENT_LORA;
    else
        g_scv.equipment_faults &= (uint16_t)~EQUIPMENT_LORA;
}

static uint8_t TTC_IntervalElapsed(uint32_t now)
{
    return (!s_has_transmitted ||
            (uint32_t)(now - s_last_tx_ms) >= TTC_TELEMETRY_INTERVAL_MS) ? 1U : 0U;
}

void TTC_Init(void)
{
    LoRaStatus_t init_status;

    s_has_transmitted = 0U;
    init_status = LoRa_Init();
    s_radio_ready = (init_status == LORA_OK) ? 1U : 0U;
    TTC_SetLoRaFault(s_radio_ready ? 0U : 1U);
}

uint8_t TTC_TelemetryDue(void)
{
    return TTC_IntervalElapsed(HAL_GetTick());
}

void TTC_Transmit(const TelemetryPacket_t *pkt)
{
    uint32_t now;
    LoRaStatus_t send_status;

    if (pkt == NULL)
        return;

    now = HAL_GetTick();
    if (!TTC_IntervalElapsed(now))
        return;

#if TTC_CDC_BINARY_MIRROR
    (void)CDC_Transmit_FS((uint8_t *)pkt, (uint16_t)sizeof(*pkt));
#endif

    if (s_radio_ready)
    {
        send_status = LoRa_Send((const uint8_t *)pkt, (uint8_t)sizeof(*pkt), 5000U);
        TTC_SetLoRaFault((send_status == LORA_OK) ? 0U : 1U);
    }
    else
    {
        TTC_SetLoRaFault(1U);
    }

    s_last_tx_ms = now;
    s_has_transmitted = 1U;
}
