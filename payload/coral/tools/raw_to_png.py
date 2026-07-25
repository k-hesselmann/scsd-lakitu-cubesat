#!/usr/bin/python3
# Converts Coral camera .RAW dumps (headerless 224x224 8-bit grayscale, see
# firmware-stm32/Core/Src/cdh/coral.c) into .png files.
#
# Usage:
#   python raw_to_png.py                                  # uses PATH below
#   python raw_to_png.py "C:\path\to\folder"               # override on the CLI
#   python raw_to_png.py --keep-going                      # skip bad files instead of stopping

import argparse
import re
import sys
from pathlib import Path

from PIL import Image

# Default folder to scan -- edit this for future runs, or pass a path on the CLI.
# Pairs with fetch_backup.py's default output dir (relative to the CWD).
PATH = "backup_frames"

WIDTH = 224
HEIGHT = 224
FRAME_BYTES = WIDTH * HEIGHT  # 50176


def load_backup_log(folder: Path) -> dict:
    """Map SEQ -> cloud fraction from the backup's log.csv ("seq,fraction" lines)
    if present. Empty dict when there's no log (e.g. plain OBC SD-card dumps)."""
    log_path = folder / "log.csv"
    mapping = {}
    if log_path.is_file():
        for line in log_path.read_text().splitlines():
            parts = line.strip().split(",")
            if len(parts) >= 2:
                try:
                    mapping[int(parts[0])] = float(parts[1])
                except ValueError:
                    pass
    return mapping


def png_name(stem: str, log: dict) -> str:
    """"<stem>_cloud<pct>.png" -- cloud percent placed *after* the enumeration --
    when the SEQ (trailing digits of the name) is in the log; else "<stem>.png"."""
    if "cloud" in stem.lower():
        return f"{stem}.png"  # firmware already embedded the percent in the name
    m = re.search(r"(\d+)", stem)
    if m and log:
        seq = int(m.group(1))
        if seq in log:
            return f"{stem}_cloud{log[seq] * 100.0:.1f}.png"
    return f"{stem}.png"


def convert_folder(folder: Path, out_dir: Path, keep_going: bool) -> None:
    # Match .raw and .RAW: the on-board backup writes lowercase img_*.raw
    # (fetch_backup.py), the OBC SD card writes uppercase F*.RAW.
    raw_files = sorted(p for p in folder.iterdir()
                       if p.is_file() and p.suffix.lower() == ".raw")
    if not raw_files:
        print(f"No .raw/.RAW files found in {folder}")
        return

    out_dir.mkdir(parents=True, exist_ok=True)
    log = load_backup_log(folder)  # SEQ -> fraction, for the cloud% suffix

    converted, skipped = 0, 0
    for raw_path in raw_files:
        data = raw_path.read_bytes()
        if len(data) != FRAME_BYTES:
            msg = (f"skip {raw_path.name}: {len(data)} bytes "
                   f"(expected {FRAME_BYTES} for {WIDTH}x{HEIGHT})")
            if keep_going:
                print(msg)
                skipped += 1
                continue
            raise ValueError(msg)

        img = Image.frombytes("L", (WIDTH, HEIGHT), data)
        png_path = out_dir / png_name(raw_path.stem, log)
        img.save(png_path)
        print(f"{raw_path.name} -> {png_path.name}")
        converted += 1

    print(f"\nDone: {converted} converted, {skipped} skipped -> {out_dir}")


def main():
    p = argparse.ArgumentParser(description="Convert Coral 224x224 Y8 .RAW frames to .png")
    p.add_argument("folder", nargs="?", default=PATH,
                   help="folder containing .raw/.RAW files")
    p.add_argument("--out", default=None,
                   help="output folder for the .png files "
                        "(default: a 'png' subdirectory of the input folder)")
    p.add_argument("--keep-going", action="store_true",
                   help="skip files with an unexpected size instead of raising")
    args = p.parse_args()

    folder = Path(args.folder)
    if not folder.is_dir():
        sys.exit(f"Not a folder: {folder}")

    out_dir = Path(args.out) if args.out else folder / "png"
    convert_folder(folder, out_dir, args.keep_going)


if __name__ == "__main__":
    main()
