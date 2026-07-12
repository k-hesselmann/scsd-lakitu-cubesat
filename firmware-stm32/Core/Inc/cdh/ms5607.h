#ifndef MS5607_H
#define MS5607_H

#include "main.h"

#define MS5607_ADDR 0x76

#define MS5607_CMD_RESET 0x1E
#define MS5607_CMD_ADC_READ 0x00
#define MS5607_CMD_CONV_D1_OSR256 0x40
#define MS5607_CMD_CONV_D1_OSR512 0x42
#define MS5607_CMD_CONV_D1_OSR1024 0x44
#define MS5607_CMD_CONV_D1_OSR2048 0x46
#define MS5607_CMD_CONV_D1_OSR4096 0x48
#define MS5607_CMD_CONV_D2_OSR256 0x50
#define MS5607_CMD_CONV_D2_OSR512 0x52
#define MS5607_CMD_CONV_D2_OSR1024 0x54
#define MS5607_CMD_CONV_D2_OSR2048 0x56
#define MS5607_CMD_CONV_D2_OSR4096 0x58
#define MS5607_CMD_PROM_RD_BASE 0xA0

typedef struct {
  float pressure;
  float temperature;
  float altitude;
} MS5607_Data;

void MS5607_Init(I2C_HandleTypeDef *hi2c);
MS5607_Data MS5607_Read(I2C_HandleTypeDef *hi2c);
uint8_t MS5607_Check(I2C_HandleTypeDef *hi2c);

#endif
