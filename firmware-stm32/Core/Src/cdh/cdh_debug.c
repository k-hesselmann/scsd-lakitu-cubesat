#include "cdh/cdh_debug.h"
#include "debug_log.h"
#include <stdio.h>

extern UART_HandleTypeDef huart2;

#define CDH_DEBUG_INTERVAL_MS 10000U

void CDH_Debug_PrintGPS(GPS_EquipmentHandler *gps)
{
  static uint32_t last_print_time = 0;
  uint32_t current_time = HAL_GetTick();

  if (current_time - last_print_time >= CDH_DEBUG_INTERVAL_MS)
  {
    last_print_time = current_time;

    uint8_t debug_buffer[256];
    int lat = (int)(gps->data.latitude * 1000000);
    int lon = (int)(gps->data.longitude * 1000000);
    int alt = (int)(gps->data.altitude * 100);
    int speed = (int)(gps->data.speed * 100);

    int len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
      "LAT=%d.%06d LON=%d.%06d ALT=%d.%02d SPEED=%d.%02d SAT=%d FIX=%d GPS_OK=%d\r\n",
      lat/1000000, (lat < 0 ? -lat : lat) % 1000000,
      lon/1000000, (lon < 0 ? -lon : lon) % 1000000,
      alt/100, (alt < 0 ? -alt : alt) % 100,
      speed/100, (speed < 0 ? -speed : speed) % 100,
      gps->data.num_satellites,
      gps->data.fix_type,
      gps->gps_valid);

    if (len > 0)
    {
      DebugLog_WriteN(debug_buffer, len);
    }

    /* Signal Status Check */
    uint8_t status_buffer[128];
    if (gps->data.latitude == 0.0f && gps->data.longitude == 0.0f &&
        gps->data.num_satellites == 0 && gps->data.fix_type == 0)
    {
      int status_len = snprintf((char*)status_buffer, sizeof(status_buffer),
        "   [FAIL] GPS: NOT COMMUNICATING (check wiring/baudrate)\r\n");
      DebugLog_WriteN(status_buffer, status_len);
    }
    else if (gps->data.num_satellites == 0)
    {
      int status_len = snprintf((char*)status_buffer, sizeof(status_buffer),
        "   [OK] GPS: COMMUNICATING but NO SIGNAL (move outdoors)\r\n");
      DebugLog_WriteN(status_buffer, status_len);
    }
    else if (gps->gps_valid)
    {
      int status_len = snprintf((char*)status_buffer, sizeof(status_buffer),
        "   [OK] GPS: FIX ACQUIRED (%d satellites)\r\n", gps->data.num_satellites);
      DebugLog_WriteN(status_buffer, status_len);
    }
  }
}

void CDH_Debug_PrintBaro(MS5607_EquipmentHandler *baro)
{
  static uint32_t last_print_time = 0;
  uint32_t current_time = HAL_GetTick();

  if (current_time - last_print_time >= CDH_DEBUG_INTERVAL_MS)
  {
    last_print_time = current_time;

    uint8_t debug_buffer[256];
    int press = (int)(baro->data.pressure * 100);
    int temp = (int)(baro->data.temperature * 100);
    int alt = (int)(baro->data.altitude * 100);

    int len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
      "PRESS=%d.%02d TEMP=%d.%02d ALT=%d.%02d BARO_OK=%d\r\n",
      press/100, (press<0 ? -press : press)%100,
      temp/100, (temp<0 ? -temp : temp)%100,
      alt/100, (alt<0 ? -alt : alt)%100,
      baro->baro_valid);

    if (len > 0)
    {
      DebugLog_WriteN(debug_buffer, len);
    }
  }
}

void CDH_Debug_PrintDatapool(SensorData_t *dp)
{
  static uint32_t last_print_time = 0;
  uint32_t current_time = HAL_GetTick();

  if (current_time - last_print_time >= CDH_DEBUG_INTERVAL_MS)
  {
    last_print_time = current_time;

    uint8_t debug_buffer[512];
    int lat = (int)(dp->gps_lat_deg * 1000000);
    int lon = (int)(dp->gps_lon_deg * 1000000);
    int alt = (int)(dp->gps_alt_m * 100);
    int speed = (int)(dp->gps_speed_mps * 100);
    int vel_d = (int)(dp->gps_vel_down_mps * 100);
    int heading = (int)(dp->gps_heading_deg * 100);

    int len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
      "GPS_POOL: LAT=%d.%06d LON=%d.%06d ALT=%d.%02d SPEED=%d.%02d "
      "VD=%d.%02d HDG=%d.%02d SAT=%d FIX=%d VALID=%d\r\n",
      lat/1000000, (lat < 0 ? -lat : lat) % 1000000,
      lon/1000000, (lon < 0 ? -lon : lon) % 1000000,
      alt/100, (alt < 0 ? -alt : alt) % 100,
      speed/100, (speed < 0 ? -speed : speed) % 100,
      vel_d/100, (vel_d < 0 ? -vel_d : vel_d) % 100,
      heading/100, (heading < 0 ? -heading : heading) % 100,
      dp->gps_num_satellites,
      dp->gps_fix_type,
      dp->gps_valid);

    if (len > 0)
    {
      DebugLog_WriteN(debug_buffer, len);
    }

    /* IMU Debug Output */
    int ax = (int)(dp->imu_accel_x_g * 1000);
    int ay = (int)(dp->imu_accel_y_g * 1000);
    int az = (int)(dp->imu_accel_z_g * 1000);
    int gx = (int)(dp->imu_gyro_x_dps * 100);
    int gy = (int)(dp->imu_gyro_y_dps * 100);
    int gz = (int)(dp->imu_gyro_z_dps * 100);

    len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
      "IMU_POOL: AX=%d.%03d AY=%d.%03d AZ=%d.%03d GX=%d.%02d GY=%d.%02d GZ=%d.%02d VALID=%d\r\n",
      ax/1000, (ax < 0 ? -ax : ax) % 1000,
      ay/1000, (ay < 0 ? -ay : ay) % 1000,
      az/1000, (az < 0 ? -az : az) % 1000,
      gx/100, (gx < 0 ? -gx : gx) % 100,
      gy/100, (gy < 0 ? -gy : gy) % 100,
      gz/100, (gz < 0 ? -gz : gz) % 100,
      dp->imu_valid);

    if (len > 0)
    {
      DebugLog_WriteN(debug_buffer, len);
    }
  }
}
