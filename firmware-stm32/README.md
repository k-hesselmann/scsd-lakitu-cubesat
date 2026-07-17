# firmware-stm32

STM32 firmware project for the CubeSat board based on `STM32L476RG`.

The project can be used with:
- `PlatformIO` via [platformio.ini](./platformio.ini)
- `STM32CubeIDE` / CubeMX via [scsd-lakitu-cubesat.ioc](./scsd-lakitu-cubesat.ioc)

## RFM95W raw telemetry downlink

The normal superloop builds and sends a **155-byte protocol-v7** telemetry frame
from the latest `g_datapool` and `g_scv` values. The packet contains direct
copies of selected source values, validity flags, Coral block, all SCV fields, and a compact volatile LoRa FDIR health snapshot;
the flight MCU performs no engineering-unit rescaling. CRC-16/CCITT is the only
additional processing. The ground station converts accelerometer g values to
m/s? and gyro deg/s values to rad/s for display.

TTC normally transmits every 20 seconds, but queues an immediate packet after a flight-state transition or a valid ground command. It accepts reliable `CMD,<id>,REQ_TELEMETRY` commands and `ACK,<sequence>` telemetry acknowledgements. Flight retries an unacknowledged packet up to three times using the same sequence, while ground retries commands with the same ID until flight echoes acceptance.
After the third unacknowledged attempt, flight records `ACK_TIMEOUT` and increments `lora_ack_timeout_count`. A separate 16-ID sliding replay window rejects repeated or stale command IDs even when a later telemetry ACK changes the displayed uplink state.

Some legacy ground-station fields (UTC, GNSS fix quality/HDOP/course, IMU/MCU
temperatures, command/link counters) are deliberately marked unavailable until
those sources exist in `SensorData_t` or `SCV_t`.

The radio uses SPI1: PA5/D13 = SCK, PA6/D12 = MISO, and PA7/D11 = MOSI.
Connect RFM95W NSS to PB6/D10 and RESET to PC7/D9. The SD card uses SPI2.


### LoRa FDIR

The packet also carries `lora_last_event`, consecutive-failure count,
recovery-attempt count, and the tick of the latest successful `TxDone`.
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
