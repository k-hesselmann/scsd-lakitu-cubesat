# TTC → FDIR functionality reference

Describes the functionality `ttc.c` exposes to FDIR on the `ttc` branch, as of
the merge with current `main` (branch `ttc-mainsync`). This supersedes the
"TTC owner" section of `docs/FDIR_INTEGRATION.md`, which still documents the
older synchronous `TTC_ConsecutiveTxFailures()`/`TTC_SetLoRaFault()` contract
that this branch replaces. All declarations are in `Core/Inc/ttc/ttc.h`.

**Status: built but not wired up.** `Core/Src/fdir/fdir.c` still calls the old
`TTC_ConsecutiveTxFailures()` hook, which `ttc.c` no longer defines — the weak
stub in `fdir_hooks.c` silently returns 0, so today none of the functionality
below is actually invoked by FDIR. See "Integration gap" at the bottom.

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

These are the raw counters FDIR is expected to threshold and turn into
`EQUIPMENT_LORA` set/clear decisions, per the "FDIR integration TODOs" in
`Core/Inc/ttc/FDIR_INTEGRATION.md`.

## Requesting actions

All four request calls are **asynchronous**: a return of `ACCEPTED` only means
the action was queued, not that it finished. `TTC_Service()` (called every
superloop tick from `main.c`) advances it; FDIR must poll
`TTC_FDIR_GetActionStatus()` to see it complete.

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
  `UplinkState_t` snapshots that get copied verbatim into the v7 telemetry
  packet by `FSW_BuildTelemetryPacket()`; useful for ground-side diagnostics
  but not part of the FDIR request/response loop above.
- `TTC_RequestTelemetry()` — queues an out-of-cycle telemetry send; called by
  `FSW_SetPhase()` on every flight-phase change. Not FDIR-related.

## Integration gap (as of this merge)

`Core/Src/fdir/fdir.c` was not touched by this branch. It still calls the
pre-existing `TTC_ConsecutiveTxFailures()` hook (declared in
`Core/Inc/fdir/fdir_hooks.h`), which `ttc.c` no longer implements — the weak
stub in `fdir_hooks.c` returns 0 unconditionally, so the LoRa monitor in
`fdir.c` is permanently inert. None of the API above is called from anywhere
in the firmware yet. Per `Core/Inc/ttc/FDIR_INTEGRATION.md`, the outstanding
work for the FDIR owner is:

1. Call `TTC_FDIR_GetHealth()` every FDIR cycle instead of
   `TTC_ConsecutiveTxFailures()`.
2. Derive and own SCV LoRa fault/NACK counters from the raw snapshot.
3. Set/clear `EQUIPMENT_LORA` using FDIR-owned thresholds and hysteresis.
4. Decide isolation / recovery / RX-restart thresholds, retry limits, and any
   recovery cooldown — TTC deliberately implements none of this policy.
5. Invoke the appropriate `TTC_FDIR_Request*()` call and monitor its action
   status to completion.
6. Have the main-loop owner call `TTC_Service()` more often than the current
   superloop cadence, since every queued SPI/register step only advances one
   step per call.

Until (1)–(5) land in `fdir.c`, a radio that fails every transmit for the
whole flight will not be detected or acted on by FDIR.
