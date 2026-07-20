#include "cdh/ms5607_equipment_handler.h"

MS5607_EquipmentHandler MS5607_EquipmentHandler_Init(I2C_HandleTypeDef *hi2c)
{
  MS5607_EquipmentHandler handler = {0};
  handler.last_good_data_ms = HAL_GetTick();
  handler.baro_valid = 0;

  if (!MS5607_Check(hi2c)) {
    return handler;
  }

  MS5607_Init(hi2c);

  handler.data = MS5607_Read(hi2c);
  handler.baseline_altitude_m = 513.0f;  /* Known altitude at this location (meters) */
  handler.baseline_pressure_pa = handler.data.pressure * 100.0f;  /* Store in Pa for calculation */
  handler.last_data = handler.data;
  handler.last_good_data_ms = HAL_GetTick();
  /* Only claim validity if that first conversion actually succeeded. */
  handler.baro_valid = handler.data.valid;

  return handler;
}

MS5607_EquipmentHandler MS5607_EquipmentHandler_Update(MS5607_EquipmentHandler handler, I2C_HandleTypeDef *hi2c)
{
  uint32_t current_time = HAL_GetTick();

  if (current_time - handler.last_data_read_ms >= MS5607_DATA_READ_INTERVAL)
  {
    MS5607_Data sample = MS5607_Read(hi2c);
    handler.last_data_read_ms = current_time;

    if (!sample.valid)
    {
      /* Conversion failed on the bus (or the calibration is unusable). Keep
       * the last good sample for reference but do not republish it as fresh. */
      handler.read_fault_count++;
      if (handler.read_fault_count >= MS5607_READ_FAULT_LIMIT)
      {
        handler.baro_valid = 0;
      }
    }
    else
    {
      handler.read_fault_count = 0;
      handler.data = sample;
      /* Altimeter reads 0 m at the baseline; add it back for absolute height */
      handler.data.altitude = handler.baseline_altitude_m + handler.data.altitude;
      handler.last_data = handler.data;
      handler.last_good_data_ms = current_time;
      handler.baro_valid = 1;
    }
  }

  /* No periodic presence check here by design, matching the IMU handler: it is
   * redundant now that MS5607_Read reports bus failures directly, and it would
   * only add another transaction to a bus already suspected of contention.
   * Presence is verified once, at Init. Note the old check could never fail
   * anyway -- MS5607_Check discarded its probe result and always returned 1,
   * so it unconditionally forced baro_valid back to 1 every 2 s and erased
   * any fault. */

  return handler;
}
