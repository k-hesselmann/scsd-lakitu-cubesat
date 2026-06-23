# ml-payload

Machine-learning payload for the SCSD CubeSat: on-board cloud detection running
on a Coral Dev Board Micro (Edge TPU). Two models are trained from the
95-Cloud / 38-Cloud satellite imagery dataset, quantized to int8, compiled for
the Edge TPU, and flashed to the Coral as firmware.

## Pipeline

```
95-Cloud / 38-Cloud dataset            (data/)
        │  training/build_cloud_classes.py
        ▼
whole-image class dataset              (data/cloud_classes/ — clear / overcast / partial + manifest.csv)
        │  training/notebooks/train_cloud_*.ipynb
        ▼
float Keras models                     (models/*.keras)
        │  post-training int8  /  quantization-aware training (QAT)
        ▼
int8 TFLite models                     (models/*_int8.tflite)
        │  edgetpu_compiler
        ▼
Edge TPU model                         (coral/models/*_edgetpu.tflite — tracked)
        │  coral/  (see coral/README.md)
        ▼
flashed firmware on Coral Dev Micro
```

Two models:
- **classifier** — 3-class whole-image label (`clear` / `overcast` / `partial`), labels in [training/labels.json](training/labels.json).
- **regressor** — continuous cloud-fraction estimate; this is the model currently deployed (see [coral/src/cloud_regressor.cc](coral/src/cloud_regressor.cc)).

## Layout

| Path | Contents | Tracked? |
|------|----------|----------|
| [training/](training/) | dataset builder, training notebooks, `labels.json` | yes |
| [coral/](coral/) | Coral Dev Micro firmware (C++/CMake) + `coralmicro` submodule | yes |
| `data/` | datasets (`95cloud/`, `cloud_classes/`) | no — gitignored |
| `models/` | float + int8 model artifacts (`.keras`, `.tflite`) | no — gitignored |
| [coral/models/](coral/models/) | the compiled Edge TPU model that ships | yes |

## Setup

```bash
python3 -m venv .venv && source .venv/bin/activate   # Python 3.12 (TensorFlow has no 3.13/3.14 wheels)
pip install -r requirements.txt                       # training environment
```

Notebooks resolve paths relative to the repo root, so launch Jupyter from
anywhere in the tree. The Coral firmware build/flash has its own dependencies —
see [coral/README.md](coral/README.md).
