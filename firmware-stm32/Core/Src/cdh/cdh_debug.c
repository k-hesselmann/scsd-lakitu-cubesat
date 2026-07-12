#include "cdh/cdh_debug.h"
#include <stdio.h>

extern UART_HandleTypeDef huart2;

void CDH_Debug_PrintGPS(GPS_EquipmentHandler *gps)
{
  static uint32_t last_print_time = 0;
  uint32_t current_time = HAL_GetTick();

  if (current_time - last_print_time >= 1000)
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
      HAL_UART_Transmit(&huart2, debug_buffer, len, 100);
    }

    /* Signal Status Check */
    uint8_t status_buffer[128];
    if (gps->data.latitude == 0.0f && gps->data.longitude == 0.0f &&
        gps->data.num_satellites == 0 && gps->data.fix_type == 0)
    {
      int status_len = snprintf((char*)status_buffer, sizeof(status_buffer),
        "   ✗ GPS: NOT COMMUNICATING (check wiring/baudrate)\r\n");
      HAL_UART_Transmit(&huart2, status_buffer, status_len, 100);
    }
    else if (gps->data.num_satellites == 0)
    {
      int status_len = snprintf((char*)status_buffer, sizeof(status_buffer),
        "   ✓ GPS: COMMUNICATING but NO SIGNAL (move outdoors)\r\n");
      HAL_UART_Transmit(&huart2, status_buffer, status_len, 100);
    }
    else if (gps->gps_valid)
    {
      int status_len = snprintf((char*)status_buffer, sizeof(status_buffer),
        "   ✓✓ GPS: FIX ACQUIRED! (%d satellites)\r\n", gps->data.num_satellites);
      HAL_UART_Transmit(&huart2, status_buffer, status_len, 100);
    }
  }
}

void CDH_Debug_PrintBaro(MS5607_EquipmentHandler *baro)
{
  static uint32_t last_print_time = 0;
  uint32_t current_time = HAL_GetTick();

  if (current_time - last_print_time >= 1000)
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
      HAL_UART_Transmit(&huart2, debug_buffer, len, 100);
    }
  }
}
