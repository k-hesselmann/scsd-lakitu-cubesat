#!/usr/bin/env python3
"""
build_cloud_classes.py

Turn a 95-Cloud / 38-Cloud SEGMENTATION dataset into a 4-class whole-image
CLASSIFICATION dataset for the balloon cloud-classifier PoC.

Auto-detects the band folders under --root by prefix, so it works whether they
are named train_red or train_red_additional_to38cloud, etc. Walks the GT folder
directly and pairs each mask to its R/G/B bands by filename, so no patch-list CSV
is required. Margin (black-border) patches are filtered by valid-pixel fraction.

Output is a standard ImageFolder layout for Keras image_dataset_from_directory()
or PyTorch ImageFolder. The pretrained MobileNet backbone wants 3-channel input,
so replicate the single gray channel to 3 channels in the data loader at train time.
"""

import argparse
import csv
from pathlib import Path

import numpy as np
import imageio.v2 as imageio
from tqdm import tqdm

# METAR sky-cover bins as cloud-pixel fraction
# clear <12.5% | partial 12.5-87.5% | overcast >87.5%
CLASS_EDGES = [0.125, 0.875]
CLASS_NAMES = ["clear", "partial", "overcast"]


def classify(f):
    for i, edge in enumerate(CLASS_EDGES):
        if f < edge:
            return CLASS_NAMES[i]
    return CLASS_NAMES[-1]


def read_band(path):
    arr = imageio.imread(path).astype(np.float32)
    if arr.ndim == 3:
        arr = arr[..., 0]
    m = arr.max()
    if m > 255:
        arr = arr / m * 255.0
    return arr


def find_band_dir(root, prefix):
    matches = sorted(d for d in root.glob(prefix + "*") if d.is_dir())
    if not matches:
        raise FileNotFoundError("No directory matching '%s*' under %s" % (prefix, root))
    return matches[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--min-valid", type=float, default=0.20)
    args = ap.parse_args()

    root = Path(args.root)
    red_dir = find_band_dir(root, "train_red")
    green_dir = find_band_dir(root, "train_green")
    blue_dir = find_band_dir(root, "train_blue")
    gt_dir = find_band_dir(root, "train_gt")
    print("red:   " + red_dir.name)
    print("green: " + green_dir.name)
    print("blue:  " + blue_dir.name)
    print("gt:    " + gt_dir.name)

    gt_files = sorted(gt_dir.glob("gt_*.TIF"))
    print("%d GT patches found" % len(gt_files))

    out = Path(args.out)
    for c in CLASS_NAMES:
        (out / c).mkdir(parents=True, exist_ok=True)

    manifest = open(out / "manifest.csv", "w", newline="")
    writer = csv.writer(manifest)
    writer.writerow(["patch", "cloud_fraction", "valid_fraction", "class"])

    counts = {c: 0 for c in CLASS_NAMES}
    skipped = 0

    for gt_path in tqdm(gt_files):
        base = gt_path.name[len("gt_"):]
        stem = gt_path.stem[len("gt_"):]
        try:
            r = read_band(red_dir / ("red_" + base))
            g = read_band(green_dir / ("green_" + base))
            b = read_band(blue_dir / ("blue_" + base))
            gt = imageio.imread(gt_path).astype(np.float32)
        except FileNotFoundError:
            skipped += 1
            continue

        if gt.ndim == 3:
            gt = gt[..., 0]

        valid = (r > 0) | (g > 0) | (b > 0)
        valid_frac = float(valid.mean())
        if valid_frac < args.min_valid:
            skipped += 1
            continue

        cloud = gt > (gt.max() / 2.0) if gt.max() > 1 else gt > 0
        cloud_frac = float((cloud & valid).sum()) / float(valid.sum())
        cls = classify(cloud_frac)

        gray = (0.299 * r + 0.587 * g + 0.114 * b).clip(0, 255).astype(np.uint8)
        imageio.imwrite(out / cls / (stem + ".png"), gray)

        writer.writerow([stem, "%.4f" % cloud_frac, "%.4f" % valid_frac, cls])
        counts[cls] += 1

    manifest.close()

    total = sum(counts.values())
    print("\nWrote %d patches, skipped %d" % (total, skipped))
    print("Class distribution:")
    for c in CLASS_NAMES:
        n = counts[c]
        pct = 100 * n / total if total else 0
        print("  %-10s %6d  (%5.1f%%)" % (c, n, pct))


if __name__ == "__main__":
    main()
