#include "fsw/fsm.h"
#include "fsw/fsm_thresholds.h"
#include "fdir/crc16.h"
#include "main.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static FlightPhase_t s_phase = PHASE_STANDBY;
static uint8_t s_standby_to_launch_count = 0U;
static uint8_t s_launch_to_ascent_count = 0U;
static uint8_t s_ascent_to_cruise_count = 0U;
static uint8_t s_descent_count = 0U;
static uint8_t s_landing_count = 0U;
static uint8_t s_cruise_baro_ref_valid = 0U;
static float s_cruise_baro_ref_m = 0.0f;
static float s_float_accel_baseline_g = 1.0f;
static uint16_t s_telemetry_sequence = 0U;

static uint8_t FSW_EquipmentUsable(uint16_t equipment)
{
    return (g_scv.equipment_faults & equipment) == 0U;
}

static uint8_t FSW_GpsUsable(const SensorData_t *dp)
{
    return dp->gps_valid && FSW_EquipmentUsable(EQUIPMENT_GPS);
}

static uint8_t FSW_ImuUsable(const SensorData_t *dp)
{
    return dp->imu_valid && FSW_EquipmentUsable(EQUIPMENT_IMU);
}

static uint8_t FSW_BaroUsable(const SensorData_t *dp)
{
    return dp->baro_valid && FSW_EquipmentUsable(EQUIPMENT_BARO);
}

static uint8_t FSW_BattUsable(const SensorData_t *dp)
{
    return dp->batt_valid && FSW_EquipmentUsable(EQUIPMENT_EPS_ADC);
}

static uint8_t FSW_IsValidPhase(uint8_t phase)
{
    return phase <= (uint8_t)PHASE_LANDING;
}

static void FSW_ResetTransitionCounters(void)
{
    s_standby_to_launch_count = 0U;
    s_launch_to_ascent_count = 0U;
    s_ascent_to_cruise_count = 0U;
    s_descent_count = 0U;
    s_landing_count = 0U;
    s_cruise_baro_ref_valid = 0U;
}

static void FSW_SetPhase(FlightPhase_t phase)
{
    if (s_phase == phase)
        return;

    s_phase = phase;
    g_scv.flight_phase = (uint8_t)phase;
    FSW_ResetTransitionCounters();
}

static uint8_t FSW_CountCondition(uint8_t condition, uint8_t *counter, uint8_t limit)
{
    if (condition)
    {
        if (*counter < UINT8_MAX)
            (*counter)++;
    }
    else
    {
        *counter = 0U;
    }

    return *counter >= limit;
}

void FSW_Init(void)
{
    if (FSW_IsValidPhase(g_scv.flight_phase))
        s_phase = (FlightPhase_t)g_scv.flight_phase;
    else
        FSW_SetPhase(PHASE_STANDBY);

}

void FSW_Update(const SensorData_t *dp)
{
    uint8_t launch_condition;
    uint8_t ascent_condition;
    uint8_t cruise_condition;
    uint8_t descent_condition;
    uint8_t landing_condition;
    float accel_delta_g;
    uint8_t gps_usable;
    uint8_t imu_usable;
    uint8_t baro_usable;

    if (dp == NULL)
        return;

    gps_usable = FSW_GpsUsable(dp);
    imu_usable = FSW_ImuUsable(dp);
    baro_usable = FSW_BaroUsable(dp);

    switch (s_phase)
    {
    case PHASE_STANDBY:
        launch_condition = ((imu_usable && dp->imu_accel_mag_g > FSM_LAUNCH_ACCEL_G) ||
                            (baro_usable && dp->baro_alt_m > FSM_LAUNCH_BARO_RISE_M));
        if (FSW_CountCondition(launch_condition, &s_standby_to_launch_count, FSM_LAUNCH_WINDOW_S))
            FSW_SetPhase(PHASE_LAUNCH);
        break;

    case PHASE_LAUNCH:
        ascent_condition = ((baro_usable && dp->baro_alt_m > FSM_ASCENT_ALT_M) ||
                            (gps_usable && dp->gps_vel_down_mps < FSM_ASCENT_VEL_DOWN_MPS));
        if (FSW_CountCondition(ascent_condition, &s_launch_to_ascent_count, FSM_ASCENT_WINDOW_S))
            FSW_SetPhase(PHASE_ASCENT);
        break;

    case PHASE_ASCENT:
        accel_delta_g = imu_usable ? fabsf(dp->imu_accel_mag_g - s_float_accel_baseline_g) : 0.0f;
        descent_condition = ((imu_usable && accel_delta_g > FSM_DESCENT_ACCEL_DELTA_G) ||
                             (gps_usable && dp->gps_vel_down_mps > FSM_DESCENT_VEL_DOWN_MPS));
        if (FSW_CountCondition(descent_condition, &s_descent_count, FSM_DESCENT_WINDOW_S))
        {
            FSW_SetPhase(PHASE_DESCENT);
            break;
        }

        if (baro_usable && !s_cruise_baro_ref_valid)
        {
            s_cruise_baro_ref_m = dp->baro_alt_m;
            s_cruise_baro_ref_valid = 1U;
        }

        cruise_condition = ((gps_usable && dp->gps_vel_down_mps > FSM_CRUISE_VEL_DOWN_MPS) ||
                            (baro_usable && s_cruise_baro_ref_valid &&
                             fabsf(dp->baro_alt_m - s_cruise_baro_ref_m) < FSM_CRUISE_ALT_BAND_M));
        if (FSW_CountCondition(cruise_condition, &s_ascent_to_cruise_count, FSM_CRUISE_WINDOW_S))
            FSW_SetPhase(PHASE_CRUISE);
        break;

    case PHASE_CRUISE:
        if (imu_usable)
            s_float_accel_baseline_g = (0.9f * s_float_accel_baseline_g) + (0.1f * dp->imu_accel_mag_g);

        accel_delta_g = imu_usable ? fabsf(dp->imu_accel_mag_g - s_float_accel_baseline_g) : 0.0f;
        descent_condition = ((imu_usable && accel_delta_g > FSM_DESCENT_ACCEL_DELTA_G) ||
                             (gps_usable && dp->gps_vel_down_mps > FSM_DESCENT_VEL_DOWN_MPS));
        if (FSW_CountCondition(descent_condition, &s_descent_count, FSM_DESCENT_WINDOW_S))
            FSW_SetPhase(PHASE_DESCENT);
        break;

    case PHASE_DESCENT:
        landing_condition = ((gps_usable && dp->gps_speed_mps < FSM_LANDING_SPEED_MPS) ||
                             (baro_usable && dp->baro_alt_m < FSM_LANDING_ALT_M));
        if (FSW_CountCondition(landing_condition, &s_landing_count, FSM_LANDING_WINDOW_S))
            FSW_SetPhase(PHASE_LANDING);
        break;

    case PHASE_LANDING:
    default:
        break;
    }
}

FlightPhase_t FSW_GetPhase(void)
{
    return s_phase;
}

void FSW_BuildTelemetryPacket(const SensorData_t *dp, const SCV_t *scv,
                              TelemetryPacket_t *pkt)
{
    if (dp == NULL || scv == NULL || pkt == NULL)
        return;

    memset(pkt, 0, sizeof(*pkt));

    pkt->packet_type = 0x01U;
    pkt->protocol_version = 0x03U;
    pkt->sequence_number = s_telemetry_sequence++;
    pkt->tx_uptime_ms = HAL_GetTick();

    pkt->datapool_timestamp_ms = dp->timestamp_ms;
    pkt->gps_lat_deg = dp->gps_lat_deg;
    pkt->gps_lon_deg = dp->gps_lon_deg;
    pkt->gps_alt_m = dp->gps_alt_m;
    pkt->gps_vvel_mps = -dp->gps_vel_down_mps;
    pkt->gps_speed_mps = dp->gps_speed_mps;
    pkt->gps_valid = dp->gps_valid;
    pkt->imu_accel_x_g = dp->imu_accel_x_g;
    pkt->imu_accel_y_g = dp->imu_accel_y_g;
    pkt->imu_accel_z_g = dp->imu_accel_z_g;
    pkt->imu_accel_mag_g = dp->imu_accel_mag_g;
    pkt->imu_gyro_x_dps = dp->imu_gyro_x_dps;
    pkt->imu_gyro_y_dps = dp->imu_gyro_y_dps;
    pkt->imu_gyro_z_dps = dp->imu_gyro_z_dps;
    pkt->imu_valid = dp->imu_valid;
    pkt->baro_pressure_pa = dp->baro_pressure_pa;
    pkt->baro_alt_m = dp->baro_alt_m;
    pkt->baro_temp_c = dp->baro_temp_c;
    pkt->baro_valid = dp->baro_valid;
    pkt->i2c_bus_state = dp->i2c_bus_state;
    pkt->batt_voltage_mv = FSW_BattUsable(dp) ? dp->batt_voltage_mv : scv->last_batt_mv;
    pkt->batt_valid = dp->batt_valid;
    memcpy(pkt->coral_block, dp->coral_block, sizeof(pkt->coral_block));
    pkt->coral_valid = dp->coral_valid;

    pkt->scv_magic = scv->magic;
    pkt->scv_boot_count = scv->boot_count;
    pkt->scv_mission_elapsed_ms = scv->mission_elapsed_ms;
    pkt->scv_flight_phase = scv->flight_phase;
    pkt->scv_reset_reason = scv->reset_reason;
    pkt->scv_equipment_enabled = scv->equipment_enabled;
    pkt->scv_equipment_faults = scv->equipment_faults;
    pkt->scv_gps_timeout_count = scv->gps_timeout_count;
    pkt->scv_imu_timeout_count = scv->imu_timeout_count;
    pkt->scv_baro_timeout_count = scv->baro_timeout_count;
    pkt->scv_coral_timeout_count = scv->coral_timeout_count;
    pkt->scv_sd_fault_count = scv->sd_fault_count;
    pkt->scv_watchdog_reset_count = scv->watchdog_reset_count;
    pkt->scv_last_batt_mv = scv->last_batt_mv;
    pkt->scv_baro_ground_alt_cm = scv->baro_ground_alt_cm;
    pkt->scv_crc16 = scv->crc16;

    pkt->crc16 = CRC16_Ccitt((const uint8_t *)pkt, offsetof(TelemetryPacket_t, crc16));
}
