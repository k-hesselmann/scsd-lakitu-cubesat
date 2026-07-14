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

/* Configure SX1276/RFM95W for 868 MHz, SF9, BW 125 kHz and CR 4/5. */
LoRaStatus_t LoRa_Init(void);

/* Send one explicit-header LoRa payload and wait for TxDone. */
LoRaStatus_t LoRa_Send(const uint8_t *data, uint8_t length, uint32_t timeout_ms);

/* Copy the latest SPI/register diagnostics captured by the driver. */
void LoRa_GetDebugStatus(LoRaDebugStatus_t *status);

/* Refresh a small register snapshot for debug reporting. */
LoRaStatus_t LoRa_ReadDebugRegisters(void);

#endif /* TTC_LORA_DRIVER_H */
