#include "ttc/telemetry_packet_test.h"

#include "main.h"

#include <string.h>

uint16_t TtcTestTelemetryCrc16Ccitt(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;

    for (uint16_t i = 0U; i < length; ++i)
    {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0U; bit < 8U; ++bit)
            crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
    }

    return crc;
}

static void TtcTestTelemetryUpdateCrc(TtcTestTelemetryPacket_t *packet)
{
    packet->crc16 = TtcTestTelemetryCrc16Ccitt((const uint8_t *)packet,
                                                sizeof(*packet) - sizeof(packet->crc16));
}

void TtcTestTelemetryFillMock(TtcTestTelemetryPacket_t *packet, uint16_t sequence_number)
{
    if (packet == NULL)
        return;

    memset(packet, 0, sizeof(*packet));

    packet->packet_type = TTC_TEST_TELEMETRY_PACKET_TYPE;
    packet->protocol_version = TTC_TEST_TELEMETRY_PROTOCOL_VERSION;
    packet->sequence_number = sequence_number;
    packet->utc_timestamp = 1700000000UL + sequence_number;
    packet->obc_uptime_ms = HAL_GetTick();
    packet->flight_state = TTC_TEST_FLIGHT_STANDBY;
    packet->status_flags = TTC_TEST_STATUS_GNSS_FIX_VALID |
                           TTC_TEST_STATUS_GNSS_TIME_VALID |
                           TTC_TEST_STATUS_IMU_VALID |
                           TTC_TEST_STATUS_BARO_VALID |
                           TTC_TEST_STATUS_BARO_RANGE_VALID |
                           TTC_TEST_STATUS_BATTERY_VALID |
                           TTC_TEST_STATUS_SD_LOGGING_OK |
                           TTC_TEST_STATUS_LAST_LORA_TX_OK;
    packet->battery_mv = 3700U;
    packet->gnss_fix_type = 3U;
    packet->gnss_satellites_used = 10U;
    packet->latitude_1e7_deg = 481353000;
    packet->longitude_1e7_deg = 115820000;
    packet->gnss_altitude_mm = 520000;
    packet->gnss_hdop_0p01 = 90U;
    packet->gnss_vdop_0p01 = 130U;
    packet->ground_speed_0p01_ms = 25U;
    packet->vertical_speed_0p01_ms = 0;
    packet->course_0p01_deg = 9000U;
    packet->baro_pressure_pa = 101325UL;
    packet->baro_temperature_0p01_c = 2200;
    packet->baro_altitude_mm = 520000;
    packet->accel_x_0p01_ms2 = 0;
    packet->accel_y_0p01_ms2 = 0;
    packet->accel_z_0p01_ms2 = 981;
    packet->gyro_x_0p001_rads = 0;
    packet->gyro_y_0p001_rads = 0;
    packet->gyro_z_0p001_rads = 0;
    packet->imu_temperature_0p01_c = 2300;
    packet->mcu_temperature_0p01_c = 2500;
    packet->reset_cause = 0U;
    packet->boot_count = 1U;
    packet->sd_log_record_counter = sequence_number;
    packet->sd_error_counter = 0U;
    packet->sensor_error_counter = 0U;
    packet->command_counter = 0U;
    packet->last_uplink_rssi_dbm = 0;
    packet->last_uplink_snr_db = 0;
    packet->coral_status = 0U;
    packet->coral_result_age_s = 65535U;
    memcpy(packet->coral_payload, "NO_CORAL_DATA", 13U);

    TtcTestTelemetryUpdateCrc(packet);
}
