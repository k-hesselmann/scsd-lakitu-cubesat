#include "cdh/gps_equipment_handler.h"
#include "cdh/m10s.h"

GPS_EquipmentHandler GPS_EquipmentHandler_Init(I2C_HandleTypeDef *hi2c)
{
  GPS_EquipmentHandler handler = {0};
  handler.gps_valid = 0;
  handler.init_in_progress = 1U;

  /* Start cooperative MAX-M10S initialization. */
  M10S_Begin(hi2c);

  return handler;
}

GPS_EquipmentHandler GPS_EquipmentHandler_Update(GPS_EquipmentHandler handler, I2C_HandleTypeDef *hi2c)
{
  M10S_NavPVT pvt = {0};

  M10S_InitService(hi2c);
  handler.init_in_progress = M10S_IsInitialized() ? 0U : 1U;

  if (handler.init_in_progress) {
    handler.gps_valid = 0U;
    return handler;
  }

  /* Check for incoming UBX streaming data. */
  M10S_CheckUblox(hi2c);

  /* A fresh NAV-PVT frame proves receiver transport health independently of
   * its fix type. The datapool freshness window tolerates the 1 Hz stream. */
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
    handler.last_message_ms = pvt.timestamp;
  }

  handler.gps_valid = (handler.last_message_ms != 0U &&
                       (uint32_t)(HAL_GetTick() - handler.last_message_ms) <=
                       GPS_MESSAGE_FRESHNESS_MS) ? 1U : 0U;

  /* Send periodic poll requests to get fresh data
   * This works alongside streaming mode - GPS responds with fresh RMC and GGA sentences
   * Polling interval is rate-limited (default 5 seconds, configurable with M10S_SetPollInterval)
   */
  M10S_RequestPVT(hi2c);

  return handler;
}
