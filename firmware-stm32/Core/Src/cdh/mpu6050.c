#include "cdh/mpu6050.h"

static void MPU6050_WriteReg(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t data)
{
  HAL_I2C_Mem_Write(hi2c, MPU6050_ADDR << 1, reg, I2C_MEMADD_SIZE_8BIT, &data, 1, 100);
}

/* Returns 1 and stores the register value on success, 0 if the I2C read
 * failed (in which case *out is untouched). */
static uint8_t MPU6050_ReadReg(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t *out)
{
  return HAL_I2C_Mem_Read(hi2c, MPU6050_ADDR << 1, reg, I2C_MEMADD_SIZE_8BIT,
                          out, 1, 100) == HAL_OK;
}

uint8_t MPU6050_Check(I2C_HandleTypeDef *hi2c)
{
  uint8_t check = 0;

  if (!MPU6050_ReadReg(hi2c, MPU6050_WHO_AM_I, &check))
    return 0;   /* bus error: cannot claim the device is present */

  return (check == 0x68);
}

void MPU6050_Init(I2C_HandleTypeDef *hi2c)
{
  MPU6050_WriteReg(hi2c, MPU6050_PWR_MGMT_1, 0x00);
  HAL_Delay(500);

  MPU6050_WriteReg(hi2c, MPU6050_GYRO_CONFIG, 0x00);
  HAL_Delay(50);
  MPU6050_WriteReg(hi2c, MPU6050_ACCEL_CONFIG, MPU6050_ACCEL_CONFIG_4G);
  HAL_Delay(500);
}

MPU6050_Data MPU6050_Read(I2C_HandleTypeDef *hi2c)
{
  uint8_t buf[14] = {0};
  uint8_t reg = MPU6050_ACCEL_XOUT_H;
  MPU6050_Data data = {0};

  /* Both transactions must be checked. Previously their return codes were
   * discarded, so a failed receive left buf holding the PREVIOUS read's bytes
   * (same stack address every call) and this function silently returned a
   * bit-identical duplicate sample -- indistinguishable from a healthy sensor
   * that simply had not moved. Return valid = 0 instead and let the caller
   * decide. */
  if (HAL_I2C_Master_Transmit(hi2c, MPU6050_ADDR << 1, &reg, 1, 1000) != HAL_OK)
    return data;   /* data.valid == 0 */

  if (HAL_I2C_Master_Receive(hi2c, MPU6050_ADDR << 1, buf, 14, 1000) != HAL_OK)
    return data;   /* data.valid == 0 */

  int16_t raw_accel_x = (int16_t)((buf[0] << 8) | buf[1]);
  int16_t raw_accel_y = (int16_t)((buf[2] << 8) | buf[3]);
  int16_t raw_accel_z = (int16_t)((buf[4] << 8) | buf[5]);

  int16_t raw_gyro_x = (int16_t)((buf[8] << 8) | buf[9]);
  int16_t raw_gyro_y = (int16_t)((buf[10] << 8) | buf[11]);
  int16_t raw_gyro_z = (int16_t)((buf[12] << 8) | buf[13]);

  data.accel_x = raw_accel_x / MPU6050_ACCEL_LSB_PER_G;
  data.accel_y = raw_accel_y / MPU6050_ACCEL_LSB_PER_G;
  data.accel_z = raw_accel_z / MPU6050_ACCEL_LSB_PER_G;

  data.gyro_x = raw_gyro_x / 131.0f;
  data.gyro_y = raw_gyro_y / 131.0f;
  data.gyro_z = raw_gyro_z / 131.0f;

  data.valid = 1;
  return data;
}
