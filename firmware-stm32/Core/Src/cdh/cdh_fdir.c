#include "cdh/cdh_fdir.h"
#include "cdh/mpu6050.h"
#include "cdh/ms5607.h"

/* I2C1 pins (see HAL_I2C_MspInit, stm32l4xx_hal_msp.c): PB8=SCL, PB9=SDA,
 * both AF_OD. Hardcoded here rather than generalised since this driver is
 * already I2C1-specific (hi2c1 is the only bus CDH owns). */
#define CDH_FDIR_I2C1_GPIO_PORT   GPIOB
#define CDH_FDIR_I2C1_SCL_PIN     GPIO_PIN_8
#define CDH_FDIR_I2C1_SDA_PIN     GPIO_PIN_9

/* FMECA C7: DeInit/Init alone cannot release a slave holding SDA low
 * mid-transaction -- DeInit floats the pins, but a slave waiting for more
 * clocks to finish its current byte just keeps waiting. Bit-bang up to 9 SCL
 * pulses (worst case: a slave part-way through clocking out one byte) with
 * SDA held as an input only, so the slave can finish and release it; stop
 * early once SDA reads high. Standard I2C bus-recovery procedure (NXP
 * UM10204 Sec 3.1.16). Runs between HAL_I2C_DeInit and the eventual
 * HAL_I2C_Init below, while the peripheral itself is not driving the bus. */
static void CDH_FDIR_BusClear_Bitbang(void)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Mode = GPIO_MODE_OUTPUT_OD;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Pin = CDH_FDIR_I2C1_SCL_PIN;
  HAL_GPIO_Init(CDH_FDIR_I2C1_GPIO_PORT, &gpio);
  HAL_GPIO_WritePin(CDH_FDIR_I2C1_GPIO_PORT, CDH_FDIR_I2C1_SCL_PIN, GPIO_PIN_SET);

  gpio.Mode = GPIO_MODE_INPUT;   /* never drive SDA, only read it */
  gpio.Pin = CDH_FDIR_I2C1_SDA_PIN;
  HAL_GPIO_Init(CDH_FDIR_I2C1_GPIO_PORT, &gpio);

  for (uint8_t i = 0U; i < 9U; i++)
  {
    if (HAL_GPIO_ReadPin(CDH_FDIR_I2C1_GPIO_PORT, CDH_FDIR_I2C1_SDA_PIN) == GPIO_PIN_SET)
      break;   /* slave already released SDA -- bus is clear */

    HAL_GPIO_WritePin(CDH_FDIR_I2C1_GPIO_PORT, CDH_FDIR_I2C1_SCL_PIN, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(CDH_FDIR_I2C1_GPIO_PORT, CDH_FDIR_I2C1_SCL_PIN, GPIO_PIN_SET);
    HAL_Delay(1);
  }

  /* HAL_I2C_Init(), called once CDH_FDIR_BUS_RESTART_HOLD elapses, redrives
   * both pins back to AF_OD I2C1 mode -- nothing further to restore here. */
}

void CDH_FDIR_Init(CDH_FDIR_Context *fdir)
{
  fdir->bus_state = CDH_FDIR_BUS_IDLE;
  fdir->bus_restart_start_ms = 0;
}

void CDH_FDIR_RecoverIMU(I2C_HandleTypeDef *hi2c)
{
  MPU6050_Init(hi2c);
}

void CDH_FDIR_RecoverBaro(I2C_HandleTypeDef *hi2c)
{
  MS5607_Init(hi2c);
}

void CDH_FDIR_BusRestart_Start(CDH_FDIR_Context *fdir)
{
  if (fdir->bus_state == CDH_FDIR_BUS_IDLE)
  {
    fdir->bus_state = CDH_FDIR_BUS_RESTART_IN_PROGRESS;
    fdir->bus_restart_start_ms = HAL_GetTick();
  }
}

void CDH_FDIR_BusRestart_Process(CDH_FDIR_Context *fdir, I2C_HandleTypeDef *hi2c)
{
  uint32_t current_time = HAL_GetTick();
  uint32_t elapsed = current_time - fdir->bus_restart_start_ms;

  if (fdir->bus_state == CDH_FDIR_BUS_RESTART_IN_PROGRESS)
  {
    HAL_I2C_DeInit(hi2c);
    CDH_FDIR_BusClear_Bitbang();
    fdir->bus_state = CDH_FDIR_BUS_RESTART_HOLD;
    fdir->bus_restart_start_ms = current_time;
  }
  else if (fdir->bus_state == CDH_FDIR_BUS_RESTART_HOLD)
  {
    if (elapsed >= CDH_FDIR_BUS_RESTART_HOLD_TIME_MS)
    {
      HAL_I2C_Init(hi2c);
      fdir->bus_state = CDH_FDIR_BUS_IDLE;
    }
  }
}
