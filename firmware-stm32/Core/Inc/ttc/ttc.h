#ifndef TTC_TTC_H
#define TTC_TTC_H

#include "datapool.h"

#define TTC_TELEMETRY_INTERVAL_MS  20000U

/* Initialise LoRa module (SPI, registers, frequency, SF/BW/CR).
 * Must be called once before the superloop starts. */
void TTC_Init(void);

/* Return 1 when a new telemetry packet should be built and transmitted. */
uint8_t TTC_TelemetryDue(void);

/* Transmit one telemetry packet if the 20 s interval has elapsed.
 * Safe to call every superloop tick — internally rate-limited. */
void TTC_Transmit(const TelemetryPacket_t *pkt);

#endif /* TTC_TTC_H */
