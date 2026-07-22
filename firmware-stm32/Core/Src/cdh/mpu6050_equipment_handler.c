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
  handler.imu_valid = 0;

  if (!MPU6050_Check(hi2c)) {
    return handler;
  }

  MPU6050_Init(hi2c);

  handler.data = MPU6050_Read(hi2c);
  handler.last_data = handler.data;
  handler.last_good_data_ms = HAL_GetTick();
  /* Only claim validity if that first read actually succeeded. */
  handler.imu_valid = handler.data.valid;

  return handler;
}

MPU6050_EquipmentHandler MPU6050_EquipmentHandler_Update(MPU6050_EquipmentHandler handler, I2C_HandleTypeDef *hi2c)
{
  uint32_t current_time = HAL_GetTick();

  if (current_time - handler.last_data_read_ms >= MPU6050_DATA_READ_INTERVAL)
  {
    MPU6050_Data sample = MPU6050_Read(hi2c);
    handler.last_data_read_ms = current_time;

    if (!sample.valid)
    {
      /* The bus read failed. Keep the last good sample for reference but do
       * not publish it as fresh, and do not feed it to the flatline detector
       * (comparing the old sample against itself would look like a stuck
       * sensor and confuse the two failure modes). */
      handler.read_fault_count++;
      if (handler.read_fault_count >= MPU6050_READ_FAULT_LIMIT)
      {
        handler.imu_valid = 0;
      }
    }
    else
    {
      handler.read_fault_count = 0;
      handler.data = sample;

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
  }

  /* No periodic WHO_AM_I check here by design. It is redundant now that the
   * read path reports I2C failures directly: any fault it could detect (the
   * device not answering) is already caught by read_fault_count, six times
   * faster (0.3 s vs 2 s). It also never caught the failure actually seen in
   * flight -- the MPU-6050 keeps answering WHO_AM_I while its bulk reads fail,
   * which is exactly what masked the fault before. Dropping it removes an
   * extra transaction from a bus already suspected of contention, and removes
   * a path where one collided ID read could falsely invalidate a healthy IMU.
   * Identity is still verified once, at Init. */

  return handler;
}
