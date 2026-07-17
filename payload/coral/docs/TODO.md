# Cloud Payload — Deferred Work

Tracking items intentionally left out of the PoC. None block the proof of
concept; all matter before an actual flight build.

## 1. Strip debug tooling before flight — DONE (behind `CLOUD_DEBUG`)
Resolved via a compile-time flag rather than deletion, so the tooling stays
available for bench work:
- The RPC endpoints (`get_last_image`, `get_burst_count`, `get_burst_frame`),
  the button-hold burst path, and the `kBurstRingSize` RAM ring (~400 KB) are
  now wrapped in `#if CLOUD_DEBUG`. Default build defines it to 0, so all of it
  compiles out; `cmake -DCLOUD_DEBUG=ON` restores it for `cloud_regressor_client.py`.
- `UseHttpServer(new JsonRpcHttpServer)` is kept **unconditionally** (it also
  enumerates the USB CDC console printf relies on); flight builds just export no
  RPC methods.

Follow-up (optional): verify USB serial still enumerates *without* the HTTP
server. If it does, the flight build could drop `UseHttpServer` entirely instead
of running a no-method server.

## 2. Thermal & power for vacuum (hardware/systems, not firmware)
- Edge TPU runs at `PerformanceMode::kHigh` (~3 W peak); board gets hot and there
  is no convective cooling in orbit. Needs a thermal path / duty-cycle analysis.
  Dropping to `kMedium`/`kLow` in `Main()` is the firmware lever if needed.
- Supply must absorb the TPU inference current spikes (datasheet: 5 V / 2 A).
  Size the OBC's rail to the Coral accordingly.

## 3. Fuller OBC command handler
`OBCCommandTask` currently handles only `TRIGGER` (0x10) and `SET_INTERVAL`
(0x11). Candidates to add, with CRC + ACK/NAK like the existing ones, and
documented in `interface_docs/UART_PROTOCOL.md` (opcodes reserved there in §4.3):
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
