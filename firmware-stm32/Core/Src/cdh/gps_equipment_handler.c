#include "cdh/gps_equipment_handler.h"
#include "cdh/m10s.h"

GPS_EquipmentHandler GPS_EquipmentHandler_Init(I2C_HandleTypeDef *hi2c)
{
  GPS_EquipmentHandler handler = {0};
  handler.last_sensor_check_ms = HAL_GetTick();
  handler.gps_valid = 0;

  /* Initialize MAX-M10S GPS module */
  M10S_Begin(hi2c);

  return handler;
}

GPS_EquipmentHandler GPS_EquipmentHandler_Update(GPS_EquipmentHandler handler, I2C_HandleTypeDef *hi2c)
{
  uint32_t current_time = HAL_GetTick();
  M10S_NavPVT pvt = {0};

  M10S_CheckUblox(hi2c);

  if (current_time - handler.last_sensor_check_ms >= GPS_SENSOR_CHECK_INTERVAL_MS)
  {
    /* Request new data from GPS (poll mode) */
    M10S_RequestPVT(hi2c);
    HAL_Delay(50);  /* Wait for GPS to respond */

    if (M10S_Read(hi2c, &pvt))
    {
      handler.data.latitude = pvt.latitude;
      handler.data.longitude = pvt.longitude;
      handler.data.altitude = pvt.altitude;
      handler.data.speed = pvt.speed;
      handler.data.vel_north = pvt.vel_north;
      handler.data.vel_east = pvt.vel_east;
      handler.data.vel_down = pvt.vel_down;
      handler.data.heading = pvt.heading;
      handler.data.num_satellites = pvt.num_satellites;
      handler.data.fix_type = pvt.fix_type;
      handler.gps_valid = (pvt.num_satellites > 0 && pvt.fix_type > 0) ? 1 : 0;
    }
    else
    {
      handler.gps_valid = 0;
    }

    handler.last_sensor_check_ms = current_time;
  }

  return handler;
}
