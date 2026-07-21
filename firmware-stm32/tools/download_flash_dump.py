#!/usr/bin/env python3
"""Download Lakitu CubeSat reserved STM32 flash regions via ST-Link."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import os
import shutil
import subprocess
from pathlib import Path

from decode_flash_dump import write_datapool_csv, write_scv_json


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT_ROOT = REPO_ROOT / "output" / "flash_dumps"

REGIONS = [
    ("datapool_nvm_0x080E5800_0x1A000.bin", "0x080E5800", "0x1A000"),
    ("scv_0x080FF800_0x800.bin", "0x080FF800", "0x800"),
    ("reserved_flash_0x080E5800_0x1A800.bin", "0x080E5800", "0x1A800"),
]


def default_st_flash() -> Path | None:
    env_path = os.environ.get("ST_FLASH")
    if env_path:
        return Path(env_path).expanduser()

    path = shutil.which("st-flash")
    if path:
        return Path(path)

    platformio_path = Path.home() / ".platformio" / "packages" / "tool-stm32duino" / "stlink" / "st-flash"
    if platformio_path.exists():
        return platformio_path

    return None


def run_st_flash(st_flash: Path, output_dir: Path) -> None:
    for filename, address, size in REGIONS:
        output_path = output_dir / filename
        cmd = [str(st_flash), "read", str(output_path), address, size]
        print(" ".join(cmd))
        subprocess.run(cmd, check=True)


def write_checksums(output_dir: Path) -> None:
    lines = []
    for path in sorted(output_dir.glob("*.bin")):
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        lines.append(f"{digest}  {path.name}\n")
    (output_dir / "SHA256SUMS").write_text("".join(lines))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument("--name", help="Dump directory name; defaults to local timestamp")
    parser.add_argument("--st-flash", type=Path, default=default_st_flash())
    args = parser.parse_args()

    if args.st_flash is None:
        parser.error("st-flash not found; set --st-flash or ST_FLASH")

    name = args.name or dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    output_dir = args.output_root / name
    output_dir.mkdir(parents=True, exist_ok=False)

    run_st_flash(args.st_flash, output_dir)
    write_datapool_csv(
        output_dir / "datapool_nvm_0x080E5800_0x1A000.bin",
        output_dir / "datapool_nvm.csv",
    )
    write_scv_json(
        output_dir / "scv_0x080FF800_0x800.bin",
        output_dir / "scv.json",
    )
    write_checksums(output_dir)

    print(f"wrote flash dump to {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
