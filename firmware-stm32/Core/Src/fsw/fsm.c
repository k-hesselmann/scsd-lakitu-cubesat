#include "fsw/fsm.h"
#include "fsw/fsm_thresholds.h"

static FlightPhase_t s_phase = PHASE_STANDBY;

void FSW_Init(void)
{
    /* TODO: read s_phase from g_scv.flight_phase to restore after reboot */
    /* TODO: enable IWDG */
}

void FSW_Update(const SensorData_t *dp)
{
    /* TODO: run median filter on sensor inputs */
    /* TODO: evaluate transition conditions per fsm_diagram.puml */
    /* TODO: on transition — update s_phase, write to g_scv, persist to NVM */
    (void)dp;
}

FlightPhase_t FSW_GetPhase(void)
{
    return s_phase;
}

void FSW_BuildTelemetryPacket(const SensorData_t *dp, TelemetryPacket_t *pkt)
{
    /* TODO: fill all TelemetryPacket_t fields from dp and s_phase */
    /* TODO: compute CRC-16 over packet bytes */
    (void)dp;
    (void)pkt;
}
