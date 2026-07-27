# Runtime observability

USART2 debug output is timestamped, queued, and sent with interrupts. Flight
work never waits for terminal text. The timestamp is captured when the message
is enqueued, not later when the UART sends it. If the terminal cannot keep up,
complete debug messages are dropped and the drop totals are exposed in
`[SYS_STAT]`.

Every non-empty terminal line starts with the monotonic milliseconds since
boot. The value resets on reboot and its 32-bit counter wraps after about
49.7 days:

```text
[t=0000018423ms] [VALIDATION] GPS no fix
```

All non-empty lines in a multi-line diagnostic receive the same enqueue
timestamp. Intentional blank separator lines remain blank.

Every 10 seconds the terminal reports a line like:

```text
[t=0000020000ms] [SYS_STAT] t=20000 win=10000 loop_max=2 dbg_q=0 dbg_high=740 dbg_drop=0/0 dbg_start_err=0 coral_q=0 coral_high=512 coral_ovf=0(+0) coral_ok=2 coral_to=0 coral_crc=0 gps_bytes=1000 gps_nav=10 gps_msg_age=250 gps_3d_age=4294967295 fix=0 sv=0
```

Interpretation:

- `loop_max`: largest superloop-to-superloop gap in the window. Unexpected
  values above the normal 100 ms CDH slot identify blocking work.
- `dbg_q`, `dbg_high`: current and lifetime maximum debug queue occupancy.
- `dbg_drop=messages/bytes`: debug output discarded to protect flight timing.
  It should remain `0/0`; a nonzero value means logs are too verbose.
- `dbg_start_err`: failed attempts to start an interrupt-driven USART2 send.
- `coral_q`, `coral_high`: current and lifetime maximum Coral RX ring use.
  The ring capacity is 16383 bytes; a high-water value near that limit means
  the consumer is not keeping up.
- `coral_ovf=total(+window_delta)`: bytes lost because the Coral ring was full.
  Both values should remain zero.
- `gps_bytes`, `gps_nav`: cumulative I2C stream bytes and valid NAV-PVT frames.
- `gps_msg_age`: milliseconds since the last valid NAV-PVT frame. With healthy
  communication at 1 Hz this stays near 0-2000 even with no satellite fix.
- `gps_3d_age`: milliseconds since the last NAV-PVT frame with `fix=3`.
- `fix`, `sv`: current fix type and satellites from the datapool.

An age of `4294967295` means that event has never been observed since boot.

Repeated validation faults and Coral overflow warnings are limited to one
message per fault class per 10 seconds. GPS runtime output is one compact
`[GPS_PVT]` line per 10 seconds. Define `M10S_VERBOSE_RUNTIME_LOGS=1` only for
a focused bench build when the full GPS parser trace is needed.
