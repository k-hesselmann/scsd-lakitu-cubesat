#include "cdh/baro_diag.h"
#include "cdh/ms5607.h"
#include "debug_log.h"
#include <stdio.h>

void Baro_Diag_Test(I2C_HandleTypeDef *hi2c)
{
  uint8_t debug_buffer[256];
  int len;

  len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
    "\r\n=== BAROMETER DIAGNOSTIC TEST ===\r\n");
  DebugLog_WriteN(debug_buffer, len);

  /* Test 1: Check Baro at 0x76 */
  len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
    "Checking Barometer at address 0x76 (MS5607)...\r\n");
  DebugLog_WriteN(debug_buffer, len);

  HAL_StatusTypeDef status = HAL_I2C_IsDeviceReady(hi2c, 0x76 << 1, 1, 10);
  if (status != HAL_OK) {
    len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
      "ERROR: No device at 0x76 - Barometer NOT DETECTED\r\n");
    DebugLog_WriteN(debug_buffer, len);
    return;
  }

  len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
    "✓ Barometer found at 0x76\r\n");
  DebugLog_WriteN(debug_buffer, len);

  /* Test 2: Try to read Baro data */
  len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
    "Reading barometer data...\r\n");
  DebugLog_WriteN(debug_buffer, len);

  MS5607_Data baro = MS5607_Read(hi2c);

  int press = (int)(baro.pressure * 100.0f);
  int temp = (int)(baro.temperature * 100.0f);
  int alt = (int)(baro.altitude * 100.0f);

  len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
    "✓ Barometer responding:\r\n"
    "  Pressure: %d.%02d hPa\r\n"
    "  Temperature: %d.%02d °C\r\n"
    "  Altitude: %d.%02d m\r\n",
    press / 100, (press < 0 ? -press : press) % 100,
    temp / 100, (temp < 0 ? -temp : temp) % 100,
    alt / 100, (alt < 0 ? -alt : alt) % 100);
  DebugLog_WriteN(debug_buffer, len);

  /* Test 3: Summary */
  len = snprintf((char*)debug_buffer, sizeof(debug_buffer),
    "\r\n=== CONCLUSION ===\r\n"
    "If you see values above:\r\n"
    "  ✓ I2C BUS IS WORKING\r\n"
    "  ✓ This means GPS I2C problem is GPS-specific\r\n"
    "If no values:\r\n"
    "  ✗ I2C BUS PROBLEM\r\n"
    "============================\r\n\r\n");
  DebugLog_WriteN(debug_buffer, len);
}
