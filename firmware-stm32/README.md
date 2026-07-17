# firmware-stm32

STM32 firmware project for the CubeSat board based on `STM32L476RG`.

The project can be used with:
- `PlatformIO` via [platformio.ini](./platformio.ini)
- `STM32CubeIDE` / CubeMX via [scsd-lakitu-cubesat.ioc](./scsd-lakitu-cubesat.ioc)

## RFM95W raw telemetry downlink

The normal superloop builds and sends a **128-byte protocol-v3** telemetry frame
from the latest `g_datapool` and `g_scv` values. The packet contains direct
copies of the source floats, validity flags, Coral block, and all SCV fields;
the flight MCU performs no engineering-unit rescaling. CRC-16/CCITT is the only
additional processing. The ground station converts accelerometer g values to
m/s? and gyro deg/s values to rad/s for display.

Some legacy ground-station fields (UTC, GNSS fix quality/HDOP/course, IMU/MCU
temperatures, command/link counters) are deliberately marked unavailable until
those sources exist in `SensorData_t` or `SCV_t`.

The radio shares SPI1 with the SD card: PA5/D13 = SCK, PA6/D12 = MISO,
PA7/D11 = MOSI. Connect RFM95W NSS to PB6/D10 and RESET to PC7/D9. PB6/D10 is
therefore unavailable as the SD-card CS; move the SD-card CS to another GPIO
before using both devices concurrently.
