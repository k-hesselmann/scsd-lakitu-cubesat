#include "cdh/sensor_validation.h"
#include "cdh/cdh.h"
#include "main.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart2;
extern I2C_HandleTypeDef hi2c1;

SensorValidationState g_sensor_validation = {0};
static uint8_t s_imu_flatline_count = 0;
static uint32_t s_imu_last_flatline_ms = 0;

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

static void validateIMU(SensorData_t *dp)
{
    uint32_t now = HAL_GetTick();
    if (!dp->imu_valid) return;

    float accel_mag = sqrtf(dp->imu_accel_x_g * dp->imu_accel_x_g +
                            dp->imu_accel_y_g * dp->imu_accel_y_g +
                            dp->imu_accel_z_g * dp->imu_accel_z_g);

    if (accel_mag < IMU_FLATLINE_THRESHOLD) {
        if ((now - s_imu_last_flatline_ms) > (IMU_FLATLINE_TIMEOUT_S * 1000)) {
            char dbg[64];
            int len = snprintf(dbg, sizeof(dbg), "[VALIDATION] IMU flatline\r\n");
            HAL_UART_Transmit(&huart2, (uint8_t*)dbg, len, 100);
            dp->imu_valid = 0;
            g_sensor_validation.imu_valid = 0;
            g_sensor_validation.imu_fault_count++;
            s_imu_flatline_count = 0;
        } else if (s_imu_flatline_count == 0) {
            s_imu_last_flatline_ms = now;
            s_imu_flatline_count = 1;
        }
    } else {
        if (!g_sensor_validation.imu_valid) {
            char dbg[64];
            int len = snprintf(dbg, sizeof(dbg), "[VALIDATION] IMU recovered\r\n");
            HAL_UART_Transmit(&huart2, (uint8_t*)dbg, len, 100);
        }
        g_sensor_validation.imu_valid = 1;
        s_imu_flatline_count = 0;
    }
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

    /* Update timestamp whenever GPS data changes (even if invalid) */
    if (dp->gps_lat_deg != 0 || dp->gps_lon_deg != 0 || dp->gps_alt_m != 0) {
        g_sensor_validation.last_gps_update_ms = now;
    }

    /* Check if GPS has a valid fix */
    if (dp->gps_fix_type == 0) {
        char dbg[80];
        int len = snprintf(dbg, sizeof(dbg), "[VALIDATION] GPS no fix\r\n");
        HAL_UART_Transmit(&huart2, (uint8_t*)dbg, len, 100);
        dp->gps_valid = 0;
        g_sensor_validation.gps_valid = 0;
        g_sensor_validation.gps_fault_count++;
        return;
    }

    /* Check if GPS data is stale (no update for GPS_TIMEOUT_S seconds) */
    if ((now - g_sensor_validation.last_gps_update_ms) > (GPS_TIMEOUT_S * 1000)) {
        char dbg[80];
        int len = snprintf(dbg, sizeof(dbg), "[VALIDATION] GPS timeout (%ds)\r\n", GPS_TIMEOUT_S);
        HAL_UART_Transmit(&huart2, (uint8_t*)dbg, len, 100);
        dp->gps_valid = 0;
        g_sensor_validation.gps_valid = 0;
        g_sensor_validation.gps_fault_count++;
        return;
    }

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

    if (!g_sensor_validation.gps_valid) {
        char dbg[64];
        int len = snprintf(dbg, sizeof(dbg), "[VALIDATION] GPS recovered\r\n");
        HAL_UART_Transmit(&huart2, (uint8_t*)dbg, len, 100);
    }
    g_sensor_validation.gps_valid = 1;
    dp->gps_valid = 1;  /* Set datapool valid flag when all checks pass */
    g_sensor_validation.last_gps_update_ms = now;
}

static void handleRecovery(SCV_t *scv)
{
    uint32_t now = HAL_GetTick();
    uint8_t fault_count = (!g_sensor_validation.imu_valid ? 1 : 0) +
                          (!g_sensor_validation.baro_valid ? 1 : 0) +
                          (!g_sensor_validation.gps_valid ? 1 : 0);

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
    handleRecovery(scv);
}
