#ifndef MS5607_EQUIPMENT_HANDLER_H
#define MS5607_EQUIPMENT_HANDLER_H

#include "main.h"
#include "cdh/ms5607.h"
#include <stdbool.h>

#define MS5607_DATA_READ_INTERVAL 500
#define MS5607_SENSOR_CHECK_INTERVAL_MS 2000
#define MS5607_FLATLINE_THRESHOLD 0.1f
#define MS5607_FLATLINE_COUNT_LIMIT 5

typedef struct {
  MS5607_Data data;
  MS5607_Data last_data;
  float baseline_altitude_m;  /* Known altitude at startup for relative height */
  float baseline_pressure_pa; /* Pressure at startup in Pa for calculations */
  uint8_t baro_valid;
  uint32_t last_good_data_ms;
  uint32_t last_sensor_check_ms;
  uint32_t last_data_read_ms;
  uint32_t flatline_count;
} MS5607_EquipmentHandler;

MS5607_EquipmentHandler MS5607_EquipmentHandler_Init(I2C_HandleTypeDef *hi2c);
MS5607_EquipmentHandler MS5607_EquipmentHandler_Update(MS5607_EquipmentHandler handler, I2C_HandleTypeDef *hi2c);

#endif
