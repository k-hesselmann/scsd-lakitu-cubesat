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

typedef struct {
  float accel_x;
  float accel_y;
  float accel_z;
  float gyro_x;
  float gyro_y;
  float gyro_z;
} MPU6050_Data;

void MPU6050_Init(I2C_HandleTypeDef *hi2c);
MPU6050_Data MPU6050_Read(I2C_HandleTypeDef *hi2c);
uint8_t MPU6050_Check(I2C_HandleTypeDef *hi2c);

#endif
