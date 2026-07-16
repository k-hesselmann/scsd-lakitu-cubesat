# FDIR Draft

> **Superseded:** fault policy, monitors, and verification now live in
> [FMECA.md](FMECA.md). This draft is kept for the original design rationale
> (watchdog sketch, mission time sketch, open items).

This document sketches the Failure Detection, Isolation and Recovery subsystem.
It is intentionally not integrated into CDH, FSW, TT&C, or the shared datapool yet.
Those subsystem interfaces should be updated by their owners before this becomes
compiled flight software.

## Scope

FDIR owns system health policy:

- decide when invalid data becomes a fault,
- request recovery actions from subsystem owners,
- select degraded operation when recovery has not yet succeeded,
- track watchdog-reset escalation policy,
- expose enough health state for telemetry and post-flight logs.

FDIR should not own hardware access. Reinitialising GPS, IMU, barometer, Coral,
SD, LoRa, or EPS ADC remains the responsibility of the subsystem that owns the
equipment handler.

## Proposed Fault Thresholds

```c
#define GPS_TIMEOUT_LIMIT      30U  /* 30 s */
#define IMU_TIMEOUT_LIMIT      3U   /* 3 s */
#define BARO_TIMEOUT_LIMIT     3U
#define CORAL_TIMEOUT_LIMIT    5U
#define SD_FAULT_LIMIT         3U
#define WATCHDOG_RESET_LIMIT   3U
```

One invalid sample should not make equipment unrecoverable. The intended policy
is:

```text
valid data missing for N cycles
 -> FDIR marks equipment faulty
 -> FDIR requests subsystem reinitialisation
 -> FDIR keeps requesting reinitialisation periodically
 -> fault clears automatically once valid data returns
```

Use a cooldown between recovery requests, for example:

```c
#define FDIR_REINIT_PERIOD_MS  10000U
```

This avoids hammering a failing bus or peripheral every 1 Hz cycle.

## Proposed Equipment Bits

These bits are a proposal for the future SCV/datapool interface. They are not
currently added to `datapool.h`.

```c
#define FDIR_EQ_GPS      (1U << 0)
#define FDIR_EQ_IMU      (1U << 1)
#define FDIR_EQ_BARO     (1U << 2)
#define FDIR_EQ_CORAL    (1U << 3)
#define FDIR_EQ_SD       (1U << 4)
#define FDIR_EQ_LORA     (1U << 5)
#define FDIR_EQ_EPS_ADC  (1U << 6)
#define FDIR_EQ_CDH      (1U << 15)
```

The future SCV should distinguish:

- `equipment_enabled`: equipment may be used and recovery may be attempted,
- `equipment_faults`: equipment is currently not trusted,
- `reinit_requests`: FDIR requests that subsystem owner perform reinit.

## Future FDIR API

The integration API could look like this:

```c
void FDIR_Init(void);
void FDIR_Update(void);

uint16_t FDIR_GetReinitRequests(void);
void FDIR_AcknowledgeReinitRequest(uint16_t equipment_mask);

uint16_t FDIR_GetEquipmentFaults(void);
uint8_t FDIR_SystemHealthyEnoughToKickWatchdog(void);
uint32_t FDIR_GetMissionElapsedMs(void);
```

Subsystem-side ownership should look like:

```text
FDIR detects timeout/fault trend
FDIR sets a reinit request bit
CDH/other subsystem consumes the request
CDH/other subsystem performs hardware reinitialisation
subsystem reports valid/invalid data in its normal interface
FDIR clears the fault once valid data returns
```

## Proposed Recovery Behaviour

| Equipment | Detection | FDIR reaction | Recovery owner |
|---|---|---|---|
| GPS | no valid fix/data for 30 cycles | fault GPS, use baro fallback if valid, request GPS reinit periodically | CDH |
| IMU | invalid for 3 cycles | fault IMU, block IMU-dependent phase decisions, request reinit periodically | CDH |
| Barometer | invalid for 3 cycles | fault baro, use GPS altitude if valid, request reinit periodically | CDH |
| Coral | no block for 5 cycles | fault Coral, continue core mission, request reinit periodically | Payload/CDH interface owner |
| SD | 3 write/mount failures | fault SD, continue telemetry, request remount/reinit periodically | CDH/storage owner |
| Watchdog | 3 watchdog resets | reduced mode recommendation | FSW/kernel owner |

GPS no-fix should be treated as expected environmental degradation, not permanent
hardware failure.

## Watchdog Draft

The watchdog should be the STM32 independent watchdog once integrated.

Boot behaviour:

```text
read reset flags
if reset reason was watchdog:
    increment watchdog_reset_count in SCV
clear reset flags
if watchdog_reset_count >= WATCHDOG_RESET_LIMIT:
    enter reduced mode
```

Runtime behaviour:

```text
run 1 Hz superloop
CDH update
FDIR update
FSW update
telemetry/log scheduling
kick watchdog only after critical tasks complete
```

Do not kick the watchdog at the top of the loop. Kicking it at the end means a
blocked CDH transaction, failed scheduler path, or stuck FSW task can be recovered
by reset.

Future STM32 sketch:

```c
if (FDIR_SystemHealthyEnoughToKickWatchdog()) {
    HAL_IWDG_Refresh(&hiwdg);
}
```

`WATCHDOG_RESET_LIMIT` should not permanently brick the system. It should select
a reduced mode, for example:

- disable Coral/payload operations,
- avoid aggressive SD writes,
- keep CDH, FDIR, FSM, and telemetry alive,
- keep attempting low-risk recovery with cooldowns.

## Mission Elapsed Time Draft

The current datapool `timestamp_ms` is boot-relative and owned by CDH. That is
fine. Persistent mission time can be reconstructed separately by FDIR/FSW once
the SCV is finalised.

Proposed behaviour:

```c
static uint32_t mission_elapsed_boot_offset_ms;

void MissionTime_Init(const SCV_t *scv)
{
    if (scv->flight_phase != 0U) {
        mission_elapsed_boot_offset_ms = scv->mission_elapsed_ms;
    } else {
        mission_elapsed_boot_offset_ms = 0U;
    }
}

uint32_t MissionTime_Now(void)
{
    return mission_elapsed_boot_offset_ms + HAL_GetTick();
}
```

The SCV should not be written to flash every second unless wear levelling exists.
Write mission time on phase transitions, fault transitions, and a slower periodic
interval such as 30-60 seconds.

## Datapool Freshness Detection

FDIR can detect datapool update failure if CDH later adds one of these fields:

```c
uint32_t cdh_update_counter;
```

or:

```c
uint32_t cdh_heartbeat_ms;
```

FDIR logic:

```text
if heartbeat/counter does not advance for 2 cycles:
    mark CDH/data-freshness fault
```

Recovery depends on the failure mode:

- if only one equipment value is stale, request that equipment reinit,
- if CDH still runs but several sensors are stale, request bus-level recovery,
- if CDH blocks and FDIR also stops running, the watchdog is the recovery.

For this reason, all future bus transactions should use bounded HAL timeouts.

## Open Integration Items

- Final SCV field names and ownership.
- Final datapool freshness field, if CDH owner accepts it.
- Exact reinit request transport: SCV bit, datapool field, service call, or queue.
- Reset reason storage and watchdog counter ownership.
- Telemetry/log fields for `equipment_faults`, `reset_reason`, and recovery events.
- Reduced-mode behaviour owned by FSW/kernel.
