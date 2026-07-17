#ifndef SENSOR_VALIDATION_H
#define SENSOR_VALIDATION_H

#include "datapool.h"
#include <stdint.h>

#define IMU_FLATLINE_THRESHOLD     0.001f
#define IMU_FLATLINE_TIMEOUT_S     4

#define BARO_MAX_PRESSURE_PA       151987.5f
#define BARO_MIN_TEMP_C            -100.0f

#define GPS_MAX_ALTITUDE_M         40000.0f
#define GPS_MIN_ALTITUDE_M         0.0f
#define GPS_TIMEOUT_S              60
#define GPS_MAX_DISTANCE_KM        1000.0f
#define MUNICH_LAT                 48.1351f
#define MUNICH_LON                 11.5820f

typedef struct {
    uint8_t imu_valid;
    uint8_t baro_valid;
    uint8_t gps_valid;
    uint8_t bus_fault;
    uint32_t imu_fault_count;
    uint32_t baro_fault_count;
    uint32_t gps_fault_count;
    uint32_t last_imu_check_ms;
    uint32_t last_baro_check_ms;
    uint32_t last_gps_update_ms;
    uint32_t last_recovery_attempt_ms;
    uint8_t recovery_in_progress;
} SensorValidationState;

extern SensorValidationState g_sensor_validation;

void SensorValidation_Init(void);
void SensorValidation_Update(SensorData_t *dp, SCV_t *scv);

#endif
