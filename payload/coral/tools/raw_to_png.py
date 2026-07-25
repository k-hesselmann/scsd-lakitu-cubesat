#!/usr/bin/python3
# Converts Coral camera .RAW dumps (headerless 224x224 8-bit grayscale, see
# firmware-stm32/Core/Src/cdh/coral.c) into .png files.
#
# Usage:
#   python raw_to_png.py                                  # uses PATH below
#   python raw_to_png.py "C:\path\to\folder"               # override on the CLI
#   python raw_to_png.py --keep-going                      # skip bad files instead of stopping

import argparse
import sys
from pathlib import Path

from PIL import Image

# Default folder to scan -- edit this for future runs, or pass a path on the CLI.
PATH = r"C:\Users\khess\OneDrive\Desktop\Neuer Ordner (2)"

WIDTH = 224
HEIGHT = 224
FRAME_BYTES = WIDTH * HEIGHT  # 50176


def convert_folder(folder: Path, keep_going: bool) -> None:
    raw_files = sorted(folder.glob("*.RAW"))
    if not raw_files:
        print(f"No .RAW files found in {folder}")
        return

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
        png_path = raw_path.with_suffix(".png")
        img.save(png_path)
        print(f"{raw_path.name} -> {png_path.name}")
        converted += 1

    print(f"\nDone: {converted} converted, {skipped} skipped.")


def main():
    p = argparse.ArgumentParser(description="Convert Coral 224x224 Y8 .RAW frames to .png")
    p.add_argument("folder", nargs="?", default=PATH, help="folder containing .RAW files")
    p.add_argument("--keep-going", action="store_true",
                   help="skip files with an unexpected size instead of raising")
    args = p.parse_args()

    folder = Path(args.folder)
    if not folder.is_dir():
        sys.exit(f"Not a folder: {folder}")

    convert_folder(folder, args.keep_going)


if __name__ == "__main__":
    main()
