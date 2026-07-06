#include "cdh/cdh_fdir.h"
#include "cdh/mpu6050.h"
#include "cdh/ms5607.h"

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
