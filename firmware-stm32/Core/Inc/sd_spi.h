#ifndef INC_SD_SPI_H_
#define INC_SD_SPI_H_

#include "main.h"
#include <stdint.h>

uint8_t SD_SPI_Init(void);
uint8_t SD_SPI_ReadBlocks(uint8_t *buff, uint32_t sector, uint32_t count);
uint8_t SD_SPI_WriteBlocks(const uint8_t *buff, uint32_t sector, uint32_t count);
uint8_t SD_SPI_Sync(void);

#endif
