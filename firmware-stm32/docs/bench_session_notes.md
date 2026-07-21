# Bench Session Notes — 2026-07-19/20

Observations from Phase 1 (bring-up/smoke test) and Phase 3 (fault injection)
on `nucleo_l476rg_testhooks`, flagged for developer follow-up. Not blocking
further campaign work, but should be looked at before flight.

## 1. IMU: intermittent bit-exact repeated readings (possible stuck-sensor glitch)

`sensor_validation.c`'s C4 stuck-value check (`IMU_STUCK_CYCLES = 3`) is
occasionally tripping on real hardware, with no fault injected. Two
consecutive `IMU_POOL` debug prints ~1 s apart showed **bit-for-bit identical**
values across all six channels simultaneously:

```
AX=0.926 AY=0.288 AZ=0.071 GX=-2.70 GY=0.04 GZ=-1.19   (20:06:11)
AX=0.926 AY=0.288 AZ=0.071 GX=-2.70 GY=0.04 GZ=-1.19   (20:06:12)
```

Six independent analog channels (3 accel + 3 gyro) matching to the last
printed decimal simultaneously is not plausible as coincidental noise-floor
overlap — this looks like a real intermittent I2C read glitch on the MPU6050
(e.g. reading a stale register set, or address/latch timing issue), not a
test artifact. `IMU_POOL VALID=0` correctly followed both times, so the C4
monitor is doing its job — but the underlying intermittent freeze on the
sensor/bus itself is worth root-causing. Recommend the CDH/IMU owner check
I2C1 timing margins and whether `MPU6050_EquipmentHandler_Update()` is
reading a fresh sample each cycle (e.g. DATA_RDY handling) vs. occasionally
re-reading a latched register.

## 2. GPS: cold-start TTFF was ~4+ minutes indoors, with garbage lat/lon while unlocked

Time-to-first-fix was long even after a deliberate repower: no fix for over
3 minutes of continuous observation (multiple capture windows), then locked
(`Fix=3`, `SV=5`) shortly after. This may simply be a weak/marginal antenna
placement rather than a firmware issue — but flagging the duration in case it
recurs consistently, since PR-006 (≤30 s boot-to-first-telemetry) and the
mission timeline assume much faster GPS acquisition once outdoors with sky
view.

Separately (informational, not a bug): while unlocked (`Fix=0`, `SV=0`), the
u-blox M10S's `NAV-PVT` lat/lon/hMSL fields carried non-zero, rapidly
changing values (e.g. swinging degrees within a few seconds — physically
impossible for a stationary receiver). This is expected M10 behavior per the
interface spec — those fields are only valid when `fixType != 0` — and our
downstream `GPS_POOL`/`VALIDATION` logic already correctly gates on
`Fix`/`SV`, not on lat/lon being non-zero, so no invalid data reached FDIR or
telemetry. Noting it only so nobody mistakes those numbers for a real
excursion if seen again in a raw UBX capture.

## 3. NAV-PVT stream: one observed 9-second gap in an otherwise steady 1 Hz cadence

During one 15 s capture, consecutive `iTOW` timestamps jumped from
`72565000` to `72574000` (a 9 s gap) while the console was being read
continuously. Could be UART line contention from the heavy `[VALIDATION]`
debug print volume (competing for the same USART2 TX budget), or a genuine
stall somewhere in the GPS read/parse path. Only observed once; worth
watching for recurrence, especially under Phase 4 endurance logging where
gaps like this would show up as real telemetry sample loss.

## 4. Fault-injection console (`FDIR_TEST_HOOKS`): RX path appears dead on this bench setup

`HOOK` commands sent to the board over USART2 (same port the debug console
TX comes from) never produced a response — not `HOOK STATUS`'s ack, not
even a `?` parse error — across 50+ retries with a concurrent reader thread
(ruling out a host-side read/write race). Ruled out on the firmware side
before concluding this:

- Disassembly confirms `hook_poll_uart()`/`HAL_UART_Receive()` is compiled
  into `FDIR_TestHooks_PreValidation()` and called every `CDH_Update()` cycle.
- `HAL_UART_Receive(&huart2, ..., 0U)` is genuine non-blocking poll semantics
  on this HAL version (verified against `stm32l4xx_hal_uart.c` source) —
  returns immediately if no byte is waiting.
- `PA2=USART2_TX` / `PA3=USART2_RX`, AF7, is configured exactly once in
  `HAL_UART_MspInit()`; nothing else in the codebase reconfigures those pins
  afterward (checked `MX_GPIO_Init()`, ADC1 MSP — PA0 only — SPI1/SPI2, I2C1,
  UART5, USB Device init).
- `SWO_Pin = GPIO_PIN_3` in `main.h` is a red herring — its `SWO_GPIO_Port`
  is `GPIOB` (real pin PB3, standard ARM SWO trace pin), and it's never
  actually initialized anywhere. Different port from `PA3`, not a conflict.
- TTC (LoRa) has zero UART/USART references — SPI1-only.
- `sd_spi.c` only *transmits* debug trace on `huart2` (gated off by default);
  never receives or re-initializes the peripheral.
- No NVIC interrupt is enabled for USART2, ruling out an ISR silently
  consuming/discarding bytes before the polling code gets to them.

TX definitely works (all bench console output all session came over this
same port). Since the software path checks out clean, the most likely
explanation is physical: either the ST-Link's USART2↔VCP RX bridge isn't
actually connected on this particular board/harness (e.g. solder bridges
SB13/SB14 unpopulated on a stock Nucleo-64), or there's a break between the
ST-Link chip and PA3 in the current bench wiring. Recommend checking
continuity/solder bridges, or scoping PA3 directly while sending a known
pattern from the host, before assuming the console itself is broken.

This blocks every FMECA §5 row that depends on the hook console: C4/C6/C8
synthetic injection and F3 (SCV erase-on-demand). Physical-injection rows
(I2C disconnects, SDA short, LoRa/Coral disconnects, IWDG loop-hang) are
unaffected and were exercised separately.

## 5. C7 (I2C bus wedge) bench attempt: partial reaction only, not full escalation

Shorting I2C1's SDA (PB9) to GND briefly during one bench attempt produced a
single-sensor reaction — GPS's I2C read glitched (`GPS_POOL` briefly reset to
0.0/0.0, `[VALIDATION] Recovery retry` printed), self-healed within a few
cycles. `i2c_bus_state`/`s_fdir.bus_state` (polled live via SWD, `mdb` at
`0x20004b87`/`0x200003f8` once a second) stayed at `0x00`
(`CDH_FDIR_BUS_IDLE`) throughout — the full bus-restart/9-pulse-clear path
(`CDH_FDIR_BusRestart_Process`) never triggered, because
`sensor_validation.c`'s `handleRecovery()` only calls
`CDH_RequestBusRestart()` when **2 or more** sensors are simultaneously
invalid (`fault_count > 1`); IMU and baro stayed valid the whole time, so
only the single-sensor retry path fired.

To actually exercise C7 end-to-end, the short needs to be held firmly enough,
long enough, that at least two I2C devices (e.g. IMU + baro) time out in the
same window. Not yet achieved on the bench; retry with a longer/firmer short
recommended as a follow-up bench session.

## Context / non-issues confirmed during this session (for reference)

- SD card was intentionally disabled for parts of this session via
  `scv->equipment_manual_disable |= EQUIPMENT_SD` (temporary, `fdir.c`,
  marked `TEMP TEST`) while the SD card itself was being reformatted.
  `[CORAL] !!! SD f_open FAILED -- draining UART without saving` was the
  expected, graceful consequence, not a fault. That line has since been
  reverted/removed from the working tree independent of this note.
- IMU/baro/Coral all initialized and produced plausible values at rest
  (accel magnitude ~0.97-0.99 g, baro altitude ~486 m stable, Coral frames
  CRC-passing) — no concerns there.
- GPS eventually acquired a stable 3D fix (`Fix=3`, `SV=5`) at a plausible
  Munich-area coordinate, holding steady within GPS jitter — no concerns
  with the fix itself once acquired.
