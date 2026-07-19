# firmware-stm32

STM32 firmware project for the CubeSat board based on `STM32L476RG`.

The project can be used with:
- `PlatformIO` via [platformio.ini](./platformio.ini)
- `STM32CubeIDE` / CubeMX via [scsd-lakitu-cubesat.ioc](./scsd-lakitu-cubesat.ioc)

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


### LoRa FDIR

The packet also carries `lora_last_event`, consecutive and lifetime TX-failure
counts, recovery-attempt count, RX state, and telemetry-ACK timeout count.
`EQUIPMENT_LORA` remains the high-level SCV fault bit.  TTC resets and
reinitializes the RFM95W after three consecutive SPI/TX failures; further
recovery attempts are rate-limited to one per minute.  These fields describe
modem operation only; without an uplink acknowledgement the spacecraft cannot
prove that a ground station received a transmitted frame.

After each initialization, the driver reads back the essential frequency,
modem, and sync-word registers. A read failure is reported as `INIT_FAIL`; a
successful read with an unexpected value is reported as `CONFIG_FAIL`. In both
cases TTC keeps the modem unavailable and uses the existing one-minute recovery
backoff.
