/* Protocol-v8 telemetry packet builder. Converts the live datapool/SCV
 * snapshot into the fixed-point 92-byte wire format defined in
 * datapool.h (TelemetryPacket_t) and docs/ICD.md section 3. This is TTC's
 * wire-format concern, split out of ttc.c to keep that file to link/state
 * management. */

#include "cdh/m10s.h"

#include "ttc/ttc.h"
#include "fdir/crc16.h"
#include "main.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static uint16_t s_telemetry_sequence = 0U;
static uint8_t s_gnss_solution_seen_valid = 0U;
static uint8_t s_imu_seen_valid = 0U;
static uint8_t s_baro_seen_valid = 0U;
static uint8_t s_batt_seen_valid = 0U;
static uint8_t s_coral_seen_valid = 0U;
static uint32_t s_coral_last_valid_rx_ms = 0U;
static TelemetryPacket_t s_last_measurements;

static int16_t TTC_ScaleI16(float value, float scale)
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

static int32_t TTC_ScaleI32(float value, float scale)
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

static uint16_t TTC_ScaleU16(float value, float scale, uint16_t maximum)
{
    double scaled;

    if (!isfinite(value) || value < 0.0f)
        return UINT16_MAX;
    scaled = (double)value * (double)scale;
    if (scaled >= (double)maximum)
        return maximum;
    return (uint16_t)(scaled + 0.5);
}

static uint32_t TTC_ScaleU32(float value, float scale)
{
    double scaled;

    if (!isfinite(value) || value < 0.0f)
        return UINT32_MAX;
    scaled = (double)value * (double)scale;
    if (scaled >= (double)(UINT32_MAX - 1UL))
        return UINT32_MAX - 1UL;
    return (uint32_t)(scaled + 0.5);
}

static uint16_t TTC_SaturateU16Reserved(uint32_t value)
{
    return (value >= UINT16_MAX) ? (uint16_t)(UINT16_MAX - 1U) : (uint16_t)value;
}

static uint8_t TTC_SaturateU8(uint16_t value)
{
    return (value >= UINT8_MAX) ? UINT8_MAX : (uint8_t)value;
}

static uint16_t TTC_ReadU16LE(const uint8_t *value)
{
    return (uint16_t)((uint16_t)value[0] | ((uint16_t)value[1] << 8));
}

static uint32_t TTC_ReadU32LE(const uint8_t *value)
{
    return (uint32_t)value[0] |
           ((uint32_t)value[1] << 8) |
           ((uint32_t)value[2] << 16) |
           ((uint32_t)value[3] << 24);
}

static uint32_t TTC_UtcToSecondsOfDay(uint32_t hhmmss)
{
    uint32_t hours = hhmmss / 10000U;
    uint32_t minutes = (hhmmss / 100U) % 100U;
    uint32_t seconds = hhmmss % 100U;

    if (hours > 23U || minutes > 59U || seconds > 59U)
        return UINT32_MAX;
    return (hours * 3600U) + (minutes * 60U) + seconds;
}

static void TTC_SetInvalidMeasurements(TelemetryPacket_t *pkt)
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

void TTC_BuildTelemetryPacket(const SensorData_t *dp, const SCV_t *scv,
                              TelemetryPacket_t *pkt)
{
    const UplinkState_t *uplink;
    const LoRaHealth_t *lora;
    uint32_t now_ms;
    uint8_t gps_transport_fresh;
    uint8_t gps_fix_usable;

    if (dp == NULL || scv == NULL || pkt == NULL)
        return;

    memset(pkt, 0, sizeof(*pkt));
    TTC_SetInvalidMeasurements(pkt);
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
                             TTC_SaturateU16Reserved(now_ms - dp->timestamp_ms);
    pkt->watchdog_reset_count = scv->watchdog_reset_count;
    pkt->sd_fault_count = scv->sd_fault_count;

    gps_transport_fresh = dp->gps_valid ? 1U : 0U;
    gps_fix_usable = (gps_transport_fresh &&
                      dp->gps_fix_type == M10S_FIX_3D) ? 1U : 0U;

    if (gps_fix_usable)
    {
        pkt->validity_flags |= TELEMETRY_VALID_GPS;
        pkt->latitude_e7 = TTC_ScaleI32(dp->gps_lat_deg, 10000000.0f);
        pkt->longitude_e7 = TTC_ScaleI32(dp->gps_lon_deg, 10000000.0f);
        pkt->gnss_altitude_dm = TTC_ScaleI32(dp->gps_alt_m, 10.0f);
        pkt->vertical_speed_cms = TTC_ScaleI16(-dp->gps_vel_down_mps, 100.0f);
        pkt->ground_speed_cms = TTC_ScaleU16(dp->gps_speed_mps, 100.0f,
                                             UINT16_MAX - 1U);
        pkt->course_cdeg = TTC_ScaleU16(dp->gps_heading_deg, 100.0f, 35999U);
        pkt->gnss_utc_sod = TTC_UtcToSecondsOfDay(dp->gps_utc_time);
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
        s_gnss_solution_seen_valid = 1U;
    }
    else if (s_gnss_solution_seen_valid)
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

    /* A fresh NAV-PVT message can report NO FIX. Preserve its fix type and
     * satellite count for diagnosis without presenting retained position data
     * as a currently usable GNSS solution. */
    if (gps_transport_fresh && !gps_fix_usable)
    {
        pkt->gnss_satellites = dp->gps_num_satellites;
        pkt->gnss_fix_type = dp->gps_fix_type;
    }

    if (dp->imu_valid)
    {
        pkt->validity_flags |= TELEMETRY_VALID_IMU;
        pkt->accel_x_mg = TTC_ScaleI16(dp->imu_accel_x_g, 1000.0f);
        pkt->accel_y_mg = TTC_ScaleI16(dp->imu_accel_y_g, 1000.0f);
        pkt->accel_z_mg = TTC_ScaleI16(dp->imu_accel_z_g, 1000.0f);
        pkt->gyro_x_ddeg_s = TTC_ScaleI16(dp->imu_gyro_x_dps, 10.0f);
        pkt->gyro_y_ddeg_s = TTC_ScaleI16(dp->imu_gyro_y_dps, 10.0f);
        pkt->gyro_z_ddeg_s = TTC_ScaleI16(dp->imu_gyro_z_dps, 10.0f);
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
        pkt->baro_pressure_pa = TTC_ScaleU32(dp->baro_pressure_pa, 1.0f);
        pkt->baro_altitude_dm = TTC_ScaleI32(dp->baro_alt_m, 10.0f);
        pkt->baro_temperature_cdeg = TTC_ScaleI16(dp->baro_temp_c, 100.0f);
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
        pkt->coral_sequence_low = TTC_ReadU16LE(&dp->coral_block[0]);
        pkt->coral_fraction_q16 = TTC_ReadU16LE(&dp->coral_block[4]);
        s_last_measurements.coral_sequence_low = pkt->coral_sequence_low;
        s_last_measurements.coral_fraction_q16 = pkt->coral_fraction_q16;
        s_coral_last_valid_rx_ms = TTC_ReadU32LE(&dp->coral_block[8]);
        s_last_measurements.coral_age_s_sat =
            TTC_SaturateU16Reserved((now_ms - s_coral_last_valid_rx_ms) / 1000U);
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
            TTC_SaturateU16Reserved((now_ms - s_coral_last_valid_rx_ms) / 1000U);
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
    pkt->lora_tx_fault_count_sat = TTC_SaturateU8(scv->lora_tx_fault_counter);
    pkt->lora_ack_timeout_count_sat = TTC_SaturateU8(lora->ack_timeout_count);
    pkt->last_command_id = uplink->last_command_id;
    pkt->uplink_state =
        (uint8_t)(uplink->last_command_status & TELEMETRY_UPLINK_STATUS_MASK) |
        (uint8_t)((uplink->last_ack_status & TELEMETRY_UPLINK_STATUS_MASK)
                  << TELEMETRY_UPLINK_ACK_STATUS_SHIFT);

    pkt->crc16 = CRC16_Ccitt((const uint8_t *)pkt,
                             offsetof(TelemetryPacket_t, crc16));
}
