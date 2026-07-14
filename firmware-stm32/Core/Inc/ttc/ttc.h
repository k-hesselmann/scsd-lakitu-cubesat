#ifndef TTC_TTC_H
#define TTC_TTC_H

#include "datapool.h"
#include "ttc/lora_driver.h"

#define TTC_TELEMETRY_INTERVAL_MS  20000U

typedef struct
{
    uint8_t radio_ready;
    LoRaStatus_t last_init_status;
    LoRaStatus_t last_send_status;
    uint8_t last_lora_version;
    uint8_t last_irq_flags;
    uint16_t last_sequence_number;
    uint16_t last_crc16;
    uint8_t last_payload_length;
    uint32_t tx_attempt_count;
    uint32_t tx_success_count;
    uint32_t tx_timeout_count;
    uint32_t tx_error_count;
} TTCDebugStatus_t;

/* Initialise LoRa module (SPI, registers, frequency, SF/BW/CR).
 * Must be called once before the superloop starts. */
void TTC_Init(void);

/* Return 1 when a new telemetry packet should be built and transmitted. */
uint8_t TTC_TelemetryDue(void);

/* Transmit one telemetry packet if the 20 s interval has elapsed.
 * Safe to call every superloop tick — internally rate-limited. */
void TTC_Transmit(const TelemetryPacket_t *pkt);

/* Copy the latest TTC-level telemetry/debug counters. */
void TTC_GetDebugStatus(TTCDebugStatus_t *status);

#endif /* TTC_TTC_H */
