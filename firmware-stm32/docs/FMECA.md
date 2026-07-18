# FMECA & FDIR Monitor Specification — Lakitu CubeSat

Working document for the FDIR design pass. Owner: Andrei (FSW/FDIR).

## 1. Severity scale

| Sev | Meaning | Example |
|---|---|---|
| 1 | Negligible — no mission impact | one lost telemetry packet |
| 2 | Degraded — partial data/function loss | Coral dead, core mission continues |
| 3 | Major — loss of a primary mission objective | no SD log recovered after flight |
| 4 | Critical — loss of mission | FSW hung for remainder of flight, no telemetry and no log |

## 2. FDIR handling levels

Handle every fault at the lowest level that contains it; escalate only on repeated
failure at that level.

| Level | Scope | Mechanism | Owner |
|---|---|---|---|
| L0 | Inside a driver | retries, CRC/plausibility checks, bounded HAL timeouts | subsystem owner |
| L1 | Single equipment | debounced fault flag + periodic protocol reinit request | FDIR detects, owner executes |
| L2 | Bus / subsystem group | I2C bus restart, SD remount | FDIR requests, CDH/owner executes |
| L3 | Processor | IWDG reset, SCV persistence across reset | FDIR + hardware |
| L4 | System | reduced mode: disable subsystem updates one after another — SD card, then CDH, then TTC, then FSW — until the reset storm stops (no power isolation exists; each stage removes a potential hang source) | FDIR policy, FSW executes |

## 3. FMECA worksheet

Sev = severity (Section 1), Lvl = handling level (Section 2).
Phases: Standby / Launch / Ascent / Cruise / Descent / Landing (fsm.h).

### CDH — I2C1 sensors & internal ADC

| # | Item | Failure mode | Possible cause | Local effect | System effect | Detection means | Isolation | Recovery | Worst phase | Sev | Lvl |
|---|---|---|---|---|---|---|---|---|---|---|---|
| C1 | GPS (MAX-M10S) | no fix (module alive, responds on I2C) | environment, antenna, altitude limits | `gps_valid=0`, fix_type indicates no fix | phase detection degrades to baro | fix_type / data-present distinction (module supports it) | FSM must not require GPS | none — environmental, wait | Ascent | 2 | L1 |
| C2 | GPS (MAX-M10S) | no I2C response at all | bus glitch, module hang | `gps_valid=0`, no data | no position in telemetry/log | I2C NACK / no bytes available (distinct from C1) | — | protocol reinit request (no power cycle exists) | Ascent | 2 | L1 |
| C3 | IMU (MPU-6050) | stops responding (NACK) | bus glitch, latch-up | `imu_valid=0` | launch/landing detection degraded | `imu_valid` low for 3 cycles | FSM falls back to baro/GPS rates | device reinit request every 10 s | Launch | 2 | L1 |
| C4 | IMU (MPU-6050) | frozen/garbage values while ACKing | internal fault | `imu_valid=1` but data wrong | wrong phase transitions | plausibility check: accel magnitude range + stuck-value counter (confirmed feasible; to implement) | ignore IMU in FSM when flagged | device reinit request | Launch | 3 | L1 |
| C5 | Baro (MS5607) | stops responding | bus glitch, latch-up | `baro_valid=0` | altitude/phase detection degraded | `baro_valid` low for 3 cycles | FSM falls back to GPS altitude | device reinit request every 10 s | Descent | 3 | L1 |
| C6 | Baro (MS5607) | plausible-but-wrong pressure | sensor drift, PROM corruption | wrong altitude | wrong phase transitions | GPS/baro altitude cross-check when both valid (confirmed feasible; to implement) | prefer GPS altitude on disagreement | reinit; re-read PROM + CRC | Descent | 3 | L1 |
| C7 | I2C1 bus | bus wedged (slave holds SDA low) | interrupted transaction, EMI | GPS, IMU **and** baro all invalid | all sensing lost; FSM blind | IMU ∧ baro faulted (fastest debounce pair); GPS follows | LoRa (SPI1) and SD (SPI2) unaffected — telemetry/log continue | bus restart (DeInit/Init); add 9-pulse SCL bit-bang clear (to implement) | any | 3 | L2 |
| C8 | Internal ADC (battery sense) | invalid/absent reading | ADC config fault, divider open | `batt_valid=0` | no battery insight in telemetry | `batt_valid` low for 3 cycles (debounce to add — currently 1) | — | internal ADC re-init (cheap); low risk overall | Cruise | 1 | L1 |
| C9 | Battery | undervoltage (real) | cold, capacity | brownouts, resets | cascading resets | visible in telemetry (`batt_voltage_mv`) | **none possible** — no load shedding or power switching exists | accepted risk; mitigate pre-flight (battery sizing, insulation) | Cruise | 3 | — |
| C10 | CDH loop | `CDH_Update` stops filling datapool | logic bug, blocking call without timeout | stale datapool, flags may stay valid | FDIR blind, FSM acts on stale data | `timestamp_ms` in datapool stops advancing (written by CDH each cycle) | — | if FDIR still runs: bus restart; if whole loop hung: IWDG (F1) | any | 4 | L2/L3 |

### Payload — Coral (RX-only interface)

| # | Item | Failure mode | Possible cause | Local effect | System effect | Detection means | Isolation | Recovery | Worst phase | Sev | Lvl |
|---|---|---|---|---|---|---|---|---|---|---|---|
| P1 | Coral | no block for 5 cycles | crash, boot loop, link fault | `coral_valid=0` | payload objective degraded | `coral_valid` low for 5 cycles | core mission unaffected | **none possible** (RX-only, no reset/power path) — fault flag for telemetry/log only | Cruise | 2 | L1 |
| P2 | Coral | garbage/malformed block | protocol desync | wrong payload data logged | corrupted payload results | framing/plausibility check in coral driver (L0) | drop block, keep `coral_valid=0` | resync on next block boundary | Cruise | 2 | L0 |

### Storage — SD card (SPI2)

| # | Item | Failure mode | Possible cause | Local effect | System effect | Detection means | Isolation | Recovery | Worst phase | Sev | Lvl |
|---|---|---|---|---|---|---|---|---|---|---|---|
| S1 | SD | write/mount failure | vibration unseat, SPI glitch, FS corruption | log rows lost | post-flight data gap | consecutive-failure count (exists in sd_logger.c; policy to move to FDIR) | telemetry continues | remount after 3 failures; rotate file | Launch | 3 | L2 |
| S2 | SD | card full | long mission, log rate | writes fail | log stops | FR_DENIED / free-space check | keep telemetry | accepted low risk (card sized for mission); no rotation policy needed | Landing | 1 | — |
| S3 | SD | corrupted filesystem after power loss | reset during write | mount fails at boot | no logging this boot | mount result at init | — | never auto-format (destructive); log the event, continue without SD | any | 3 | L2 |

### TTC — LoRa RFM9x (SPI1, downlink only)

Detection and recovery are both implemented: bounded 100 ms SPI timeouts,
version-register (0x42) check at init, TxDone wait with timeout, and `fdir.c`
thresholds TTC's raw TX-outcome counters (`TTC_FDIR_GetHealth()`) into
`EQUIPMENT_LORA` set/clear and recovery requests — TTC itself sets neither.
The radio also has a hardware RST line pulsed by `LoRa_Init()`, invoked via
`TTC_FDIR_RequestRecovery()`.

| # | Item | Failure mode | Possible cause | Local effect | System effect | Detection means | Isolation | Recovery | Worst phase | Sev | Lvl |
|---|---|---|---|---|---|---|---|---|---|---|---|
| T1 | LoRa | init fails at boot, or send fails / TxDone timeout | SPI fault, module hang | packet not sent, LORA fault set | ground blind | FDIR thresholds TTC_FDIR_GetHealth() → EQUIPMENT_LORA (≥90% of last 10 TX failed) | logging continues | recovery request (reinit bitmask) → `TTC_FDIR_RequestRecovery()` (pulses RST), on ≥5 consecutive failures or ≥60% of last 20, 10 s cooldown | any | 3 | L1 |
| T2 | LoRa | TX reports OK but nothing radiated | antenna, RF stage | packets lost silently | ground blind | not software-detectable (downlink only, no ACK) | — | accepted risk; mitigate by pre-flight RF range test | any | 3 | — |
| T3 | LoRa | driver blocks the superloop | SPI hang | loop stalls | whole FSW hung | already mitigated at L0: all SPI calls bounded (100 ms), TxDone wait bounded (5 s) | — | IWDG backstop (F1) | any | 4 | L0/L3 |

### Processor / FSW (owner: Andrei)

| # | Item | Failure mode | Possible cause | Local effect | System effect | Detection means | Isolation | Recovery | Worst phase | Sev | Lvl |
|---|---|---|---|---|---|---|---|---|---|---|---|
| F1 | MCU | superloop hangs | blocking call, logic bug | everything stops | mission loss if unrecovered | IWDG — **not enabled yet (main.c TODO)**; kick at end of loop only | — | watchdog reset; SCV restores phase/state | any | 4 | L3 |
| F2 | MCU | repeated watchdog resets (boot loop) | persistent fault re-triggered each boot | reset storm | mission loss | `watchdog_reset_count >= 3` (counted today, **not acted on**) | staged: each further reset disables the next subsystem update (SD → CDH → TTC → FSW) | reduced mode; FDIR + watchdog kick stay alive at every stage | any | 4 | L4 |
| F3 | SCV/flash | SCV corrupt or erased | reset during flash write, wear | state lost | phase/counters reset | magic + CRC16 check (exists) | — | reinit defaults; log the event | any | 2 | L1 |
| F4 | FSW time | `mission_elapsed_ms` resets on reboot | boot-relative tick stored as mission time (fdir.c:183) | wrong mission time after any reset | phase logic errors if time-dependent | code review finding — fix, not monitor | — | boot-offset reconstruction from SCV | any | 2 | — |

## 4. Derived monitor specification

Every monitor = one row in the table-driven `FDIR_Update()` loop, except the
escalation rules which stay explicit code. Recovery = bit in `reinit_requests`,
consumed and acknowledged by the owning subsystem's own `_Update()`/
`_Service()` — the one uniform pattern used for GPS/IMU/BARO reinit, I2C bus
restart, and LoRa recovery alike.

| Monitor | FMECA | Signal | Debounce | Fault bit | Recovery request | Executor | Escalation |
|---|---|---|---|---|---|---|---|
| GPS no-data | C2 | I2C response absent | 30 cycles | EQUIPMENT_GPS | GPS protocol reinit (new) | CDH | part of C7 bus escalation |
| GPS no-fix | C1 | fix_type with data present | 30 cycles | (flag only, distinct from no-data) | none — environmental | — | none |
| IMU timeout | C3 | `imu_valid` | 3 cycles | EQUIPMENT_IMU | IMU reinit, 10 s cooldown | CDH | with baro fault → bus restart |
| IMU plausibility | C4 | accel magnitude range / stuck values | 3 cycles | EQUIPMENT_IMU | IMU reinit | CDH | FSM ignores IMU while flagged |
| Baro timeout | C5 | `baro_valid` | 3 cycles | EQUIPMENT_BARO | baro reinit, 10 s cooldown | CDH | with IMU fault → bus restart |
| Baro cross-check | C6 | GPS vs baro altitude disagreement | 5 cycles | EQUIPMENT_BARO | baro reinit | CDH | FSM prefers GPS altitude |
| I2C bus | C7 | IMU ∧ baro faulted | — | — | bus restart, 10 s cooldown | CDH | affects GPS too; SPI buses unaffected |
| Battery ADC | C8 | `batt_valid` | 3 cycles (new — currently 1) | EQUIPMENT_EPS_ADC | internal ADC reinit (cheap, new) | CDH | none — low risk |
| Coral timeout | P1 | `coral_valid` | 5 cycles | EQUIPMENT_CORAL | **none** (RX-only) — flag for telemetry/log | — | none |
| SD failures | S1 | consecutive-failure count from sd_logger | 3 failures | EQUIPMENT_SD | remount request | SD logger | rotate file; never auto-format |
| LoRa TX | T1 | TTC_FDIR_GetHealth() consecutive/lifetime fault counters | 5 consecutive, or ≥60%/last 20 (recovery); ≥90%/last 10 (fault bit) | EQUIPMENT_LORA | re-run LoRa_Init via TTC_FDIR_RequestRecovery() (RST pulse), 10 s cooldown | TTC | none |
| CDH freshness | C10 | datapool `timestamp_ms` stops advancing | 2 cycles | (new CDH bit) | bus restart | CDH | loop hung → IWDG |
| Watchdog escalation | F2 | `watchdog_reset_count` | ≥ 3 at boot | — | — | FSW | enter reduced mode - stop updating particular subsystems |

## 5. Verification matrix (fault injection)

One test per monitor. Run on the bench before flight; record actual FDIR reaction.

| Test | Injection | Expected reaction | Status |
|---|---|---|---|
| GPS no-fix | shield/disconnect antenna only | no-fix flagged, no reinit spam, telemetry continues | ☐ |
| GPS no-data | disconnect GPS from I2C | GPS fault after 30 cycles, protocol reinit requests | ☐ |
| IMU loss | disconnect IMU from I2C | fault after 3 s, reinit requests every 10 s, clears on reconnect | ☐ |
| IMU garbage | inject stuck values (test hook) | plausibility flag, FSM ignores IMU | ☐ |
| Baro loss | disconnect baro from I2C | fault after 3 s, reinit requests every 10 s | ☐ |
| Baro drift | offset baro reading (test hook) | cross-check flags baro, FSM uses GPS altitude | ☐ |
| Bus wedge | short SDA to GND during operation | GPS+IMU+baro all faulted → bus restart executes; LoRa TX and SD logging continue throughout | ☐ |
| Coral loss | disconnect Coral RX line | CORAL fault after 5 cycles, no recovery attempted, core loop unaffected | ☐ |
| ADC fault | misconfigure/disable ADC (test hook) | EPS_ADC fault after debounce, telemetry marks battery invalid | ☐ |
| SD failure | remove card mid-run | remount attempts, telemetry unaffected, logging resumes on reinsert | ☐ |
| SD full | fill card to capacity (optional — accepted risk) | writes fail with SD fault set, telemetry unaffected | ☐ |
| LoRa failure | disconnect radio SPI | LORA fault, periodic LoRa_Init retries (RST pulse), recovery on reconnect | ☐ |
| LoRa boot failure | hold radio in reset at boot | TTC starts degraded, retries init, recovers when released | ☐ |
| Loop hang | debug-halt or induced infinite loop | IWDG resets MCU, SCV restores phase, watchdog_reset_count increments | ☐ |
| Boot loop | force repeated watchdog resets | staged disabling: SD off after 3 resets, then CDH, TTC, FSW on further resets; FDIR + watchdog kick alive at every stage | ☐ |
| SCV corruption | erase SCV flash page | defaults reinitialised, boot continues, event logged | ☐ |
