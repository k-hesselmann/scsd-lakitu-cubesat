#ifndef MPU6050_EQUIPMENT_HANDLER_H
#define MPU6050_EQUIPMENT_HANDLER_H

#include "main.h"
#include "cdh/mpu6050.h"
#include <stdbool.h>

#define MPU6050_DATA_READ_INTERVAL 100
/* Consecutive failed I2C reads before the IMU is declared invalid. At the
 * 100 ms read interval this is a ~0.3 s outage, short enough to catch bus
 * trouble but long enough to ride out a single collided transaction. */
#define MPU6050_READ_FAULT_LIMIT 3

typedef struct {
  MPU6050_Data data;
  uint8_t imu_valid;
  uint32_t last_good_data_ms;
  uint32_t last_data_read_ms;
  /* Consecutive failed I2C reads, reset by any successful read. */
  uint32_t read_fault_count;
} MPU6050_EquipmentHandler;

MPU6050_EquipmentHandler MPU6050_EquipmentHandler_Init(I2C_HandleTypeDef *hi2c);
MPU6050_EquipmentHandler MPU6050_EquipmentHandler_Update(MPU6050_EquipmentHandler handler, I2C_HandleTypeDef *hi2c);

#endif
