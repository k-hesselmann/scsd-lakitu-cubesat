#ifndef INC_SD_SPI_H_
#define INC_SD_SPI_H_

#include "main.h"
#include <stdint.h>

/* SD card chip-select line (SPI2 uses soft NSS). The pin is not defined in
 * the CubeMX .ioc, so declare it here; SD_SPI_Init() configures it as a
 * push-pull output. If SPI2/CS is ever added in CubeMX, move these to main.h. */
#ifndef SD_CS_Pin
#define SD_CS_Pin        GPIO_PIN_1
#define SD_CS_GPIO_Port  GPIOB
#endif

uint8_t SD_SPI_Init(void);
uint8_t SD_SPI_ReadBlocks(uint8_t *buff, uint32_t sector, uint32_t count);
uint8_t SD_SPI_WriteBlocks(const uint8_t *buff, uint32_t sector, uint32_t count);
uint8_t SD_SPI_Sync(void);
uint8_t SD_SPI_GetSectorCount(uint32_t *sector_count);

typedef struct
{
    uint8_t initialized;
    uint8_t card_type;
    uint8_t last_command;
    uint8_t last_response;
    uint8_t last_rx;
    uint32_t sector_count;
    uint32_t init_attempts;
    uint32_t spi_error_count;
    uint32_t hal_error_last;
    uint32_t hal_state_last;
    uint32_t ready_timeout_count;
    uint32_t data_token_timeout_count;
    uint32_t write_reject_count;
    uint32_t read_operations;
    uint32_t read_failures;
    uint32_t write_operations;
    uint32_t write_failures;
    uint32_t sync_operations;
    uint32_t sync_failures;
    uint32_t last_sector;
    uint32_t last_sector_count;
    uint32_t last_failure_ms;
} SD_SPIDiagnostics_t;

void SD_SPI_GetDiagnostics(SD_SPIDiagnostics_t *diagnostics);
const char *SD_SPI_GetCardTypeName(void);

#endif
