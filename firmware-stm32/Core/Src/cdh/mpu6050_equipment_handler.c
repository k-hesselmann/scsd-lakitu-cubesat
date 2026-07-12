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

MPU6050_EquipmentHandler MPU6050_EquipmentHandler_Init(I2C_HandleTypeDef *hi2c)
{
  MPU6050_EquipmentHandler handler = {0};
  handler.last_good_data_ms = HAL_GetTick();
  handler.last_sensor_check_ms = HAL_GetTick();
  handler.imu_valid = 0;

  if (!MPU6050_Check(hi2c)) {
    return handler;
  }

  MPU6050_Init(hi2c);

  handler.data = MPU6050_Read(hi2c);
  handler.last_good_data_ms = HAL_GetTick();
  handler.last_sensor_check_ms = HAL_GetTick();
  handler.imu_valid = 1;

  return handler;
}

MPU6050_EquipmentHandler MPU6050_EquipmentHandler_Update(MPU6050_EquipmentHandler handler, I2C_HandleTypeDef *hi2c)
{
  uint32_t current_time = HAL_GetTick();

  if (current_time - handler.last_data_read_ms >= MPU6050_DATA_READ_INTERVAL)
  {
    handler.data = MPU6050_Read(hi2c);
    handler.last_data_read_ms = current_time;

    if (MPU6050_IsDataFlatlined(handler.data, handler.last_data))
    {
      handler.flatline_count++;
      if (handler.flatline_count >= MPU6050_FLATLINE_COUNT_LIMIT)
      {
        handler.imu_valid = 0;
      }
    }
    else
    {
      handler.flatline_count = 0;
      handler.imu_valid = 1;
      handler.last_good_data_ms = current_time;
    }

    handler.last_data = handler.data;
  }

  if (current_time - handler.last_sensor_check_ms >= MPU6050_SENSOR_CHECK_INTERVAL_MS)
  {
    if (!MPU6050_Check(hi2c))
    {
      handler.imu_valid = 0;
    }
    else
    {
      handler.imu_valid = 1;
    }
    handler.last_sensor_check_ms = current_time;
  }

  return handler;
}
