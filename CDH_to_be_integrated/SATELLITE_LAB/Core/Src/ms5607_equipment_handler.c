#include "ms5607_equipment_handler.h"
#include <math.h>

static uint8_t MS5607_IsDataFlatlined(MS5607_Data current, MS5607_Data previous)
{
  float press_diff = fabs(current.pressure - previous.pressure);
  float temp_diff = fabs(current.temperature - previous.temperature);

  if (press_diff < MS5607_FLATLINE_THRESHOLD &&
      temp_diff < MS5607_FLATLINE_THRESHOLD)
    return 1;

  return 0;
}

static void I2C_BusRestart(I2C_HandleTypeDef *hi2c)
{
  HAL_I2C_DeInit(hi2c);
  HAL_Delay(50);
  HAL_I2C_Init(hi2c);
  HAL_Delay(50);
}

static void MS5607_Recover(I2C_HandleTypeDef *hi2c)
{
  MS5607_Init(hi2c);
  HAL_Delay(100);

  if (!MS5607_Check(hi2c))
  {
    I2C_BusRestart(hi2c);
    MS5607_Init(hi2c);
    HAL_Delay(100);
  }
}

MS5607_EquipmentHandler MS5607_EquipmentHandler_Init(I2C_HandleTypeDef *hi2c)
{
  MS5607_EquipmentHandler handler = {0};
  handler.last_good_data_ms = HAL_GetTick();
  handler.last_sensor_check_ms = HAL_GetTick();
  handler.outdated = true;

  if (!MS5607_Check(hi2c)) {
    return handler;
  }

  MS5607_Init(hi2c);

  handler.data = MS5607_Read(hi2c);
  handler.last_good_data_ms = HAL_GetTick();
  handler.last_sensor_check_ms = HAL_GetTick();
  handler.outdated = false;

  return handler;
}

MS5607_EquipmentHandler MS5607_EquipmentHandler_Update(MS5607_EquipmentHandler handler, I2C_HandleTypeDef *hi2c)
{
  uint32_t current_time = HAL_GetTick();

  if (current_time - handler.last_data_read_ms >= MS5607_DATA_READ_INTERVAL)
  {
    handler.data = MS5607_Read(hi2c);
    handler.last_good_data_ms = current_time;
    handler.last_data_read_ms = current_time;
    handler.outdated = false;
  }

  // Regular health check with recovery
  if (current_time - handler.last_sensor_check_ms >= MS5607_SENSOR_CHECK_INTERVAL_MS)
  {
    if (!MS5607_Check(hi2c))
    {
      handler.outdated = true;
      // Try to recover
      MS5607_Recover(hi2c);
      // Check if recovery worked
      if (MS5607_Check(hi2c))
      {
        handler.outdated = false;
      }
    }
    else
    {
      handler.outdated = false;
    }
    handler.last_sensor_check_ms = current_time;
  }

  // Periodic recovery attempt every 5s if sensor is outdated
  if (handler.outdated && (current_time - handler.last_recovery_attempt_ms >= MS5607_RECOVERY_ATTEMPT_INTERVAL_MS))
  {
    MS5607_Recover(hi2c);
    if (MS5607_Check(hi2c))
    {
      handler.outdated = false;
    }
    handler.last_recovery_attempt_ms = current_time;
  }

  return handler;
}
