#ifndef INC_SD_SPI_H_
#define INC_SD_SPI_H_

#include "main.h"
#include <stdint.h>

/* SD card chip-select line (SPI1 uses soft NSS). The pin is not defined in
 * the CubeMX .ioc, so declare it here; SD_SPI_Init() configures it as a
 * push-pull output. If SPI1/CS is ever added in CubeMX, move these to main.h. */
#ifndef SD_CS_Pin
#define SD_CS_Pin        GPIO_PIN_6
#define SD_CS_GPIO_Port  GPIOB
#endif

uint8_t SD_SPI_Init(void);
uint8_t SD_SPI_ReadBlocks(uint8_t *buff, uint32_t sector, uint32_t count);
uint8_t SD_SPI_WriteBlocks(const uint8_t *buff, uint32_t sector, uint32_t count);
uint8_t SD_SPI_Sync(void);

#endif
