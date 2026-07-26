#ifndef TTC_TTC_H
#define TTC_TTC_H

#include "datapool.h"
#include "ttc/lora_driver.h"

#define TTC_TELEMETRY_INTERVAL_MS  10000U

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

/* Initialise the TTC state machine. Hardware work is queued, not awaited. */
void TTC_Init(void);

/* Advance TTC and LoRa state machines. Called every superloop iteration so
 * queued SPI/register actions and FDIR recovery requests make progress. */
void TTC_Service(void);

/* Queue telemetry without waiting for the normal interval. */
void TTC_RequestTelemetry(void);

/* Return 1 when a new telemetry packet should be built and queued. */
uint8_t TTC_TelemetryDue(void);

/* Assemble a protocol-v8 telemetry packet (with CRC) from the current
 * datapool and SCV state. Call before TTC_Transmit(). */
void TTC_BuildTelemetryPacket(const SensorData_t *dp, const SCV_t *scv,
                              TelemetryPacket_t *pkt);

/* Queue one telemetry packet. It never waits for the modem or elapsed time. */
void TTC_Transmit(const TelemetryPacket_t *pkt);

/* Volatile health and independently latched command/ACK state consumed by the
 * compact protocol-v8 packet builder. */
const LoRaHealth_t *TTC_GetHealth(void);
const UplinkState_t *TTC_GetUplinkState(void);
void TTC_GetDebugStatus(TTCDebugStatus_t *status);

/* Raw TTC observations for FDIR. These fields are not SCV fields and TTC never
 * writes g_scv; FDIR owns EQUIPMENT_LORA, thresholds and cooldowns. */
typedef struct
{
    uint8_t radio_ready;
    uint8_t rx_active;
    uint8_t radio_busy;
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
