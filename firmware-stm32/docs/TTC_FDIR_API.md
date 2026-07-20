# TTC → FDIR functionality reference

Describes the functionality `ttc.c` exposes to FDIR, as of the merge with
current `main` (branch `ttc-mainsync`) plus FDIR's subsequent integration.
This supersedes the "TTC owner" section of `docs/FDIR_INTEGRATION.md`, which
documented the older synchronous `TTC_ConsecutiveTxFailures()`/
`TTC_SetLoRaFault()` contract this branch replaced. All declarations are in
`Core/Inc/ttc/ttc.h`.

**Status: wired up.** `fdir.c` polls `TTC_FDIR_GetHealth()` every FDIR cycle,
derives its own thresholds/failure-rate windows, and requests recovery via the
shared reinit-request bitmask (`FDIR_GetReinitRequests()`/
`FDIR_AcknowledgeReinit()`, `EQUIPMENT_LORA`) rather than calling
`TTC_FDIR_RequestRecovery()` directly — see "Recovery trigger: the reinit
bitmask" below for why.

## Design boundary

TTC never writes `g_scv`, never sets `EQUIPMENT_LORA`, and applies no
autonomous fault policy (no thresholds, no retry-limit-triggered isolation,
no cooldowns). It only reports raw observations and executes explicitly
requested actions. All policy — when to isolate, when to recover, what counts
as "faulted enough" — belongs to FDIR. This mirrors the CDH/FDIR ownership
split already used elsewhere in the firmware.

## Reading radio health: `TTC_FDIR_GetHealth()`

```c
void TTC_FDIR_GetHealth(TTC_FDIR_Health_t *health);
```

Fills a snapshot FDIR should poll every cycle:

| Field | Meaning |
|---|---|
| `radio_ready` | 1 when the driver reports `LoRa_IsReady()` and TTC's own init/recovery bookkeeping agrees. |
| `rx_active` | 1 when the modem is in continuous-RX mode (`LoRa_IsRxActive()`). |
| `consecutive_tx_failures` | Count of back-to-back failed telemetry sends; reset to 0 on the next successful TX. Saturates at `UINT8_MAX`, does not wrap. |
| `recovery_in_progress` | 1 while a `RECOVERY` or `RETURN_TO_SERVICE` action is `IN_PROGRESS`. |
| `isolation_active` | 1 while the modem is held in reset by `LoRa_Isolate()` (`LoRa_IsIsolated()`). |
| `lora_tx_fault_counter` | Lifetime (not consecutive) count of TX failures since boot. `uint16_t`, saturates. |
| `nack_counter` | Declared in the struct; not currently incremented anywhere in `ttc.c` — reads 0 always on this branch. |
| `last_tx_success_ms` | `HAL_GetTick()` value of the most recent successful transmit. |
| `last_rx_success_ms` | `HAL_GetTick()` value of the most recent CRC-valid received packet. |

These are the raw counters FDIR thresholds into recovery/fault decisions (see
`fdir.c`'s LoRa block, FMECA T1): recovery trips on
`consecutive_tx_failures >= FDIR_LORA_TX_FAILURE_LIMIT` (5) or a failure rate
of at least 60% over the last 20 TX attempts; `EQUIPMENT_LORA` is set once at
least 90% of the last 10 attempts failed, cleared once that ratio drops.
`lora_tx_fault_counter` is also mirrored into a new persisted
`SCV_t.lora_tx_fault_counter` field (flash/SD-log only, not yet in the
telemetry packet — same deferred-v4 treatment as `lora_timeout_count`). TTC
itself derives no attempt history; FDIR reconstructs a pass/fail window by
edge-detecting changes in `lora_tx_fault_counter` and `last_tx_success_ms`
each cycle (exactly one changes per real TX attempt).

## Recovery trigger: the reinit bitmask, not a direct call

`fdir.c` does not call `TTC_FDIR_RequestRecovery()` (or any `Request*()`)
directly. Instead, once its policy trips, it sets `EQUIPMENT_LORA` in the same
`FDIR_GetReinitRequests()` bitmask used for GPS/IMU/BARO reinit and I2C bus
restart. `TTC_Service()` polls that bit at the top of every call: if set, it
calls `TTC_FDIR_RequestRecovery()` on itself and immediately calls
`FDIR_AcknowledgeReinit(EQUIPMENT_LORA)` — fire-and-forget, ignoring the
returned `TTC_FDIR_Result_t`. This keeps one uniform trigger mechanism across
every FDIR-requested recovery, at the cost of `ttc.c` now depending on
`fdir.h` (previously a one-directional FDIR → TTC dependency). The full
`Request*()`/`GetActionStatus()` API below is otherwise unchanged and remains
directly callable — the C test harness (`test_ttc_state_machine.c`) still
exercises it that way, and `TTC_FDIR_RequestIsolation()`/
`RequestReturnToService()` remain available, unused, for a future give-up
escalation (see the TODO in `fdir.c`'s LoRa block).

## Requesting actions

All four request calls are **asynchronous**: a return of `ACCEPTED` only means
the action was queued, not that it finished. `TTC_Service()` (called every
superloop tick from `main.c`) advances it; a caller polling directly (as the
test harness does) would poll `TTC_FDIR_GetActionStatus()` to see it complete.
FDIR itself does not poll action status for the LoRa recovery path above — it
is fire-and-forget.

```c
typedef enum {
    TTC_FDIR_RESULT_ACCEPTED = 0,
    TTC_FDIR_RESULT_ALREADY_COMPLETE,
    TTC_FDIR_RESULT_IN_PROGRESS,
    TTC_FDIR_RESULT_REJECTED
} TTC_FDIR_Result_t;
```

| Call | Effect | `ALREADY_COMPLETE` when... | `REJECTED` when... |
|---|---|---|---|
| `TTC_FDIR_RequestIsolation()` | Holds the modem in reset (`LoRa_Isolate()`), stops all TTC radio activity. Preempts any other in-flight TTC action. | modem is already isolated | never (isolation always accepted unless already done) |
| `TTC_FDIR_RequestRecovery()` | Full re-init: reset → reconfigure → re-enter RX. The only path by which TTC attempts radio recovery after a fault. | radio already ready, not faulted, not isolated, and RX active | never explicitly, but rejected implicitly if another action is already in progress on a *different* action type (see below) |
| `TTC_FDIR_RequestRxRestart()` | Re-enters RX-continuous mode without a full reset; for an otherwise-healthy radio that dropped out of RX. | — | radio isolated, not ready, or faulted |
| `TTC_FDIR_RequestReturnToService()` | Reinitializes an isolated or faulted radio; idempotent if RX is already active. | radio already ready, not faulted, not isolated, RX active | never explicitly |

**Action-in-flight rule** (`TTC_RequestAction`, ttc.c:769): if an action is
already `PENDING` or `IN_PROGRESS`, calling the *same* action again returns
`IN_PROGRESS`; calling a *different* action returns `REJECTED`. Only one
action can be outstanding at a time, except isolation, which always preempts.

## Polling action completion: `TTC_FDIR_GetActionStatus()`

```c
typedef struct {
    TTC_FDIR_Action_t action;             /* which action this status is for */
    TTC_FDIR_ActionState_t state;         /* IDLE / PENDING / IN_PROGRESS / SUCCEEDED / FAILED */
    uint8_t last_lora_status;             /* LoRaStatus_t of the last driver call, byte-sized */
} TTC_FDIR_ActionStatus_t;

TTC_FDIR_ActionStatus_t TTC_FDIR_GetActionStatus(void);
```

FDIR should poll this after issuing a request until `state` is
`TTC_FDIR_ACTION_SUCCEEDED` or `TTC_FDIR_ACTION_FAILED`. On failure,
`last_lora_status` carries the `LoRaStatus_t` (e.g. `LORA_TIMEOUT`,
`LORA_SPI_ERROR`) from the underlying driver call that failed, for
diagnostics/telemetry.

## Everything else TTC exposes (not FDIR-specific, unchanged contract)

- `TTC_GetHealth()` / `TTC_GetUplinkState()` — the same `LoRaHealth_t` /
  `UplinkState_t` snapshots compacted into protocol-v8 telemetry by
  `TTC_BuildTelemetryPacket()` (`ttc_telemetry.c`), an internal read within
  TTC, not a cross-module one; useful for ground-side diagnostics but not
  part of the FDIR request/response loop above.
- `TTC_RequestTelemetry()` — queues an out-of-cycle telemetry send; called by
  `FSW_SetPhase()` on every flight-phase change. Not FDIR-related.

## Integration status

Closed:

1. `fdir.c` calls `TTC_FDIR_GetHealth()` every FDIR cycle.
2. `SCV_t.lora_timeout_count` (consecutive) and the new
   `SCV_t.lora_tx_fault_counter` (lifetime) are derived and owned by FDIR from
   the raw snapshot. `nack_counter` is not currently consumed by FDIR (it
   also always reads 0 on this branch — see the health table above).
3. `EQUIPMENT_LORA` is set/cleared by FDIR using its own thresholds
   (`FDIR_LORA_FAULT_WINDOW`/`_FAILS` in `fdir.h`) — previously this bit was
   never actually set anywhere.
4. Recovery thresholds/cooldown are FDIR-owned
   (`FDIR_LORA_TX_FAILURE_LIMIT`, `FDIR_LORA_RECOVERY_WINDOW`/`_FAILS`,
   `FDIR_REINIT_PERIOD_MS`); TTC still implements none of this policy itself.
5. The trigger is the reinit bitmask (see above), not a direct
   `TTC_FDIR_Request*()` call, and is fire-and-forget rather than polled to
   completion.
6. `TTC_Service()` is already called unconditionally every superloop
   iteration in `main.c` (no period gate), satisfying this.

Open:

- Isolation / return-to-service give-up policy (escalate after N recovery
  attempts with no lasting improvement) — TODO in `fdir.c`'s LoRa block, needs
  its own persisted retry count and re-arm rule.

Until (1)–(5) land in `fdir.c`, a radio that fails every transmit for the
whole flight will not be detected or acted on by FDIR.
