# firmware-stm32

STM32 firmware project for the CubeSat board based on `STM32L476RG`.

The project can be used with:
- `PlatformIO` via [platformio.ini](./platformio.ini)
- `STM32CubeIDE` / CubeMX via [scsd-lakitu-cubesat.ioc](./scsd-lakitu-cubesat.ioc)

## RFM95W telemetry-packet test

Set `TTC_TELEMETRY_PACKET_TEST` to `1` in `Core/Src/main.c`, then flash the
NUCLEO-L476RG. The test sends the 104-byte protocol-v2 packet ported from
`TTC_subsyst/Core/Src/telemetry_packet.c` immediately and every 20 seconds
using LoRa at 868 MHz, SF9, BW 125 kHz, CR 4/5, +14 dBm. Its sensor and health
fields are fixed mock values; only the sequence number and timing fields advance.

The radio shares SPI1 with the SD card: PA5/D13 = SCK, PA6/D12 = MISO,
PA7/D11 = MOSI. Connect RFM95W NSS to PB6/D10 and RESET to PC7/D9. PB6/D10 is therefore unavailable as the SD-card CS; move the SD-card CS to another GPIO before using both devices concurrently.
remains reserved for SD-card chip select.
