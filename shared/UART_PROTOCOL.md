# Cloud Cover Estimator — UART Protocol Specification

**Version:** 1.0  
**Interface:** UART6 on Coral Dev Micro left header (J9)  
**Author:** Konstantin Hesselmann 

---

## 1. Physical Interface

| Signal | MCU pin       | J9 pin | Direction      |
|--------|---------------|--------|----------------|
| TX     | GPIO_EMC_B1_40 | —      | Coral → OBC   |
| RX     | GPIO_EMC_B1_41 | —      | OBC → Coral   |
| GND    | GND           | GND    | Common ground  |

**Serial parameters:** 115200 baud, 8N1, no hardware flow control.

> Note: The TX and RX pins are the dedicated UART6 lines on the left header
> (labelled UART6_TX\* and UART6_RX\* in the Coral Dev Micro datasheet).
> They are not repurposed GPIO; do not use them for anything else.

---

## 2. Data Flow Overview

```
Coral Dev Micro                         OBC
──────────────────────────────────────────────────────
Infer (every N seconds or on trigger)
  ├─ captures 224×224 grayscale frame
  ├─ runs model → cloud fraction [0,1]
  └─ sends IMAGE PACKET ──────────────────────────────► receive & store on SD card
       │ contains SEQ + FRACTION + raw pixels             <date>_<time>_cloud<pct>_seq<NNN>.bin
       │                                                  (raw, unannotated pixels)
       └─ FRACTION + SEQ ──────────────────► append to telemetry downlink log
                                              format: <SEQ>,<TIMESTAMP>,<PCT>
```

The **sequence number** (SEQ) is the shared key: the downlink telemetry entry
and the SD card image file for the same inference always carry the same SEQ,
so ground station can unambiguously pair a downlink cloud-cover reading with
its archived raw image.

---

## 3. Coral → OBC: Image Packet

Sent after every inference. All multi-byte integers are **big-endian**.

```
Offset  Size  Field        Value / Description
──────  ────  ───────────  ──────────────────────────────────────────────────
0       2     SOF          0xAA 0x55  (start-of-frame marker, not in CRC)
2       1     TYPE         0x01
3       4     SEQ          uint32, starts at 0, increments by 1 per inference
7       4     LEN          uint32, payload length in bytes = 7 + W×H
11      2     FRAC         uint16, cloud fraction × 65535
                             0x0000 = 0 %  (clear sky)
                             0xFFFF = 100 % (fully overcast)
13      2     WIDTH        uint16, image width  (currently always 224)
15      2     HEIGHT       uint16, image height (currently always 224)
17      1     FORMAT       0x01 = Y8 (8-bit grayscale, row-major, top-left origin)
18    W×H     PIXELS       raw uint8 pixel data, W×H bytes
18+W×H  2     CRC16        CRC-16/CCITT-FALSE over bytes at offsets 2 .. 17+W×H
                             (TYPE through last pixel, SOF excluded)
```

**Total frame size for 224×224:** 2 + 16 + 50176 + 2 = **50196 bytes**

### 3.1 CRC Algorithm

CRC-16/CCITT-FALSE:
- Polynomial: 0x1021
- Initial value: 0xFFFF
- Input/output reflection: none
- XOR out: 0x0000

Reference implementation (Python):

```python
def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = (crc << 1) ^ 0x1021 if crc & 0x8000 else crc << 1
        crc &= 0xFFFF
    return crc
```

### 3.2 FRACTION Encoding

```
cloud_fraction_float = FRAC / 65535.0   # [0.0 … 1.0]
cloud_cover_percent  = FRAC / 655.35    # [0.0 … 100.0]
```

### 3.3 Image Format

- **FORMAT 0x01 (Y8):** each pixel is one unsigned byte (0 = black, 255 = white).
  Width × Height bytes, row-major, top-left origin.
- Image represents exactly what the model saw: 224×224 grayscale, rotated 270°
  (USB-port side down, matching the intended camera orientation).

### 3.4 OBC Receiver Pseudocode

```python
from datetime import datetime, timezone

def receive_packet(serial_port):
    # Sync to SOF
    buf = b''
    while buf[-2:] != b'\xaa\x55':
        buf += serial_port.read(1)

    hdr = serial_port.read(16)          # TYPE .. FORMAT
    type_  = hdr[0]
    seq    = int.from_bytes(hdr[1:5],  'big')
    length = int.from_bytes(hdr[5:9],  'big')
    frac   = int.from_bytes(hdr[9:11], 'big') / 65535.0
    width  = int.from_bytes(hdr[11:13],'big')
    height = int.from_bytes(hdr[13:15],'big')
    fmt    = hdr[15]

    pixel_count = width * height        # = length - 7
    pixels = serial_port.read(pixel_count)
    rx_crc = int.from_bytes(serial_port.read(2), 'big')

    exp_crc = crc16(hdr + pixels)
    if rx_crc != exp_crc:
        raise ValueError(f"CRC mismatch seq={seq}")

    # The Coral has no real-time clock, so the OBC stamps the time of receipt
    # from its own RTC. SEQ remains the authoritative key (see section 6).
    now = datetime.now(timezone.utc)
    pct = frac * 100.0

    # Save the image as raw, UNANNOTATED pixels — keep it clean for training.
    # Filename carries date/time, cloud percent, and SEQ.
    stem = f"{now:%Y%m%d_%H%M%S}_cloud{pct:.1f}_seq{seq:06d}"
    with open(stem + ".bin", 'wb') as f:   # raw Y8, width*height bytes
        f.write(pixels)

    # Append to the downlink telemetry log (no image — percent + time + SEQ).
    with open("telemetry.csv", 'a') as f:
        f.write(f"{seq},{now.isoformat()},{pct:.2f}\n")

    return seq, frac
```

> **Clean images:** the firmware transmits raw Y8 pixels with **no overlay** —
> the cloud percentage lives only in the packet header (and your telemetry log
> / filename), never burned into the pixels. Keep it that way so the archived
> frames are directly usable as training data.
>
> **Optional PNG:** if the OBC can encode (e.g. has Pillow), saving a grayscale
> PNG with the same `stem` and embedding `timestamp` + `cloud_percent` as PNG
> text chunks matches the on-bench debug client's output exactly. Otherwise the
> raw `.bin` plus the filename/CSV is sufficient — decode later as
> `width × height` uint8, row-major.

---

## 4. OBC → Coral: Commands

Commands allow the OBC to trigger an immediate inference or adjust the
inference interval. All fields big-endian.

### 4.1 TRIGGER  (0x10)

Request one immediate inference (in addition to the periodic schedule).

```
Offset  Size  Field   Value
──────  ────  ──────  ─────────────────────────────
0       1     CMD     0x10
1       2     CRC16   CRC-16/CCITT-FALSE over byte 0 only
```

Total: **3 bytes**

### 4.2 SET_INTERVAL  (0x11)

Change the periodic inference interval. Takes effect immediately after ACK.

```
Offset  Size  Field     Value
──────  ────  ────────  ─────────────────────────────────────────────────
0       1     CMD       0x11
1       4     INTERVAL  uint32, new interval in milliseconds (big-endian)
                          minimum recommended: 1000 ms
5       2     CRC16     CRC-16/CCITT-FALSE over bytes 0–4
```

Total: **7 bytes**

### 4.3 Reserved / planned commands

The firmware currently implements only `TRIGGER` (0x10) and `SET_INTERVAL`
(0x11); the opcodes below are **reserved** for planned commands so the OBC team
can design around them and the command space is not reused. Each will follow the
same framing (CMD + payload + CRC-16/CCITT-FALSE, ACK/NAK reply) when added.
Tracked payload-side in [`payload/coral/docs/TODO.md`](../payload/coral/docs/TODO.md) §3.

| CMD  | Name              | Purpose                                                       |
|------|-------------------|---------------------------------------------------------------|
| 0x12 | `GET_STATUS`      | Liveness/health poll: uptime, current SEQ, last cloud %, reset count, TPU/camera OK flags |
| 0x13 | `REQUEST_FRAME`   | Re-send a specific image by SEQ the OBC missed (needs an on-board retained-frame cache) |
| 0x14 | `SOFT_RESET`      | Commanded reboot                                              |
| 0x16 | `SET_EXPOSURE`    | Adjust camera params (e.g. motion-blur tuning in flight)      |

> 0x15 is intentionally skipped — it is the NAK response byte (§5).
> These opcodes are not yet implemented: sending one today yields a single
> **NAK** (per §5). Behaviour for unknown/garbage commands beyond that single
> NAK (e.g. a resync window so line noise can't wedge the parser) is itself an
> open item.

---

## 5. Coral → OBC: Command Responses

Sent immediately after a command is processed.

| Byte  | Meaning                                        |
|-------|------------------------------------------------|
| 0x06  | **ACK** — command accepted and applied         |
| 0x15  | **NAK** — bad CRC, unknown command, or timeout |

---

## 6. Linking Images to Telemetry

The SEQ field is the authoritative link between an image on the SD card and
its corresponding entry in the downlink telemetry log. The date/time and
percent are also embedded in the filename for convenience, but SEQ is what
pairs the two unambiguously (timestamps can collide; SEQ cannot).

**SD card naming convention (OBC responsibility):**
```
<date>_<time>_cloud<pct>_seq<NNNNNN>.bin
20260613_143052_cloud23.4_seq000000.bin   ← SEQ = 0
20260613_143103_cloud67.1_seq000001.bin   ← SEQ = 1
…
```
- `date`/`time` — OBC RTC at packet receipt (Coral has no clock). UTC suggested.
- `cloud<pct>` — cloud-cover percent, one decimal.
- `seq<NNNNNN>` — zero-padded SEQ, the link to telemetry.
- This matches the on-bench debug client's `<date>_<time>_cloud<pct>_…png`
  naming, so flight and bench captures form one consistent dataset.

**Telemetry log format (one line per inference):**
```
SEQ,TIMESTAMP,CLOUD_COVER_PCT
0,2026-06-13T14:30:52+00:00,23.40
1,2026-06-13T14:31:03+00:00,67.10
…
```

If a packet is lost (CRC failure or missing), the SEQ gap in the telemetry log
identifies which SD card image has no corresponding downlink reading.

---

## 7. Timing

| Parameter                  | Value                            |
|----------------------------|----------------------------------|
| Default inference interval | 10 000 ms                        |
| Minimum recommended        | 1 000 ms                         |
| UART baud rate             | 115 200 bps                      |
| Packet size (224×224)      | 50 196 bytes                     |
| Tx time at 115200 baud     | ≈ 4.4 s                          |
| Inference + capture time   | ≈ 0.5–1.0 s (TPU)               |

> **Important:** at the default 10 s interval the 4.4 s transmit fits
> comfortably. If the OBC sets a shorter interval via SET_INTERVAL, ensure
> it is long enough that the previous packet finishes transmitting before the
> next inference fires (minimum ≈ 6 s at 115200).  Alternatively, increase
> the baud rate in firmware (UartInit in cloud_regressor.cc) and update this
> table accordingly.

---

## 8. Error Handling

| Condition          | Coral behaviour                        | OBC behaviour                        |
|--------------------|----------------------------------------|--------------------------------------|
| Camera capture fail | Prints error on USB console, no packet | Timeout → log gap, no SD file        |
| TPU invoke fail    | Prints error on USB console, no packet | Same as above                        |
| Bad command CRC    | Sends NAK (0x15), discards command     | Retry the command                    |
| Packet CRC error   | Not applicable (Coral is sender)       | Discard frame, log SEQ gap, continue |
