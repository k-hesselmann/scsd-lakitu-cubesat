#!/usr/bin/env python3
"""
Read the CDH datapool black-box out of the STM32L476RG's internal flash and
write it to CSV.

The flight computer mirrors the whole datapool into on-chip flash every 30 s as
an SD-independent backup (see firmware-stm32/Core/Src/cdh/datapool_nvm.c). This
tool pulls that region out over the Nucleo's USB port -- which is the ST-Link
debug probe, so the transfer is SWD via STM32CubeProgrammer, not a serial
console -- decodes the records, and writes them in chronological order.

Typical use, with the board plugged in:

    python read_datapool_nvm.py -o flight.csv

Re-parse a dump you already have (no board needed):

    python read_datapool_nvm.py --from-bin datapool.bin -o flight.csv

Storage layout it decodes (must match datapool_nvm.c / datapool.h):

    region  0x080E5800, 104 KB = 52 pages x 2048 B
    slot    104 B = magic(2) + seq(2) + SensorData_t(98) + CRC-16(2)
    layout  19 slots per page, then 72 B of unused tail

That tail matters: records are NOT on a flat 104-byte stride across the whole
region, so a naive linear scan drifts out of alignment after the first page and
silently produces garbage. Slots are addressed per page instead.
"""

import argparse
import csv
import glob
import os
import shutil
import struct
import subprocess
import sys
import tempfile

# --- Storage geometry (keep in sync with cdh/datapool_nvm.h) ----------------
NVM_ADDR = 0x080E5800
NVM_SIZE = 0x1A000          # 104 KB
PAGE_SIZE = 2048
SLOT_SIZE = 104
SLOTS_PER_PAGE = PAGE_SIZE // SLOT_SIZE      # 19
PAGE_COUNT = NVM_SIZE // PAGE_SIZE           # 52
RECORD_MAGIC = 0xDA7A
SNAPSHOT_PERIOD_S = 30

# --- SensorData_t, packed, little-endian (keep in sync with datapool.h) -----
#   u32 timestamp_ms
#   f32 gps_lat, gps_lon, gps_alt, gps_speed, gps_vel_down, gps_heading
#   u32 gps_utc_time
#   u8  gps_num_satellites, gps_fix_type, gps_valid
#   f32 imu_accel_x, y, z, mag, imu_gyro_x, y, z
#   u8  imu_valid
#   f32 baro_pressure_pa, baro_alt_m, baro_temp_c
#   u8  baro_valid, i2c_bus_state
#   u16 batt_voltage_mv
#   u8  batt_valid
#   u8  coral_block[16]
#   u8  coral_valid
DATAPOOL_FMT = "<I6fI3B7fB3fBBHB16sB"
DATAPOOL_SIZE = struct.calcsize(DATAPOOL_FMT)
assert DATAPOOL_SIZE == 98, f"SensorData_t must be 98 B, got {DATAPOOL_SIZE}"

RECORD_FMT = "<HH98sH"
assert struct.calcsize(RECORD_FMT) == SLOT_SIZE

FIELDS = [
    "timestamp_ms",
    "gps_lat_deg", "gps_lon_deg", "gps_alt_m",
    "gps_speed_mps", "gps_vel_down_mps", "gps_heading_deg",
    "gps_utc_time",
    "gps_num_satellites", "gps_fix_type", "gps_valid",
    "imu_accel_x_g", "imu_accel_y_g", "imu_accel_z_g", "imu_accel_mag_g",
    "imu_gyro_x_dps", "imu_gyro_y_dps", "imu_gyro_z_dps",
    "imu_valid",
    "baro_pressure_pa", "baro_alt_m", "baro_temp_c",
    "baro_valid",
    "i2c_bus_state",
    "batt_voltage_mv", "batt_valid",
    "coral_block", "coral_valid",
]


def crc16_ccitt(data):
    """CRC-16/CCITT-FALSE: init 0xFFFF, poly 0x1021, no reflection, no final
    XOR. Matches CRC16_Ccitt() in firmware-stm32/Core/Src/fdir/crc16.c."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def find_programmer():
    """Locate STM32_Programmer_CLI on PATH, or in the usual Windows installs."""
    found = shutil.which("STM32_Programmer_CLI")
    if found:
        return found

    patterns = [
        r"C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
        r"C:\Program Files (x86)\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
        r"C:\ST\STM32CubeIDE*\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.*\tools\bin\STM32_Programmer_CLI.exe",
        "/opt/stm32cubeprog/bin/STM32_Programmer_CLI",
        os.path.expanduser("~/STM32CubeProgrammer/bin/STM32_Programmer_CLI"),
    ]
    for pattern in patterns:
        matches = sorted(glob.glob(pattern))
        if matches:
            return matches[-1]
    return None


def dump_flash(out_path, verbose=True):
    """Read the reserved region off the target over SWD (the USB/ST-Link)."""
    cli = find_programmer()
    if cli is None:
        sys.exit(
            "STM32_Programmer_CLI not found.\n"
            "Install STM32CubeProgrammer, or pass a previously captured dump\n"
            "with --from-bin. You can also dump manually:\n"
            f"  STM32_Programmer_CLI -c port=SWD -r 0x{NVM_ADDR:08X} 0x{NVM_SIZE:X} datapool.bin"
        )

    cmd = [cli, "-c", "port=SWD", "-r", f"0x{NVM_ADDR:08X}", f"0x{NVM_SIZE:X}", out_path]
    if verbose:
        print(f"[dump] {cli}")
        print(f"[dump] reading 0x{NVM_ADDR:08X} +0x{NVM_SIZE:X} ({NVM_SIZE // 1024} KB) over SWD...")

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0 or not os.path.exists(out_path):
        sys.exit(
            "flash read failed. Is the board plugged in and the ST-Link free\n"
            "(close any running debug session)?\n\n" + result.stdout + result.stderr
        )
    if verbose:
        print(f"[dump] wrote {os.path.getsize(out_path)} bytes to {out_path}")


def slot_offsets():
    """Byte offset of every slot, honouring the 72 B tail on each page."""
    for page in range(PAGE_COUNT):
        for slot in range(SLOTS_PER_PAGE):
            yield page, slot, page * PAGE_SIZE + slot * SLOT_SIZE


def parse(blob):
    """Decode every valid record. Returns (records, stats)."""
    records = []
    stats = {"erased": 0, "bad_magic": 0, "bad_crc": 0, "ok": 0}

    for page, slot, off in slot_offsets():
        raw = blob[off:off + SLOT_SIZE]
        if len(raw) < SLOT_SIZE:
            break

        if raw == b"\xFF" * SLOT_SIZE:
            stats["erased"] += 1
            continue

        magic, seq, payload, crc = struct.unpack(RECORD_FMT, raw)
        if magic != RECORD_MAGIC:
            stats["bad_magic"] += 1
            continue
        if crc16_ccitt(raw[:SLOT_SIZE - 2]) != crc:
            stats["bad_crc"] += 1
            continue

        values = struct.unpack(DATAPOOL_FMT, payload)
        row = dict(zip(FIELDS, values))
        row["coral_block"] = row["coral_block"].hex()
        row["seq"] = seq
        row["_page"] = page
        row["_slot"] = slot
        records.append(row)
        stats["ok"] += 1

    return records, stats


def order_by_seq(records):
    """Sort chronologically, tolerating 16-bit sequence wrap.

    The ring holds at most 988 records, so the true span is far below the
    32768 half-range; comparing differences modulo 2^16 therefore recovers the
    real order even when seq wrapped mid-flight."""
    if not records:
        return records
    base = records[0]["seq"]

    def key(rec):
        delta = (rec["seq"] - base) & 0xFFFF
        return delta - 0x10000 if delta > 0x8000 else delta

    return sorted(records, key=key)


def main():
    ap = argparse.ArgumentParser(
        description="Read the CDH datapool black-box from STM32 flash into a CSV.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("-o", "--output", default="datapool.csv", help="CSV to write (default: datapool.csv)")
    ap.add_argument("--from-bin", metavar="FILE", help="parse an existing dump instead of reading the board")
    ap.add_argument("--save-bin", metavar="FILE", help="keep the raw flash dump at this path")
    args = ap.parse_args()

    # --- obtain the raw region -------------------------------------------
    tmp = None
    if args.from_bin:
        bin_path = args.from_bin
        if not os.path.exists(bin_path):
            sys.exit(f"no such file: {bin_path}")
    else:
        bin_path = args.save_bin
        if bin_path is None:
            tmp = tempfile.NamedTemporaryFile(suffix=".bin", delete=False)
            tmp.close()
            bin_path = tmp.name
        dump_flash(bin_path)

    with open(bin_path, "rb") as fh:
        blob = fh.read()

    if len(blob) < NVM_SIZE:
        print(f"warning: dump is {len(blob)} B, expected {NVM_SIZE} B -- parsing what is there",
              file=sys.stderr)

    # --- decode -----------------------------------------------------------
    records, stats = parse(blob)
    records = order_by_seq(records)

    if tmp is not None:
        os.unlink(tmp.name)

    total_slots = PAGE_COUNT * SLOTS_PER_PAGE
    print(f"[parse] slots {total_slots}  valid {stats['ok']}  erased {stats['erased']}  "
          f"bad-magic {stats['bad_magic']}  bad-crc {stats['bad_crc']}")

    if not records:
        print("no valid records -- the region may never have been written "
              "(the recorder writes its first snapshot 30 s after boot).")
        return 1

    first, last = records[0], records[-1]
    # timestamp_ms is boot-local and can reset after a watchdog/power reset while
    # seq continues from flash, so derive the span from the record cadence.
    span_s = max(0, len(records) - 1) * SNAPSHOT_PERIOD_S
    print(f"[parse] seq {first['seq']}..{last['seq']}, "
          f"uptime {first['timestamp_ms'] / 1000.0:.0f}s..{last['timestamp_ms'] / 1000.0:.0f}s "
          f"(~{span_s / 60.0:.1f} min at {SNAPSHOT_PERIOD_S}s cadence; uptime resets across reboots)")
    if stats["bad_crc"]:
        print(f"[parse] note: {stats['bad_crc']} slot(s) failed CRC and were dropped "
              "(expected at most one, from a snapshot interrupted by power loss)")

    # --- write CSV --------------------------------------------------------
    columns = ["seq"] + FIELDS
    with open(args.output, "w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=columns, extrasaction="ignore")
        writer.writeheader()
        for rec in records:
            writer.writerow(rec)

    print(f"[csv]   wrote {len(records)} rows -> {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
