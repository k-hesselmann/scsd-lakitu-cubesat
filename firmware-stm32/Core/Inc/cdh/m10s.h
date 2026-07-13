#ifndef M10S_H
#define M10S_H

#include "main.h"
#include <stdint.h>

/**
 * @file m10s.h
 * @brief SparkFun u-blox GNSS (MAX-M10S) I2C Driver Interface
 *
 * Complete port of SparkFun u-blox GNSS Arduino Library to STM32 C.
 * Implements exact same behavior for initialization, data buffering,
 * message parsing, and PVT data access.
 */

#define M10S_I2C_ADDR 0x42

/**
 * @brief Position/Velocity/Time Data Structure
 *
 * Stores parsed NAV-PVT message data with conversions applied:
 * - latitude/longitude: degrees (WGS-84)
 * - altitude: meters (above ellipsoid)
 * - speed: meters per second (ground speed, 2D magnitude)
 * - num_satellites: count of satellites used in solution
 * - fix_type: GNSS fix type (0=None, 1=2D, 2=3D, 3=DGPS, 4=RTK-Float, 5=RTK-Fixed)
 * - timestamp: HAL_GetTick() at time of parsing
 */
typedef struct {
    double latitude;        /* degrees, WGS-84 */
    double longitude;       /* degrees, WGS-84 */
    int32_t altitude;       /* meters above ellipsoid */
    float speed;            /* meters per second (ground speed, 2D) */
    float vvel;             /* meters per second, positive = upward */
    uint8_t num_satellites; /* count */
    uint8_t fix_type;       /* GNSS fix type */
    uint32_t timestamp;     /* HAL_GetTick() */
} M10S_NavPVT;

/* ========================================================================== */
/* Public API Functions                                                      */
/* ========================================================================== */

/**
 * @brief Initialize MAX-M10S GPS module
 *
 * Checks device presence at I2C 0x42, configures output protocols,
 * enables NAV-PVT messages, and saves configuration to flash.
 *
 * @param hi2c I2C handle
 * @return 1 if successful, 0 if device not found or init failed
 */
uint8_t M10S_Begin(I2C_HandleTypeDef *hi2c);

/**
 * @brief Check for available data and buffer it
 *
 * Must be called frequently (10-100 ms intervals) to maintain responsiveness.
 * Reads bytes available from I2C register 0xFD and buffers incoming data.
 * Does NOT parse messages (that's done by M10S_Read).
 *
 * @param hi2c I2C handle
 * @return Number of bytes added to buffer (0 if nothing received or error)
 */
uint16_t M10S_CheckUblox(I2C_HandleTypeDef *hi2c);

/**
 * @brief Search buffer for complete UBX-NAV-PVT message and parse it
 *
 * Implements exact SparkFun library behavior:
 * - Searches buffer for sync characters and valid NAV-PVT message
 * - Validates checksum and payload
 * - Parses and validates data quality
 * - Removes processed message from buffer
 *
 * CRITICAL: Returns 1 ONLY if a NEW complete message was found and parsed.
 * Returns 0 if no new message, or if data validation failed.
 *
 * @param hi2c I2C handle (kept for API compatibility)
 * @param pvt Pointer to M10S_NavPVT structure to receive parsed data
 * @return 1 if NEW complete NAV-PVT message was found and parsed, 0 otherwise
 */
uint8_t M10S_Read(I2C_HandleTypeDef *hi2c, M10S_NavPVT *pvt);

/**
 * @brief Get the last successfully parsed PVT data
 *
 * Returns a copy of the most recent valid PVT data parsed by M10S_Read.
 * Useful for retrieving data without waiting for a new message.
 *
 * @param pvt Pointer to M10S_NavPVT structure to receive data
 * @return 1 if data is available, 0 if no data has been parsed yet
 */
uint8_t M10S_GetLastPVT(M10S_NavPVT *pvt);

/**
 * @brief Check if GPS module is initialized
 * @return 1 if M10S_Begin() succeeded, 0 otherwise
 */
uint8_t M10S_IsInitialized(void);

/**
 * @brief Get current receive buffer fill level (for debugging)
 * @return Number of bytes currently buffered
 */
uint16_t M10S_GetBufferFillLevel(void);

/**
 * @brief Clear all buffered data (for recovery/reset)
 */
void M10S_ClearBufferedData(void);

/**
 * @brief Request new PVT data from GPS (poll mode)
 *
 * Sends a UBX-NAV-PVT poll request to the GPS module.
 * The GPS will respond with a new NAV-PVT message containing current position/velocity/time data.
 * This function must be called regularly to receive updated GPS data in poll mode.
 *
 * @param hi2c I2C handle
 */
void M10S_RequestPVT(I2C_HandleTypeDef *hi2c);

#endif
