#ifndef DATAPOOL_H
#define DATAPOOL_H

#include <stdint.h>

/* ICD Section 1 — SensorData_t
 * Written by CDH_Update() at 1 Hz. Read by FSW_Update() and TTC_Transmit().
 * Check *_valid flags before using any field. */
typedef struct __attribute__((packed)) {

    uint32_t timestamp_ms;

    /* GPS (u-blox NEO-M8N) */
    float    gps_lat_deg;
    float    gps_lon_deg;
    float    gps_alt_m;
    float    gps_vvel_mps;       /* positive = upward */
    float    gps_speed_mps;      /* 3D magnitude */
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

    /* EPS */
    uint16_t batt_voltage_mv;
    uint8_t  batt_valid;

    /* Coral payload (16-byte opaque block, FR-027) */
    uint8_t  coral_block[16];
    uint8_t  coral_valid;

} SensorData_t;

/* ICD Section 2 — SCV_t
 * Persisted to NVM. CDH writes health fields; FSW writes flight_phase. */
typedef struct __attribute__((packed)) {

    uint16_t magic;              /* 0xCAFE */
    uint16_t reboot_count;
    uint32_t last_update_ms;

    uint8_t  flight_phase;       /* FlightPhase_t from fsm.h */

    uint8_t  sensor_faults;      /* bitmask: bit0=GPS bit1=IMU bit2=Baro bit3=SD bit4=Coral */
    uint16_t last_batt_mv;

    uint8_t  gps_timeout_count;
    uint8_t  imu_timeout_count;
    uint8_t  baro_timeout_count;
    uint8_t  coral_timeout_count;

    uint16_t crc16;

} SCV_t;

/* ICD Section 3 — TelemetryPacket_t
 * Assembled by FSW, transmitted by TTC every 20 s. */
typedef struct __attribute__((packed)) {

    uint8_t  sync[2];            /* 0xAA 0x55 */
    uint8_t  packet_type;        /* 0x01 = telemetry */
    uint8_t  packet_length;

    uint16_t sequence_number;
    uint32_t timestamp_ms;
    uint8_t  flight_phase;

    float    gps_lat_deg;
    float    gps_lon_deg;
    float    gps_alt_m;

    float    imu_accel_x_g;
    float    imu_accel_y_g;
    float    imu_accel_z_g;

    uint16_t batt_voltage_mv;

    uint8_t  coral_excerpt[8];   /* first 8 bytes of coral_block — TBD by Payload */

    uint16_t crc16;

} TelemetryPacket_t;

/* Shared datapool instance — defined in datapool.c, extern everywhere else */
extern SensorData_t g_datapool;
extern SCV_t        g_scv;

#endif /* DATAPOOL_H */
