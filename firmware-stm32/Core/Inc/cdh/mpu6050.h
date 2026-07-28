#ifndef MPU6050_H
#define MPU6050_H

#include "main.h"

#define MPU6050_ADDR 0x68

#define MPU6050_PWR_MGMT_1 0x6B
#define MPU6050_GYRO_CONFIG 0x1B
#define MPU6050_ACCEL_CONFIG 0x1C
#define MPU6050_ACCEL_XOUT_H 0x3B
#define MPU6050_GYRO_XOUT_H 0x43
#define MPU6050_WHO_AM_I 0x75

/* ACCEL_CONFIG.AFS_SEL = 1: ±4 g full scale, 8192 LSB/g. Keep the register
 * value and conversion scale together so a future range change cannot leave
 * the decoded units inconsistent with the hardware configuration. */
#define MPU6050_ACCEL_CONFIG_4G 0x08U
#define MPU6050_ACCEL_LSB_PER_G 8192.0f

typedef struct {
  float accel_x;
  float accel_y;
  float accel_z;
  float gyro_x;
  float gyro_y;
  float gyro_z;
  /* 1 only if this sample came from a successful I2C transaction. Callers must
   * check it: a failed read returns a zeroed struct, never stale data. */
  uint8_t valid;
} MPU6050_Data;

void MPU6050_Init(I2C_HandleTypeDef *hi2c);
MPU6050_Data MPU6050_Read(I2C_HandleTypeDef *hi2c);
uint8_t MPU6050_Check(I2C_HandleTypeDef *hi2c);

#endif
