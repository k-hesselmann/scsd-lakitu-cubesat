# SD Log Test 1 Analysis Findings

Source: `logs/test1/LOG_*.CSV`

Analyzed on: 2026-07-23

## Summary

This SD-card log set is readable and the housekeeping CSV schema covers the
main datapool and SCV fields required for post-flight reconstruction. It is not
clean pass evidence for the current requirements/test campaign because there
are multi-second logging gaps, repeated IMU invalid periods, and Coral payload
frames do not arrive at the required 1 Hz cadence.

The directory contains 17 CSV files:

- Sessions `49..64`: cleanly closed one-minute files.
- Session `65`: `_OPEN` with one row, indicating the run ended without a clean
  `SD_Logger_Close()`.

Overall span:

- Rows: `7796`
- First timestamp: `244977 ms`
- Last timestamp: `1221186 ms`
- Span: `976.209 s` (`~16.3 min`)

## Requirement Verdict

| Item | Verdict | Evidence |
|---|---|---|
| FR-016 SD housekeeping fields | Pass for CSV housekeeping fields | Timestamp, phase, GPS, IMU, baro, battery, Coral block, validity flags, and SCV/FDIR fields are present. Payload image files/verification footage are not verified from these CSVs alone. |
| MR-003 / FR-017 / PR-002 logging rate and capture | Fail / not sign-off ready | Nominal cadence is 10 Hz, but there are `86` gaps over `1 s`, `31` gaps over `1.5 s`, and max gap is `7603 ms`. Per-second bucket coverage is `902 / 978 = 92.23%`, below the 99% requirement. |
| FR-013 GPS acquisition | Partial bench pass | GPS is alive before fix, then achieves 3D fix at `520700 ms`. Indoor/no-sky-view cold start is not flight evidence. |
| FR-014 IMU acquisition | Fail / needs root cause | `imu_valid=0` for `1586 / 7796` rows, with invalid runs up to `8842 ms`. |
| FR-015 barometer acquisition | Pass / acceptable | `baro_valid=1` for `7759 / 7796` rows; invalid samples are isolated one-row drops. |
| MR-009 / FR-027 Coral payload 1 Hz block | Fail | `coral_valid=0` for `5287 / 7796` rows. Valid-good-frame interval median is `25.205 s`, max is `70.071 s`. Final invalid interval lasts `163.219 s`. |
| MR-002 six-hour endurance | Not verified | This run covers only `~16.3 min`. |

## FMECA / FDIR Observations

- `scv_equipment_faults` never shows the SD bit (`0x0010`) in this run, so live
  SD logging was not faulted according to FDIR.
- `scv_sd_fault_count=255` for every row. This is saturated persisted fault
  history, not current-session evidence by itself.
- GPS no-fix appears as `gps_satellites=0`, `gps_fix_type=0`, `gps_valid=0`,
  plus the GPS fault bit before the first fix. This matches an alive receiver
  without a valid navigation solution.
- IMU faults match FMECA C3/C4-style behavior, but the CSV does not include the
  exact invalidation reason.
- Coral timeouts match FMECA P1 detection: the last good frame is marked stale
  when a new valid frame does not arrive in time.
- `i2c_bus_state=2` appears for two rows around `858.1..859.4 s`, consistent
  with a brief bus-recovery hold state while SD logging continued.

## Field Coverage And Redundancy

The SD CSV records all fields from `SensorData_t`:

- `timestamp_ms`
- GPS: lat, lon, altitude, speed, vertical velocity down, heading, UTC time,
  satellites, fix type, valid flag
- IMU: accel XYZ, accel magnitude, gyro XYZ, valid flag
- Barometer: pressure, relative altitude, temperature, valid flag
- `i2c_bus_state`
- Battery voltage and valid flag
- Coral 16-byte opaque block and valid flag

It also records most SCV health/state fields.

Available firmware fields that are not in this CSV:

- `equipment_manual_disable`
- detailed LoRa health: last event, RX state, ACK timeout count, recovery count,
  RX packet counters, CRC-error counters
- uplink command/ACK state
- SD logger live health: state, last FatFs error, consecutive faults,
  last successful sync, file-open state
- Coral debug counters: good frames, CRC errors, UART timeouts, RX overflows,
  SD errors
- GPS quality/debug fields parsed by `m10s.c` but discarded from the datapool:
  `iTOW`, `validFlags`, `flags`, `hAcc`, `vAcc`, `sAcc`, `headAcc`, `pDOP`,
  `tAcc`
- IMU read-fault count, flatline/stuck counters, or an explicit fault reason

Redundant or near-redundant fields:

- `session` repeats the file session number encoded in the filename.
- `scv_magic` is constant `0xCAFE`.
- `scv_boot_count`, `scv_reset_reason`, `scv_equipment_enabled`, and
  `scv_watchdog_reset_count` are constant throughout this run.
- `imu_accel_mag_mg` is derived from accel XYZ.
- `scv_last_batt_mv` mostly duplicates the latest valid `batt_voltage_mv`.
- `scv_crc16` is the last persisted SCV CRC, not a row CRC, and can be
  misread as a per-row integrity check.

Recommended additions for future diagnosis:

- `gps_data_present`, GPS message age, `iTOW`, `hAcc`, `vAcc`, `pDOP`,
  `validFlags`, `flags`
- `imu_fault_reason`, `imu_read_fault_count`, `imu_flatline_count`,
  `imu_stuck_count`
- `coral_good_frames`, `coral_timeout_count`, `coral_crc_err_count`,
  `coral_rx_overflow_count`, `coral_sd_err_count`
- SD logger `state`, `last_error`, `consecutive_faults`, `last_success_ms`
- LoRa/uplink detailed health if TT&C post-flight diagnosis is required from
  SD alone

## Loop Cadence And Freezes

The log cadence can be extracted from `record_timestamp_ms`, which is the
datapool timestamp written by `CDH_Update()` on the 100 ms scheduler slot.

Measured cadence:

- Timestamp monotonic: yes
- Duplicate timestamps: none
- Median delta: `100 ms`
- Mean delta: `125.235 ms`
- Minimum delta: `99 ms`
- Maximum delta: `7603 ms`
- Gaps > `150 ms`: `267`
- Gaps > `500 ms`: `102`
- Gaps > `1000 ms`: `86`
- Gaps > `2000 ms`: `21`
- Gaps > `5000 ms`: `5`

There are no repeated/frozen timestamps. There are real missing periods where
no SD rows were produced. From SD CSV alone, this cannot prove whether the whole
superloop blocked or only the SD logging path was skipped. The timestamp jumps
show that these are not simply stale repeated rows.

Largest gaps:

- `451148 -> 458751 ms`: `7603 ms`
- `511127 -> 518689 ms`: `7562 ms`
- `271154 -> 278653 ms`: `7499 ms`
- `331149 -> 338496 ms`: `7347 ms`
- `391120 -> 398440 ms`: `7320 ms`

## GPS Behavior

First valid GPS sample:

- Timestamp: `520700 ms`
- `gps_utc_time=215238`
- `gps_satellites=4`
- `gps_fix_type=3`
- `gps_valid=1`
- Position: `48.0967520`, `11.5309952`
- Altitude: `684 m`

Before this point:

- `gps_satellites=0` for all pre-fix rows.
- `gps_fix_type=0` for all pre-fix rows.
- `gps_valid=0` for all pre-fix rows.
- `gps_utc_time` is mostly nonzero, ranging from `214802` to `215238`.
- Raw lat/lon/alt fields eventually change before fix, but they are invalid
  because `fix_type=0`.

Interpretation:

The GPS receiver was alive and emitting UBX NAV-PVT data before the first fix.
It was not producing a valid navigation solution until `520700 ms`. The sudden
appearance with 4 satellites is the first valid 3D fix reported by the receiver,
not the first evidence of receiver life.

Rows where `gps_utc_time=0` before first fix:

- 48 rows total, in short ranges around the large logging gaps.
- Nonzero pre-fix UTC starts at `214802` (`21:48:02`) and reaches `215238`
  (`21:52:38`) immediately before first fix.

## IMU Validity

Counts:

- `imu_valid=1`: `6210` rows
- `imu_valid=0`: `1586` rows
- Invalid runs: `246`
- Longest invalid run: `8842 ms`

All `1586` invalid IMU rows repeat the previous row's six accel/gyro values.
The acceleration magnitude remains plausible during invalid rows
(`976..992 mg`), so this is not an implausible high-g reading.

Likely causes from firmware:

- `MPU6050_Read()` failed one of the I2C bulk-read transactions.
- After 3 consecutive failed reads, `MPU6050_EquipmentHandler_Update()` clears
  `imu_valid` while retaining the last good sample.
- The validation layer can also clear `imu_valid` for stuck/flatline behavior,
  but the SD log does not record which path fired.

Conclusion:

The logged data points to intermittent IMU read/refresh failures or stuck-value
detection, not physically impossible acceleration. Add explicit IMU fault-reason
logging to distinguish I2C read failures from flatline/stuck-value validation.

## Coral Validity

Firmware configuration:

- `CORAL_DEFAULT_INTERVAL_MS = 10000 ms`
- `FRAME_STALE_TIMEOUT_MS = CORAL_DEFAULT_INTERVAL_MS`

This means the current firmware asks Coral for a 10 s autonomous frame interval,
not 1 Hz. `coral_valid` is set only after a clean frame. If no new clean frame
arrives within the stale timeout, `coral_valid` is cleared and byte 7 of
`coral_block_hex` is set to `0x01` (`CORAL_STATUS_TIMEOUT`).

Counts:

- `coral_valid=1`: `2509` rows
- `coral_valid=0`: `5287` rows
- Coral status byte `0x00`: `2541` rows
- Coral status byte `0x01`: `5255` rows
- No CRC-error (`0x02`) or SD-error (`0x04`) status is visible in the
  housekeeping block.

Frame cadence:

- Distinct sequences: `29`
- Sequence range: `2955..3001`
- Valid-good-frame interval median: `25.205 s`
- Valid-good-frame interval max: `70.071 s`
- Final invalid interval: `1057967 -> 1221186 ms`, `163.219 s`

Interpretation:

Coral is invalid because fresh frames are not arriving by the expected cadence.
The housekeeping block shows timeouts, not CRC or SD write errors. The current
10 s configured interval already conflicts with the 1 Hz payload requirement,
and the observed good-frame cadence is even slower than 10 s for much of the
run.

## Follow-Up Actions

1. Clear or intentionally snapshot SCV before future requirement evidence runs,
   so stale saturated counters such as `scv_sd_fault_count=255` do not confuse
   current-session interpretation.
2. Investigate the large SD timestamp gaps. Add SD logger live health fields to
   the CSV, or collect simultaneous serial/ground logs to separate main-loop
   stalls from logger-only loss.
3. Add GPS quality fields and a `gps_data_present`/message-age field so no-fix
   and no-data are distinguishable from the SD CSV alone.
4. Add IMU fault reason/counters to determine whether invalidity is I2C read
   failure, flatline, or stuck-value validation.
5. Align Coral interval with the 1 Hz requirement, or update the requirement if
   10 s is the intended payload cadence.
6. Add Coral diagnostic counters to the CSV to separate UART timeout, RX
   overflow, CRC mismatch, and SD image write failures.
