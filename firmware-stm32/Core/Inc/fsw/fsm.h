#ifndef FSW_FSM_H
#define FSW_FSM_H

#include "datapool.h"

/* Six flight phases — numeric values are part of the SCV interface; do not reorder. */
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
 * Called from the superloop after CDH_Update(), currently every
 * LOOP_CDH_FSW_PERIOD_MS (10 Hz — see main.c), not 1 Hz. Transition
 * debouncing (FSW_ConditionHeld) is elapsed-time based, so correctness does
 * not depend on the call rate; only threshold rationale comments written
 * assuming 1 Hz need re-checking if the rate changes again. */
void FSW_Update(const SensorData_t *dp);

/* Return the current flight phase. */
FlightPhase_t FSW_GetPhase(void);

#endif /* FSW_FSM_H */
