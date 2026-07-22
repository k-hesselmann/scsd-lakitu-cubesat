# TTC ground station

The backend can receive protocol-v8 telemetry without hardware by using
`python backend_server.py --no-radio`. Hardware operation is fail-closed and
requires a provisioned 128-bit uplink authentication key:

```powershell
$env:TTC_RF_AUTH_KEY_HEX = "<32 hexadecimal digits>"
python backend_server.py
```

The same 16 key bytes must be injected into the flight build as two
little-endian 64-bit words:

```powershell
$env:TTC_AUTH_KEY_0 = "0x<bytes-0-through-7-in-little-endian-order>ULL"
$env:TTC_AUTH_KEY_1 = "0x<bytes-8-through-15-in-little-endian-order>ULL"
pio run -d firmware-stm32 -e nucleo_l476rg
```

Do not commit or print a flight key. The public values in CI are test vectors
only. Both `ACK,<boot>,<sequence>,<tx_uptime_s>` and `CMD,<id>,<verb>` are
transmitted with a 16-hex-character SipHash-2-4 tag; flight rejects legacy
unauthenticated uplinks. The ACK tuple binds the acknowledgement to the exact
outstanding flight frame instead of trusting a reusable sequence number alone.

When binding outside loopback, also set a strong `TTC_GROUND_API_TOKEN`. The
dashboard sends it through `X-Ground-Station-Token`. Reliable commands require
both the token (when configured) and a one-shot `ARM TTC` action. Automatic
telemetry ACKs bypass operator arming but still require RF authentication.

Command IDs are durably persisted and do not wrap under one RF key. If all
65,535 IDs are consumed, coordinated flight/ground rekeying is required before
the backend will arm another reliable command.

Install and validate with:

```powershell
python -m pip install -r requirements-dev.txt
python -m pytest -q
```
