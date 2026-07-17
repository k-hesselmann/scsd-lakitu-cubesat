# Interface Control Document
**1U CubeSat Balloon Mission — Software Interfaces**

All multi-byte fields are **little-endian** (native STM32 byte order).
All structs are **packed** (`__attribute__((packed))`) to avoid compiler padding in transmitted or stored blocks.
`TBD` marks fields whose definition is deferred to the owning subsystem.

---

## 1. Datapool — `SensorData_t`

**Interface:** IF-006
**Direction:** CDH writes sensor fields → FDIR writes health state → FSW reads
**Update rate:** 1 Hz (`CDH_Update()`, then `FDIR_Update()`, then `FSW_Update()`)
**Location:** global variable in `datapool.h`, accessible to all modules

CDH is responsible for filling sensor fields each cycle. If a sensor read fails,
CDH sets the corresponding `_valid` flag to `0` and leaves the value field at
its last known good value. FDIR consumes those flags to update SCV health fields,
request CDH-owned recovery functions, and report shared bus recovery state. FSW
must check `_valid` and SCV fault bits before using any field.

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

    /* ── CDH/FDIR bus state ────────────────────────────────── */
    uint8_t  i2c_bus_state;      /* CDH_FDIR_BUS_* value */

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
- `i2c_bus_state` is written by FDIR after processing the CDH nonblocking I2C
  bus restart state machine.

---

## 2. Spacecraft Configuration Vector — `SCV_t`

**Interface:** FR-020
**Direction:** FSW/FDIR owns the SCV; other subsystems provide source health data
**Storage:** STM32 internal flash, last 2 KiB page reserved for SCV
**Update rate:** FSW updates `flight_phase` on transition; FDIR updates health
fields on each 1 Hz monitoring cycle.

The SCV is the single persistent system state record. It survives power cycles
and resets. FSW/FDIR initialise and validate it, restore `flight_phase` after
unexpected reset, and persist selected fields to flash.

The reserved flash region is:

| Constant | Value | Meaning |
|---|---:|---|
| `SCV_FLASH_ADDR` | `0x080FF800` | Start of the final STM32L476RG 2 KiB flash page |
| `SCV_FLASH_SIZE` | `0x00000800` | Reserved SCV flash size, 2048 bytes |

A CRC-16 covers all fields except `crc16` itself. Any consumer reading the SCV
must verify the CRC before trusting the contents.

```c
typedef struct __attribute__((packed)) {

    uint16_t magic;              /* 0xCAFE, initialized SCV marker */
    uint32_t boot_count;
    uint32_t mission_elapsed_ms;
    uint8_t  flight_phase;       /* FlightPhase_t enum value, see fsm.h */
    uint8_t  reset_reason;       /* reset reason code, see table below */

    uint16_t equipment_enabled;  /* equipment allowed for use/recovery */
    uint16_t equipment_faults;   /* equipment currently not trusted */

    uint8_t  gps_timeout_count;
    uint8_t  imu_timeout_count;
    uint8_t  baro_timeout_count;
    uint8_t  coral_timeout_count;
    uint8_t  sd_fault_count;
    uint8_t  watchdog_reset_count;

    uint16_t last_batt_mv;
    int32_t  baro_ground_alt_cm;

    uint16_t crc16;              /* CRC-16/CCITT over all preceding bytes */

} SCV_t;
```

`SCV_MAGIC = 0xCAFE` is a recognizable sanity marker, not a cryptographic value.
Erased STM32 flash reads as `0xFFFF`, so `0xCAFE` distinguishes an initialized
SCV record from blank flash before the CRC is checked.

**Equipment bitmask definitions:**

| Bit | Mask   | Meaning                        |
|-----|--------|-------------------------------|
| 0   | `0x0001` | GPS                          |
| 1   | `0x0002` | IMU                          |
| 2   | `0x0004` | Barometer                    |
| 3   | `0x0008` | Coral payload                |
| 4   | `0x0010` | SD card                      |
| 5   | `0x0020` | LoRa                         |
| 6   | `0x0040` | EPS ADC / battery monitor    |
| 7–15 | —       | Reserved                     |

`equipment_enabled` and `equipment_faults` use the same bit assignments. A set
bit in `equipment_enabled` means the equipment may be used or recovered. A set
bit in `equipment_faults` means FSW/FDIR should not trust the equipment data.

**Reset reason codes:**

| Value | Meaning |
|---|---|
| `0` | Unknown / not yet classified |
| `1` | Power-on reset |
| `2` | Watchdog reset |
| `3` | Software reset |

Fields without a valid value use reserved impossible sentinels, e.g. `0xFFFF`
for unknown `uint16_t` measurements and `INT32_MIN` for unknown signed values.

**Default SCV values on invalid magic or CRC:**

| Field | Default |
|---|---:|
| `magic` | `SCV_MAGIC` |
| `boot_count` | `0`, then incremented during boot handling |
| `mission_elapsed_ms` | `0` |
| `flight_phase` | `PHASE_STANDBY` |
| `reset_reason` | `RESET_REASON_UNKNOWN` |
| `equipment_enabled` | `EQUIPMENT_ALL_NOMINAL` |
| `equipment_faults` | `0` |
| `gps_timeout_count`, `imu_timeout_count`, `baro_timeout_count`, `coral_timeout_count` | `0` |
| `sd_fault_count`, `watchdog_reset_count` | `0` |
| `last_batt_mv` | `SCV_INVALID_U16` |
| `baro_ground_alt_cm` | `SCV_INVALID_I32` |
| `crc16` | CRC-16/CCITT over all preceding bytes |

**Ownership summary:**

| Field | Written by | Read by |
|---|---|---|
| `magic`, `boot_count`, `mission_elapsed_ms`, `flight_phase`, `crc16` | FSW/FDIR | all SCV consumers |
| `reset_reason`, `watchdog_reset_count` | FSW/FDIR after reset classification | FSW/FDIR, telemetry |
| `equipment_enabled`, `equipment_faults`, `*_timeout_count` | FDIR | FSW, CDH, telemetry |
| `last_batt_mv` | FDIR from CDH/EPS source data | FSW/FDIR, telemetry |
| `baro_ground_alt_cm` | FSW/FDIR from barometer baseline source | FSW/FDIR |

**Current FDIR policy:** IMU and barometer recovery is available through
CDH-owned recovery wrappers. FDIR marks equipment faulty after the configured
timeout limit, calls the relevant CDH recovery wrapper no more often than once
per `FDIR_REINIT_PERIOD_MS`, and starts a nonblocking I2C bus restart when both
I2C sensors are faulted. GPS, Coral, SD, LoRa, and EPS recovery ownership remains
reserved for their subsystem handlers.

---

## 3. Telemetry Packet ? `TelemetryPacket_t` (protocol v3)

**Direction:** OBC ? ground station over LoRa
**Wire order:** packed, little-endian
**Total length:** **128 bytes**
**Integrity:** CRC-16/CCITT (`0xFFFF` initial value, polynomial `0x1021`) over
bytes 0?125; the final two bytes are the packet CRC.

The v3 packet is a raw snapshot: it copies every `SensorData_t` field and every
`SCV_t` field (including the SCV CRC) without flight-side rescaling or unit
conversion. The only generated values are `packet_type` (`0x01`),
`protocol_version` (`0x03`), sequence number, transmit uptime, and packet CRC.

| Byte range | Fields | Source / notes |
|---|---|---|
| 0?7 | type, version, sequence, `tx_uptime_ms` | TTC/FSW transport metadata |
| 8?95 | complete `SensorData_t` snapshot | Direct copies; source units are preserved |
| 96?125 | complete `SCV_t` snapshot | Direct copies, including `scv_crc16` |
| 126?127 | `crc16` | Packet CRC-16/CCITT |

Ground conversion rules:

- GPS degrees, metres, and m/s; barometer Pa, metres, and ?C; and battery mV
  are already the native datapool units and are displayed directly.
- IMU acceleration is transmitted in **g** and converted on ground to m/s?
  using `9.80665`.
- IMU gyro is transmitted in **deg/s** and converted on ground to rad/s.
- Validity flags and SCV equipment bitmasks are transmitted independently;
  ground derives display health flags from them.
- UTC, GNSS fix/satellite/HDOP/VDOP/course, IMU/MCU temperatures, command
  counters, and OBC uplink metrics are not presently available in the datapool
  or SCV and are reported as unavailable by the ground station.


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
