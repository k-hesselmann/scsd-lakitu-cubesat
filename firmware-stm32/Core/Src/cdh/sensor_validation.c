#include "cdh/sensor_validation.h"
#include "cdh/cdh.h"
#include "cdh/m10s.h"
#include "main.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart2;
extern I2C_HandleTypeDef hi2c1;

SensorValidationState g_sensor_validation = {0};
static uint8_t s_gps_no_fix_reported = 0;

/* FMECA C4: repeated bit-identical six-axis output indicates frozen
 * registers; a stationary or free-fall payload is not a health fault. */
static float s_imu_prev_accel_x = 0.0f;
static float s_imu_prev_accel_y = 0.0f;
static float s_imu_prev_accel_z = 0.0f;
static float s_imu_prev_gyro_x = 0.0f;
static float s_imu_prev_gyro_y = 0.0f;
static float s_imu_prev_gyro_z = 0.0f;
static uint8_t s_imu_stuck_count = 0;
static uint8_t s_imu_prev_valid_for_stuck = 0;

/* FMECA C6: cross-check debounce state. */
static uint32_t s_baro_disagree_since_ms = 0;

void SensorValidation_Init(void)
{
    memset(&g_sensor_validation, 0, sizeof(g_sensor_validation));
    g_sensor_validation.imu_valid = 1;
    g_sensor_validation.baro_valid = 1;
    g_sensor_validation.gps_valid = 1;
    g_sensor_validation.last_recovery_attempt_ms = HAL_GetTick();
}

static float calculateDistance(float lat1, float lon1, float lat2, float lon2)
{
    const float R = 6371.0f;
    float dlat = (lat2 - lat1) * (M_PI / 180.0f);
    float dlon = (lon2 - lon1) * (M_PI / 180.0f);
    float a = sinf(dlat / 2.0f) * sinf(dlat / 2.0f) +
              cosf(lat1 * M_PI / 180.0f) * cosf(lat2 * M_PI / 180.0f) *
              sinf(dlon / 2.0f) * sinf(dlon / 2.0f);
    float c = 2.0f * asinf(sqrtf(a));
    return R * c;
}

/* FMECA C4: implausible-magnitude and frozen-output checks. */
static uint8_t validateImuPlausibility(SensorData_t *dp, float accel_mag)
{
    if (accel_mag > IMU_ACCEL_MAG_MAX_G) {
        char dbg[64];
        int len = snprintf(dbg, sizeof(dbg), "[VALIDATION] IMU magnitude implausible\r\n");
        HAL_UART_Transmit(&huart2, (uint8_t*)dbg, len, 100);
        dp->imu_valid = 0;
        g_sensor_validation.imu_valid = 0;
        g_sensor_validation.imu_plausibility_fault_count++;
        s_imu_stuck_count = 0;
        s_imu_prev_valid_for_stuck = 0;
        return 1U;
    }

    if (s_imu_prev_valid_for_stuck &&
        (dp->imu_accel_x_g == s_imu_prev_accel_x) &&
        (dp->imu_accel_y_g == s_imu_prev_accel_y) &&
        (dp->imu_accel_z_g == s_imu_prev_accel_z) &&
        (dp->imu_gyro_x_dps == s_imu_prev_gyro_x) &&
        (dp->imu_gyro_y_dps == s_imu_prev_gyro_y) &&
        (dp->imu_gyro_z_dps == s_imu_prev_gyro_z)) {
        if (s_imu_stuck_count < UINT8_MAX)
            s_imu_stuck_count++;
    } else {
        s_imu_stuck_count = 0;
    }
    s_imu_prev_accel_x = dp->imu_accel_x_g;
    s_imu_prev_accel_y = dp->imu_accel_y_g;
    s_imu_prev_accel_z = dp->imu_accel_z_g;
    s_imu_prev_gyro_x = dp->imu_gyro_x_dps;
    s_imu_prev_gyro_y = dp->imu_gyro_y_dps;
    s_imu_prev_gyro_z = dp->imu_gyro_z_dps;
    s_imu_prev_valid_for_stuck = 1U;

    if (s_imu_stuck_count >= IMU_STUCK_CYCLES) {
        char dbg[64];
        int len = snprintf(dbg, sizeof(dbg), "[VALIDATION] IMU stuck value\r\n");
        HAL_UART_Transmit(&huart2, (uint8_t*)dbg, len, 100);
        dp->imu_valid = 0;
        g_sensor_validation.imu_valid = 0;
        g_sensor_validation.imu_plausibility_fault_count++;
        s_imu_stuck_count = 0;
        s_imu_prev_valid_for_stuck = 0;
        return 1U;
    }

    return 0U;
}

static void validateIMU(SensorData_t *dp)
{
    if (!dp->imu_valid) {
        s_imu_prev_valid_for_stuck = 0;
        return;
    }

    float accel_mag = sqrtf(dp->imu_accel_x_g * dp->imu_accel_x_g +
                            dp->imu_accel_y_g * dp->imu_accel_y_g +
                            dp->imu_accel_z_g * dp->imu_accel_z_g);

    if (validateImuPlausibility(dp, accel_mag))
        return;

    if (!g_sensor_validation.imu_valid) {
        char dbg[64];
        int len = snprintf(dbg, sizeof(dbg), "[VALIDATION] IMU recovered\r\n");
        HAL_UART_Transmit(&huart2, (uint8_t*)dbg, len, 100);
    }
    g_sensor_validation.imu_valid = 1;
}

static void validateBaro(SensorData_t *dp)
{
    if (!dp->baro_valid) return;

    if (dp->baro_pressure_pa < 0.0f || dp->baro_pressure_pa > BARO_MAX_PRESSURE_PA) {
        char dbg[80];
        int len = snprintf(dbg, sizeof(dbg), "[VALIDATION] BARO pressure fault\r\n");
        HAL_UART_Transmit(&huart2, (uint8_t*)dbg, len, 100);
        dp->baro_valid = 0;
        g_sensor_validation.baro_valid = 0;
        g_sensor_validation.baro_fault_count++;
        return;
    }

    if (dp->baro_temp_c < BARO_MIN_TEMP_C) {
        char dbg[80];
        int len = snprintf(dbg, sizeof(dbg), "[VALIDATION] BARO temp fault\r\n");
        HAL_UART_Transmit(&huart2, (uint8_t*)dbg, len, 100);
        dp->baro_valid = 0;
        g_sensor_validation.baro_valid = 0;
        g_sensor_validation.baro_fault_count++;
        return;
    }

    if (!g_sensor_validation.baro_valid) {
        char dbg[64];
        int len = snprintf(dbg, sizeof(dbg), "[VALIDATION] BARO recovered\r\n");
        HAL_UART_Transmit(&huart2, (uint8_t*)dbg, len, 100);
    }
    g_sensor_validation.baro_valid = 1;
}

static void validateGPS(SensorData_t *dp)
{
    uint32_t now = HAL_GetTick();

    if (!dp->gps_valid) {
        if (g_sensor_validation.gps_valid) {
            g_sensor_validation.gps_valid = 0U;
            g_sensor_validation.gps_fault_count++;
        }
        return;
    }

    g_sensor_validation.gps_valid = 1U;
    g_sensor_validation.last_gps_update_ms = now;
    if (dp->gps_fix_type != M10S_FIX_3D) {
        if (!s_gps_no_fix_reported) {
            char dbg[80];
            int len = snprintf(dbg, sizeof(dbg), "[VALIDATION] GPS transport healthy, no 3D fix\r\n");
            HAL_UART_Transmit(&huart2, (uint8_t*)dbg, len, 100);
            s_gps_no_fix_reported = 1U;
        }
        return;
    }
    s_gps_no_fix_reported = 0U;

    if (dp->gps_alt_m < GPS_MIN_ALTITUDE_M || dp->gps_alt_m > GPS_MAX_ALTITUDE_M) {
        char dbg[80];
        int len = snprintf(dbg, sizeof(dbg), "[VALIDATION] GPS altitude fault\r\n");
        HAL_UART_Transmit(&huart2, (uint8_t*)dbg, len, 100);
        dp->gps_valid = 0;
        g_sensor_validation.gps_valid = 0;
        g_sensor_validation.gps_fault_count++;
        return;
    }

    float distance_km = calculateDistance(MUNICH_LAT, MUNICH_LON, dp->gps_lat_deg, dp->gps_lon_deg);
    if (distance_km > GPS_MAX_DISTANCE_KM) {
        char dbg[80];
        int len = snprintf(dbg, sizeof(dbg), "[VALIDATION] GPS distance fault\r\n");
        HAL_UART_Transmit(&huart2, (uint8_t*)dbg, len, 100);
        dp->gps_valid = 0;
        g_sensor_validation.gps_valid = 0;
        g_sensor_validation.gps_fault_count++;
        return;
    }

}

/* FMECA C6: only meaningful once each sensor has already passed its own
 * checks above (dp->baro_valid / dp->gps_valid still true) -- a device
 * already flagged invalid for its own reasons shouldn't also get blamed for
 * disagreeing with the other. Debounced independently of C5's baro-timeout
 * debounce: this is a distinct failure mode (plausible-but-wrong reading,
 * not a stopped-responding device). */
static void validateBaroGpsCrossCheck(SensorData_t *dp, const SCV_t *scv)
{
    uint32_t now = HAL_GetTick();
    float baro_pressure_alt_m;

    if (!dp->baro_valid || !dp->gps_valid ||
        scv->baro_ground_alt_cm == SCV_INVALID_I32) {
        s_baro_disagree_since_ms = 0;
        return;
    }

    baro_pressure_alt_m =
        dp->baro_alt_m + ((float)scv->baro_ground_alt_cm / 100.0f);
    if (fabsf(baro_pressure_alt_m - dp->gps_alt_m) <= BARO_GPS_ALT_DISAGREE_M) {
        s_baro_disagree_since_ms = 0;
        return;
    }

    if (s_baro_disagree_since_ms == 0) {
        s_baro_disagree_since_ms = (now != 0U) ? now : 1U;
        return;
    }

    if ((now - s_baro_disagree_since_ms) >= BARO_CROSSCHECK_DEBOUNCE_MS) {
        char dbg[80];
        int len = snprintf(dbg, sizeof(dbg), "[VALIDATION] BARO/GPS altitude disagree\r\n");
        HAL_UART_Transmit(&huart2, (uint8_t*)dbg, len, 100);
        dp->baro_valid = 0;   /* FMECA C6 isolation: prefer GPS altitude on disagreement */
        g_sensor_validation.baro_valid = 0;
        g_sensor_validation.baro_crosscheck_fault_count++;
        s_baro_disagree_since_ms = 0;
    }
}

static void handleRecovery(SCV_t *scv)
{
    uint32_t now = HAL_GetTick();
    uint8_t fault_count = (!g_sensor_validation.imu_valid ? 1 : 0) +
                          (!g_sensor_validation.baro_valid ? 1 : 0);

    if (fault_count == 0) return;

    char dbg[80];
    int len;

    if (fault_count > 1) {
        if ((now - g_sensor_validation.last_recovery_attempt_ms) < 1000) return;
        len = snprintf(dbg, sizeof(dbg), "[VALIDATION] %d sensors broken - I2C restart\r\n", fault_count);
        HAL_UART_Transmit(&huart2, (uint8_t*)dbg, len, 100);
        CDH_RequestBusRestart();
        g_sensor_validation.last_recovery_attempt_ms = now;
    } else {
        uint32_t retry_interval = g_sensor_validation.recovery_in_progress ? 60000 : 0;
        if ((now - g_sensor_validation.last_recovery_attempt_ms) >= retry_interval) {
            if (!g_sensor_validation.recovery_in_progress) {
                len = snprintf(dbg, sizeof(dbg), "[VALIDATION] Recovery attempt\r\n");
                g_sensor_validation.recovery_in_progress = 1;
            } else {
                len = snprintf(dbg, sizeof(dbg), "[VALIDATION] Recovery retry\r\n");
            }
            HAL_UART_Transmit(&huart2, (uint8_t*)dbg, len, 100);
            g_sensor_validation.last_recovery_attempt_ms = now;
        }
    }
}

void SensorValidation_Update(SensorData_t *dp, SCV_t *scv)
{
    if (!dp || !scv) return;
    validateIMU(dp);
    validateBaro(dp);
    validateGPS(dp);
    validateBaroGpsCrossCheck(dp, scv);
    handleRecovery(scv);
}
