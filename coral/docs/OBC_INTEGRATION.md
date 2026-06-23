# NOT FINAL, to be edited: OBC Integration — Payload ↔ CDH/OBC Handoff

**Mission:** high-altitude balloon, cloud-cover estimator payload
**Payload:** Coral Dev Micro (this repo) → **OBC:** Nucleo **STM32L476RG** (M4 @ 80 MHz, 128 KB SRAM, low-power; SD card on OBC)
**Audience:** CDH / OBC team leads
**Status:** interface contract for review — items in §3/§4 are *asks on the OBC*, not payload implementation.

This document does **not** tell the OBC team how to build their firmware. It states
what the payload guarantees on the wire, what it assumes the OBC provides, and the
open decisions that need both teams to agree. The byte-level protocol is the
authoritative spec in [UART_PROTOCOL.md](UART_PROTOCOL.md); this is the integration layer.

> **OBC note:** the OBC is an **L476RG** (M4 @ 80 MHz, **128 KB** SRAM, ultra-low-power line;
> SD is most likely over **SPI** — the Nucleo-64 has no native SD slot). This is a much
> tighter machine than a performance MCU, which drives the §3/§4 choices: you **cannot buffer
> a whole image in RAM** (a 50 KB frame is 39 % of SRAM), and a high-baud burst is *harder*
> to absorb, not easier. Favour the slow link + streaming writes.

---

## 1. What the payload guarantees (the contract)

- **Link:** UART6, **115200 8N1**, no hardware flow control. TX = Coral→OBC, RX = OBC→Coral.
- **Cadence:** one inference every N seconds (default 10 000 ms, min recommended 1 000 ms,
  settable by the OBC via `SET_INTERVAL`).
- **Per inference, the payload sends one Image Packet** (UART_PROTOCOL.md §3):
  SOF `0xAA55`, TYPE, SEQ (uint32), LEN, FRAC, WIDTH, HEIGHT, FORMAT, raw pixels,
  **CRC-16/CCITT-FALSE** over TYPE…last-pixel.
- **Current frame:** 224×224 Y8 grayscale → **50 196 bytes**, ~**4.4 s** TX at 115200.
- **Fire-and-forget:** the payload does **not** wait for the OBC and cannot be blocked by it.
  A busy OBC never stalls the payload; at worst the OBC drops that frame.
- **SEQ is the authoritative key** linking a downlink telemetry row to its SD image.

## 2. Payload non-goals (explicitly the OBC's job, or nobody's)

- **No RTC on the Coral** — the OBC timestamps each packet from its own clock. SEQ, not
  time, is the pairing key (timestamps can collide; SEQ cannot).
- **Losing the SD card loses *images*, not *telemetry*.** Cloud-cover %, SEQ and time go out
  on the downlink independently of the SD archive. The image archive is a *training-data hedge*
  and is **non-essential to flight or science**. Treat the SD path as droppable.
- The payload does no compression, retransmission, or buffering on behalf of the OBC.

## 3. What the payload assumes the OBC provides (asks — please accept or push back)

Tailored to the L476RG's 128 KB SRAM and 80 MHz M4:

1. **DMA RX + USART IDLE-line detection**, not per-byte interrupts. At 80 MHz a 50 KB frame as
   byte ISRs (~50k interrupts/image) will dominate the CPU.
2. **Stream UART → SD; do not buffer whole frames.** A frame won't fit comfortably in RAM
   (50 KB ≈ 39 % of SRAM; a native 324² frame would be ~82 %). Use a small DMA double-buffer /
   ring (e.g. 8–16 KB) and flush blocks to the card as they arrive.
3. **SD writes isolated in a lower-priority task**, fed from the ring buffer — never on the
   RX path or a flight-critical loop. SDMMC1 if wired, otherwise SPI SD.
4. **Single-owner FATFS** (or `_FS_REENTRANT` + mutex); never `f_write` from an ISR.
5. **One file per image** (already the naming convention) so corruption blast radius = one frame;
   **`f_sync`/close after each frame**, don't hold a file open across many.
6. **Power-loss handling:** brown-out detection that finishes/aborts the current write cleanly.
   FAT has no journaling — consider a power-loss-resilient FS if feasible. Use a card with
   power-loss protection.
7. **Industrial-temp (−40 °C) SD card** and a **retained/solder-down socket** (vibration + cold).
8. **Independent watchdog (IWDG)** to recover from a hung SD driver or SEU-induced lockup.
9. **Hardware flow control (RTS/CTS) — recommended.** With little RAM to absorb SD stalls, the
   robust fix is to let the OBC pause the Coral when its buffer fills (see §4). More valuable
   on this OBC than a faster baud. Requires wiring RTS/CTS and a small protocol change.

## 4. Open decisions (need both teams)

| Topic | Current | Proposed | Owner to confirm |
|-------|---------|----------|------------------|
| **Baud rate** | 115200 (~4.4 s/frame) | **Keep 115200.** On a 128 KB / SPI-SD OBC a 1 Mbaud burst is *harder* to absorb (a ~250 ms SD GC stall overruns a small RX buffer); the slow trickle lets SD writes keep up | OBC: confirm SD streaming keeps up at 115200 |
| **Flow control** | none (8N1) | **Add RTS/CTS** so the OBC can back-pressure the Coral during SD stalls — the real robustness lever here | Both: wiring + small protocol change |
| **Archive resolution/format** | 224×224 Y8 (what the model sees) | **Archive the 224² grayscale the model already produces** — already in RAM, fits the OBC, still builds a flight dataset. Native 324² / RAW Bayer dropped: too large for 128 KB SRAM, for color the cloud task won't use | Payload + science |
| **Cadence vs TX** | 10 s interval, 4.4 s TX | keep interval ≥ ~6 s at 115200 | Both |

> Recommendation (payload side, revised for the L476RG): **keep 115200, archive the existing
> 224² grayscale frame, and add RTS/CTS flow control** rather than chasing a higher baud or
> native RAW. The earlier 1 Mbaud + RAW-Bayer plan suited a high-RAM M7 OBC; on a 128 KB
> low-power M4 with SPI SD it's the wrong trade — big frames don't fit and fast bursts are hard
> to absorb. If a training archive isn't a priority, dropping image storage entirely is also fine:
> the science survives on telemetry alone (§2).

## 5. Error handling summary (from UART_PROTOCOL.md §8)

| Condition | Payload | OBC |
|-----------|---------|-----|
| Camera/TPU fail | log on USB console, **no packet** | timeout → log SEQ gap, no SD file |
| Bad command CRC | NAK (0x15), discard | retry command |
| Packet CRC error | n/a (payload is sender) | discard frame, log SEQ gap, continue |
| OBC busy / RX overflow | n/a (fire-and-forget) | frame dropped → SEQ gap (fail-soft) |
