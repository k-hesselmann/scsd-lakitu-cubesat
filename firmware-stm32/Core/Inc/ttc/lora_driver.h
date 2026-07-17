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

/* Queue SX1276/RFM95W configuration for 868 MHz, SF9, BW 125 kHz and CR 4/5.
 * Call LoRa_Service() until LoRa_IsBusy() becomes zero, then inspect
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
LoRaState_t LoRa_GetState(void);
LoRaStatus_t LoRa_GetLastStatus(void);

/* Hold the modem in reset and cancel future driver activity. LORA_BUSY means
 * an in-flight SPI transfer is being cancelled asynchronously; call
 * LoRa_Service() until LoRa_IsBusy() is zero and inspect the last status.
 * Isolation is intentionally explicit and never an automatic fault policy. */
LoRaStatus_t LoRa_Isolate(void);

#endif /* TTC_LORA_DRIVER_H */
