#include "datapool.h"
#include <stdio.h>

extern UART_HandleTypeDef huart2;

Datapool g_datapool = {0};

void Datapool_Init(I2C_HandleTypeDef *hi2c)
{
  g_datapool.test_counter = 0;
  g_datapool.connection_ready = false;
  g_datapool.imu = MPU6050_EquipmentHandler_Init(hi2c);
  g_datapool.altimeter = MS5607_EquipmentHandler_Init(hi2c);
}

void Datapool_Update(I2C_HandleTypeDef *hi2c)
{
  static uint32_t last_time = 0;
  uint32_t current_time = HAL_GetTick();

  g_datapool.imu = MPU6050_EquipmentHandler_Update(g_datapool.imu, hi2c);
  g_datapool.altimeter = MS5607_EquipmentHandler_Update(g_datapool.altimeter, hi2c);

  if (current_time - last_time >= 1000)
  {
    last_time = current_time;
    g_datapool.connection_ready = true;
  }
}

void Datapool_PrintDebug(void)
{
  static uint32_t last_print_time = 0;
  uint32_t current_time = HAL_GetTick();

  if (current_time - last_print_time >= 300)
  {
    last_print_time = current_time;

    uint8_t debug_buffer[256];
    int ax = (int)(g_datapool.imu.data.accel_x * 100);
    int ay = (int)(g_datapool.imu.data.accel_y * 100);
    int az = (int)(g_datapool.imu.data.accel_z * 100);
    int gx = (int)(g_datapool.imu.data.gyro_x * 10);
    int gy = (int)(g_datapool.imu.data.gyro_y * 10);
    int gz = (int)(g_datapool.imu.data.gyro_z * 10);
    int press = (int)(g_datapool.altimeter.data.pressure * 100);
    int temp = (int)(g_datapool.altimeter.data.temperature * 100);
    int alt = (int)(g_datapool.altimeter.data.altitude * 100);

    int len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
      "AX=%d.%02d AY=%d.%02d AZ=%d.%02d | GX=%d.%d GY=%d.%d GZ=%d.%d | P=%d.%02d T=%d.%02d A=%d.%02d | IMU_OK=%d ALT_OK=%d\r\n",
      ax/100, (ax<0 ? -ax : ax)%100,
      ay/100, (ay<0 ? -ay : ay)%100,
      az/100, (az<0 ? -az : az)%100,
      gx/10, (gx<0 ? -gx : gx)%10,
      gy/10, (gy<0 ? -gy : gy)%10,
      gz/10, (gz<0 ? -gz : gz)%10,
      press/100, (press<0 ? -press : press)%100,
      temp/100, (temp<0 ? -temp : temp)%100,
      alt/100, (alt<0 ? -alt : alt)%100,
      !g_datapool.imu.outdated,
      !g_datapool.altimeter.outdated);

    if (len > 0)
    {
      HAL_UART_Transmit(&huart2, debug_buffer, len, 100);
    }
  }
}
