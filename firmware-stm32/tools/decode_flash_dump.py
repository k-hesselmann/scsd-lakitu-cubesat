#!/usr/bin/env python3
"""Decode STM32 reserved flash dumps from the Lakitu CubeSat firmware."""

from __future__ import annotations

import argparse
import csv
import json
import struct
from pathlib import Path


FLASH_PAGE_SIZE = 2048

DATAPOOL_MAGIC = 0xDA7A
DATAPOOL_REGION_SIZE = 0x1A000
DATAPOOL_SLOT_SIZE = 104
DATAPOOL_SLOTS_PER_PAGE = 19

SCV_MAGIC = 0xCAFE
SCV_SLOT_SIZE = 40

SENSOR_STRUCT = struct.Struct("<IffffffIBBBfffffffBfffBBHB16sB")
SCV_STRUCT = struct.Struct("<HIIBBHHHBBBBBHBBHiH")

SENSOR_FIELDS = [
    "record_timestamp_ms",
    "gps_lat_deg",
    "gps_lon_deg",
    "gps_alt_m",
    "gps_speed_mps",
    "gps_vel_down_mps",
    "gps_heading_deg",
    "gps_utc_time",
    "gps_num_satellites",
    "gps_fix_type",
    "gps_valid",
    "imu_accel_x_g",
    "imu_accel_y_g",
    "imu_accel_z_g",
    "imu_accel_mag_g",
    "imu_gyro_x_dps",
    "imu_gyro_y_dps",
    "imu_gyro_z_dps",
    "imu_valid",
    "baro_pressure_pa",
    "baro_alt_m",
    "baro_temp_c",
    "baro_valid",
    "i2c_bus_state",
    "batt_voltage_mv",
    "batt_valid",
    "coral_block",
    "coral_valid",
]

SCV_FIELDS = [
    "magic",
    "boot_count",
    "mission_elapsed_ms",
    "flight_phase",
    "reset_reason",
    "equipment_enabled",
    "equipment_faults",
    "equipment_manual_disable",
    "gps_timeout_count",
    "imu_timeout_count",
    "baro_timeout_count",
    "coral_timeout_count",
    "lora_timeout_count",
    "lora_tx_fault_counter",
    "sd_fault_count",
    "watchdog_reset_count",
    "last_batt_mv",
    "baro_ground_alt_cm",
    "crc16",
]


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def iter_datapool_records(blob: bytes):
    page_count = min(len(blob), DATAPOOL_REGION_SIZE) // FLASH_PAGE_SIZE
    for page in range(page_count):
        page_base = page * FLASH_PAGE_SIZE
        for slot in range(DATAPOOL_SLOTS_PER_PAGE):
            offset = page_base + slot * DATAPOOL_SLOT_SIZE
            raw = blob[offset : offset + DATAPOOL_SLOT_SIZE]
            if len(raw) != DATAPOOL_SLOT_SIZE:
                continue

            magic, seq = struct.unpack_from("<HH", raw, 0)
            stored_crc = struct.unpack_from("<H", raw, DATAPOOL_SLOT_SIZE - 2)[0]
            valid = magic == DATAPOOL_MAGIC and crc16_ccitt(raw[:-2]) == stored_crc
            if not valid:
                continue

            values = list(SENSOR_STRUCT.unpack_from(raw, 4))
            row = dict(zip(SENSOR_FIELDS, values))
            row["coral_block"] = row["coral_block"].hex()
            row.update(
                {
                    "nvm_seq": seq,
                    "flash_page": page,
                    "flash_slot": slot,
                    "flash_offset": offset,
                    "record_crc16": f"0x{stored_crc:04X}",
                }
            )
            yield row


def decode_scv(blob: bytes):
    slots = []
    for offset in range(0, len(blob) - SCV_SLOT_SIZE + 1, SCV_SLOT_SIZE):
        raw = blob[offset : offset + SCV_SLOT_SIZE]
        magic = struct.unpack_from("<H", raw, 0)[0]
        stored_crc = struct.unpack_from("<H", raw, SCV_STRUCT.size - 2)[0]
        valid = magic == SCV_MAGIC and crc16_ccitt(raw[: SCV_STRUCT.size - 2]) == stored_crc
        if not valid:
            continue

        values = SCV_STRUCT.unpack_from(raw, 0)
        slot = dict(zip(SCV_FIELDS, values))
        slot["magic"] = f"0x{slot['magic']:04X}"
        slot["equipment_enabled"] = f"0x{slot['equipment_enabled']:04X}"
        slot["equipment_faults"] = f"0x{slot['equipment_faults']:04X}"
        slot["equipment_manual_disable"] = f"0x{slot['equipment_manual_disable']:04X}"
        slot["crc16"] = f"0x{slot['crc16']:04X}"
        slot["flash_slot"] = offset // SCV_SLOT_SIZE
        slot["flash_offset"] = offset
        slots.append(slot)
    return slots


def write_datapool_csv(input_path: Path, output_path: Path) -> int:
    records = list(iter_datapool_records(input_path.read_bytes()))
    records.sort(key=lambda row: row["nvm_seq"])
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", newline="") as f:
        fieldnames = [
            "nvm_seq",
            "flash_page",
            "flash_slot",
            "flash_offset",
            *SENSOR_FIELDS,
            "record_crc16",
        ]
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(records)
    return len(records)


def write_scv_json(input_path: Path, output_path: Path) -> int:
    slots = decode_scv(input_path.read_bytes())
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps({"slots": slots, "latest": slots[-1] if slots else None}, indent=2) + "\n")
    return len(slots)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--datapool-bin", type=Path, help="Datapool NVM binary dump")
    parser.add_argument("--datapool-csv", type=Path, help="Decoded datapool CSV path")
    parser.add_argument("--scv-bin", type=Path, help="SCV page binary dump")
    parser.add_argument("--scv-json", type=Path, help="Decoded SCV JSON path")
    args = parser.parse_args()

    if args.datapool_bin or args.datapool_csv:
        if not args.datapool_bin or not args.datapool_csv:
            parser.error("--datapool-bin and --datapool-csv must be used together")
        count = write_datapool_csv(args.datapool_bin, args.datapool_csv)
        print(f"decoded {count} datapool records -> {args.datapool_csv}")

    if args.scv_bin or args.scv_json:
        if not args.scv_bin or not args.scv_json:
            parser.error("--scv-bin and --scv-json must be used together")
        count = write_scv_json(args.scv_bin, args.scv_json)
        print(f"decoded {count} SCV slots -> {args.scv_json}")

    if not (args.datapool_bin or args.scv_bin):
        parser.error("provide at least one input")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
