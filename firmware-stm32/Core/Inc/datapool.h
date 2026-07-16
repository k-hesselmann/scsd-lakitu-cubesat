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
    float    gps_vel_north_mps;  /* NED North velocity */
    float    gps_vel_east_mps;   /* NED East velocity */
    float    gps_vel_down_mps;   /* NED Down velocity */
    float    gps_heading_deg;    /* heading of motion */
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
 * Persisted to reserved flash. FSW/FDIR owns health and flight state. */
typedef struct __attribute__((packed)) {

    uint16_t magic;              /* 0xCAFE */
    uint32_t boot_count;
    uint32_t mission_elapsed_ms;
    uint8_t  flight_phase;       /* FlightPhase_t from fsm.h */
    uint8_t  reset_reason;

    uint16_t equipment_enabled;
    uint16_t equipment_faults;

    uint8_t  gps_timeout_count;
    uint8_t  imu_timeout_count;
    uint8_t  baro_timeout_count;
    uint8_t  coral_timeout_count;
    /* Mirror of TTC's consecutive TX failure count (FMECA T1). Persisted in
     * the SCV; NOT yet in TelemetryPacket_t (v3) or the CSV — add both at
     * the next coordinated format revision (v4). */
    uint8_t  lora_timeout_count;
    uint8_t  sd_fault_count;
    uint8_t  watchdog_reset_count;

    uint16_t last_batt_mv;
    int32_t  baro_ground_alt_cm;

    uint16_t crc16;

} SCV_t;

/* Raw downlink telemetry v3. Fields are copied directly from SensorData_t and
 * SCV_t. Engineering-unit conversions are intentionally performed by the
 * ground station, not by the flight CPU. */
typedef struct __attribute__((packed)) {
    uint8_t  packet_type;        /* 0x01 = telemetry */
    uint8_t  protocol_version;   /* 0x03 = raw datapool/SCV layout */
    uint16_t sequence_number;
    uint32_t tx_uptime_ms;       /* HAL_GetTick() when this frame was built */

    /* SensorData_t snapshot (raw source units and validity flags). */
    uint32_t datapool_timestamp_ms;
    float    gps_lat_deg;
    float    gps_lon_deg;
    float    gps_alt_m;
    float    gps_vvel_mps;       /* positive = upward */
    float    gps_speed_mps;
    uint8_t  gps_valid;
    float    imu_accel_x_g;
    float    imu_accel_y_g;
    float    imu_accel_z_g;
    float    imu_accel_mag_g;
    float    imu_gyro_x_dps;
    float    imu_gyro_y_dps;
    float    imu_gyro_z_dps;
    uint8_t  imu_valid;
    float    baro_pressure_pa;
    float    baro_alt_m;
    float    baro_temp_c;
    uint8_t  baro_valid;
    uint8_t  i2c_bus_state;
    uint16_t batt_voltage_mv;
    uint8_t  batt_valid;
    uint8_t  coral_block[16];
    uint8_t  coral_valid;

    /* SCV_t snapshot, including its persisted CRC. */
    uint16_t scv_magic;
    uint32_t scv_boot_count;
    uint32_t scv_mission_elapsed_ms;
    uint8_t  scv_flight_phase;
    uint8_t  scv_reset_reason;
    uint16_t scv_equipment_enabled;
    uint16_t scv_equipment_faults;
    uint8_t  scv_gps_timeout_count;
    uint8_t  scv_imu_timeout_count;
    uint8_t  scv_baro_timeout_count;
    uint8_t  scv_coral_timeout_count;
    uint8_t  scv_sd_fault_count;
    uint8_t  scv_watchdog_reset_count;
    uint16_t scv_last_batt_mv;
    int32_t  scv_baro_ground_alt_cm;
    uint16_t scv_crc16;

    uint16_t crc16;              /* CRC-16/CCITT over every preceding byte */
} TelemetryPacket_t;

/* Shared datapool instance — defined in datapool.c, extern everywhere else */
extern SensorData_t g_datapool;
extern SCV_t        g_scv;

#endif /* DATAPOOL_H */
