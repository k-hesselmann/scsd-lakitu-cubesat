#include "ms5607.h"
#include <math.h>

static uint16_t calib[8];

static void MS5607_WriteCmd(I2C_HandleTypeDef *hi2c, uint8_t cmd)
{
  HAL_I2C_Master_Transmit(hi2c, MS5607_ADDR << 1, &cmd, 1, 100);
}

static uint8_t MS5607_ReadReg(I2C_HandleTypeDef *hi2c, uint8_t reg)
{
  uint8_t data;
  HAL_I2C_Master_Transmit(hi2c, MS5607_ADDR << 1, &reg, 1, 100);
  HAL_I2C_Master_Receive(hi2c, MS5607_ADDR << 1, &data, 1, 100);
  return data;
}

static uint32_t MS5607_ReadADC(I2C_HandleTypeDef *hi2c)
{
  uint8_t buf[3];
  uint8_t reg = MS5607_CMD_ADC_READ;
  HAL_I2C_Master_Transmit(hi2c, MS5607_ADDR << 1, &reg, 1, 100);
  HAL_I2C_Master_Receive(hi2c, MS5607_ADDR << 1, buf, 3, 100);
  return ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2];
}

static void MS5607_ReadPROM(I2C_HandleTypeDef *hi2c)
{
  for (int i = 0; i < 8; i++)
  {
    uint8_t reg = MS5607_CMD_PROM_RD_BASE + (i << 1);
    uint8_t buf[2];
    HAL_I2C_Master_Transmit(hi2c, MS5607_ADDR << 1, &reg, 1, 100);
    HAL_I2C_Master_Receive(hi2c, MS5607_ADDR << 1, buf, 2, 100);
    calib[i] = ((uint16_t)buf[0] << 8) | buf[1];
  }
}

uint8_t MS5607_Check(I2C_HandleTypeDef *hi2c)
{
  HAL_I2C_IsDeviceReady(hi2c, MS5607_ADDR << 1, 3, 100);
  return 1;
}

void MS5607_Init(I2C_HandleTypeDef *hi2c)
{
  MS5607_WriteCmd(hi2c, MS5607_CMD_RESET);
  HAL_Delay(100);
  MS5607_ReadPROM(hi2c);
}

MS5607_Data MS5607_Read(I2C_HandleTypeDef *hi2c)
{
  MS5607_Data data = {0};
  uint32_t D1, D2;
  int32_t dT, T2, OFF2, SENS2;
  int64_t OFF, SENS, T;

  MS5607_WriteCmd(hi2c, MS5607_CMD_CONV_D1_OSR4096);
  HAL_Delay(10);
  D1 = MS5607_ReadADC(hi2c);

  MS5607_WriteCmd(hi2c, MS5607_CMD_CONV_D2_OSR4096);
  HAL_Delay(10);
  D2 = MS5607_ReadADC(hi2c);

  dT = D2 - ((int32_t)calib[5] << 8);
  T = 2000 + ((int64_t)dT * calib[6]) / 8388608LL;
  OFF = ((int64_t)calib[2] << 16) + ((int64_t)dT * calib[4]) / 128;
  SENS = ((int64_t)calib[1] << 15) + ((int64_t)dT * calib[3]) / 256;

  if (T < 2000)
  {
    T2 = (dT * dT) / (1LL << 31);
    OFF2 = 5 * ((T - 2000) * (T - 2000)) / 2;
    SENS2 = 5 * ((T - 2000) * (T - 2000)) / 4;
    T = T - T2;
    OFF = OFF - OFF2;
    SENS = SENS - SENS2;
  }

  data.pressure = (float)(((D1 * SENS) / 2097152LL - OFF) / 32768LL) / 100.0f;
  data.temperature = (float)T / 100.0f;
  data.altitude = 44330.0f * (1.0f - powf(data.pressure / 1013.25f, 1.0f / 5.255f));

  return data;
}
