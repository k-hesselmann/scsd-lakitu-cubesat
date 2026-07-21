#!/usr/bin/env python3
"""Bench log-checker for the Phase 4 endurance test (see the pre-launch test
campaign plan). Cross-checks a ground-station telemetry CSV (from
ground-station/telemetry_store.py, decoded via telemetry_decoder.py) against
one or more SD-card CSV logs (from firmware-stm32/Core/Src/sd_logger.c) taken
from the same bench session.

Produces the evidence numbers for:
  - PR-002 / FR-017: SD sample-rate completeness (>=1 Hz, >=99% capture)
  - FR-022: telemetry packet rate (~1 per 20 s)
  - CRC-fail rate on the downlink
  - A cross-check of overlapping timestamps between the two logs, to catch
    silent data corruption that neither side's own CRC would show (a value
    correct-per-CRC on both ends but different between them means something
    upstream of both CRCs -- e.g. a datapool aliasing bug -- corrupted it).

Usage:
    python3 bench_log_check.py --ground telemetry_20260714_143447.csv \\
        --sd LOG_000001_START_..._END_..._DUR_....CSV [LOG_...CSV ...]

No third-party dependencies (stdlib csv only), so it runs on a bare bench
laptop without setting up the ground-station's Python environment.
"""

import argparse
import csv
import sys

# SD CSV fields are scaled integers (see s_csv_header in sd_logger.c); convert
# back to the same physical units the ground-station CSV already uses.
SD_SCALE = {
    "gps_alt_cm": ("gnss_altitude_m", 0.01),
    "baro_alt_cm": ("baro_altitude_m", 0.01),
    "imu_accel_x_mg": ("imu_accel_x_g", 0.001),
    "imu_accel_y_mg": ("imu_accel_y_g", 0.001),
    "imu_accel_z_mg": ("imu_accel_z_g", 0.001),
}

# Absolute tolerance per cross-checked physical-unit field. Loose enough to
# absorb the SD side's integer rounding, tight enough to catch real
# divergence between what was logged and what was transmitted.
CROSS_CHECK_TOLERANCE = {
    "gnss_altitude_m": 0.5,
    "baro_altitude_m": 0.5,
    "imu_accel_x_g": 0.02,
    "imu_accel_y_g": 0.02,
    "imu_accel_z_g": 0.02,
}

SD_EXPECTED_PERIOD_MS = 1000  # LOOP_SD_PERIOD_MS in main.c
SD_GAP_WARN_FACTOR = 1.5      # flag a gap wider than 1.5x the expected period

# Protocol v8 (92-byte packet) dropped the raw datapool_timestamp_ms field
# that earlier versions carried; obc_uptime_ms is now only second-resolution
# (tx_uptime_s * 1000, see ttc_telemetry.c). Reconstruct the millisecond
# dp->timestamp_ms the transmitted sample was actually read at by
# subtracting sample_age_ms (how stale the sample already was when the
# packet was built), then match against the nearest SD row within
# CROSS_CHECK_MATCH_TOLERANCE_MS -- an exact match is no longer possible at
# second resolution.
CROSS_CHECK_MATCH_TOLERANCE_MS = 1000


def load_ground_csv(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def load_sd_csvs(paths):
    rows = []
    for path in paths:
        with open(path, newline="") as f:
            rows.extend(list(csv.DictReader(f)))
    rows.sort(key=lambda r: int(r["record_timestamp_ms"]))
    return rows


def check_sd_completeness(sd_rows):
    if len(sd_rows) < 2:
        print("SD: not enough rows to assess completeness")
        return

    timestamps = [int(r["record_timestamp_ms"]) for r in sd_rows]
    span_s = (timestamps[-1] - timestamps[0]) / 1000.0
    expected_rows = int(span_s) + 1
    capture_pct = 100.0 * len(sd_rows) / expected_rows if expected_rows else 0.0

    gaps = []
    for prev, cur in zip(timestamps, timestamps[1:]):
        delta = cur - prev
        if delta > SD_EXPECTED_PERIOD_MS * SD_GAP_WARN_FACTOR:
            gaps.append((prev, cur, delta))

    print(f"SD: {len(sd_rows)} rows over {span_s:.0f} s "
          f"({expected_rows} expected at 1 Hz) -> {capture_pct:.2f}% capture "
          f"({'PASS' if capture_pct >= 99.0 else 'FAIL'} vs PR-002 >=99%)")
    print(f"SD: {len(gaps)} gap(s) > {SD_EXPECTED_PERIOD_MS * SD_GAP_WARN_FACTOR:.0f} ms")
    for prev, cur, delta in gaps[:20]:
        print(f"    gap at t={prev} -> {cur} ms (delta {delta} ms)")
    if len(gaps) > 20:
        print(f"    ... and {len(gaps) - 20} more")

    final = sd_rows[-1]
    print(f"SD: final scv_sd_fault_count={final.get('scv_sd_fault_count')} "
          f"(consecutive-failure counter, FMECA S1)")


def check_ground_stats(ground_rows):
    if not ground_rows:
        print("Ground: no packets received")
        return

    n = len(ground_rows)
    crc_fail = sum(1 for r in ground_rows if r.get("crc_ok") != "True")
    total_lost = ground_rows[-1].get("total_lost_packets", "?")

    first_t = float(ground_rows[0]["pc_receive_time_unix"])
    last_t = float(ground_rows[-1]["pc_receive_time_unix"])
    span_s = last_t - first_t
    expected_packets = int(span_s / 20.0) + 1 if span_s > 0 else n

    print(f"Ground: {n} packets received over {span_s:.0f} s "
          f"(~{expected_packets} expected at FR-022's 1/20s rate)")
    print(f"Ground: CRC failures = {crc_fail}/{n} "
          f"({'PASS' if crc_fail == 0 else 'CHECK'} -- any CRC failure on the "
          f"downlink is worth investigating, not just counting)")
    print(f"Ground: total_lost_packets (per ground station's own sequence "
          f"tracking) = {total_lost}")


def _nearest_sd_row(sd_timestamps, sd_rows, target_ms, tolerance_ms):
    """Binary-search sd_timestamps (sorted) for the closest entry to
    target_ms; returns None if nothing is within tolerance_ms."""
    import bisect

    i = bisect.bisect_left(sd_timestamps, target_ms)
    candidates = [j for j in (i - 1, i) if 0 <= j < len(sd_timestamps)]
    if not candidates:
        return None
    best = min(candidates, key=lambda j: abs(sd_timestamps[j] - target_ms))
    if abs(sd_timestamps[best] - target_ms) > tolerance_ms:
        return None
    return sd_rows[best]


def cross_check(ground_rows, sd_rows):
    sd_timestamps = [int(r["record_timestamp_ms"]) for r in sd_rows]

    matched = 0
    unmatched = 0
    mismatches = []
    for g in ground_rows:
        try:
            uptime_ms = int(g.get("obc_uptime_ms") or -1)
            sample_age_ms = int(g.get("sample_age_ms") or 0)
        except (ValueError, TypeError):
            continue
        if uptime_ms < 0:
            continue
        target_ms = uptime_ms - sample_age_ms

        sd = _nearest_sd_row(sd_timestamps, sd_rows, target_ms,
                              CROSS_CHECK_MATCH_TOLERANCE_MS)
        if sd is None:
            unmatched += 1
            continue
        matched += 1
        for sd_field, (ground_field, scale) in SD_SCALE.items():
            if sd_field not in sd or ground_field not in g:
                continue
            try:
                sd_val = int(sd[sd_field]) * scale
                ground_val = float(g[ground_field])
            except (ValueError, TypeError):
                continue
            tol = CROSS_CHECK_TOLERANCE.get(ground_field, 0.5)
            if abs(sd_val - ground_val) > tol:
                mismatches.append((target_ms, ground_field, sd_val, ground_val))

    print(f"Cross-check: {matched} ground packets matched to an SD row "
          f"within {CROSS_CHECK_MATCH_TOLERANCE_MS} ms "
          f"({unmatched} unmatched -- expected if the SD log doesn't fully "
          f"overlap the telemetry session)")
    print(f"Cross-check: {len(mismatches)} field(s) diverged beyond tolerance")
    for ts, field, sd_val, ground_val in mismatches[:20]:
        print(f"    t~={ts} {field}: SD={sd_val:.3f} ground={ground_val:.3f}")
    if len(mismatches) > 20:
        print(f"    ... and {len(mismatches) - 20} more")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ground", required=True,
                         help="ground-station telemetry CSV path")
    parser.add_argument("--sd", required=True, nargs="+",
                         help="one or more SD-card CSV log paths from the same session")
    args = parser.parse_args()

    ground_rows = load_ground_csv(args.ground)
    sd_rows = load_sd_csvs(args.sd)

    print("=== SD completeness (PR-002 / FR-017) ===")
    check_sd_completeness(sd_rows)
    print("\n=== Downlink stats (FR-022, CRC) ===")
    check_ground_stats(ground_rows)
    print("\n=== Cross-check (data-integrity beyond either side's own CRC) ===")
    cross_check(ground_rows, sd_rows)


if __name__ == "__main__":
    sys.exit(main())
