#include "cdh/baro_diag.h"
#include "cdh/ms5607.h"
#include <stdio.h>

extern UART_HandleTypeDef huart2;

void Baro_Diag_Test(I2C_HandleTypeDef *hi2c)
{
  uint8_t debug_buffer[256];
  int len;

  len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
    "\r\n=== BAROMETER DIAGNOSTIC TEST ===\r\n");
  HAL_UART_Transmit(&huart2, debug_buffer, len, 100);

  /* Test 1: Check Baro at 0x76 */
  len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
    "Checking Barometer at address 0x76 (MS5607)...\r\n");
  HAL_UART_Transmit(&huart2, debug_buffer, len, 100);

  HAL_StatusTypeDef status = HAL_I2C_IsDeviceReady(hi2c, 0x76 << 1, 1, 10);
  if (status != HAL_OK) {
    len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
      "ERROR: No device at 0x76 - Barometer NOT DETECTED\r\n");
    HAL_UART_Transmit(&huart2, debug_buffer, len, 100);
    return;
  }

  len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
    "✓ Barometer found at 0x76\r\n");
  HAL_UART_Transmit(&huart2, debug_buffer, len, 100);

  /* Test 2: Try to read Baro data */
  len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
    "Reading barometer data...\r\n");
  HAL_UART_Transmit(&huart2, debug_buffer, len, 100);

  MS5607_Data baro = MS5607_Read(hi2c);

  len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
    "✓ Barometer responding:\r\n"
    "  Pressure: %.2f hPa\r\n"
    "  Temperature: %.2f °C\r\n"
    "  Altitude: %.2f m\r\n",
    baro.pressure, baro.temperature, baro.altitude);
  HAL_UART_Transmit(&huart2, debug_buffer, len, 100);

  /* Test 3: Summary */
  len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
    "\r\n=== CONCLUSION ===\r\n"
    "If you see values above:\r\n"
    "  ✓ I2C BUS IS WORKING\r\n"
    "  ✓ This means GPS I2C problem is GPS-specific\r\n"
    "If no values:\r\n"
    "  ✗ I2C BUS PROBLEM\r\n"
    "============================\r\n\r\n");
  HAL_UART_Transmit(&huart2, debug_buffer, len, 100);
}
