#ifndef MPU6050_EQUIPMENT_HANDLER_H
#define MPU6050_EQUIPMENT_HANDLER_H

#include "main.h"
#include "mpu6050.h"
#include <stdbool.h>

#define MPU6050_DATA_READ_INTERVAL 100
#define MPU6050_SENSOR_CHECK_INTERVAL_MS 2000
#define MPU6050_RECOVERY_ATTEMPT_INTERVAL_MS 5000
#define MPU6050_FLATLINE_THRESHOLD 0.01f
#define MPU6050_FLATLINE_COUNT_LIMIT 10

typedef struct {
  MPU6050_Data data;
  MPU6050_Data last_data;
  bool outdated;
  uint32_t last_good_data_ms;
  uint32_t last_sensor_check_ms;
  uint32_t last_data_read_ms;
  uint32_t last_recovery_attempt_ms;
  uint32_t flatline_count;
} MPU6050_EquipmentHandler;

MPU6050_EquipmentHandler MPU6050_EquipmentHandler_Init(I2C_HandleTypeDef *hi2c);
MPU6050_EquipmentHandler MPU6050_EquipmentHandler_Update(MPU6050_EquipmentHandler handler, I2C_HandleTypeDef *hi2c);

#endif
