#ifndef TTC_TTC_H
#define TTC_TTC_H

#include "datapool.h"

/* Initialise LoRa module (SPI, registers, frequency, SF/BW/CR).
 * Must be called once before the superloop starts. */
void TTC_Init(void);

/* Transmit one telemetry packet if the 20 s interval has elapsed.
 * Safe to call every superloop tick — internally rate-limited. */
void TTC_Transmit(const TelemetryPacket_t *pkt);

#endif /* TTC_TTC_H */
