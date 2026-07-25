# Bench Session Notes — 2026-07-19/20

Observations from Phase 1 (bring-up/smoke test) and Phase 3 (fault injection)
on `nucleo_l476rg_testhooks`, flagged for developer follow-up. Not blocking
further campaign work, but should be looked at before flight.

## 2026-07-21 Phase 3 continuation

Test-hooks image was flashed successfully with PlatformIO
`nucleo_l476rg_testhooks`; OpenOCD verified the image. The runtime banner
`FDIR_TEST_HOOKS build - bench-only` appeared on the ST-Link VCP.

USART2 TX remains healthy, but host-to-board RX still did not reach the hook
console: repeated `HOOK STATUS` and intentionally malformed commands over both
`/dev/cu.usbmodem14303` and `/dev/tty.usbmodem14303` produced no hook response
or parse-error response. Synthetic hook rows were therefore driven over SWD by
writing the hook state variables in RAM:

- C4 IMU garbage: armed `s_imu_freeze_active` with accel pinned at 15 g.
  Evidence seen on console: `[VALIDATION] IMU magnitude implausible`,
  `IMU_POOL: AX=15.000 ... VALID=0`. Because GPS had no indoor fix at the same
  time, validation also escalated to `2 sensors broken - I2C restart`.
- C6 baro offset: armed `s_baro_offset_active` with +1000 m. The datapool field
  `g_datapool.baro_alt_m` read back over SWD as about 999.7 m, confirming the
  injection path and SD/telemetry data source. The actual BARO/GPS cross-check
  did not fire because GPS was invalid indoors; this row still needs a valid GPS
  fix or a GPS simulator/further hook to exercise the expected 5 s disagreement
  debounce.
- C8 ADC fault: armed `s_adc_fault_active`; after debounce, `batt_valid` read
  back as 0 and `equipment_faults` as `0x0041` (GPS + EPS_ADC). After clearing
  the hook, `batt_valid` returned to 1 and `equipment_faults` returned to
  `0x0001` (GPS-only, expected indoors).
- F3 SCV erase: erased only the reserved SCV flash page
  `0x080FF800..0x080FFFFF` over OpenOCD and reset the MCU. Boot continued
  normally; `g_scv` read back with `magic=0xCAFE`, `boot_count=1`,
  `mission_elapsed_ms=0`, and `flight_phase=STANDBY`, confirming default
  reinitialisation after blank SCV.

SD card logging was observed working during this continuation session
(`CORAL] SD file opened OK`, frame files closed with CRC OK), so SD evidence is
usable again for follow-up campaign steps.

Physical fault actions continued on the same flashed image, with a timestamped
serial capture in `output/test_campaign/phase3_20260721/serial.log`:

- SD removal while the Coral stream was active produced
  `[CORAL] !!! SD f_open FAILED -- draining UART without saving`. Live SCV
  readback showed `equipment_faults=0x0011` (GPS + SD) and `sd_fault_count`
  incrementing. Reinsertion alone did not immediately clear the fault, but a
  reset with the SD inserted remounted cleanly: later console output showed
  `SD file opened OK`, `Pixel stream done, SD file closed`, CRC OK, and SCV
  returned to `equipment_faults=0x0001` (GPS-only indoors). This should recover
  at runtime after hot reinsertion without requiring an MCU reset; current
  behavior points to an incomplete FatFS/SPI/logger remount or reinitialisation
  path after removal.
- GPS/I2C physical disturbance produced a useful C7-style bus fault: IMU went
  invalid, baro went invalid, and SWD readback showed
  `equipment_faults=0x0007` (GPS + IMU + BARO) while the main loop and SD/Coral
  logging continued. After reseating the sensor wiring, I2C scan again found
  `0x42`, `0x68`, and `0x76`, IMU/BARO samples returned valid, and SCV cleared
  back to `0x0001`.
- GPS disconnect was later unambiguous: GPS recovery printed
  `[M10S] ERROR: Device not found at I2C 0x42`; after reconnect, GPS
  reinitialised and NAV-PVT streaming resumed.
- Coral stream disconnect exposed a gap in the current FDIR observable. After
  the line was disconnected, no new `[CORAL] SOF/Header/SD file opened` lines
  appeared, but `g_datapool.coral_valid` remained `1`, `coral_timeout_count`
  stayed 0, and `equipment_faults` stayed `0x0001`. Code inspection matched the
  bench result: `Coral_Update()` clears `coral_valid` only after a new SOF is
  seen, then sets it on a clean frame. A completely silent Coral link after the
  last good frame therefore leaves the last sample sticky-valid forever and the
  FDIR Coral timeout monitor cannot trip. Recommend adding an age/last-rx
  timeout for `coral_valid` or a Coral heartbeat/expected-frame deadline before
  closing the Coral-loss row.
- LoRa/TTC physical fault detection was observed after disturbing the radio SPI
  path or holding the radio in reset: SWD readback showed
  `equipment_faults=0x0021` (GPS + LORA). After the radio path was restored and
  the attempt window had time to refresh, SWD readback returned to
  `equipment_faults=0x0001` (GPS-only indoors), confirming LoRa recovery.

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
