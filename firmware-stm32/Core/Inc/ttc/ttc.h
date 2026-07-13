#ifndef TTC_TTC_H
#define TTC_TTC_H

#include "datapool.h"

#define TTC_TELEMETRY_INTERVAL_MS  20000U

/* Initialise LoRa module (SPI, registers, frequency, SF/BW/CR).
 * Must be called once before the superloop starts. */
void TTC_Init(void);

/* Transmit one telemetry packet if the 20 s interval has elapsed.
 * Safe to call every superloop tick — internally rate-limited. */
void TTC_Transmit(const TelemetryPacket_t *pkt);

/*
 * Stand-alone radio/packet integration test. It sends a deterministic ICD
 * telemetry frame immediately and then once every 20 seconds. It does not
 * return. Select it in main.c with TTC_TELEMETRY_PACKET_TEST.
 */
void TTC_RunTelemetryPacketTest(void);
#endif /* TTC_TTC_H */
