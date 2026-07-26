#ifndef GPS_EQUIPMENT_HANDLER_H
#define GPS_EQUIPMENT_HANDLER_H

#include "main.h"
#include <stdint.h>

#define UBLOX_MAX10S_I2C_ADDRESS 0x42
#define GPS_SENSOR_CHECK_INTERVAL_MS 1000
#define GPS_MESSAGE_FRESHNESS_MS 2000U

typedef struct {
  float latitude;
  float longitude;
  float altitude;
  float speed;
  float vel_down;
  float heading;
  uint32_t utc_time;
  uint8_t num_satellites;
  uint8_t fix_type;
} GPS_Data;

typedef struct {
  GPS_Data data;
  uint8_t gps_valid;
  uint8_t init_in_progress;
  uint32_t last_message_ms;
  uint32_t last_sensor_check_ms;
} GPS_EquipmentHandler;

GPS_EquipmentHandler GPS_EquipmentHandler_Init(I2C_HandleTypeDef *hi2c);
GPS_EquipmentHandler GPS_EquipmentHandler_Update(GPS_EquipmentHandler handler, I2C_HandleTypeDef *hi2c);

#endif
