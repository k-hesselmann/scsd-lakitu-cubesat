# TTC / FDIR integration contract

TTC exposes raw radio observations through `TTC_FDIR_GetHealth()`. It never
writes `g_scv`, `SCV_t`, or `EQUIPMENT_LORA`; FDIR remains the owner of those
policy decisions.

The action requests are intentionally asynchronous. A successful request only
means it was queued. FDIR should poll `TTC_FDIR_GetActionStatus()` until the
state is `TTC_FDIR_ACTION_SUCCEEDED` or `TTC_FDIR_ACTION_FAILED`.

- `TTC_FDIR_RequestIsolation()` holds the modem reset low and stops TTC radio
  activity. Repeating it after isolation reports `ALREADY_COMPLETE`.
- `TTC_FDIR_RequestRecovery()` performs a fresh reset/configuration/RX startup.
  It is the only way TTC attempts radio recovery after a radio fault.
- `TTC_FDIR_RequestRxRestart()` re-enters RX-continuous mode while an otherwise
  healthy radio remains in service.
- `TTC_FDIR_RequestReturnToService()` reinitializes an isolated or faulted radio
  and is idempotent when RX is already active.

FDIR integration TODOs:

1. Call `TTC_FDIR_GetHealth()` every FDIR cycle.
2. Derive and own the SCV LoRa fault/NACK counters from the raw TTC snapshot.
3. Set or clear `EQUIPMENT_LORA` using FDIR-owned thresholds and hysteresis.
4. Decide isolation, recovery, RX-restart thresholds, retry limits, and any
   recovery cooldown; TTC deliberately applies none of these policies.
5. Invoke the appropriate TTC request API and monitor its action status.
6. Have the main-loop owner call `TTC_Service()` far more often than the
   current one-second superloop cadence. All modem work is non-blocking, but
   every queued SPI/register action advances on a service call.

The TTC source-contract test is runnable with the host Python interpreter:

```text
python tests/ttc/test_ttc_source_contract.py
```

The TTC-owned C harnesses `test_ttc_state_machine.c` and
`test_lora_driver_state_machine.c` mock `HAL_GetTick()`, SPI completion/abort,
and the LoRa/TTC boundary. They cover startup recovery, unsolicited RX faults,
ACK retry/NACK counting, action idempotency, oversized RX observations, and
SPI-abort rejection/timeout. They can be built by an STM32 test target or a
host C runner with the STM32 HAL headers available.
