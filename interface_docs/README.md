# OBC-Payload Interface

Cross-target interface definitions — the contract between the Coral payload and
the STM32 OBC. These docs are authoritative for both sides so the framing cannot
drift between implementations.

- [UART_PROTOCOL.md](UART_PROTOCOL.md) — byte-level wire spec (packets, CRC, commands).
- [OBC_INTEGRATION.md](OBC_INTEGRATION.md) — payload ↔ OBC handoff: guarantees,
  asks on the OBC, and open decisions for both teams.
