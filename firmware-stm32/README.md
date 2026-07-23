# firmware-stm32

STM32 firmware for the SCSD Lakitu CubeSat board, based on the `STM32L476RG`
(Nucleo-L476RG). The project can be used with:

- `PlatformIO` via [platformio.ini](./platformio.ini) (env `nucleo_l476rg`)
- `STM32CubeIDE` / CubeMX via [scsd-lakitu-cubesat.ioc](./scsd-lakitu-cubesat.ioc)

## Build & flash (PlatformIO)

From this directory, with an ST-Link connected:

```sh
pio run                     # build
pio run -t upload           # flash via ST-Link
pio device monitor -b 115200
```

Non-volatile regions at the top of flash are kept **out of the firmware
image** by the linker script, so a normal `pio run -t upload` does *not*
touch them:

| Region                | Address                   | Size   | Owner                 |
|-----------------------|---------------------------|--------|-----------------------|
| Datapool black-box    | `0x080E5800..0x080FF7FF`  | 104 KB | `cdh/datapool_nvm.c`  |
| SCV persistence       | `0x080FF800..0x080FFFFF`  | 2 KB   | `fdir/scv.c`          |

### Flashing with a default SCV

The Spacecraft Configuration Vector (boot count, watchdog reset count,
`equipment_manual_disable`, reduced-mode evidence, …) survives reflashing.
To start from a clean default SCV — `FDIR_Init()` falls back to
`FDIR_InitDefaults()` when the SCV page holds no valid record — erase the
SCV page before/while flashing:

```sh
# Option A: full chip erase (also wipes the datapool black-box), then flash
pio run -t erase
pio run -t upload

# Option B: erase only the SCV page (last 2 KB page, 0x080FF800)
STM32_Programmer_CLI -c port=SWD mode=UR -e 511
pio run -t upload
```

## Bench test: persisted SD-card disable

To bench-verify that a manual SD disable is applied and survives resets,
add this line in `FDIR_Init()` at `Core/Src/fdir/fdir.c:240`, directly
**before** the `FDIR_ApplyReducedMode(scv);` call:

```c
scv->equipment_manual_disable |= EQUIPMENT_SD;  /* TEMP TEST: bench-verify persisted SD disable, revert after */
```

Then build and flash as usual (`pio run -t upload`). Expected behaviour:

1. After boot, `EQUIPMENT_SD` is cleared from `equipment_enabled` and SD
   logging stays off.
2. The disable is persisted to the SCV by the `SCV_Backup()` at the end of
   `FDIR_Init()`, so it survives power cycles.

**Revert after the test.** Note that removing the line is not enough:
`equipment_manual_disable` lives in the SCV, so the disable persists until
you clear it — do a [default SCV flash](#flashing-with-a-default-scv)
(or re-enable via the corresponding ground command) after reverting.

## RFM95W raw telemetry downlink

The normal superloop builds and sends a **92-byte protocol-v8** telemetry frame
from the latest `g_datapool`, `g_scv`, and TTC health values. The packed wire
format contains only fixed-width integers, explicit fixed-point scaling,
validity bits, operational FDIR masks, compact Coral results, and CRC-16/CCITT.
The ground station accepts only this 92-byte v8 frame and converts its wire
values back to engineering units.

TTC normally transmits every 20 seconds, but queues an immediate packet after a
flight-state transition or valid ground command. It accepts reliable
`CMD,<id>,REQ_TELEMETRY` commands and `ACK,<sequence>` telemetry
acknowledgements. Flight retries an unacknowledged packet up to three times
using the exact same bytes and sequence. Command confirmation and telemetry-ACK
status are independently latched, so ACK processing cannot hide a command
response before ground receives it.

V8 includes GNSS UTC, fix type, satellites, and course; all IMU axes; barometer
pressure/altitude/temperature; battery voltage; Coral cloud fraction/status;
and the health values used by the dashboard. HDOP/VDOP and IMU/MCU temperatures
remain unavailable because no onboard producer currently supplies them.

The radio uses SPI1: PA5/D13 = SCK, PA6/D12 = MISO, and PA7/D11 = MOSI.
Connect RFM95W NSS to PB6/D10 and RESET to PC7/D9. The SD card uses SPI2.

The normal radio profile on flight and ground is **869.525 MHz, SF8,
125 kHz bandwidth, coding rate 4/5, 17 dBm PA_BOOST, preamble 8, explicit
header, payload CRC, and private sync word `0x12`**. A 92-byte frame occupies
about 287 ms, so the nominal 20-second cadence uses about 1.44% duty cycle;
three transmissions of the same frame use about 4.31% in the conservative
retry case. Antenna gain and feeder loss must be included when checking ERP.


### LoRa FDIR

The packet also carries `lora_last_event`, consecutive and lifetime TX-failure
counts, recovery-attempt count, RX state, and telemetry-ACK timeout count.
`EQUIPMENT_LORA` remains the high-level SCV fault bit. FDIR requests a TTC
radio recovery when the modem is idle but unavailable/RX-inactive, after five
consecutive failed TX attempts, or when at least 12 of the last 20 TX attempts
failed. Recovery attempts are rate-limited to one every 10 seconds. These fields
describe modem operation only; without an uplink acknowledgement the spacecraft
cannot prove that a ground station received a transmitted frame.

After each initialization, the driver reads back the essential frequency,
modem, and sync-word registers. A read failure is reported as `INIT_FAIL`; a
successful read with an unexpected value is reported as `CONFIG_FAIL`. In both
cases TTC keeps the modem unavailable and uses the existing FDIR recovery
cooldown.
