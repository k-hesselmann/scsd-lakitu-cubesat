#ifndef TTC_TTC_H
#define TTC_TTC_H

#include "datapool.h"

#define TTC_TELEMETRY_INTERVAL_MS  20000U

/* Initialise the TTC state machine. Hardware work is queued, not awaited. */
void TTC_Init(void);

/* Advance TTC and LoRa state machines. Integration TODO: the main-loop owner
 * should call this much more often than the current one-second cadence. */
void TTC_Service(void);

/* Queue telemetry without waiting for the normal interval. */
void TTC_RequestTelemetry(void);

/* Return 1 when a new telemetry packet should be built and queued. */
uint8_t TTC_TelemetryDue(void);

/* Queue one telemetry packet. It never waits for the modem or elapsed time. */
void TTC_Transmit(const TelemetryPacket_t *pkt);

/* Existing volatile telemetry snapshots. Their layout is intentionally
 * unchanged for protocol-v7 compatibility. */
const LoRaHealth_t *TTC_GetHealth(void);
const UplinkState_t *TTC_GetUplinkState(void);

/* Raw TTC observations for the future FDIR owner. These fields are not SCV
 * fields and TTC never writes g_scv. TODO(FDIR): derive SCV counters, own
 * EQUIPMENT_LORA, thresholds and cooldowns, then invoke the request APIs. */
typedef struct
{
    uint8_t radio_ready;
    uint8_t rx_active;
    uint8_t consecutive_tx_failures;
    uint8_t recovery_in_progress;
    uint8_t isolation_active;
    uint16_t lora_tx_fault_counter;
    uint16_t nack_counter;
    uint32_t last_tx_success_ms;
    uint32_t last_rx_success_ms;
} TTC_FDIR_Health_t;

typedef enum
{
    TTC_FDIR_RESULT_ACCEPTED = 0,
    TTC_FDIR_RESULT_ALREADY_COMPLETE,
    TTC_FDIR_RESULT_IN_PROGRESS,
    TTC_FDIR_RESULT_REJECTED
} TTC_FDIR_Result_t;

typedef enum
{
    TTC_FDIR_ACTION_NONE = 0,
    TTC_FDIR_ACTION_ISOLATION,
    TTC_FDIR_ACTION_RECOVERY,
    TTC_FDIR_ACTION_RX_RESTART,
    TTC_FDIR_ACTION_RETURN_TO_SERVICE
} TTC_FDIR_Action_t;

typedef enum
{
    TTC_FDIR_ACTION_IDLE = 0,
    TTC_FDIR_ACTION_PENDING,
    TTC_FDIR_ACTION_IN_PROGRESS,
    TTC_FDIR_ACTION_SUCCEEDED,
    TTC_FDIR_ACTION_FAILED
} TTC_FDIR_ActionState_t;

typedef struct
{
    TTC_FDIR_Action_t action;
    TTC_FDIR_ActionState_t state;
    uint8_t last_lora_status; /* LoRaStatus_t, kept byte-sized for TTC ABI. */
} TTC_FDIR_ActionStatus_t;

void TTC_FDIR_GetHealth(TTC_FDIR_Health_t *health);
TTC_FDIR_Result_t TTC_FDIR_RequestIsolation(void);
TTC_FDIR_Result_t TTC_FDIR_RequestRecovery(void);
TTC_FDIR_Result_t TTC_FDIR_RequestRxRestart(void);
TTC_FDIR_Result_t TTC_FDIR_RequestReturnToService(void);
TTC_FDIR_ActionStatus_t TTC_FDIR_GetActionStatus(void);

#endif /* TTC_TTC_H */
