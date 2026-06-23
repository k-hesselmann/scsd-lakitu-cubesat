# Cloud Payload — Deferred Work

Tracking items intentionally left out of the PoC. None block the proof of
concept; all matter before an actual flight build.

## 1. Strip debug tooling before flight
Everything marked `DEBUG ONLY — remove before flight` in `cloud_regressor.cc`:
- The HTTP/RPC endpoints: `get_last_image`, `get_burst_count`, `get_burst_frame`.
- The button-hold burst-capture path and the `kBurstRingSize` RAM ring (~400 KB).
- `cloud_regressor_client.py` (host-side) goes with them.

Caveat: `UseHttpServer(new JsonRpcHttpServer)` also initializes the USB CDC
console. If you drop it, confirm USB/serial still comes up — or keep the server
but unexport the RPC methods. Decide whether any in-flight diagnostics endpoint
is worth keeping behind a build flag.

## 2. Thermal & power for vacuum (hardware/systems, not firmware)
- Edge TPU runs at `PerformanceMode::kHigh` (~3 W peak); board gets hot and there
  is no convective cooling in orbit. Needs a thermal path / duty-cycle analysis.
  Dropping to `kMedium`/`kLow` in `Main()` is the firmware lever if needed.
- Supply must absorb the TPU inference current spikes (datasheet: 5 V / 2 A).
  Size the OBC's rail to the Coral accordingly.

## 3. Fuller OBC command handler
`OBCCommandTask` currently handles only `TRIGGER` (0x10) and `SET_INTERVAL`
(0x11). Candidates to add, with CRC + ACK/NAK like the existing ones, and
documented in `shared/UART_PROTOCOL.md` (opcodes reserved there in §4.3):
- **GET_STATUS / health** — uptime, current SEQ, last cloud %, reset count
  (`ResetGetStats()`), TPU/camera OK flags. Lets the OBC poll liveness.
- **REQUEST_FRAME(seq)** — re-send a specific image the OBC missed (needs a
  small on-board retained-frame cache, or only "last frame").
- **SOFT_RESET** — commanded reboot (`ResetToFlash()`).
- **SET_EXPOSURE / camera params** — if motion-blur tuning needs it in flight.
- Define behavior for unknown/garbage commands beyond the current single-NAK
  (e.g. resync window) so line noise can't wedge the parser.

## Notes / smaller follow-ups
- **SEQ flash wear:** SEQ is persisted to `/cloud_seq` on every capture
  (`SaveSeq`). At a 10 s cadence that's a lot of NAND writes over a mission.
  Options: persist every N captures, or treat the OBC's RTC timestamp as the
  authoritative key and persist SEQ only occasionally.
- **Runtime watchdog:** init failures now reboot via `FatalRestart`, but a hang
  *inside* the main loop (e.g. a wedged peripheral) is not yet covered. Consider
  a hardware watchdog (RTWDOG) refreshed from the main loop for full coverage.
