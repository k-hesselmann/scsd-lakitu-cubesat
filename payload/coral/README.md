# Coral Firmware (`coral/`)

On-device firmware for the **Coral Dev Board Micro**, built as an out-of-tree
[coralmicro](https://github.com/google-coral/coralmicro) project. This is the
deployment target for the model trained in the rest of the `payload/` tree.

A MobileNetV2 regression model (int8 QAT, Edge TPU compiled) captures 224×224
grayscale frames, infers a cloud-cover fraction `[0,1]`, and ships the image +
result to an OBC (On-Board Computer) over UART6. Independent of that link, it
also keeps a backup on its own flash (every inference logged, every 6th frame
saved — see "On-board backup" below). See
[`interface_docs/UART_PROTOCOL.md`](../../interface_docs/UART_PROTOCOL.md) for the
wire protocol (and [`interface_docs/OBC_INTEGRATION.md`](../../interface_docs/OBC_INTEGRATION.md)
for the OBC handoff), and [`docs/TODO.md`](docs/TODO.md) for remaining firmware work.

All paths below are relative to this `coral/` directory.

## Layout

```
coral/
  src/cloud_regressor.cc          firmware (M7)
  models/*.tflite                 model — committed here, lands on device at /models/<file>
  tools/cloud_regressor_client.py debug HTTP client (image preview, DEBUG ONLY)
  tools/fetch_backup.py           pulls /backup off the board over USB (any build)
  tools/clear_backup.py           pre-flight: wipes /backup on the board (any build)
  tools/quant_check.py            quantization sanity check
  docs/TODO.md                    deferred firmware work before flight
  patches/                        the coralmicro changes this project needs
  coralmicro/                     submodule → our coralmicro fork (see below)
```

> The deployed model is committed under `models/` via a `.gitignore` exception
> (the repo root otherwise ignores `*.tflite` training artifacts), so the
> firmware builds standalone without regenerating the model.

## The coralmicro fork

This project needs **one** change inside coralmicro, compiled as part of
coralmicro's own libs, so we carry it via a fork pinned as a submodule:

1. The M7 debug console is moved off **LPUART6**
   (`third_party/modified/nxp/rt1176-sdk/board.h`) so the UART6 header pins
   are free for the OBC link.
   [`patches/coralmicro-board-h-lpuart7.patch`](patches/coralmicro-board-h-lpuart7.patch)

A second patch lives in `patches/` but is **not applied for flight**:

2. [`patches/coralmicro-littlefs-full-partition.patch`](patches/coralmicro-littlefs-full-partition.patch)
   would widen the littlefs partition (`libs/base/filesystem.cc`) from
   upstream's 64 MiB to the NAND chip's full 128 MiB, giving the on-flash
   image backup (see "On-board backup" below) more headroom. It left a board
   unable to flash during bring-up, so the flight fork stays on the stock
   64 MiB partition instead; `kBackupImageEveryN` is sized against that (see
   "On-board backup" below). **Changes the on-flash geometry if ever
   applied** — any board previously flashed at the old `block_count`
   auto-reformats (wiping its filesystem) the first time it boots firmware
   built with this patch.

**One-time setup of the fork** (replace `<YOU>` with your GitHub user/org):

```bash
# 1. Fork github.com/google-coral/coralmicro on GitHub (UI), then:
git clone https://github.com/k-hesselmann/coralmicro
cd coralmicro
git checkout c9f665b0          # the upstream commit this was developed against
git checkout -b board-h-uart7
git apply /path/to/scsd-lakitu-cubesat/payload/coral/patches/coralmicro-board-h-lpuart7.patch
git commit -am "board.h: move M7 debug console off LPUART6"
git push -u origin board-h-uart7
```

## Build

```bash
# Clone the payload repo with the coralmicro submodule (pinned to the fork branch):
git clone --recurse-submodules git@github.com:k-hesselmann/scsd-lakitu-cubesat.git
cd scsd-lakitu-cubesat/payload/coral

cmake -B out -S .                 # flight build — debug tooling compiled out
make -C out -j$(nproc)
```

The bench debug tooling (last-image / burst RPC endpoints + button-hold burst
capture) is off by default. To build the bench firmware that keeps it, configure
with `-DCLOUD_DEBUG=ON`:

```bash
cmake -B out -S . -DCLOUD_DEBUG=ON
make -C out -j$(nproc)
```

## Flash

`flashtool` needs both the build dir (for the bootloader) and the elf path:

```bash
python3 coralmicro/scripts/flashtool.py --build_dir out --elf_path out/cloud_payload
```

Close any `screen`/serial session on `/dev/ttyACM*` first. If the auto-reset
fails, hold the User button while plugging in (Serial Downloader mode), flash,
then unplug/replug.

## Debug image client (host)

Requires a firmware built with `-DCLOUD_DEBUG=ON` (see Build) — the flight image
does not export these RPC endpoints.

```bash
pip install -r requirements.txt
python3 tools/cloud_regressor_client.py            # last frame, preview
python3 tools/cloud_regressor_client.py --save ./debug_frames/   # save clean PNG
python3 tools/cloud_regressor_client.py --burst --save ./burst_frames/  # button burst
```

The `get_last_image` / burst RPC endpoints and this client are **bench-only**;
they compile out of the default (flight) build via the `CLOUD_DEBUG` flag.

## On-board backup (flash)

Independent of the OBC/UART link, every successful inference is logged to
`/backup/log.csv` (`seq,fraction`), and every 6th frame (`kBackupImageEveryN`)
also gets its full 224×224 image saved to `/backup/img_<seq8>_cloud<pct>.raw`
— same headerless format the OBC's SD card gets, so
[`tools/raw_to_png.py`](tools/raw_to_png.py) converts these too. This is a
dumb backup: no OBC command triggers or retrieves it, and once flash fills up
it just **stops** rather than overwriting the earliest (ascent) frames.

This runs on coralmicro's **stock 64 MiB** littlefs partition — the
`coralmicro-littlefs-full-partition.patch` above (128 MiB) is *not* applied;
it left a board unable to flash during bring-up, so it's not worth the risk
this close to flight. Capacity is block-limited, not byte-summed: littlefs
allocates a whole 131072 B block per file, and each 50176 B image is smaller
than that, so every image actually costs a full block (~62% wasted). After
the ~2.8 MiB model, that leaves ~475 free blocks. A full 6 h flight at the
OBC's 10 s cadence is <= 2160 inferences, so archiving every 6th frame needs
360 images — comfortably inside the ~475-block budget (~24% spare); every
3rd frame (720 images) does *not* fit and would silently stop archiving
images around the 4 h mark (`log.csv` keeps logging every inference either
way, so the cloud-cover series itself always stays complete).

**Pre-flight: wipe any bench-test backups before launch.** A plain reflash
does *not* clear `/backup` — it only rewrites the model file into the
existing littlefs, so bench frames survive and eat into the flight's block
budget. Run this once right before flight and confirm it reports empty:

```bash
python3 tools/clear_backup.py
```

Retrieval is manual, post-flight, over USB — these RPCs are always exported
(flight build included):

```bash
pip install -r requirements.txt
python3 tools/fetch_backup.py --out ./flight1_backup/
```
