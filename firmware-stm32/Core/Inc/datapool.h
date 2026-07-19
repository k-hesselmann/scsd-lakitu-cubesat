#ifndef DATAPOOL_H
#define DATAPOOL_H

#include <stdint.h>
#include <limits.h>

/* SCV_MAGIC is a recognizable sanity marker. Erased STM32 flash reads as
 * 0xFFFF, so 0xCAFE lets boot code distinguish an initialized SCV record from
 * blank/corrupt storage before checking crc16. */
#define SCV_MAGIC                 0xCAFEU

#define SCV_FLASH_ADDR            0x080FF800UL
#define SCV_FLASH_SIZE            0x00000800UL

#define SCV_INVALID_U8            0xFFU
#define SCV_INVALID_U16           0xFFFFU
#define SCV_INVALID_U32           0xFFFFFFFFUL
#define SCV_INVALID_I32           INT32_MIN

#define EQUIPMENT_GPS             (1U << 0)
#define EQUIPMENT_IMU             (1U << 1)
#define EQUIPMENT_BARO            (1U << 2)
#define EQUIPMENT_CORAL           (1U << 3)
#define EQUIPMENT_SD              (1U << 4)
#define EQUIPMENT_LORA            (1U << 5)
#define EQUIPMENT_EPS_ADC         (1U << 6)
/* Pseudo-equipment: datapool freshness fault raised by FDIR when CDH stops
 * advancing timestamp_ms (FMECA C10). Never part of EQUIPMENT_ALL_NOMINAL. */
#define EQUIPMENT_CDH             (1U << 15)

#define EQUIPMENT_ALL_NOMINAL     (EQUIPMENT_GPS | EQUIPMENT_IMU | EQUIPMENT_BARO | \
                                   EQUIPMENT_CORAL | EQUIPMENT_SD | EQUIPMENT_LORA | \
                                   EQUIPMENT_EPS_ADC)

#define RESET_REASON_UNKNOWN      0U
#define RESET_REASON_POWER_ON     1U
#define RESET_REASON_WATCHDOG     2U
#define RESET_REASON_SOFTWARE     3U
#define RESET_REASON_INVALID      SCV_INVALID_U8

/* ICD Section 1 — SensorData_t
 * Written by CDH_Update() at 1 Hz. Read by FSW_Update() and TTC_Transmit().
 * Check *_valid flags before using any field. */
typedef struct __attribute__((packed)) {

    uint32_t timestamp_ms;

    /* GPS (u-blox MAX-M10S) */
    float    gps_lat_deg;
    float    gps_lon_deg;
    float    gps_alt_m;
    float    gps_speed_mps;      /* 2D ground speed magnitude */
    float    gps_vel_down_mps;   /* vertical velocity from altitude changes */
    float    gps_heading_deg;    /* heading of motion */
    uint32_t gps_utc_time;       /* HHMMSS format from GPRMC */
    uint8_t  gps_num_satellites;
    uint8_t  gps_fix_type;
    uint8_t  gps_valid;

    /* IMU (MPU-6050) */
    float    imu_accel_x_g;
    float    imu_accel_y_g;
    float    imu_accel_z_g;
    float    imu_accel_mag_g;
    float    imu_gyro_x_dps;
    float    imu_gyro_y_dps;
    float    imu_gyro_z_dps;
    uint8_t  imu_valid;

    /* Barometer (MS5607) — altitude relative to Standby ground baseline */
    float    baro_pressure_pa;
    float    baro_alt_m;
    float    baro_temp_c;
    uint8_t  baro_valid;

    /* Shared I2C bus recovery state reported by FDIR. */
    uint8_t  i2c_bus_state;

    /* EPS */
    uint16_t batt_voltage_mv;
    uint8_t  batt_valid;

    /* Coral payload (16-byte opaque block, FR-027) */
    uint8_t  coral_block[16];
    uint8_t  coral_valid;

} SensorData_t;

/* ICD Section 2 — SCV_t
 * Runtime configuration/health state. FSW/FDIR owns health and flight state;
 * a separate persistence integration may load/store this structure. */
typedef struct __attribute__((packed)) {

    uint16_t magic;              /* 0xCAFE */
    uint32_t boot_count;
    uint32_t mission_elapsed_ms;
    uint8_t  flight_phase;       /* FlightPhase_t from fsm.h */
    uint8_t  reset_reason;

    uint16_t equipment_enabled;
    uint16_t equipment_faults;
    /* Ground/bench-settable equipment disable, independent of reduced-mode
     * staging. Persists across boots as-is (unlike equipment_enabled, which
     * FDIR_ApplyReducedMode() rebuilds from EQUIPMENT_ALL_NOMINAL every
     * boot) — folded into equipment_enabled/s_subsys_enabled[] there. */
    uint16_t equipment_manual_disable;

    uint8_t  gps_timeout_count;
    uint8_t  imu_timeout_count;
    uint8_t  baro_timeout_count;
    uint8_t  coral_timeout_count;
    /* Mirror of TTC's consecutive TX failure count (FMECA T1). */
    uint8_t  lora_timeout_count;
    /* Lifetime (not consecutive) count of failed LoRa TX attempts since boot,
     * mirrored from TTC_FDIR_Health_t.lora_tx_fault_counter. */
    uint16_t lora_tx_fault_counter;
    uint8_t  sd_fault_count;
    uint8_t  watchdog_reset_count;

    uint16_t last_batt_mv;
    int32_t  baro_ground_alt_cm;

    uint16_t crc16;

} SCV_t;

/* Volatile TTC/FDIR health snapshot. It is intentionally separate from SCV
 * because it describes only the current boot's modem recovery activity. */
typedef struct __attribute__((packed)) {
    uint8_t  last_event;           /* LoRaHealthEvent_t */
    uint8_t  consecutive_failures; /* consecutive SPI/TX failures */
    uint8_t  recovery_count;       /* modem reset/reinit attempts since boot */
    uint32_t last_success_ms;      /* HAL tick of most recent TxDone */
    uint8_t  rx_mode_active;       /* RX-continuous mode readback succeeded */
    uint8_t  last_rx_status;       /* LoRaRxHealthStatus_t */
    uint16_t rx_packet_count;      /* CRC-valid packets received since boot */
    uint16_t rx_crc_error_count;   /* packets rejected by modem CRC */
    uint16_t ack_timeout_count;    /* telemetry packets never ACKed by ground */
    uint32_t last_rx_ms;           /* HAL tick of most recent valid packet */
} LoRaHealth_t;

typedef enum {
    LORA_EVENT_NONE = 0,
    LORA_EVENT_INIT_OK,
    LORA_EVENT_INIT_FAIL,
    LORA_EVENT_TX_OK,
    LORA_EVENT_TX_SPI_FAIL,
    LORA_EVENT_TX_TIMEOUT,
    LORA_EVENT_TX_BAD_LENGTH,
    LORA_EVENT_NOT_READY,
    LORA_EVENT_CONFIG_FAIL,
    LORA_EVENT_RX_OK,
    LORA_EVENT_RX_CRC_ERROR,
    LORA_EVENT_RX_SPI_FAIL,
    LORA_EVENT_RX_MODE_FAIL,
    LORA_EVENT_ACK_TIMEOUT
} LoRaHealthEvent_t;

typedef enum {
    LORA_RX_HEALTH_NONE = 0,
    LORA_RX_HEALTH_ACTIVE,
    LORA_RX_HEALTH_PACKET_OK,
    LORA_RX_HEALTH_CRC_ERROR,
    LORA_RX_HEALTH_BAD_LENGTH,
    LORA_RX_HEALTH_SPI_ERROR,
    LORA_RX_HEALTH_MODE_ERROR
} LoRaRxHealthStatus_t;

/* Most recent ground command and telemetry acknowledgement state observed by
 * TTC. Command and ACK statuses are independent so an ACK cannot hide command
 * confirmation before ground receives it. */
typedef enum {
    UPLINK_COMMAND_NONE = 0,
    UPLINK_COMMAND_REQUEST_TELEMETRY,
    UPLINK_COMMAND_ACKNOWLEDGE
} UplinkCommand_t;

typedef enum {
    UPLINK_STATUS_NONE = 0,
    UPLINK_STATUS_ACCEPTED,
    UPLINK_STATUS_INVALID_FORMAT,
    UPLINK_STATUS_UNSUPPORTED,
    UPLINK_STATUS_DUPLICATE,
    UPLINK_STATUS_UNEXPECTED_ACK
} UplinkStatus_t;

typedef struct __attribute__((packed)) {
    uint8_t  last_command;        /* UplinkCommand_t for last CMD packet */
    uint8_t  last_command_status; /* UplinkStatus_t; ACK never overwrites it */
    uint8_t  last_ack_status;     /* UplinkStatus_t for last ACK packet */
    uint16_t last_command_id;     /* ID supplied by CMD,<id>,<verb> */
    uint16_t last_ack_sequence;   /* Last accepted/duplicate ACK sequence */
    uint16_t command_count;       /* Unique valid commands since boot */
} UplinkState_t;

/* Compact downlink telemetry v8. Every member is a fixed-width integer so the
 * 92-byte wire format is compiler-independent once packed. Invalid/never-seen
 * signed measurements use INT16_MIN/INT32_MIN; invalid unsigned measurements
 * use their maximum value. Validity flags remain authoritative. */
#define TELEMETRY_PACKET_TYPE               0x01U
#define TELEMETRY_PROTOCOL_VERSION          0x08U
#define TELEMETRY_PACKET_V8_SIZE            92U

#define TELEMETRY_VALID_GPS                 (1U << 0)
#define TELEMETRY_VALID_IMU                 (1U << 1)
#define TELEMETRY_VALID_BARO                (1U << 2)
#define TELEMETRY_VALID_BATTERY             (1U << 3)
#define TELEMETRY_VALID_CORAL               (1U << 4)

#define TELEMETRY_LORA_RX_STATUS_MASK       0x07U
#define TELEMETRY_LORA_RX_ACTIVE            (1U << 3)

#define TELEMETRY_UPLINK_STATUS_MASK        0x07U
#define TELEMETRY_UPLINK_ACK_STATUS_SHIFT   3U

typedef struct __attribute__((packed)) {
    uint8_t  packet_type;
    uint8_t  protocol_version;
    uint16_t sequence_number;
    uint32_t tx_uptime_s;
    uint32_t mission_elapsed_s;
    uint16_t boot_count_sat;
    uint8_t  flight_phase;
    uint8_t  reset_reason;
    uint8_t  validity_flags;
    uint8_t  i2c_bus_state;
    uint16_t equipment_enabled;
    uint16_t equipment_faults;
    uint16_t sample_age_ms_sat;
    uint8_t  watchdog_reset_count;
    uint8_t  sd_fault_count;

    int32_t  latitude_e7;
    int32_t  longitude_e7;
    int32_t  gnss_altitude_dm;
    int16_t  vertical_speed_cms; /* positive = upward */
    uint16_t ground_speed_cms;
    uint16_t course_cdeg;
    uint32_t gnss_utc_sod;
    uint8_t  gnss_satellites;
    uint8_t  gnss_fix_type;

    int16_t  accel_x_mg;
    int16_t  accel_y_mg;
    int16_t  accel_z_mg;
    int16_t  gyro_x_ddeg_s;
    int16_t  gyro_y_ddeg_s;
    int16_t  gyro_z_ddeg_s;

    uint32_t baro_pressure_pa;
    int32_t  baro_altitude_dm;
    int16_t  baro_temperature_cdeg;
    uint16_t battery_mv;

    uint16_t coral_sequence_low;
    uint16_t coral_fraction_q16;
    uint8_t  coral_status;
    uint16_t coral_age_s_sat;

    uint8_t  lora_last_event;
    uint8_t  lora_consecutive_failures;
    uint8_t  lora_recovery_count;
    uint8_t  lora_rx_state;
    uint8_t  lora_tx_fault_count_sat;
    uint8_t  lora_ack_timeout_count_sat;
    uint16_t last_command_id;
    uint8_t  uplink_state;       /* CMD status bits 0..2, ACK status bits 3..5 */

    uint16_t crc16;              /* CRC-16/CCITT over bytes 0..89 */
} TelemetryPacket_t;

/* Shared datapool instance — defined in datapool.c, extern everywhere else */
extern SensorData_t g_datapool;
extern SCV_t        g_scv;

#endif /* DATAPOOL_H */
