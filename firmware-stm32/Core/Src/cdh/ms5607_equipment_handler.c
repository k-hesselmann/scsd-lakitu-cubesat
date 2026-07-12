#include "cdh/ms5607_equipment_handler.h"

MS5607_EquipmentHandler MS5607_EquipmentHandler_Init(I2C_HandleTypeDef *hi2c)
{
  MS5607_EquipmentHandler handler = {0};
  handler.last_good_data_ms = HAL_GetTick();
  handler.last_sensor_check_ms = HAL_GetTick();
  handler.baro_valid = 0;

  if (!MS5607_Check(hi2c)) {
    return handler;
  }

  MS5607_Init(hi2c);

  handler.data = MS5607_Read(hi2c);
  handler.last_good_data_ms = HAL_GetTick();
  handler.last_sensor_check_ms = HAL_GetTick();
  handler.baro_valid = 1;

  return handler;
}

MS5607_EquipmentHandler MS5607_EquipmentHandler_Update(MS5607_EquipmentHandler handler, I2C_HandleTypeDef *hi2c)
{
  uint32_t current_time = HAL_GetTick();

  if (current_time - handler.last_data_read_ms >= MS5607_DATA_READ_INTERVAL)
  {
    handler.data = MS5607_Read(hi2c);
    handler.last_good_data_ms = current_time;
    handler.last_data_read_ms = current_time;
    handler.baro_valid = 1;
  }

  if (current_time - handler.last_sensor_check_ms >= MS5607_SENSOR_CHECK_INTERVAL_MS)
  {
    if (!MS5607_Check(hi2c))
    {
      handler.baro_valid = 0;
    }
    else
    {
      handler.baro_valid = 1;
    }
    handler.last_sensor_check_ms = current_time;
  }

  return handler;
}
