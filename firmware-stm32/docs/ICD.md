# Interface Control Document
**1U CubeSat Balloon Mission — Software Interfaces**

All multi-byte fields are **little-endian** (native STM32 byte order).  
All structs are **packed** (`__attribute__((packed))`) to avoid compiler padding in transmitted or stored blocks.  
`TBD` marks fields whose definition is deferred to the owning subsystem.

---

## 1. Datapool — `SensorData_t`

**Interface:** IF-006  
**Direction:** CDH writes → FSW reads  
**Update rate:** 1 Hz (written by `CDH_Update()`, read by `FSW_Update()`)  
**Location:** global variable in `datapool.h`, accessible to all modules

CDH is responsible for filling every field each cycle. If a sensor read fails,
CDH sets the corresponding `_valid` flag to `0` and leaves the value field at
its last known good value. FSW must check `_valid` before using any field.

```c
typedef struct __attribute__((packed)) {

    /* ── Timestamp ─────────────────────────────────────────── */
    uint32_t timestamp_ms;       /* HAL_GetTick() at time of CDH_Update() */

    /* ── GPS (u-blox NEO-M8N, IF-001) ──────────────────────── */
    float    gps_lat_deg;        /* degrees, WGS-84 */
    float    gps_lon_deg;        /* degrees, WGS-84 */
    float    gps_alt_m;          /* metres above MSL */
    float    gps_vvel_mps;       /* vertical velocity, positive = upward */
    float    gps_speed_mps;      /* 3D speed magnitude */
    uint8_t  gps_valid;          /* 1 = fresh fix this cycle, 0 = no fix */

    /* ── IMU (MPU-6050, IF-002) ─────────────────────────────── */
    float    imu_accel_x_g;      /* body-frame X, g */
    float    imu_accel_y_g;      /* body-frame Y, g */
    float    imu_accel_z_g;      /* body-frame Z, +1.0 g on ground (gravity up) */
    float    imu_accel_mag_g;    /* magnitude: sqrt(x²+y²+z²) */
    float    imu_gyro_x_dps;     /* deg/s */
    float    imu_gyro_y_dps;     /* deg/s */
    float    imu_gyro_z_dps;     /* deg/s */
    uint8_t  imu_valid;

    /* ── Barometer (MS5607, IF-003) ─────────────────────────── */
    float    baro_pressure_pa;   /* Pascal */
    float    baro_alt_m;         /* metres above Standby ground baseline */
    float    baro_temp_c;        /* degrees Celsius */
    uint8_t  baro_valid;

    /* ── EPS (ADC via IF-008) ───────────────────────────────── */
    uint16_t batt_voltage_mv;    /* millivolts, after resistor divider scaling */
    uint8_t  batt_valid;

    /* ── Coral payload (UART, IF-009 / FR-027) ──────────────── */
    uint8_t  coral_block[16];    /* raw 16-byte output block, see Section 4 */
    uint8_t  coral_valid;        /* 1 = block received this cycle */

} SensorData_t;
```

**Notes:**
- `baro_alt_m` is relative to the ground baseline recorded during Standby. CDH
  computes and stores this baseline on startup. FSW must not recompute it.
- `gps_vvel_mps` is derived from u-blox NED velocity output. CDH negates the
  down component so positive = upward, consistent with `baro_alt_m` convention.
- If `gps_valid == 0`, FSW uses `baro_alt_m` and a baro-derived vertical velocity
  as fallback per FR-021.

---

## 2. Spacecraft Configuration Vector — `SCV_t`

**Interface:** FR-020  
**Direction:** CDH writes sensor fields; FSW writes `flight_phase` on transition  
**Storage:** STM32 internal flash, dedicated page (address TBD by OBC)  
**Update rate:** CDH updates health fields at 1 Hz; FSW updates `flight_phase`
within 1 second of any state transition (FR-010)

The SCV is the single persistent system state record. It survives power cycles
and resets. On boot, CDH reads it from flash and makes it available. FSW reads
`flight_phase` to restore state after an unexpected reset (FR-010).

A CRC-16 covers all fields except `crc16` itself. Any consumer reading the SCV
must verify the CRC before trusting the contents.

```c
typedef struct __attribute__((packed)) {

    /* ── Identity ───────────────────────────────────────────── */
    uint16_t magic;              /* 0xCAFE — sanity check on flash read */
    uint16_t reboot_count;       /* incremented by CDH on every boot */
    uint32_t last_update_ms;     /* HAL_GetTick() of last write */

    /* ── Flight state (written by FSW) ─────────────────────── */
    uint8_t  flight_phase;       /* FlightPhase_t enum value, see fsm.h */

    /* ── Equipment health (written by CDH) ──────────────────── */
    uint8_t  sensor_faults;      /* bitmask, see fault bit definitions below */
    uint16_t last_batt_mv;       /* last valid battery voltage reading */

    /* ── FDIR counters (written by CDH) ─────────────────────── */
    uint8_t  gps_timeout_count;  /* consecutive cycles with gps_valid == 0 */
    uint8_t  imu_timeout_count;
    uint8_t  baro_timeout_count;
    uint8_t  coral_timeout_count;

    /* ── Integrity ───────────────────────────────────────────── */
    uint16_t crc16;              /* CRC-16/CCITT over all preceding bytes */

} SCV_t;
```

**`sensor_faults` bitmask:**

| Bit | Mask   | Meaning                        |
|-----|--------|-------------------------------|
| 0   | `0x01` | GPS fault (no fix > 30 s)     |
| 1   | `0x02` | IMU fault (read error)        |
| 2   | `0x04` | Barometer fault (read error)  |
| 3   | `0x08` | SD card fault (write failed)  |
| 4   | `0x10` | Coral payload fault (no data) |
| 5–7 | —      | Reserved                      |

**Ownership summary:**

| Field | Written by | Read by |
|---|---|---|
| `magic`, `reboot_count` | CDH (boot) | CDH |
| `flight_phase` | FSW (on transition) | FSW (on boot) |
| `sensor_faults`, `*_timeout_count`, `last_batt_mv` | CDH (1 Hz) | FSW (FDIR decisions) |
| `crc16` | whoever writes last | all readers before trusting data |

---

## 3. Telemetry Packet — `TelemetryPacket_t`

**Interface:** IF-007, FR-023  
**Direction:** FSW assembles → TTC transmits  
**Rate:** 1 packet per 20 seconds (FR-022)  
**Transport:** LoRa RFM95W SPI, SF9, BW 125 kHz, CR 4/5, 868 MHz (FR-026)

```c
typedef struct __attribute__((packed)) {

    /* ── Header (4 bytes) ───────────────────────────────────── */
    uint8_t  sync[2];            /* 0xAA 0x55 */
    uint8_t  packet_type;        /* 0x01 = telemetry */
    uint8_t  packet_length;      /* total packet length in bytes including header */

    /* ── Metadata (7 bytes) ─────────────────────────────────── */
    uint16_t sequence_number;    /* wraps at 65535 */
    uint32_t timestamp_ms;       /* mission elapsed time, ms since boot */
    uint8_t  flight_phase;       /* FlightPhase_t enum value */

    /* ── GPS (12 bytes) ─────────────────────────────────────── */
    float    gps_lat_deg;
    float    gps_lon_deg;
    float    gps_alt_m;

    /* ── IMU (12 bytes) ─────────────────────────────────────── */
    float    imu_accel_x_g;
    float    imu_accel_y_g;
    float    imu_accel_z_g;

    /* ── EPS (2 bytes) ──────────────────────────────────────── */
    uint16_t batt_voltage_mv;

    /* ── Payload excerpt (8 bytes) ──────────────────────────── */
    uint8_t  coral_excerpt[8];   /* TBD — first 8 bytes of coral_block, or
                                    a selected subset defined by Payload team */

    /* ── Integrity (2 bytes) ────────────────────────────────── */
    uint16_t crc16;              /* CRC-16/CCITT over all preceding bytes */

} TelemetryPacket_t;            /* total: 49 bytes */
```

**Sizing check against FR-026:**
- Packet size: 49 bytes = 392 bits
- LoRa data rate at SF9/BW125/CR4-5: 1758 bps
- Air time per packet: ~223 ms
- TX interval: 20 s → duty cycle: 223 ms / 20 000 ms = **1.1%**
- This marginally exceeds the 1% ISM band limit (FR-022, CR-005). TTC must
  verify actual air time and adjust packet size or interval accordingly.

---

## 4. Coral Payload Block

**Interface:** FR-027  
**Direction:** Coral Dev Board Micro → OBC  
**Transport:** UART, 115200 baud, 8N1  
**Rate:** 1 block per second  
**Block size:** 16 bytes fixed

The OBC triggers inference and receives a fixed 16-byte output block. Internal
field layout is defined by the Payload subsystem and is TBD. The OBC treats
the block as opaque — it stores all 16 bytes to the SD card (`coral_block` in
the datapool) and forwards a subset to telemetry (`coral_excerpt` in the
telemetry packet).

```
Byte offset   Field             Owner
───────────   ───────────────   ───────
0–15          TBD               Payload
```

When the Payload team defines the block layout, this section must be updated
and `coral_excerpt` field selection in `TelemetryPacket_t` confirmed.

---

## 5. SD Card Log Record

**Interface:** FR-016, FR-017  
**Direction:** CDH writes  
**Rate:** ≥ 1 record per second  
**Format:** CSV or binary TBD by CDH

Minimum fields per record (FR-016):

| Field | Source |
|---|---|
| Timestamp (ms) | HAL_GetTick() |
| Flight phase | FSW via SCV |
| GPS lat, lon, alt | datapool |
| IMU accel X/Y/Z, gyro X/Y/Z | datapool |
| Baro pressure, altitude | datapool |
| Coral block (16 bytes) | datapool |
| Battery voltage (mV) | datapool |

CDH decides the exact file format, column order, and filename convention.
FSW has no direct SD card access.

---

## 6. Hardware Interface Summary

For reference only — full details in OBC and CDH subsystem sections.

| Bus | Peripheral | Protocol | Speed | Owner |
|---|---|---|---|---|
| UART1 | GPS (NEO-M8N) | NMEA 0183 | 9600 baud | CDH |
| UART2 | Debug / ST-Link | — | 115200 baud | OBC |
| UART3 | Coral Dev Board Micro | Raw binary | 115200 baud | OBC/FSW |
| I2C1 | IMU (MPU-6050) | — | 400 kHz | CDH |
| I2C2 | Barometer (MS5607) | — | 400 kHz | CDH |
| SPI1 | SD card | — | up to 25 MHz | CDH |
| SPI2 | LoRa (RFM95W) | — | up to 10 MHz | TTC |
| ADC | Battery voltage divider | — | — | CDH |

UART/I2C/SPI port assignments are TBD pending OBC pin allocation in STM32CubeMX.
Update this table when OBC finalises the `.ioc` configuration.
