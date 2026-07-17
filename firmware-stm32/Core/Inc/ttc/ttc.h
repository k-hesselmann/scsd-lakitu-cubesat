#ifndef TTC_TTC_H
#define TTC_TTC_H

#include "datapool.h"

#define TTC_TELEMETRY_INTERVAL_MS  20000U

/* Initialise the LoRa modem once at boot. */
void TTC_Init(void);

/* Run bounded modem recovery when the radio is faulted. Call every superloop tick. */
void TTC_Service(void);

/* Queue telemetry without waiting for the normal interval. */
void TTC_RequestTelemetry(void);

/* Return 1 when a new telemetry packet should be built and transmitted. */
uint8_t TTC_TelemetryDue(void);

/* Transmit one telemetry packet if the interval has elapsed. */
void TTC_Transmit(const TelemetryPacket_t *pkt);

/* Current volatile LoRa FDIR snapshot for telemetry formation. */
const LoRaHealth_t *TTC_GetHealth(void);

/* Current ground-to-flight command/acknowledgement snapshot. */
const UplinkState_t *TTC_GetUplinkState(void);

#endif /* TTC_TTC_H */
