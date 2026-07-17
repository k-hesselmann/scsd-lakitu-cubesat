#ifndef TTC_LORA_DRIVER_H
#define TTC_LORA_DRIVER_H

#include <stdint.h>

typedef enum
{
    LORA_OK = 0,
    LORA_ERROR,
    LORA_TIMEOUT,
    LORA_LENGTH_ERROR
} LoRaStatus_t;

/* Configure SX1276/RFM95W for 868 MHz, SF9, BW 125 kHz and CR 4/5. */
LoRaStatus_t LoRa_Init(void);

/* Send one explicit-header LoRa payload and wait for TxDone. */
LoRaStatus_t LoRa_Send(const uint8_t *data, uint8_t length, uint32_t timeout_ms);

#endif /* TTC_LORA_DRIVER_H */
