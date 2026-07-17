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
    LORA_RX_CRC_ERROR
} LoRaStatus_t;

/* Configure SX1276/RFM95W for 868 MHz, SF9, BW 125 kHz and CR 4/5. */
LoRaStatus_t LoRa_Init(void);

/* Send one explicit-header LoRa payload and wait for TxDone. */
LoRaStatus_t LoRa_Send(const uint8_t *data, uint8_t length, uint32_t timeout_ms);

/* Enter continuous receive mode for ground-to-flight commands. */
LoRaStatus_t LoRa_StartReceive(void);

/* Read one received packet, if available. */
LoRaStatus_t LoRa_Receive(uint8_t *data, uint8_t *length, uint8_t capacity);

#endif /* TTC_LORA_DRIVER_H */
