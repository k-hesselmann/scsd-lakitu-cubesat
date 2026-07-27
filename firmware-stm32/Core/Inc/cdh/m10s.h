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

/* UBX-NAV-PVT fixType values. MAX-M10S reports 3 for a 3D GNSS fix. */
#define M10S_FIX_NONE 0U
#define M10S_FIX_2D   2U
#define M10S_FIX_3D   3U

/**
 * @brief Position/Velocity/Time Data Structure
 *
 * Stores parsed NAV-PVT message data with conversions applied:
 * - latitude/longitude: degrees (WGS-84)
 * - altitude: meters (above ellipsoid)
 * - speed: meters per second (ground speed, 2D magnitude)
 * - num_satellites: count of satellites used in solution
 * - fix_type: UBX-NAV-PVT fix type (0=None, 2=2D, 3=3D)
 * - timestamp: HAL_GetTick() at time of parsing
 */
typedef struct {
    double latitude;        /* degrees, WGS-84 */
    double longitude;       /* degrees, WGS-84 */
    int32_t altitude;       /* meters above ellipsoid */
    float speed;            /* meters per second (gSpeed - 2D ground speed) */
    float vel_down;         /* m/s, vertical velocity from altitude changes */
    float heading;          /* degrees, heading of motion */
    uint32_t utc_time;      /* HHMMSS format */
    uint8_t num_satellites; /* count */
    uint8_t fix_type;       /* GNSS fix type */
    uint32_t timestamp;     /* HAL_GetTick() */
} M10S_NavPVT;

typedef struct {
    uint32_t last_i2c_data_ms;
    uint32_t last_nav_pvt_ms;
    uint32_t last_valid_fix_ms;
    uint32_t i2c_bytes_received;
    uint32_t nav_pvt_count;
    uint32_t bad_checksum_count;
} M10S_Diagnostics_t;

/* ========================================================================== */
/* Public API Functions                                                      */
/* ========================================================================== */

/**
 * @brief Initialize MAX-M10S GPS module
 *
 * Starts asynchronous, cooperative initialization. Call M10S_InitService()
 * from the scheduler until M10S_IsInitialized() returns true.
 *
 * @param hi2c I2C handle
 * @return 1 when initialization was accepted for servicing
 */
uint8_t M10S_Begin(I2C_HandleTypeDef *hi2c);

/* Advance one initialization transaction or elapsed-time step. */
void M10S_InitService(I2C_HandleTypeDef *hi2c);

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

/* Snapshot of communication freshness and parser activity for [SYS_STAT]. */
void M10S_GetDiagnostics(M10S_Diagnostics_t *diagnostics);

/**
 * @brief Clear all buffered data (for recovery/reset)
 */
void M10S_ClearBufferedData(void);

/**
 * @brief Request new PVT data from GPS (not used in streaming mode)
 *
 * @param hi2c I2C handle (unused)
 */
void M10S_RequestPVT(I2C_HandleTypeDef *hi2c);

#endif
