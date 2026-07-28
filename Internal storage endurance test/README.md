# Internal-storage endurance test data

This directory preserves the raw `LOGS/` and `CORAL/` data collected during
the internal-storage endurance test.  The data was imported from commit
`c39052f` of the former `endurance-test` branch without modifying the raw
files.

The firmware tree used for the test is identified by the annotated tag
`endurance-test-firmware-2026-07-27`, which points to main commit `3c85a8a`.
That commit has the exact `firmware-stm32` tree used by the original test
branch.

The directory layout is preserved from the flight-storage export:

- `LOGS/` contains rotated telemetry CSV files, grouped by boot and batch.
- `CORAL/` contains payload frame data and state files, grouped by boot and
  batch.
