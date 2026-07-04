#include "cdh/mpu6050_equipment_handler.h"
#include <math.h>

static uint8_t MPU6050_IsDataFlatlined(MPU6050_Data current, MPU6050_Data previous)
{
  float ax_diff = fabs(current.accel_x - previous.accel_x);
  float ay_diff = fabs(current.accel_y - previous.accel_y);
  float az_diff = fabs(current.accel_z - previous.accel_z);

  if (ax_diff < MPU6050_FLATLINE_THRESHOLD &&
      ay_diff < MPU6050_FLATLINE_THRESHOLD &&
      az_diff < MPU6050_FLATLINE_THRESHOLD)
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

static void MPU6050_Recover(I2C_HandleTypeDef *hi2c)
{
  MPU6050_Init(hi2c);
  HAL_Delay(100);

  if (!MPU6050_Check(hi2c))
  {
    I2C_BusRestart(hi2c);
    MPU6050_Init(hi2c);
    HAL_Delay(100);
  }
}

MPU6050_EquipmentHandler MPU6050_EquipmentHandler_Init(I2C_HandleTypeDef *hi2c)
{
  MPU6050_EquipmentHandler handler = {0};
  handler.last_good_data_ms = HAL_GetTick();
  handler.last_sensor_check_ms = HAL_GetTick();
  handler.outdated = true;

  if (!MPU6050_Check(hi2c)) {
    return handler;
  }

  MPU6050_Init(hi2c);

  handler.data = MPU6050_Read(hi2c);
  handler.last_good_data_ms = HAL_GetTick();
  handler.last_sensor_check_ms = HAL_GetTick();
  handler.outdated = false;

  return handler;
}

MPU6050_EquipmentHandler MPU6050_EquipmentHandler_Update(MPU6050_EquipmentHandler handler, I2C_HandleTypeDef *hi2c)
{
  uint32_t current_time = HAL_GetTick();

  if (current_time - handler.last_data_read_ms >= MPU6050_DATA_READ_INTERVAL)
  {
    handler.data = MPU6050_Read(hi2c);
    handler.last_data_read_ms = current_time;

    // Detect flatlined data
    if (MPU6050_IsDataFlatlined(handler.data, handler.last_data))
    {
      handler.flatline_count++;
      if (handler.flatline_count >= MPU6050_FLATLINE_COUNT_LIMIT)
      {
        handler.outdated = true;
      }
    }
    else
    {
      handler.flatline_count = 0;
      handler.outdated = false;
      handler.last_good_data_ms = current_time;
    }

    handler.last_data = handler.data;
  }

  // Regular health check
  if (current_time - handler.last_sensor_check_ms >= MPU6050_SENSOR_CHECK_INTERVAL_MS)
  {
    if (!MPU6050_Check(hi2c))
    {
      handler.outdated = true;
      // Try to recover
      MPU6050_Recover(hi2c);
      // Check if recovery worked
      if (MPU6050_Check(hi2c))
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
  if (handler.outdated && (current_time - handler.last_recovery_attempt_ms >= MPU6050_RECOVERY_ATTEMPT_INTERVAL_MS))
  {
    MPU6050_Recover(hi2c);
    if (MPU6050_Check(hi2c))
    {
      handler.outdated = false;
    }
    handler.last_recovery_attempt_ms = current_time;
  }

  return handler;
}
