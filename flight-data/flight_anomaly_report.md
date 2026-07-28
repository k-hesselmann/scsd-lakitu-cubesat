# Actual-flight anomaly review

Analysed file: `flight_data_2026-07-28_1429-1732_CEST.csv`  
Window: 14:29:00--17:32:00 CEST (103,510 rows)

## Executive assessment

The flight record is largely coherent: all non-Coral sensor validity flags
remain asserted, I2C remains healthy, no SD/LoRa/GNSS/IMU/barometer/EPS FDIR
fault is recorded, and there is no watchdog reset. The material in-flight
issues are repeated **Coral link timeouts** and one **power-on reset** during
the landing phase.

## Flight-state timeline

| CEST time | State change | Supporting flight data |
| --- | --- | --- |
| 14:38:56 | Standby -> Launch | GNSS altitude 580 m; barometric altitude 10.5 m; 3D fix. |
| 14:39:08 | Launch -> Ascent | GNSS altitude 652 m; barometric altitude 65.8 m; upward velocity 7.55 m/s. |
| 16:15:22 | Ascent -> Descent | GNSS altitude 37,935 m; vertical descent 80.5 m/s. |
| 16:51:18 | Descent -> Landing | GNSS altitude 753 m; barometric altitude 170.3 m. |
| 16:51:26--16:51:39 | Boot count 5 -> 6 | 13 s interruption; phase remained Landing after restart. |

The state machine never entered Cruise (phase 3): it changed directly from
Ascent to Descent. This is consistent with the implemented logic, which tests
the descent condition before the cruise condition; it is not by itself a data
integrity failure. It should nevertheless be noted if a distinct cruise phase
was a mission requirement.

## Reboot

At 16:51:26 CEST the final B5 record is followed by B6 at 16:51:39 CEST.
`scv_reset_reason=1` means **power-on reset**; it is not a watchdog or software
reset. `scv_watchdog_reset_count` stays zero. The persisted flight phase and
mission elapsed time resume in Landing, so the state persistence worked, but
the source of this mid-landing power interruption needs investigation.

## Equipment faults and sensor validity

`scv_equipment_enabled` is 127 throughout. The only non-zero
`scv_equipment_faults` value is 8, which is the Coral fault bit. There are no
GPS, IMU, barometer, SD, LoRa, EPS, or CDH fault bits; all corresponding timeout
counters remain zero. `i2c_bus_state` remains 0 for all rows.

| Item | Result |
| --- | --- |
| GPS / IMU / barometer / battery validity | 100% valid flags throughout. |
| GPS navigation solution | No 3D fix from 14:29:00--14:37:35; three brief 2D/no-fix events later (all <=1.9 s). `gps_valid=1` means receiver transport was alive, not that a 3D solution existed. |
| Coral validity | Invalid for 1,620 rows (1.57% of the file). |
| Coral status | 1,552 rows carry status 1 (timeout); no CRC-error or Coral-SD-error status appears. |
| Coral FDIR events | 15 intervals; each sets fault bit 8. The normal short events recovered automatically. |

Coral fault intervals (CEST; duration is telemetry-tick duration):

| Start--end | Duration |
| --- | ---: |
| 14:36:03--14:36:12 | 9.0 s |
| 14:48:55--14:48:58 | 2.9 s |
| 15:07:05--15:07:08 | 3.1 s |
| 15:23:49--15:23:58 | 9.0 s |
| 15:25:15--15:25:21 | 6.1 s |
| 15:32:20--15:32:29 | 8.5 s |
| 15:43:28--15:43:31 | 3.0 s |
| 15:55:20--15:55:29 | 8.8 s |
| 16:01:39--16:01:41 | 2.7 s |
| 16:19:49--16:19:52 | 2.9 s |
| 16:20:34--16:20:43 | 9.0 s |
| 16:41:26--16:41:29 | 3.0 s |
| 16:51:39--16:52:00 | 21.6 s (post-reset recovery) |
| 16:58:12--16:58:15 | 3.3 s |
| 17:17:03--17:17:07 | 4.1 s |

Five missing Coral sequence values fall within this flight window, and each is
adjacent to a Coral timeout: 454->456 (14:35:38--14:36:12), 620->622
(15:23:24--15:23:59), 649->651 (15:31:55--15:32:29), 729->731
(15:54:56--15:55:30), and 816->818 (16:20:09--16:20:44). These are consistent
with frames being discarded after a failed/partial Coral reception rather than
with corrupt stored RAW/PNG files. See `data_gaps.md` for the complete archive
sequence ledger.

## CSV continuity

The full archive has no gap over two seconds, as recorded in `data_gaps.md`.
In this *cut* file there is an apparent 8.673 s gap from 14:34:58 to 14:35:07.
It is a **filtering artefact**, not a logging outage: the full CSV contains 81
rows in that interval, but their `gps_utc_time` is zero and they were excluded
when this file was cut by GNSS clock time. The largest step among those retained
rows is 275 ms.

Five genuine sampling stalls of 1.0--1.3 s remain in the source data, all below
the two-second gap threshold:

| CEST time | Gap |
| --- | ---: |
| 15:47:49--15:47:50 | 1.145 s |
| 16:06:33--16:06:34 | 1.065 s |
| 16:34:24--16:34:25 | 1.075 s |
| 17:17:41--17:17:42 | 1.065 s |
| 17:19:43--17:19:44 | 1.299 s |

## Measurement plausibility observations

* GPS altitude spans 568--38,480 m; barometric altitude spans -1.48--28,881 m.
  The roughly 9 km disagreement near peak altitude warrants calibration review,
  but neither sensor was declared invalid by flight software.
* Battery validity stays asserted. Six isolated ADC readings lie outside the
  design range of roughly 3.0--4.2 V: four high spikes (4.348--4.668 V) and two
  low spikes (3.088 and 3.180 V). Each is a single 100 ms sample surrounded by
  normal readings around 3.7--3.9 V, indicating measurement glitches rather
  than sustained over/undervoltage.
* Peak IMU magnitude is 4.675 g at 14:35:24; this is within the configured
  ±4 g per-axis measurement envelope only as a vector magnitude and should be
  correlated with launch handling/vehicle dynamics if it was unexpected.

## Recommended follow-up

1. Investigate the power-on reset around 16:51:26 CEST (power path, connector,
   battery/UVLO behaviour, and reset-cause register capture).
2. Prioritise the Coral UART/payload path: timeouts recur throughout the flight
   and account for every in-window missing Coral frame sequence.
3. Reconcile barometric altitude against GNSS at maximum altitude and validate
   the battery ADC sampling/filtering, especially the isolated outliers.
4. If this cut CSV will be the analysis master, restore the 81 omitted rows
   across the GNSS-time-zero interval so it remains continuous.
