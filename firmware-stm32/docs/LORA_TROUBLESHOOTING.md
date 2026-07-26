# LoRa SPI fault capture

Use the bench-only PlatformIO environment to capture the first failing SPI
operation without adding continuous register reads to the flight image. It is
the default development environment, so the short upload-and-monitor command
includes the LoRa trace.

```powershell
cd firmware-stm32
C:\Users\alber\.platformio\penv\Scripts\pio.exe run --target upload --target monitor
```

The first TTC service pass prints `[LORA_TRACE] enabled ...`, confirming that
the correct image is running. Select `-e nucleo_l476rg` explicitly when
building the normal flight image without UART trace records.

USART2 is routed through the Nucleo ST-LINK virtual COM port. If PlatformIO
selects the wrong port, add `--port COMx` to the monitor command. Stop the
monitor with `Ctrl+C`.

The trace image emits only two new record types:

```text
[LORA_FAULT] n=3 t=42851 cause=XFER_TIMEOUT phase=RX_POLL state=0 reg=IRQ_FLAGS(0x12) op=READ hal=TIMEOUT(3) err=0x00000020 active=1 wait=1 abort=0 irq=0x00 txdone=0
[LORA_STAT] t=50000 window_ms=10000 spi=2000 complete=2000 irqerr=0 timeout=0 abort=0 txpoll=0 rxpoll=2000 dropped=0
```

`LORA_FAULT` is captured at the failure site, before recovery changes the
driver state. Its fields show:

- `cause`: HAL start rejection, IRQ error, transfer timeout, abort rejection,
  abort timeout, SPI deinit/init failure, or modem register verify failure.
- `phase`: init, TX setup/poll/finish, RX start/poll/payload, isolation, SPI
  reinitialisation, or an explicitly requested debug read.
- `reg` and `op`: the exact SX1276 register address and read/write operation.
- `hal` and `err`: the HAL status and the full `HAL_SPI_GetError()` bit mask.
- `active`, `wait`, and `abort`: the SPI lifecycle state at capture time.
- `irq` and `txdone`: the last modem IRQ flags and whether the last packet had
  already reached the SX1276 `TxDone` boundary.

`LORA_STAT` reports deltas for the preceding ten-second window. Use its
`txpoll` and `rxpoll` values to verify the actual rate under load. The driver
now permits one TX IRQ poll every 2 ms (at most 500 polls/s while transmitting)
and one RX IRQ poll every 5 ms (at most 200 polls/s while continuous RX is
idle). SPI completion time and superloop scheduling can only reduce those
observed rates.

The normal `nucleo_l476rg` environment does not enable these UART records.
Neither image performs the old synchronous eleven-register snapshot after
every init or TX completion; operational register access remains IRQ-driven.
