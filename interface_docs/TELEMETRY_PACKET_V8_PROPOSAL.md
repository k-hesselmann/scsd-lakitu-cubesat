# Telemetry packet v8 proposal

Status: implemented in firmware and ground station. The ground station accepts protocol v8 only.

## Recommendation

Replace the current 155-byte protocol-v7 raw-structure dump with a fixed,
little-endian **92-byte protocol-v8 packet**. The proposed packet retains the
measurements and state needed to answer the ground team's two main questions:

1. What is the payload measuring now?
2. Is each spacecraft subsystem healthy and producing trustworthy data?

The reduction comes from fixed-point encoding, removing derived and duplicated
values, and sending decoded Coral results instead of its complete internal
16-byte block. It does not depend on C compiler padding or bitfields.

At the configured 868 MHz, SF9, 125 kHz, CR 4/5 radio settings, the estimated
airtime of one explicit-header packet with radio CRC enabled is:

| Layout | Payload | Estimated airtime |
|---|---:|---:|
| Current v7 | 155 bytes | 800 ms |
| Proposed v8 | 92 bytes | 513 ms |

This is a **41% payload-size reduction** and approximately a **36% airtime
reduction** per transmission or retry.

## Findings in the current implementation

Useful data already produced onboard but omitted from v7:

- GNSS UTC (`gps_utc_time`)
- GNSS satellites used (`gps_num_satellites`)
- GNSS fix type (`gps_fix_type`)
- GNSS course/heading (`gps_heading_deg`)
- lifetime LoRa transmit-failure count (`SCV_t.lora_tx_fault_counter`)
- the now-defined Coral result fields inside `coral_block`

Fields that consume downlink space without adding useful live observability:

- `imu_accel_mag_g`, because ground can derive it from X/Y/Z
- `datapool_timestamp_ms`, because only sample age relative to packet-build
  time is needed
- `scv_last_batt_mv`, which duplicates the transmitted battery value
- `scv_magic` and `scv_crc16`, which are flash-persistence internals; the SCV
  CRC is explicitly stale between backups
- `scv_baro_ground_alt_cm`, which is currently never populated
- the four per-sensor timeout counters, which duplicate validity flags and the
  equipment-fault mask for live operations
- the full 16-byte Coral block, including reserved bytes, receive tick, frame
  counter, and duplicate percentage/raw-fraction representations
- absolute LoRa event timestamps and cumulative RX counters, which are less
  actionable than current radio state and failure indicators
- `uplink.last_command`, `last_ack_sequence`, and `command_count`; ground can
  track these locally, while a separately latched command ID/status plus ACK
  status is sufficient for reliable-command and telemetry-ACK diagnosis

Legacy ground-station properties whose source still does not exist should not
be allocated wire bytes: GNSS HDOP/VDOP, IMU temperature, MCU temperature, SD
record count, generic sensor-error count, and onboard uplink RSSI/SNR. They can
remain absent until a real producer and operational use are defined.

## Proposed 92-byte wire layout

All multi-byte values are packed **little-endian**. Offsets are absolute byte
offsets. Signed values use two's-complement representation.

| Offset | Size | Wire field | Type | Scale / interpretation |
|---:|---:|---|---|---|
| 0 | 1 | `packet_type` | `uint8` | `0x01` telemetry |
| 1 | 1 | `protocol_version` | `uint8` | `0x08` |
| 2 | 2 | `sequence_number` | `uint16` | unchanged on a retry |
| 4 | 4 | `tx_uptime_s` | `uint32` | OBC uptime, seconds |
| 8 | 4 | `mission_elapsed_s` | `uint32` | persistent mission time, seconds |
| 12 | 2 | `boot_count_sat` | `uint16` | boot count, saturated at 65535 |
| 14 | 1 | `flight_phase` | `uint8` | existing `FlightPhase_t` values |
| 15 | 1 | `reset_reason` | `uint8` | existing reset-reason values |
| 16 | 1 | `validity_flags` | `uint8` | validity bits defined below |
| 17 | 1 | `i2c_bus_state` | `uint8` | 0 idle, 1 restart, 2 hold |
| 18 | 2 | `equipment_enabled` | `uint16` | existing policy-plane mask |
| 20 | 2 | `equipment_faults` | `uint16` | existing FDIR fault mask |
| 22 | 2 | `sample_age_ms_sat` | `uint16` | `tx_tick - datapool_timestamp`; saturating |
| 24 | 1 | `watchdog_reset_count` | `uint8` | persisted, saturating |
| 25 | 1 | `sd_fault_count` | `uint8` | persisted, saturating |
| 26 | 4 | `latitude_e7` | `int32` | degrees x 10,000,000 |
| 30 | 4 | `longitude_e7` | `int32` | degrees x 10,000,000 |
| 34 | 4 | `gnss_altitude_dm` | `int32` | altitude x 10, decimetres |
| 38 | 2 | `vertical_speed_cms` | `int16` | positive upward, m/s x 100 |
| 40 | 2 | `ground_speed_cms` | `uint16` | m/s x 100 |
| 42 | 2 | `course_cdeg` | `uint16` | degrees x 100, range 0..35999 |
| 44 | 4 | `gnss_utc_sod` | `uint32` | UTC seconds of day, converted from HHMMSS |
| 48 | 1 | `gnss_satellites` | `uint8` | satellites used in solution |
| 49 | 1 | `gnss_fix_type` | `uint8` | existing fix-type enum |
| 50 | 2 | `accel_x_mg` | `int16` | g x 1000 |
| 52 | 2 | `accel_y_mg` | `int16` | g x 1000 |
| 54 | 2 | `accel_z_mg` | `int16` | g x 1000 |
| 56 | 2 | `gyro_x_ddeg_s` | `int16` | deg/s x 10 |
| 58 | 2 | `gyro_y_ddeg_s` | `int16` | deg/s x 10 |
| 60 | 2 | `gyro_z_ddeg_s` | `int16` | deg/s x 10 |
| 62 | 4 | `baro_pressure_pa` | `uint32` | pascals |
| 66 | 4 | `baro_altitude_dm` | `int32` | relative altitude x 10 |
| 70 | 2 | `baro_temperature_cdeg` | `int16` | degrees C x 100 |
| 72 | 2 | `battery_mv` | `uint16` | millivolts |
| 74 | 2 | `coral_sequence_low` | `uint16` | low 16 bits; sufficient for the 6 h mission |
| 76 | 2 | `coral_fraction_q16` | `uint16` | cloud fraction, 0..65535 |
| 78 | 1 | `coral_status` | `uint8` | existing Coral status bitmask |
| 79 | 2 | `coral_age_s_sat` | `uint16` | age since Coral `RX_TICK`; saturating |
| 81 | 1 | `lora_last_event` | `uint8` | existing LoRa event enum |
| 82 | 1 | `lora_consecutive_failures` | `uint8` | current consecutive TX failures |
| 83 | 1 | `lora_recovery_count` | `uint8` | recovery attempts since boot |
| 84 | 1 | `lora_rx_state` | `uint8` | packed RX state defined below |
| 85 | 1 | `lora_tx_fault_count_sat` | `uint8` | lifetime TX failures, saturated at 255 |
| 86 | 1 | `lora_ack_timeout_count_sat` | `uint8` | exhausted retry budgets, saturated at 255 |
| 87 | 2 | `last_command_id` | `uint16` | last `CMD` ID observed by flight; latched across telemetry ACKs |
| 89 | 1 | `uplink_state` | `uint8` | packed command and telemetry-ACK status |
| 90 | 2 | `crc16` | `uint16` | CRC-16/CCITT-FALSE over bytes 0..89 |

The corresponding Python layout is:

```python
struct.Struct("<BBHIIHBBBBHHHBBiiihHHIBBhhhhhhIihHHHBHBBBBBBHBH")
# size == 92
```

The proposed C definition should use only fixed-width integer members and
`__attribute__((packed))`, with a `_Static_assert(sizeof(...) == 92U)`.

## Bit definitions

`validity_flags`:

| Bit | Meaning |
|---:|---|
| 0 | GNSS solution valid |
| 1 | IMU sample valid |
| 2 | barometer sample valid |
| 3 | battery ADC sample valid |
| 4 | Coral result valid |
| 5..7 | reserved; transmit as zero |

`lora_rx_state`:

| Bits | Meaning |
|---:|---|
| 0..2 | existing `LoRaRxHealthStatus_t` value, 0..6 |
| 3 | RX-continuous mode active/readback valid |
| 4..7 | reserved; transmit as zero |

`uplink_state`:

| Bits | Meaning |
|---:|---|
| 0..2 | status of the last `CMD` packet using existing `UplinkStatus_t` values |
| 3..5 | status of the last telemetry `ACK` using existing `UplinkStatus_t` values |
| 6..7 | reserved; transmit as zero |

Command ID/status and ACK status must be maintained as independent latches.
In the current v7 implementation, processing an `ACK` sets
`last_command_id = 0` and overwrites `last_status`, which can hide a command
confirmation before ground observes it. V8 should correct that state ownership
rather than copy the ambiguous v7 snapshot.

The ground station must always interpret a measurement together with its
validity bit and the equipment masks. A valid flag describes the current
sample; `equipment_faults` describes debounced FDIR health; and
`equipment_enabled` describes intentional isolation/reduced-mode policy.

## Encoding rules

- Round scaled values to the nearest integer and clamp before casting; never
  permit integer wraparound.
- When a source becomes temporarily invalid, retain its last measurement but
  clear the corresponding validity flag. This matches the useful part of the
  current behavior and lets ground distinguish "last known" from "current."
- Before any first valid measurement, use `INT16_MIN`/`INT32_MIN` for signed
  fields and the maximum unsigned value for unsigned measurements. Validity
  remains the authoritative test.
- Counter fields ending in `_sat` saturate at their maximum and never wrap.
- `sample_age_ms_sat == 65535` and `coral_age_s_sat == 65535` mean unknown or
  at least the representable maximum age.
- Retries must send the exact original 92 bytes, including sequence and CRC,
  across every attempt.
- A telemetry ACK must not overwrite the last command ID or command status.
  Each latch changes only when its own packet class is processed.
- CRC remains CRC-16/CCITT-FALSE with polynomial `0x1021` and initial value
  `0xFFFF`.

## Why 92 bytes rather than the absolute minimum

A smaller navigation-only packet could be built, but would remove the IMU
axes, payload result, or reliable-uplink/FDIR evidence that the ground station
uses to assess health. This proposal intentionally keeps those values in every
packet. It removes only data that is derived, duplicated, internal, unavailable,
or better tracked on the ground.

If later radio testing shows that still less airtime is required, the next
step should be two packet types rather than silently deleting health fields:

- a frequent core sensor/health packet; and
- an on-change or requested diagnostic-counter packet.

That split is not recommended for v8 because it increases decoder, retry, and
dashboard complexity before the 92-byte fixed layout has been range-tested.

## Deployment and acceptance status

Protocol v8 is implemented in the firmware packet builder, TTC size assertion,
ground decoder, telemetry store, backend, dashboard, ICD, and regression tests.
The receiver rejects every payload whose length is not exactly 92 bytes, and the
decoder validates `protocol_version == 8`. There is no fallback decoder for older
telemetry formats.

Acceptance criteria:

- encoded size is exactly 92 bytes on STM32 and in Python;
- every dashboard health alert has an equivalent v8 input;
- ground displays all three IMU acceleration and gyro axes, barometer values,
  battery voltage, GNSS navigation/quality, and Coral fraction/status;
- invalid/stale data are visually distinct from current valid measurements;
- command ID/status confirmation and telemetry ACK/retry behavior still work;
- non-v8 packet lengths are rejected before decoding.
