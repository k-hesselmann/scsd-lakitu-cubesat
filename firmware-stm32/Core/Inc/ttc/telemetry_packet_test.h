#ifndef TTC_TELEMETRY_PACKET_TEST_H
#define TTC_TELEMETRY_PACKET_TEST_H

#include <stdint.h>

/* Deprecated protocol-v2 mock definitions. They are retained only because
 * existing CubeIDE generated makefiles still compile the source; TTC no longer
 * calls this code. The active downlink is TelemetryPacket_t protocol v3. */

#define TTC_TEST_TELEMETRY_PACKET_TYPE       0x01U
#define TTC_TEST_TELEMETRY_PROTOCOL_VERSION  0x02U
#define TTC_TEST_TELEMETRY_PACKET_SIZE       104U

typedef enum
{
    TTC_TEST_FLIGHT_BOOT = 0,
    TTC_TEST_FLIGHT_STANDBY = 1,
    TTC_TEST_FLIGHT_LAUNCH = 2,
    TTC_TEST_FLIGHT_ASCENT = 3,
    TTC_TEST_FLIGHT_CRUISE = 4,
    TTC_TEST_FLIGHT_DESCENT = 5,
    TTC_TEST_FLIGHT_LANDED = 6
} TtcTestFlightState_t;

typedef enum
{
    TTC_TEST_STATUS_GNSS_FIX_VALID        = 1U << 0,
    TTC_TEST_STATUS_GNSS_TIME_VALID       = 1U << 1,
    TTC_TEST_STATUS_IMU_VALID             = 1U << 2,
    TTC_TEST_STATUS_BARO_VALID            = 1U << 3,
    TTC_TEST_STATUS_BARO_RANGE_VALID      = 1U << 4,
    TTC_TEST_STATUS_BATTERY_VALID         = 1U << 5,
    TTC_TEST_STATUS_SD_LOGGING_OK         = 1U << 8,
    TTC_TEST_STATUS_LAST_LORA_TX_OK       = 1U << 9
} TtcTestTelemetryStatusFlags_t;

#pragma pack(push, 1)
typedef struct
{
    uint8_t packet_type;
    uint8_t protocol_version;
    uint16_t sequence_number;
    uint32_t utc_timestamp;
    uint32_t obc_uptime_ms;
    uint8_t flight_state;
    uint16_t status_flags;
    uint16_t battery_mv;
    uint8_t gnss_fix_type;
    uint8_t gnss_satellites_used;
    int32_t latitude_1e7_deg;
    int32_t longitude_1e7_deg;
    int32_t gnss_altitude_mm;
    uint16_t gnss_hdop_0p01;
    uint16_t gnss_vdop_0p01;
    uint16_t ground_speed_0p01_ms;
    int16_t vertical_speed_0p01_ms;
    uint16_t course_0p01_deg;
    uint32_t baro_pressure_pa;
    int16_t baro_temperature_0p01_c;
    int32_t baro_altitude_mm;
    int16_t accel_x_0p01_ms2;
    int16_t accel_y_0p01_ms2;
    int16_t accel_z_0p01_ms2;
    int16_t gyro_x_0p001_rads;
    int16_t gyro_y_0p001_rads;
    int16_t gyro_z_0p001_rads;
    int16_t imu_temperature_0p01_c;
    int16_t mcu_temperature_0p01_c;
    uint8_t reset_cause;
    uint16_t boot_count;
    uint32_t sd_log_record_counter;
    uint16_t sd_error_counter;
    uint16_t sensor_error_counter;
    uint16_t command_counter;
    int16_t last_uplink_rssi_dbm;
    int8_t last_uplink_snr_db;
    uint8_t coral_status;
    uint16_t coral_result_age_s;
    uint8_t coral_payload[16];
    uint16_t crc16;
} TtcTestTelemetryPacket_t;
#pragma pack(pop)

uint16_t TtcTestTelemetryCrc16Ccitt(const uint8_t *data, uint16_t length);
void TtcTestTelemetryFillMock(TtcTestTelemetryPacket_t *packet, uint16_t sequence_number);

#endif /* TTC_TELEMETRY_PACKET_TEST_H */
