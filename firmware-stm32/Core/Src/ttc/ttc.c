#include "ttc/ttc.h"
#include "ttc/lora_driver.h"

#include "main.h"

_Static_assert(sizeof(TelemetryPacket_t) == 128U, "TelemetryPacket_t must match the raw v3 wire format");

static uint32_t s_last_tx_ms  = 0;
static uint8_t s_has_transmitted = 0U;
static uint8_t s_radio_ready = 0U;

void TTC_Init(void)
{
    s_has_transmitted = 0U;
    s_radio_ready = (LoRa_Init() == LORA_OK) ? 1U : 0U;
}

void TTC_Transmit(const TelemetryPacket_t *pkt)
{
    uint32_t now;

    if (pkt == NULL || !s_radio_ready)
        return;

    now = HAL_GetTick();
    if (s_has_transmitted &&
        (uint32_t)(now - s_last_tx_ms) < TTC_TELEMETRY_INTERVAL_MS)
        return;

    if (LoRa_Send((const uint8_t *)pkt, (uint8_t)sizeof(*pkt), 5000U) == LORA_OK)
    {
        s_last_tx_ms = now;
        s_has_transmitted = 1U;
    }
}
