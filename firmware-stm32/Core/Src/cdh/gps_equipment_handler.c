#include "cdh/gps_equipment_handler.h"
#include "cdh/m10s.h"

GPS_EquipmentHandler GPS_EquipmentHandler_Init(I2C_HandleTypeDef *hi2c)
{
  GPS_EquipmentHandler handler = {0};
  handler.gps_valid = 0;

  /* Initialize MAX-M10S GPS module */
  M10S_Begin(hi2c);

  return handler;
}

GPS_EquipmentHandler GPS_EquipmentHandler_Update(GPS_EquipmentHandler handler, I2C_HandleTypeDef *hi2c)
{
  M10S_NavPVT pvt = {0};

  /* Check for incoming streaming data (continuous NMEA from GPS) */
  M10S_CheckUblox(hi2c);

  /* Parse any complete NMEA sentence available (updates immediately on new message) */
  if (M10S_Read(hi2c, &pvt))
  {
    handler.data.latitude = pvt.latitude;
    handler.data.longitude = pvt.longitude;
    handler.data.altitude = pvt.altitude;
    handler.data.speed = pvt.speed;
    handler.data.vel_down = pvt.vel_down;
    handler.data.heading = pvt.heading;
    handler.data.utc_time = pvt.utc_time;
    handler.data.num_satellites = pvt.num_satellites;
    handler.data.fix_type = pvt.fix_type;
    handler.gps_valid = (pvt.num_satellites > 0 && pvt.fix_type > 0) ? 1 : 0;
  }
  else
  {
    handler.gps_valid = 0;
  }

  /* Send periodic poll requests to get fresh data
   * This works alongside streaming mode - GPS responds with fresh RMC and GGA sentences
   * Polling interval is rate-limited (default 5 seconds, configurable with M10S_SetPollInterval)
   */
  M10S_RequestPVT(hi2c);

  return handler;
}
