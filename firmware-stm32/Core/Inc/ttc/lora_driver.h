#ifndef TTC_LORA_DRIVER_H
#define TTC_LORA_DRIVER_H

#include <stdint.h>

typedef enum
{
    LORA_OK = 0,
    LORA_SPI_ERROR,
    LORA_TIMEOUT,
    LORA_LENGTH_ERROR,
    LORA_VERSION_MISMATCH,
    LORA_CONFIG_ERROR,
    LORA_NO_PACKET,
    LORA_RX_CRC_ERROR,
    LORA_BUSY,
    LORA_NOT_READY,
    LORA_ISOLATED
} LoRaStatus_t;

typedef enum
{
    LORA_STATE_IDLE = 0,
    LORA_STATE_INITIALISING,
    LORA_STATE_TRANSMITTING,
    LORA_STATE_STARTING_RX,
    LORA_STATE_ISOLATING,
    LORA_STATE_ISOLATED,
    LORA_STATE_FAULT
} LoRaState_t;

typedef enum
{
    LORA_PHASE_NONE = 0,
    LORA_PHASE_INIT,
    LORA_PHASE_TX_SETUP,
    LORA_PHASE_TX_POLL,
    LORA_PHASE_TX_FINISH,
    LORA_PHASE_RX_START,
    LORA_PHASE_RX_POLL,
    LORA_PHASE_RX_PAYLOAD,
    LORA_PHASE_ISOLATION,
    LORA_PHASE_SPI_REINIT,
    LORA_PHASE_DEBUG_READ
} LoRaOperationPhase_t;

typedef enum
{
    LORA_SPI_FAIL_START_REJECTED = 0,
    LORA_SPI_FAIL_IRQ_ERROR,
    LORA_SPI_FAIL_TRANSFER_TIMEOUT,
    LORA_SPI_FAIL_ABORT_REJECTED,
    LORA_SPI_FAIL_ABORT_TIMEOUT,
    LORA_SPI_FAIL_DEINIT,
    LORA_SPI_FAIL_REINIT,
    LORA_SPI_FAIL_VERIFY
} LoRaSpiFailureCause_t;

typedef struct
{
    uint32_t occurrence;
    uint32_t timestamp_ms;
    uint8_t lora_state;
    uint8_t phase;
    uint8_t register_address;
    uint8_t operation;
    uint8_t cause;
    uint8_t hal_status;
    uint32_t hal_error;
    uint8_t spi_active;
    uint8_t action_waiting;
    uint8_t abort_pending;
    uint8_t irq_flags;
    uint8_t tx_done_seen;
} LoRaFaultTrace_t;

typedef struct
{
    uint32_t spi_transfer_count;
    uint32_t spi_completion_count;
    uint32_t spi_irq_error_count;
    uint32_t spi_timeout_count;
    uint32_t spi_abort_count;
    uint32_t tx_irq_poll_count;
    uint32_t rx_irq_poll_count;
    uint32_t dropped_fault_trace_count;
} LoRaRuntimeStats_t;

typedef struct
{
    LoRaStatus_t last_status;
    uint8_t last_failed_reg;
    uint8_t last_failed_op;
    uint32_t last_hal_status;
    uint32_t spi_error_code;
    uint8_t version;
    uint8_t op_mode;
    uint8_t irq_flags;
    uint8_t frf_msb;
    uint8_t frf_mid;
    uint8_t frf_lsb;
    uint8_t modem_config_1;
    uint8_t modem_config_2;
    uint8_t modem_config_3;
    uint8_t sync_word;
    uint8_t payload_length;
} LoRaDebugStatus_t;

/* Queue the normal SX1276/RFM95W profile: 869.525 MHz, SF8, BW 125 kHz,
 * CR 4/5, 17 dBm PA_BOOST, explicit header and payload CRC. Call
 * LoRa_Service() until LoRa_IsBusy() becomes zero, then inspect
 * LoRa_GetLastStatus(). */
LoRaStatus_t LoRa_Init(void);

/* Queue one explicit-header LoRa payload. Completion is reported through
 * LoRa_IsBusy() and LoRa_GetLastStatus(); this function never waits for TxDone. */
LoRaStatus_t LoRa_Send(const uint8_t *data, uint8_t length, uint32_t timeout_ms);

/* Queue entry into continuous receive mode. */
LoRaStatus_t LoRa_StartReceive(void);

/* Advance at most one asynchronous SPI/register step. This driver owns SPI1
 * interrupt completion handling; the caller must invoke this frequently. */
void LoRa_Service(void);

/* Retrieve one completed RX observation (packet, CRC error, or length error).
 * LORA_NO_PACKET means that no new observation is waiting. */
LoRaStatus_t LoRa_Receive(uint8_t *data, uint8_t *length, uint8_t capacity);

uint8_t LoRa_IsBusy(void);
uint8_t LoRa_IsReady(void);
uint8_t LoRa_IsRxActive(void);
uint8_t LoRa_IsIsolated(void);
/* True once the SX1276 TxDone IRQ has been observed for the most recent send,
 * even if a later IRQ-clear or standby write failed. */
uint8_t LoRa_WasLastTxOnAir(void);
LoRaState_t LoRa_GetState(void);
LoRaStatus_t LoRa_GetLastStatus(void);
void LoRa_GetDebugStatus(LoRaDebugStatus_t *status);
LoRaStatus_t LoRa_ReadDebugRegisters(void);
uint8_t LoRa_TakeFaultTrace(LoRaFaultTrace_t *trace);
void LoRa_GetRuntimeStats(LoRaRuntimeStats_t *stats);

/* Hold the modem in reset and cancel future driver activity. LORA_BUSY means
 * an in-flight SPI transfer is being cancelled asynchronously; call
 * LoRa_Service() until LoRa_IsBusy() is zero and inspect the last status.
 * Isolation is intentionally explicit and never an automatic fault policy. */
LoRaStatus_t LoRa_Isolate(void);

#endif /* TTC_LORA_DRIVER_H */
