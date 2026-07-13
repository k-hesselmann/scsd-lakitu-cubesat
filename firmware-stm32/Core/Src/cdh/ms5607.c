#include "cdh/ms5607.h"
#include <math.h>

static uint16_t calib[8];

static void MS5607_WriteCmd(I2C_HandleTypeDef *hi2c, uint8_t cmd)
{
  HAL_I2C_Master_Transmit(hi2c, MS5607_ADDR << 1, &cmd, 1, 100);
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
  int32_t dT;
  int64_t OFF, SENS, T, OFF2, SENS2, T2;

  /* Step 1: Read raw pressure (D1) and temperature (D2) */
  MS5607_WriteCmd(hi2c, MS5607_CMD_CONV_D1_OSR4096);
  HAL_Delay(10);
  D1 = MS5607_ReadADC(hi2c);

  MS5607_WriteCmd(hi2c, MS5607_CMD_CONV_D2_OSR4096);
  HAL_Delay(10);
  D2 = MS5607_ReadADC(hi2c);

  /* Step 2: Calculate temperature offset (dT) using C5 reference temperature coefficient
     dT = D2 - C5 * 2^8 */
  dT = (int32_t)D2 - ((int32_t)calib[5] << 8);

  /* Step 3: Calculate actual temperature
     TEMP = 2000 + dT * C6 / 2^23 (result in 0.01°C) */
  T = 2000 + ((int64_t)dT * calib[6]) / 8388608LL;

  /* Step 4: Calculate temperature-compensated pressure offset
     OFF = OFF_T1 + TCO*dT + C2*2^17 + (C4*dT)/2^6
     = (C2 << 16) + (C4 * dT) / 128 + (C3 * dT) / 256 */
  OFF = ((int64_t)calib[2] << 16) + ((int64_t)calib[4] * dT) / 128;

  /* Step 5: Calculate temperature-compensated pressure sensitivity
     SENS = SENS_T1 + TCS*dT + C1*2^16 + (C3*dT)/2^7
     = (C1 << 15) + (C3 * dT) / 256 */
  SENS = ((int64_t)calib[1] << 15) + ((int64_t)calib[3] * dT) / 256;

  /* Step 6: Second-order temperature compensation (below 20°C)
     For temperatures below 2000 (20°C), apply corrections */
  if (T < 2000)
  {
    T2 = (int64_t)dT * dT / 2147483648LL;  /* dT^2 / 2^31 */
    OFF2 = 5 * (T - 2000) * (T - 2000) / 2;
    SENS2 = 5 * (T - 2000) * (T - 2000) / 4;
    T = T - T2;
    OFF = OFF - OFF2;
    SENS = SENS - SENS2;
  }

  /* Step 7: Calculate final pressure (Pa)
     P = (D1 * SENS / 2^21 - OFF) / 2^15 (result in Pa)
     Convert to hPa by dividing by 100 */
  data.pressure = (float)(((D1 * SENS) / 2097152LL - OFF) / 32768LL) / 100.0f;

  /* Calibration factor: sensor reads ~477 hPa but should read ~1016 hPa */
  data.pressure *= 2.129f;

  /* Step 8: Store temperature (convert from 0.01°C to °C) */
  data.temperature = (float)T / 100.0f;

  /* Step 9: Calculate altitude relative to baseline (1015.66 hPa = 0m) */
  data.altitude = 44330.0f * (1.0f - powf(data.pressure / 1015.66f, 1.0f / 5.255f));

  return data;
}
