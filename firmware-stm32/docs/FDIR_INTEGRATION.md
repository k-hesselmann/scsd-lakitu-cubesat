# FDIR Integration — What Each Subsystem Owner Needs to Do

FDIR (owner: Andrei) now implements the monitor spec of `FMECA.md` §4 plus SCV
flash persistence, without touching any subsystem code. This document lists the
small changes each owner is asked to make in **their own files** to complete the
loop. Until then the firmware builds and runs (weak stubs in
`Core/Src/fdir/fdir_hooks.c` keep the affected monitors inert), but the items
marked ⚠ are functional gaps.

## The interface (implemented, in `Core/Inc/fdir/fdir.h`)

```c
uint16_t FDIR_GetReinitRequests(void);          /* EQUIPMENT_x bits          */
void     FDIR_AcknowledgeReinit(uint16_t mask); /* clear after executing     */
void     FDIR_SetEquipmentFault(uint16_t mask, uint8_t active);
uint8_t  FDIR_SubsystemEnabled(FDIR_Subsystem_t subsystem);
```

Pattern (FMECA §2, L1/L2): FDIR detects and debounces, then sets a request bit.
The owning subsystem polls its bits once per cycle, performs the recovery it
knows how to do, and acknowledges. FDIR clears the fault automatically once
valid data returns. FDIR enforces the cooldowns (10 s; 60 s for GPS), so
consumers may execute every request they see.

## CDH owner

⚠ **Priority — this replaces behaviour that FDIR previously did itself.**
`fdir.c` no longer holds an I2C handle and no longer calls
`CDH_FDIR_RecoverIMU/Baro` directly. Until CDH consumes the requests, IMU/baro
device reinit does not happen (the L2 bus restart via `CDH_RequestBusRestart()`
still works — that interface is unchanged).

In `CDH_Update()` (suggested: at the top, so a recovered device can deliver
valid data in the same tick):

```c
uint16_t requests = FDIR_GetReinitRequests();
if (requests & EQUIPMENT_IMU)  { CDH_FDIR_RecoverIMU(&hi2c1);  FDIR_AcknowledgeReinit(EQUIPMENT_IMU);  }
if (requests & EQUIPMENT_BARO) { CDH_FDIR_RecoverBaro(&hi2c1); FDIR_AcknowledgeReinit(EQUIPMENT_BARO); }
if (requests & EQUIPMENT_GPS)  { s_gps = GPS_EquipmentHandler_Init(&hi2c1); FDIR_AcknowledgeReinit(EQUIPMENT_GPS); }
```

Also requested:
- Replace any direct writes to `g_scv.equipment_faults` (e.g. in
  `sensor_validation.c`) with `FDIR_SetEquipmentFault(mask, active)` so the
  bit has a single implementation.
- The `/* TODO: write updated scv to NVM */` in `CDH_Update()` can be deleted —
  SCV persistence is now owned by FDIR (`Core/Src/fdir/scv.c`), called from the
  superloop.
- Later (FMECA C7): add the 9-pulse SCL bit-bang bus clear to
  `CDH_FDIR_BusRestart_Process()` — DeInit/Init alone cannot release a slave
  holding SDA low.

## TTC owner

⚠ Two gaps: FDIR cannot see TX failures, and a failed `LoRa_Init()` at boot is
permanent (`s_radio_ready` is never retried) — FMECA T1.

1. Count consecutive send failures and expose them by defining the strong
   version of the hook (prototype in `Core/Inc/fdir/fdir_hooks.h`; the weak
   stub returns 0 and disables the LoRa monitor):

```c
uint8_t TTC_ConsecutiveTxFailures(void) { return s_consecutive_tx_failures; }
```

   Increment on every failed/skipped send, reset to 0 on success.

2. Consume the reinit request in `TTC_Transmit()` before sending:

```c
if (FDIR_GetReinitRequests() & EQUIPMENT_LORA) {
    s_radio_ready = (LoRa_Init() == LORA_OK);   /* pulses the RST line */
    FDIR_AcknowledgeReinit(EQUIPMENT_LORA);
}
```

3. Replace the local `TTC_SetLoRaFault()` with
   `FDIR_SetEquipmentFault(EQUIPMENT_LORA, active)`.

## SD / storage owner

Low priority — your remount logic stays exactly as is (you own recovery).
Requested: replace the two direct `scv->equipment_faults` writes in
`sd_set_fault()` / `sd_set_healthy()` with
`FDIR_SetEquipmentFault(EQUIPMENT_SD, 1U/0U)`. `sd_fault_count` remains yours.

## Everyone: staged reduced mode (FMECA F2)

`main()` now skips a subsystem's `_Init`/`_Update` calls after repeated
consecutive watchdog resets: ≥3 disables SD, ≥4 also CDH, ≥5 also TTC, ≥6 also
FSW. Any boot for a reason other than the watchdog re-enables everything. Your
module must tolerate simply not being called; no code change expected — just be
aware when testing.

The active stage is expressed in `scv.equipment_enabled` (disabled subsystem →
its equipment bits cleared: SD, LoRa, or all five CDH sensors), so the degraded
configuration is visible in telemetry and the post-flight log. Semantics of the
two fields: `equipment_faults` = detection plane, set/cleared automatically by
monitors; `equipment_enabled` = policy plane, changed only by deliberate FDIR
decisions. The FSM uses equipment only when it is enabled **and** not faulted.
Monitors don't run for equipment whose subsystem is disabled (no phantom
faults for sensors nobody samples).

Optional next step (not implemented): a **give-up policy** — after N reinit
requests with no recovery, FDIR clears the equipment's enabled bit so a dead
device stops generating recovery traffic on the bus. Marked as a TODO in
`fdir.c`; needs its own persisted state and a re-arm rule (e.g. power-on
reset) before it can be added.

## SCV — ownership and rules (FMECA / ICD supplement)

The SCV is now actually persisted: newest-valid-slot scheme in the reserved
flash page at `0x080FF800` (see `STM32L476RGTX_FLASH_SCV.ld`), written every
60 s and on phase/fault changes. It survives resets **and reflashing**
(PlatformIO builds only — the stock CubeIDE linker script has no SCV carve-out;
do not flash CubeIDE-built images). Explicit reset: build once with
`-DSCV_FORCE_RESET`, or call `SCV_Erase()` from a debug hook.

| Field | Writer |
|---|---|
| magic, crc16, boot_count, mission_elapsed_ms, reset_reason, watchdog_reset_count | FDIR/SCV module only |
| equipment_enabled, equipment_faults, gps/imu/baro/coral_timeout_count, lora_timeout_count, last_batt_mv | FDIR (faults via `FDIR_SetEquipmentFault` for everyone; lora_timeout_count mirrors TTC's TX failure counter) |
| flight_phase | FSW (`FSW_SetPhase`) |
| baro_ground_alt_cm | FSW/CDH — still unwritten today; owner TBD |
| sd_fault_count | SD logger |

Pending for the next coordinated format revision (telemetry v4): `SCV_t` gained
`lora_timeout_count` (persisted to flash only). Adding it to the downlink needs
a joint bump — `scv_lora_timeout_count` in `TelemetryPacket_t` +
`protocol_version 0x04` (FSW), the 128→129-byte `_Static_assert` (TTC), the CSV
column (SD), and the ground-station parser. Until then the field flies in the
SCV/flash only.

Rules:
- **Never compute or write `crc16`.** It is computed once per flash backup by
  `SCV_Backup()` and validated only at boot. Between backups the RAM field is
  simply "last persisted CRC".
- Counters saturate at `UINT8_MAX` during a continuous fault and reset to 0 on
  valid data — never let one wrap, a wrap would clear a fault spuriously.
- `mission_elapsed_ms` is now real mission time: it starts at the
  STANDBY→LAUNCH transition, survives resets via the flash backup, and runs
  until power-off. Don't write it from subsystem code.

## `i2c_bus_state` (FYI, no action)

The datapool field `i2c_bus_state` (written by CDH from its bus-restart state
machine: 0 idle, 1 restart in progress, 2 hold) is **telemetry/observability
only** — no onboard logic reads it. It exists so ground/post-flight analysis
can correlate sensor dropouts with bus-restart activity. Decided 2026-07-16:
FDIR intentionally does not consume it.

## Watchdog (FYI, no action)

The IWDG is armed in `main()` after init (~30 s timeout). It is configured in
the `.ioc` (prescaler 256, reload 3750) **and** mirrored by the idempotent
`IWDG_UserInit()` in a main.c USER CODE block, same pattern as the SPIs — if
you change one, change both. After pulling, CubeIDE users should regenerate
from the `.ioc` so `stm32l4xx_hal_iwdg.c` is compiled. It is kicked at the
**end** of the superloop, gated by
`FDIR_SystemHealthyEnoughToKickWatchdog()`. Any single call that blocks longer
than ~30 s now resets the MCU. All your HAL calls must use bounded timeouts
(LoRa driver already verified; please check your own paths).
