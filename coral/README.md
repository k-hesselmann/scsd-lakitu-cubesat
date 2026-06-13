# Coral Firmware (`coral/`)

On-device firmware for the **Coral Dev Board Micro**, built as an out-of-tree
[coralmicro](https://github.com/google-coral/coralmicro) project. This is the
deployment target for the model trained in the rest of `scsd-ml-payload`.

A MobileNetV2 regression model (int8 QAT, Edge TPU compiled) captures 224×224
grayscale frames, infers a cloud-cover fraction `[0,1]`, and ships the image +
result to an OBC (On-Board Computer) over UART6. See
[`docs/UART_PROTOCOL.md`](docs/UART_PROTOCOL.md) for the wire protocol and
[`docs/TODO.md`](docs/TODO.md) for remaining work.

All paths below are relative to this `coral/` directory.

## Layout

```
coral/
  src/cloud_regressor.cc          firmware (M7)
  models/*.tflite                 model — committed here, lands on device at /models/<file>
  tools/cloud_regressor_client.py debug HTTP client (image preview, DEBUG ONLY)
  tools/quant_check.py            quantization sanity check
  docs/UART_PROTOCOL.md           OBC ↔ Coral protocol spec
  patches/                        the one coralmicro change this project needs
  coralmicro/                     submodule → our coralmicro fork (see below)
```

> The deployed model is committed under `models/` via a `.gitignore` exception
> (the repo root otherwise ignores `*.tflite` training artifacts), so the
> firmware builds standalone without regenerating the model.

## The coralmicro fork

This project needs **one** change inside coralmicro: the M7 debug console is
moved off **LPUART6** (`third_party/modified/nxp/rt1176-sdk/board.h`) so the
UART6 header pins are free for the OBC link. Because that file is compiled as
part of coralmicro's own libs, we carry it via a fork pinned as a submodule.
The change is captured in [`patches/coralmicro-board-h-lpuart7.patch`](patches/coralmicro-board-h-lpuart7.patch).

**One-time setup of the fork** (replace `<YOU>` with your GitHub user/org):

```bash
# 1. Fork github.com/google-coral/coralmicro on GitHub (UI), then:
git clone https://github.com/k-hesselmann/coralmicro
cd coralmicro
git checkout c9f665b0          # the upstream commit this was developed against
git checkout -b board-h-uart7
git apply /path/to/scsd-ml-payload/coral/patches/coralmicro-board-h-lpuart7.patch
git commit -am "board.h: move M7 debug console off LPUART6 for cloud_payload"
git push -u origin board-h-uart7
```

## Build

```bash
# Clone the payload repo with the coralmicro submodule (pinned to the fork branch):
git clone --recurse-submodules git@github.com:k-hesselmann/scsd-ml-payload.git
cd scsd-ml-payload/coral

cmake -B out -S .
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

```bash
pip install -r requirements.txt
python3 tools/cloud_regressor_client.py            # last frame, preview
python3 tools/cloud_regressor_client.py --save ./debug_frames/   # save clean PNG
python3 tools/cloud_regressor_client.py --burst --save ./burst_frames/  # button burst
```

The `get_last_image` / burst RPC endpoints and this client are **debug-only**
and marked for removal before flight (see `docs/TODO.md`).
