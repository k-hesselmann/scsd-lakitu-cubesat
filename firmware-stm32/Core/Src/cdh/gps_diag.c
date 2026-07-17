#include "cdh/gps_diag.h"
#include "cdh/m10s.h"
#include <stdio.h>

extern UART_HandleTypeDef huart2;

void GPS_Diag_Test(I2C_HandleTypeDef *hi2c)
{
  uint8_t debug_buffer[256];

  /* Test 1: Complete I2C Address Scan */
  HAL_StatusTypeDef status;
  uint8_t address_found = 0;
  uint8_t found_addresses[10];
  uint8_t found_count = 0;

  int len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
    "\r\n=== I2C ADDRESS SCAN ===\r\nScanning all I2C addresses (0x08-0x77)...\r\n");
  HAL_UART_Transmit(&huart2, debug_buffer, len, 100);

  for (uint8_t addr = 0x08; addr < 0x78; addr++) {
    status = HAL_I2C_IsDeviceReady(hi2c, addr << 1, 1, 10);
    if (status == HAL_OK) {
      len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
        "  ✓ Device found at 0x%02X", addr);

      if (addr == 0x42) {
        len += snprintf((char*)&debug_buffer[len], sizeof(debug_buffer)-len, " ← GPS (MAX-M10S)");
        address_found = 1;
      } else if (addr == 0x76) {
        len += snprintf((char*)&debug_buffer[len], sizeof(debug_buffer)-len, " ← Barometer (MS5607)");
      }
      len += snprintf((char*)&debug_buffer[len], sizeof(debug_buffer)-len, "\r\n");
      HAL_UART_Transmit(&huart2, debug_buffer, len, 100);

      if (found_count < 10) {
        found_addresses[found_count++] = addr;
      }
    }
  }

  if (found_count == 0) {
    len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
      "  ✗ No devices found on I2C bus!\r\n");
    HAL_UART_Transmit(&huart2, debug_buffer, len, 100);
  }

  /* Test 2: Check GPS at 0x42 */
  len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
    "\r\nChecking GPS at address 0x42 (MAX10S)...\r\n");
  HAL_UART_Transmit(&huart2, debug_buffer, len, 100);

  if (!address_found) {
    len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
      "ERROR: No device at 0x42 - GPS NOT DETECTED\r\n");
    HAL_UART_Transmit(&huart2, debug_buffer, len, 100);
    return;
  }

  len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
    "✓ GPS found at 0x42\r\n");
  HAL_UART_Transmit(&huart2, debug_buffer, len, 100);

  /* Test 3: Try to read GPS data with multiple retries */
  len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
    "Waiting for GPS data (polling I2C)...\r\n");
  HAL_UART_Transmit(&huart2, debug_buffer, len, 100);

  M10S_NavPVT pvt = {0};
  uint8_t read_success = 0;

  for (int retry = 0; retry < 10; retry++) {
    /* Poll for incoming data */
    uint16_t bytes_buffered = M10S_CheckUblox(hi2c);
    HAL_Delay(100);

    len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
      "[Poll %d] Buffered: %u bytes\r\n", retry, bytes_buffered);
    HAL_UART_Transmit(&huart2, debug_buffer, len, 100);

    /* Try to parse a complete message */
    if (M10S_Read(hi2c, &pvt)) {
      read_success = 1;
      len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
        "\r\n=== UBX MESSAGE PARSED ===\r\n");
      HAL_UART_Transmit(&huart2, debug_buffer, len, 100);
      break;
    }

    /* Show buffer status */
    uint16_t buffer_fill = M10S_GetBufferFillLevel();
    len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
      "  Buffer fill: %u bytes\r\n", buffer_fill);
    HAL_UART_Transmit(&huart2, debug_buffer, len, 100);
  }

  if (read_success) {
    int lat = (int)(pvt.latitude * 1000000.0);
    int lon = (int)(pvt.longitude * 1000000.0);

    len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
      "✓ GPS responding: Lat=%d.%06d Lon=%d.%06d Sats=%d Fix=%d\r\n",
      lat / 1000000, (lat < 0 ? -lat : lat) % 1000000,
      lon / 1000000, (lon < 0 ? -lon : lon) % 1000000,
      pvt.num_satellites, pvt.fix_type);
    HAL_UART_Transmit(&huart2, debug_buffer, len, 100);
  } else {
    len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
      "WARNING: GPS at 0x42 but not responding to commands\r\n"
      "I2C communication issue. Check:\r\n"
      "  1. Pull-up resistors (4.7k) on SDA/SCL\r\n"
      "  2. GPS power supply (3.3V stable?)\r\n"
      "  3. SDA/SCL wiring (no shorts?)\r\n"
      "  4. GPS module not defective\r\n");
    HAL_UART_Transmit(&huart2, debug_buffer, len, 100);
  }

  /* Test 4: Summary */
  len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
    "\r\n=== DIAGNOSIS ===\r\n"
    "If you see coordinates above: GPS is working!\r\n"
    "If Sats=0 & Fix=0: GPS is fine, just waiting for satellite lock\r\n"
    "If no response above: Check I2C wiring\r\n"
    "===========================\r\n\r\n");
  HAL_UART_Transmit(&huart2, debug_buffer, len, 100);
}
