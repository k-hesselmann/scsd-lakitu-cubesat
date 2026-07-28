# Lakitu CubeSat flight data — 2026-07-28

`flight_data_2026-07-28_1429-1732_CEST.csv` contains the public flight-analysis
window from 14:29:00 to 17:32:00 CEST on 2026-07-28. It contains 103,510
telemetry rows.

## Time and ordering

`gps_utc_time` is UTC in `HHMMSS` form. The filename window is CEST (UTC+2),
so its CSV bounds are 12:29:00--15:32:00 UTC. `record_timestamp_ms` is the
STM32 HAL tick and resets when the OBC reboots; use `scv_boot_count` together
with GNSS time to reconstruct the timeline.

The public cut is selected by non-zero GNSS time. During one in-window GNSS
time-loss interval, 81 otherwise valid rows have `gps_utc_time=0` and are not
present. See `flight_anomaly_report.md` in the working archive for the exact
interval and interpretation.

## Field conventions

The header encodes both names and scale factors. Examples: `gps_lat_e7` and
`gps_lon_e7` are degrees × 1e7; `gps_alt_cm` and `baro_alt_cm` are centimetres;
`gps_vel_down_cms` is positive downward; IMU acceleration is mg; battery is
mV. `coral_block_hex` is the firmware's 16-byte Coral receipt block.

`gps_valid` indicates receiver transport freshness. A usable navigation
solution additionally requires `gps_fix_type=3` (3D fix).

The authoritative on-board CSV schema is defined in
`firmware-stm32/Core/Src/sd_logger.c`; equipment-bit and reset-reason meanings
are defined in `firmware-stm32/Core/Inc/datapool.h`.

## Scope

The release intentionally excludes Coral photographs, RAW frames, and the
complete internal-memory archive. The photos contain private imagery and are
not required to analyse the public telemetry window.
