# SD-card terminal diagnostics

High-level SD diagnostics are enabled in every firmware environment and use
the common non-blocking, timestamped USART2 queue.

## Initialization

```text
[t=0000001200ms] [SD_INIT] phase=start flush_ms=5000 rotate_s=60 batch_rows=50
[t=0000001840ms] [SD_INIT] result=OK fatfs=FR_OK(0) op=SYNC duration=640ms card=SDHC sectors=62521344 capacity_mib=30528 session=12 file=LOG_000012_START_0000000001_OPEN.CSV
```

The summary identifies the card type/capacity, exact FatFS result, operation,
session and open filename. A mount failure caused by a card/SPI initialization
failure is reported as `op=CARD_INIT`.

## Periodic status

`[SD_STAT]` and `[SD_SPI]` are emitted every 10 seconds. Important fields are:

- `state`: `OFF`, `ACTIVE`, or `WAIT_RETRY`.
- `err`: current FatFS result with both name and number.
- `file`, `session`, `rows`, `buf`: active file and CSV-buffer state.
- `bytes`: data successfully written and synchronized since boot.
- `flush`, `last_max_ms`, `sync_age`: successful flush count, latency and
  freshness. A large maximum exposes blocking SD operations.
- `faults`, `recover`, `rotate`, `discard`: filesystem fault/recovery history.
- `[SD_SPI] card`, `sectors`: card identity and capacity.
- `spi_err`, `cmd`, `r1`: SPI HAL errors and the most recent SD command result.
- `rd_fail`, `wr_fail`, `sync_fail`, `ready_to`, `token_to`, `reject`: exact
  low-level failure categories.
- `coral_sd`: SD failures while writing Coral image files.

An age of `4294967295` means no successful synchronization has been observed.

## Fault and recovery records

```text
[t=0000014320ms] [SD_FAULT] op=SYNC fatfs=FR_DISK_ERR(1) req=18420 wrote=0 buf_drop=18420 consecutive=1 total=1 spi_err=0 hal_err=0x00000000 hal_state=1 cmd=24 r1=0xFF rx=0xFF sector=8128 count=1
[t=0000034400ms] [SD_RECOVERY] attempt=1 phase=start previous_op=SYNC previous=FR_DISK_ERR(1)
[t=0000035020ms] [SD_RECOVERY] attempt=1 result=OK fatfs=FR_OK(0) op=SYNC duration=620ms session=13 file=LOG_000013_START_0000000035_OPEN.CSV
```

Repeated failures are not printed for every 10 Hz CSV row: once the logger is
in `WAIT_RETRY`, FDIR retries it on the existing 20-second cooldown.

## Bench-only command trace

For card initialization troubleshooting, use the dedicated build:

```powershell
platformio.exe run -e nucleo_l476rg_sd_trace --target upload --target monitor
```

It adds command-level records such as:

```text
[t=0000000710ms] [SD_CMD] cmd=0 arg=0x00000000 r1=0x01
[t=0000000735ms] [SD_CMD] cmd=8 arg=0x000001AA r1=0x01
```

Individual SPI bytes and normal CSV rows are intentionally never printed.
