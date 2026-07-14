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

static uint8_t TTC_IntervalElapsed(uint32_t now)
{
    return (!s_has_transmitted ||
            (uint32_t)(now - s_last_tx_ms) >= TTC_TELEMETRY_INTERVAL_MS) ? 1U : 0U;
}

void TTC_Init(void)
{
    s_has_transmitted = 0U;
    s_radio_ready = (LoRa_Init() == LORA_OK) ? 1U : 0U;
}

uint8_t TTC_TelemetryDue(void)
{
    return TTC_IntervalElapsed(HAL_GetTick());
}

void TTC_Transmit(const TelemetryPacket_t *pkt)
{
    uint32_t now;

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
        (void)LoRa_Send((const uint8_t *)pkt, (uint8_t)sizeof(*pkt), 5000U);
    }

    s_last_tx_ms = now;
    s_has_transmitted = 1U;
}
