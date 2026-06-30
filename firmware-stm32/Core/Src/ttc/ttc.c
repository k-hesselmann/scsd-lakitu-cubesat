#include "ttc/ttc.h"

#define TTC_TX_INTERVAL_MS  20000U   /* 20 s per FR-022 */

static uint32_t s_last_tx_ms  = 0;
static uint16_t s_sequence    = 0;

void TTC_Init(void)
{
    /* TODO: initialise RFM95W over SPI */
    /* TODO: configure SF9, BW 125 kHz, CR 4/5, 868 MHz (FR-026) */
    /* TODO: set TX power to legal limit +14 dBm ERP (CR-005) */
}

void TTC_Transmit(const TelemetryPacket_t *pkt)
{
    /* TODO: replace 0 with HAL_GetTick() */
    uint32_t now = 0;
    if ((now - s_last_tx_ms) < TTC_TX_INTERVAL_MS) return;

    /* TODO: transmit pkt over LoRa SPI */
    /* TODO: increment s_sequence (written into pkt by FSW_BuildTelemetryPacket) */

    s_last_tx_ms = now;
    s_sequence++;
    (void)pkt;
}
