#ifndef SENSOR_VALIDATION_H
#define SENSOR_VALIDATION_H

#include "datapool.h"
#include <stdint.h>

/* FMECA C4: plausibility check for "frozen/garbage values while ACKing"
 * (distinct from C3's NACK/timeout case, already covered by imu_valid). */
#define IMU_ACCEL_MAG_MAX_G        7.0f  /* MPU6050_Init configures ±4g per
                                           * axis. sqrt(3) × 4g is 6.93g, so
                                           * this rejects only impossible
                                           * datapool corruption/test data,
                                           * never a real in-range manoeuvre. */
#define IMU_STUCK_CYCLES           3     /* consecutive bit-identical six-axis
                                           * repeats while imu_valid=1
                                           * (ACKing) = frozen registers. */

#define BARO_MAX_PRESSURE_PA       151987.5f
#define BARO_MIN_TEMP_C            -100.0f

/* FMECA C6: GPS/baro altitude cross-check is a pre-flight static diagnostic.
 * It is deliberately limited to Standby: GPS and pressure altitude have
 * different latency and atmospheric models, so they must not invalidate a
 * responding barometer during ascent, descent, or deployment dynamics. */
#define BARO_GPS_ALT_DISAGREE_M    200.0f
#define BARO_CROSSCHECK_DEBOUNCE_MS 5000U

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
    uint32_t imu_plausibility_fault_count;   /* FMECA C4 */
    uint32_t baro_crosscheck_fault_count;    /* FMECA C6 */
    uint32_t last_imu_check_ms;
    uint32_t last_baro_check_ms;
    uint32_t last_gps_update_ms;
    uint32_t last_recovery_attempt_ms;
    uint8_t recovery_in_progress;
} SensorValidationState;

extern SensorValidationState g_sensor_validation;

void SensorValidation_Init(void);
/* Called immediately after FDIR has attempted the physical IMU reinitialisation.
 * A frozen-output fault remains latched until a subsequent raw sample differs
 * from the frozen six-axis tuple. */
void SensorValidation_NotifyImuReinitialized(void);
void SensorValidation_Update(SensorData_t *dp, SCV_t *scv);

#endif
