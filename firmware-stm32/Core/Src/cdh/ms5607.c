#include "cdh/ms5607.h"
#include <math.h>

static uint16_t calib[8];
/* Cleared until a PROM read succeeds. Without valid coefficients every
 * pressure/temperature result is nonsense, so reads must refuse to run. */
static uint8_t s_calib_valid = 0;

static uint8_t MS5607_WriteCmd(I2C_HandleTypeDef *hi2c, uint8_t cmd)
{
  return HAL_I2C_Master_Transmit(hi2c, MS5607_ADDR << 1, &cmd, 1, 100) == HAL_OK;
}

/* Returns 1 and stores the 24-bit conversion on success, 0 on bus error.
 * The return codes must be checked: buf is a local at the same stack address
 * every call, so ignoring them would silently re-decode the PREVIOUS
 * conversion and pass a duplicate reading off as fresh. */
static uint8_t MS5607_ReadADC(I2C_HandleTypeDef *hi2c, uint32_t *out)
{
  uint8_t buf[3] = {0};
  uint8_t reg = MS5607_CMD_ADC_READ;

  if (HAL_I2C_Master_Transmit(hi2c, MS5607_ADDR << 1, &reg, 1, 100) != HAL_OK)
    return 0;
  if (HAL_I2C_Master_Receive(hi2c, MS5607_ADDR << 1, buf, 3, 100) != HAL_OK)
    return 0;

  *out = ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2];
  return 1;
}

static uint8_t MS5607_ReadPROM(I2C_HandleTypeDef *hi2c)
{
  for (int i = 0; i < 8; i++)
  {
    uint8_t reg = MS5607_CMD_PROM_RD_BASE + (i << 1);
    uint8_t buf[2] = {0};

    if (HAL_I2C_Master_Transmit(hi2c, MS5607_ADDR << 1, &reg, 1, 100) != HAL_OK)
      return 0;
    if (HAL_I2C_Master_Receive(hi2c, MS5607_ADDR << 1, buf, 2, 100) != HAL_OK)
      return 0;

    calib[i] = ((uint16_t)buf[0] << 8) | buf[1];
  }

  /* A blank (0x0000) or floating (0xFFFF) PROM reads "successfully" but gives
   * garbage pressure; reject it rather than fly on it. */
  if ((calib[1] == 0x0000U && calib[2] == 0x0000U) ||
      (calib[1] == 0xFFFFU && calib[2] == 0xFFFFU))
    return 0;

  return 1;
}

uint8_t MS5607_Check(I2C_HandleTypeDef *hi2c)
{
  /* This used to discard the probe result and unconditionally return 1, so the
   * barometer could never be reported missing no matter what the bus did. */
  return HAL_I2C_IsDeviceReady(hi2c, MS5607_ADDR << 1, 3, 100) == HAL_OK;
}

void MS5607_Init(I2C_HandleTypeDef *hi2c)
{
  s_calib_valid = 0;

  if (!MS5607_WriteCmd(hi2c, MS5607_CMD_RESET))
    return;

  HAL_Delay(100);
  s_calib_valid = MS5607_ReadPROM(hi2c);
}

MS5607_Data MS5607_Read(I2C_HandleTypeDef *hi2c)
{
  MS5607_Data data = {0};
  uint32_t D1 = 0, D2 = 0;
  int32_t dT;
  int64_t OFF, SENS, T, OFF2, SENS2, T2;

  /* No usable calibration -> no usable result. */
  if (!s_calib_valid)
    return data;   /* data.valid == 0 */

  /* Step 1: Read raw pressure (D1) and temperature (D2). Every transaction is
   * checked; any failure aborts the conversion with valid = 0 rather than
   * publishing a stale or half-updated sample. */
  if (!MS5607_WriteCmd(hi2c, MS5607_CMD_CONV_D1_OSR4096))
    return data;
  HAL_Delay(10);
  if (!MS5607_ReadADC(hi2c, &D1))
    return data;

  if (!MS5607_WriteCmd(hi2c, MS5607_CMD_CONV_D2_OSR4096))
    return data;
  HAL_Delay(10);
  if (!MS5607_ReadADC(hi2c, &D2))
    return data;

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

  data.valid = 1;
  return data;
}
