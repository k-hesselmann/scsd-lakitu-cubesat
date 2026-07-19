#include "fsw/fsm.h"
#include "fsw/fsm_thresholds.h"
#include "fdir/crc16.h"
#include "main.h"
#include "ttc/ttc.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static FlightPhase_t s_phase = PHASE_STANDBY;
/* Tick at which each transition condition first became (and stayed) true;
 * 0 = condition not currently held. */
static uint32_t s_standby_to_launch_since_ms = 0U;
static uint32_t s_launch_to_ascent_since_ms = 0U;
static uint32_t s_ascent_to_cruise_since_ms = 0U;
static uint32_t s_descent_since_ms = 0U;
static uint32_t s_landing_since_ms = 0U;
static uint8_t s_cruise_baro_ref_valid = 0U;
static float s_cruise_baro_ref_m = 0.0f;
static float s_float_accel_baseline_g = 1.0f;
static uint16_t s_telemetry_sequence = 0U;
static uint8_t s_gps_seen_valid = 0U;
static uint8_t s_imu_seen_valid = 0U;
static uint8_t s_baro_seen_valid = 0U;
static uint8_t s_batt_seen_valid = 0U;
static uint8_t s_coral_seen_valid = 0U;
static uint32_t s_coral_last_valid_rx_ms = 0U;
static TelemetryPacket_t s_last_measurements;

/* Equipment feeds phase decisions only when it is enabled (policy plane:
 * reduced mode, future give-up isolation) AND not currently faulted
 * (detection plane). An isolated device whose data happens to look valid
 * must not steer phase transitions. */
static uint8_t FSW_EquipmentUsable(uint16_t equipment)
{
    return ((g_scv.equipment_enabled & equipment) == equipment) &&
           ((g_scv.equipment_faults & equipment) == 0U);
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

static uint8_t FSW_IsValidPhase(uint8_t phase)
{
    return phase <= (uint8_t)PHASE_LANDING;
}

static void FSW_ResetTransitionCounters(void)
{
    s_standby_to_launch_since_ms = 0U;
    s_launch_to_ascent_since_ms = 0U;
    s_ascent_to_cruise_since_ms = 0U;
    s_descent_since_ms = 0U;
    s_landing_since_ms = 0U;
    s_cruise_baro_ref_valid = 0U;
}

static void FSW_SetPhase(FlightPhase_t phase)
{
    if (s_phase == phase)
        return;

    s_phase = phase;
    g_scv.flight_phase = (uint8_t)phase;
    FSW_ResetTransitionCounters();
    /* State changes are operationally significant: report them immediately. */
    TTC_RequestTelemetry();
}

/* Elapsed-time debounce: returns 1 once `condition` has held continuously for
 * `window_ms`. Rate-independent — FSW_Update may be called at any frequency.
 * A single false sample releases the latch and restarts the window. */
static uint8_t FSW_ConditionHeld(uint8_t condition, uint32_t *since_ms, uint32_t window_ms)
{
    uint32_t now_ms = HAL_GetTick();

    if (!condition)
    {
        *since_ms = 0U;
        return 0U;
    }

    if (*since_ms == 0U)
        *since_ms = (now_ms != 0U) ? now_ms : 1U; /* keep 0 = "not held" */

    return (now_ms - *since_ms) >= window_ms;
}

void FSW_Init(void)
{
    s_gps_seen_valid = 0U;
    s_imu_seen_valid = 0U;
    s_baro_seen_valid = 0U;
    s_batt_seen_valid = 0U;
    s_coral_seen_valid = 0U;
    s_coral_last_valid_rx_ms = 0U;
    memset(&s_last_measurements, 0, sizeof(s_last_measurements));
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
        if (FSW_ConditionHeld(launch_condition, &s_standby_to_launch_since_ms, FSM_LAUNCH_WINDOW_MS))
            FSW_SetPhase(PHASE_LAUNCH);
        break;

    case PHASE_LAUNCH:
        ascent_condition = ((baro_usable && dp->baro_alt_m > FSM_ASCENT_ALT_M) ||
                            (gps_usable && dp->gps_vel_down_mps < FSM_ASCENT_VEL_DOWN_MPS));
        if (FSW_ConditionHeld(ascent_condition, &s_launch_to_ascent_since_ms, FSM_ASCENT_WINDOW_MS))
            FSW_SetPhase(PHASE_ASCENT);
        break;

    case PHASE_ASCENT:
        accel_delta_g = imu_usable ? fabsf(dp->imu_accel_mag_g - s_float_accel_baseline_g) : 0.0f;
        descent_condition = ((imu_usable && accel_delta_g > FSM_DESCENT_ACCEL_DELTA_G) ||
                             (gps_usable && dp->gps_vel_down_mps > FSM_DESCENT_VEL_DOWN_MPS));
        if (FSW_ConditionHeld(descent_condition, &s_descent_since_ms, FSM_DESCENT_WINDOW_MS))
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
        if (FSW_ConditionHeld(cruise_condition, &s_ascent_to_cruise_since_ms, FSM_CRUISE_WINDOW_MS))
            FSW_SetPhase(PHASE_CRUISE);
        break;

    case PHASE_CRUISE:
        if (imu_usable)
            s_float_accel_baseline_g = (0.9f * s_float_accel_baseline_g) + (0.1f * dp->imu_accel_mag_g);

        accel_delta_g = imu_usable ? fabsf(dp->imu_accel_mag_g - s_float_accel_baseline_g) : 0.0f;
        descent_condition = ((imu_usable && accel_delta_g > FSM_DESCENT_ACCEL_DELTA_G) ||
                             (gps_usable && dp->gps_vel_down_mps > FSM_DESCENT_VEL_DOWN_MPS));
        if (FSW_ConditionHeld(descent_condition, &s_descent_since_ms, FSM_DESCENT_WINDOW_MS))
            FSW_SetPhase(PHASE_DESCENT);
        break;

    case PHASE_DESCENT:
        landing_condition = ((gps_usable && dp->gps_speed_mps < FSM_LANDING_SPEED_MPS) ||
                             (baro_usable && dp->baro_alt_m < FSM_LANDING_ALT_M));
        if (FSW_ConditionHeld(landing_condition, &s_landing_since_ms, FSM_LANDING_WINDOW_MS))
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

static int16_t FSW_ScaleI16(float value, float scale)
{
    double scaled;

    if (!isfinite(value))
        return INT16_MIN;
    scaled = (double)value * (double)scale;
    if (scaled >= (double)INT16_MAX)
        return INT16_MAX;
    if (scaled <= (double)(INT16_MIN + 1))
        return (int16_t)(INT16_MIN + 1);
    return (int16_t)(scaled >= 0.0 ? scaled + 0.5 : scaled - 0.5);
}

static int32_t FSW_ScaleI32(float value, float scale)
{
    double scaled;

    if (!isfinite(value))
        return INT32_MIN;
    scaled = (double)value * (double)scale;
    if (scaled >= (double)INT32_MAX)
        return INT32_MAX;
    if (scaled <= (double)(INT32_MIN + 1LL))
        return INT32_MIN + 1;
    return (int32_t)(scaled >= 0.0 ? scaled + 0.5 : scaled - 0.5);
}

static uint16_t FSW_ScaleU16(float value, float scale, uint16_t maximum)
{
    double scaled;

    if (!isfinite(value) || value < 0.0f)
        return UINT16_MAX;
    scaled = (double)value * (double)scale;
    if (scaled >= (double)maximum)
        return maximum;
    return (uint16_t)(scaled + 0.5);
}

static uint32_t FSW_ScaleU32(float value, float scale)
{
    double scaled;

    if (!isfinite(value) || value < 0.0f)
        return UINT32_MAX;
    scaled = (double)value * (double)scale;
    if (scaled >= (double)(UINT32_MAX - 1UL))
        return UINT32_MAX - 1UL;
    return (uint32_t)(scaled + 0.5);
}

static uint16_t FSW_SaturateU16Reserved(uint32_t value)
{
    return (value >= UINT16_MAX) ? (uint16_t)(UINT16_MAX - 1U) : (uint16_t)value;
}

static uint8_t FSW_SaturateU8(uint16_t value)
{
    return (value >= UINT8_MAX) ? UINT8_MAX : (uint8_t)value;
}

static uint16_t FSW_ReadU16LE(const uint8_t *value)
{
    return (uint16_t)((uint16_t)value[0] | ((uint16_t)value[1] << 8));
}

static uint32_t FSW_ReadU32LE(const uint8_t *value)
{
    return (uint32_t)value[0] |
           ((uint32_t)value[1] << 8) |
           ((uint32_t)value[2] << 16) |
           ((uint32_t)value[3] << 24);
}

static uint32_t FSW_UtcToSecondsOfDay(uint32_t hhmmss)
{
    uint32_t hours = hhmmss / 10000U;
    uint32_t minutes = (hhmmss / 100U) % 100U;
    uint32_t seconds = hhmmss % 100U;

    if (hours > 23U || minutes > 59U || seconds > 59U)
        return UINT32_MAX;
    return (hours * 3600U) + (minutes * 60U) + seconds;
}

static void FSW_SetInvalidMeasurements(TelemetryPacket_t *pkt)
{
    pkt->latitude_e7 = INT32_MIN;
    pkt->longitude_e7 = INT32_MIN;
    pkt->gnss_altitude_dm = INT32_MIN;
    pkt->vertical_speed_cms = INT16_MIN;
    pkt->ground_speed_cms = UINT16_MAX;
    pkt->course_cdeg = UINT16_MAX;
    pkt->gnss_utc_sod = UINT32_MAX;
    pkt->gnss_satellites = UINT8_MAX;
    pkt->gnss_fix_type = UINT8_MAX;
    pkt->accel_x_mg = INT16_MIN;
    pkt->accel_y_mg = INT16_MIN;
    pkt->accel_z_mg = INT16_MIN;
    pkt->gyro_x_ddeg_s = INT16_MIN;
    pkt->gyro_y_ddeg_s = INT16_MIN;
    pkt->gyro_z_ddeg_s = INT16_MIN;
    pkt->baro_pressure_pa = UINT32_MAX;
    pkt->baro_altitude_dm = INT32_MIN;
    pkt->baro_temperature_cdeg = INT16_MIN;
    pkt->battery_mv = UINT16_MAX;
    pkt->coral_sequence_low = UINT16_MAX;
    pkt->coral_fraction_q16 = UINT16_MAX;
    pkt->coral_age_s_sat = UINT16_MAX;
}

void FSW_BuildTelemetryPacket(const SensorData_t *dp, const SCV_t *scv,
                              TelemetryPacket_t *pkt)
{
    const UplinkState_t *uplink;
    const LoRaHealth_t *lora;
    uint32_t now_ms;

    if (dp == NULL || scv == NULL || pkt == NULL)
        return;

    memset(pkt, 0, sizeof(*pkt));
    FSW_SetInvalidMeasurements(pkt);
    now_ms = HAL_GetTick();

    pkt->packet_type = TELEMETRY_PACKET_TYPE;
    pkt->protocol_version = TELEMETRY_PROTOCOL_VERSION;
    pkt->sequence_number = s_telemetry_sequence++;
    pkt->tx_uptime_s = now_ms / 1000U;
    pkt->mission_elapsed_s = scv->mission_elapsed_ms / 1000U;
    pkt->boot_count_sat = (scv->boot_count >= UINT16_MAX) ?
                          UINT16_MAX : (uint16_t)scv->boot_count;
    pkt->flight_phase = scv->flight_phase;
    pkt->reset_reason = scv->reset_reason;
    pkt->i2c_bus_state = dp->i2c_bus_state;
    pkt->equipment_enabled = scv->equipment_enabled;
    pkt->equipment_faults = scv->equipment_faults;
    pkt->sample_age_ms_sat = (dp->timestamp_ms == 0U && now_ms != 0U) ?
                             UINT16_MAX :
                             FSW_SaturateU16Reserved(now_ms - dp->timestamp_ms);
    pkt->watchdog_reset_count = scv->watchdog_reset_count;
    pkt->sd_fault_count = scv->sd_fault_count;

    if (dp->gps_valid)
    {
        pkt->validity_flags |= TELEMETRY_VALID_GPS;
        pkt->latitude_e7 = FSW_ScaleI32(dp->gps_lat_deg, 10000000.0f);
        pkt->longitude_e7 = FSW_ScaleI32(dp->gps_lon_deg, 10000000.0f);
        pkt->gnss_altitude_dm = FSW_ScaleI32(dp->gps_alt_m, 10.0f);
        pkt->vertical_speed_cms = FSW_ScaleI16(-dp->gps_vel_down_mps, 100.0f);
        pkt->ground_speed_cms = FSW_ScaleU16(dp->gps_speed_mps, 100.0f,
                                             UINT16_MAX - 1U);
        pkt->course_cdeg = FSW_ScaleU16(dp->gps_heading_deg, 100.0f, 35999U);
        pkt->gnss_utc_sod = FSW_UtcToSecondsOfDay(dp->gps_utc_time);
        pkt->gnss_satellites = dp->gps_num_satellites;
        pkt->gnss_fix_type = dp->gps_fix_type;
        s_last_measurements.latitude_e7 = pkt->latitude_e7;
        s_last_measurements.longitude_e7 = pkt->longitude_e7;
        s_last_measurements.gnss_altitude_dm = pkt->gnss_altitude_dm;
        s_last_measurements.vertical_speed_cms = pkt->vertical_speed_cms;
        s_last_measurements.ground_speed_cms = pkt->ground_speed_cms;
        s_last_measurements.course_cdeg = pkt->course_cdeg;
        s_last_measurements.gnss_utc_sod = pkt->gnss_utc_sod;
        s_last_measurements.gnss_satellites = pkt->gnss_satellites;
        s_last_measurements.gnss_fix_type = pkt->gnss_fix_type;
        s_gps_seen_valid = 1U;
    }
    else if (s_gps_seen_valid)
    {
        pkt->latitude_e7 = s_last_measurements.latitude_e7;
        pkt->longitude_e7 = s_last_measurements.longitude_e7;
        pkt->gnss_altitude_dm = s_last_measurements.gnss_altitude_dm;
        pkt->vertical_speed_cms = s_last_measurements.vertical_speed_cms;
        pkt->ground_speed_cms = s_last_measurements.ground_speed_cms;
        pkt->course_cdeg = s_last_measurements.course_cdeg;
        pkt->gnss_utc_sod = s_last_measurements.gnss_utc_sod;
        pkt->gnss_satellites = s_last_measurements.gnss_satellites;
        pkt->gnss_fix_type = s_last_measurements.gnss_fix_type;
    }

    if (dp->imu_valid)
    {
        pkt->validity_flags |= TELEMETRY_VALID_IMU;
        pkt->accel_x_mg = FSW_ScaleI16(dp->imu_accel_x_g, 1000.0f);
        pkt->accel_y_mg = FSW_ScaleI16(dp->imu_accel_y_g, 1000.0f);
        pkt->accel_z_mg = FSW_ScaleI16(dp->imu_accel_z_g, 1000.0f);
        pkt->gyro_x_ddeg_s = FSW_ScaleI16(dp->imu_gyro_x_dps, 10.0f);
        pkt->gyro_y_ddeg_s = FSW_ScaleI16(dp->imu_gyro_y_dps, 10.0f);
        pkt->gyro_z_ddeg_s = FSW_ScaleI16(dp->imu_gyro_z_dps, 10.0f);
        s_last_measurements.accel_x_mg = pkt->accel_x_mg;
        s_last_measurements.accel_y_mg = pkt->accel_y_mg;
        s_last_measurements.accel_z_mg = pkt->accel_z_mg;
        s_last_measurements.gyro_x_ddeg_s = pkt->gyro_x_ddeg_s;
        s_last_measurements.gyro_y_ddeg_s = pkt->gyro_y_ddeg_s;
        s_last_measurements.gyro_z_ddeg_s = pkt->gyro_z_ddeg_s;
        s_imu_seen_valid = 1U;
    }
    else if (s_imu_seen_valid)
    {
        pkt->accel_x_mg = s_last_measurements.accel_x_mg;
        pkt->accel_y_mg = s_last_measurements.accel_y_mg;
        pkt->accel_z_mg = s_last_measurements.accel_z_mg;
        pkt->gyro_x_ddeg_s = s_last_measurements.gyro_x_ddeg_s;
        pkt->gyro_y_ddeg_s = s_last_measurements.gyro_y_ddeg_s;
        pkt->gyro_z_ddeg_s = s_last_measurements.gyro_z_ddeg_s;
    }

    if (dp->baro_valid)
    {
        pkt->validity_flags |= TELEMETRY_VALID_BARO;
        pkt->baro_pressure_pa = FSW_ScaleU32(dp->baro_pressure_pa, 1.0f);
        pkt->baro_altitude_dm = FSW_ScaleI32(dp->baro_alt_m, 10.0f);
        pkt->baro_temperature_cdeg = FSW_ScaleI16(dp->baro_temp_c, 100.0f);
        s_last_measurements.baro_pressure_pa = pkt->baro_pressure_pa;
        s_last_measurements.baro_altitude_dm = pkt->baro_altitude_dm;
        s_last_measurements.baro_temperature_cdeg = pkt->baro_temperature_cdeg;
        s_baro_seen_valid = 1U;
    }
    else if (s_baro_seen_valid)
    {
        pkt->baro_pressure_pa = s_last_measurements.baro_pressure_pa;
        pkt->baro_altitude_dm = s_last_measurements.baro_altitude_dm;
        pkt->baro_temperature_cdeg = s_last_measurements.baro_temperature_cdeg;
    }

    if (dp->batt_valid)
    {
        pkt->validity_flags |= TELEMETRY_VALID_BATTERY;
        pkt->battery_mv = dp->batt_voltage_mv;
        s_last_measurements.battery_mv = pkt->battery_mv;
        s_batt_seen_valid = 1U;
    }
    else if (s_batt_seen_valid)
        pkt->battery_mv = s_last_measurements.battery_mv;
    else if (scv->last_batt_mv != SCV_INVALID_U16)
    {
        pkt->battery_mv = scv->last_batt_mv;
        s_last_measurements.battery_mv = pkt->battery_mv;
        s_batt_seen_valid = 1U;
    }

    pkt->coral_status = dp->coral_block[7];
    if (dp->coral_valid)
    {
        pkt->validity_flags |= TELEMETRY_VALID_CORAL;
        pkt->coral_sequence_low = FSW_ReadU16LE(&dp->coral_block[0]);
        pkt->coral_fraction_q16 = FSW_ReadU16LE(&dp->coral_block[4]);
        s_last_measurements.coral_sequence_low = pkt->coral_sequence_low;
        s_last_measurements.coral_fraction_q16 = pkt->coral_fraction_q16;
        s_coral_last_valid_rx_ms = FSW_ReadU32LE(&dp->coral_block[8]);
        s_last_measurements.coral_age_s_sat =
            FSW_SaturateU16Reserved((now_ms - s_coral_last_valid_rx_ms) / 1000U);
        s_coral_seen_valid = 1U;
    }
    else if (s_coral_seen_valid)
    {
        pkt->coral_sequence_low = s_last_measurements.coral_sequence_low;
        pkt->coral_fraction_q16 = s_last_measurements.coral_fraction_q16;
    }
    if (s_coral_seen_valid)
    {
        s_last_measurements.coral_age_s_sat =
            FSW_SaturateU16Reserved((now_ms - s_coral_last_valid_rx_ms) / 1000U);
        pkt->coral_age_s_sat = s_last_measurements.coral_age_s_sat;
    }

    uplink = TTC_GetUplinkState();
    lora = TTC_GetHealth();
    pkt->lora_last_event = lora->last_event;
    pkt->lora_consecutive_failures = lora->consecutive_failures;
    pkt->lora_recovery_count = lora->recovery_count;
    pkt->lora_rx_state = (uint8_t)(lora->last_rx_status & TELEMETRY_LORA_RX_STATUS_MASK);
    if (lora->rx_mode_active)
        pkt->lora_rx_state |= TELEMETRY_LORA_RX_ACTIVE;
    pkt->lora_tx_fault_count_sat = FSW_SaturateU8(scv->lora_tx_fault_counter);
    pkt->lora_ack_timeout_count_sat = FSW_SaturateU8(lora->ack_timeout_count);
    pkt->last_command_id = uplink->last_command_id;
    pkt->uplink_state =
        (uint8_t)(uplink->last_command_status & TELEMETRY_UPLINK_STATUS_MASK) |
        (uint8_t)((uplink->last_ack_status & TELEMETRY_UPLINK_STATUS_MASK)
                  << TELEMETRY_UPLINK_ACK_STATUS_SHIFT);

    pkt->crc16 = CRC16_Ccitt((const uint8_t *)pkt,
                             offsetof(TelemetryPacket_t, crc16));
}
