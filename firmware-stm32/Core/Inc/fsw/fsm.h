#ifndef FSW_FSM_H
#define FSW_FSM_H

#include "datapool.h"

/* Six flight phases — values persisted to SCV; do not reorder. */
typedef enum {
    PHASE_STANDBY  = 0,
    PHASE_LAUNCH   = 1,
    PHASE_ASCENT   = 2,
    PHASE_CRUISE   = 3,
    PHASE_DESCENT  = 4,
    PHASE_LANDING  = 5,
} FlightPhase_t;

/* Restore phase from SCV on boot, enable watchdog. */
void FSW_Init(void);

/* Evaluate transition conditions and advance the FSM.
 * Called once per 1 Hz superloop tick after CDH_Update(). */
void FSW_Update(const SensorData_t *dp);

/* Return the current flight phase. */
FlightPhase_t FSW_GetPhase(void);

/* Assemble a telemetry packet from the current datapool and FSM state.
 * Called by TTC before each transmission. */
void FSW_BuildTelemetryPacket(const SensorData_t *dp, TelemetryPacket_t *pkt);

#endif /* FSW_FSM_H */
