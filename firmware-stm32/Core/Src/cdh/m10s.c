/**
 * @file m10s.c
 * @brief u-blox MAX-M10S GPS NMEA Parser for I2C
 *
 * Parses NMEA sentences from MAX-M10S GPS module via I2C.
 * Extracts position, velocity, heading, and satellite information.
 */

#include "cdh/m10s.h"
#include "debug_log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

extern UART_HandleTypeDef huart2;

/* ========================================================================== */
/* I2C Communication Registers                                              */
/* ========================================================================== */

#define M10S_I2C_REG_BYTES_AVAIL   0xFD
#define M10S_I2C_REG_DATA_STREAM   0xFF
#define M10S_BUFFER_SIZE           1024

#ifndef M10S_VERBOSE_RUNTIME_LOGS
#define M10S_VERBOSE_RUNTIME_LOGS 0
#endif

/* ========================================================================== */
/* Static Module Data                                                        */
/* ========================================================================== */

static uint8_t s_rx_buffer[M10S_BUFFER_SIZE];
static uint16_t s_rx_index = 0;
static M10S_NavPVT s_last_pvt = {0};
static uint8_t s_initialized = 0;
static int32_t s_last_altitude_m = 0;
static uint32_t s_last_altitude_update_ms = 0;
static M10S_Diagnostics_t s_diagnostics = {0};

/* ========================================================================== */
/* I2C Helper Functions                                                      */
/* ========================================================================== */

static uint8_t M10S_GetBytesAvailable(I2C_HandleTypeDef *hi2c, uint16_t *bytes_avail)
{
    /* u-blox byte-count is a 16-bit register: 0xFD = high byte, 0xFE = low byte.
     * Reading 2 bytes starting at 0xFD gives the full count. Reading only 0xFD
     * (as before) returns count/256, which broke low-rate UBX streaming. */
    uint8_t buf[2];
    if (HAL_I2C_Mem_Read(hi2c, (M10S_I2C_ADDR << 1), M10S_I2C_REG_BYTES_AVAIL,
                         I2C_MEMADD_SIZE_8BIT, buf, 2, 100) != HAL_OK) {
        return 0;
    }
    uint16_t count = ((uint16_t)buf[0] << 8) | buf[1];
    if (count == 0xFFFF) count = 0;  /* 0xFFFF = module not ready / no data */
    *bytes_avail = count;
    return 1;
}

static uint8_t M10S_ReadDataStream(I2C_HandleTypeDef *hi2c, uint8_t *buffer, uint16_t len)
{
    return HAL_I2C_Mem_Read(hi2c, (M10S_I2C_ADDR << 1), M10S_I2C_REG_DATA_STREAM,
                           I2C_MEMADD_SIZE_8BIT, buffer, len, 100) == HAL_OK;
}

static uint8_t M10S_WriteDataStream(I2C_HandleTypeDef *hi2c, uint8_t *buffer, uint16_t len)
{
    return HAL_I2C_Mem_Write(hi2c, (M10S_I2C_ADDR << 1), M10S_I2C_REG_DATA_STREAM,
                            I2C_MEMADD_SIZE_8BIT, buffer, len, 100) == HAL_OK;
}

/**
 * @brief Query UBX-MON-VER to verify GPS responds to UBX commands
 * Returns firmware version info if successful
 */
static void M10S_QueryVersion(I2C_HandleTypeDef *hi2c)
{
    char dbg[256];
    int len;

    /* Build UBX-MON-VER request (no payload) */
    uint8_t request[8];
    request[0] = 0xB5;           /* Sync 1 */
    request[1] = 0x62;           /* Sync 2 */
    request[2] = 0x0A;           /* Class: MON */
    request[3] = 0x04;           /* ID: VER */
    request[4] = 0x00;           /* Length low (no payload) */
    request[5] = 0x00;           /* Length high */

    /* Calculate checksum */
    uint8_t ck_a = 0, ck_b = 0;
    for (int i = 2; i < 6; i++) {
        ck_a += request[i];
        ck_b += ck_a;
    }

    request[6] = ck_a;
    request[7] = ck_b;

    len = snprintf(dbg, sizeof(dbg), "[M10S] Querying UBX-MON-VER (testing UBX communication)...\r\n");
    DebugLog_WriteN((uint8_t*)dbg, len);

    /* Clear any buffered data first */
    uint16_t bytes_avail;
    uint8_t flush_buf[64];
    for (int i = 0; i < 5; i++) {
        if (M10S_GetBytesAvailable(hi2c, &bytes_avail) && bytes_avail > 0) {
            if (bytes_avail > 64) bytes_avail = 64;
            M10S_ReadDataStream(hi2c, flush_buf, bytes_avail);
        }
        HAL_Delay(50);
    }

    /* Send request */
    uint8_t result = M10S_WriteDataStream(hi2c, request, 8);

    if (!result) {
        len = snprintf(dbg, sizeof(dbg), "[M10S] ERROR: Failed to send UBX-MON-VER request\r\n");
        DebugLog_WriteN((uint8_t*)dbg, len);
        return;
    }

    /* Wait for response */
    HAL_Delay(500);

    /* Try to read response */
    if (M10S_GetBytesAvailable(hi2c, &bytes_avail) && bytes_avail > 0) {
        if (bytes_avail > 64) bytes_avail = 64;

        uint8_t response[64];
        if (M10S_ReadDataStream(hi2c, response, bytes_avail)) {
            /* Look for UBX-MON-VER response (class 0x0A, id 0x04) */
            if (bytes_avail >= 8 && response[0] == 0xB5 && response[1] == 0x62 &&
                response[2] == 0x0A && response[3] == 0x04) {

                len = snprintf(dbg, sizeof(dbg),
                    "[M10S] [OK] GPS responds to UBX; got %d bytes of UBX-MON-VER response\r\n",
                    bytes_avail);
                DebugLog_WriteN((uint8_t*)dbg, len);

                /* Print first part of version string if available (starts at byte 8) */
                if (bytes_avail > 8) {
                    char ver_str[50];
                    int ver_len = (bytes_avail - 8 > 40) ? 40 : (bytes_avail - 8);
                    strncpy(ver_str, (char*)&response[8], ver_len);
                    ver_str[ver_len] = '\0';

                    len = snprintf(dbg, sizeof(dbg), "[M10S] Firmware: %s\r\n", ver_str);
                    DebugLog_WriteN((uint8_t*)dbg, len);
                }
            } else {
                len = snprintf(dbg, sizeof(dbg),
                    "[M10S] Got response but not UBX-MON-VER. First bytes: %02X %02X %02X %02X\r\n",
                    response[0], response[1], response[2], response[3]);
                DebugLog_WriteN((uint8_t*)dbg, len);
            }
        }
    } else {
        len = snprintf(dbg, sizeof(dbg), "[M10S] No response to UBX-MON-VER query\r\n");
        DebugLog_WriteN((uint8_t*)dbg, len);
    }
}

/**
 * @brief Calculate NMEA checksum (XOR of all characters between $ and *)
 * Returns checksum as two hex digits
 */
static void M10S_CalculateNMEAChecksum(const char *sentence, char *checksum_hex)
{
    uint8_t checksum = 0;
    const char *p = sentence;

    /* Skip the $ character */
    if (*p == '$') p++;

    /* XOR all characters until we hit * or end of string */
    while (*p && *p != '*') {
        checksum ^= (uint8_t)*p;
        p++;
    }

    /* Convert to hex string (uppercase) */
    snprintf(checksum_hex, 3, "%02X", checksum);
}


/**
 * @brief Helper: encode 32-bit value to little-endian
 */
static uint32_t M10S_EncodeUint32LE(uint32_t value)
{
    return ((value & 0xFF) << 0) | ((value & 0xFF00) << 8) |
           ((value & 0xFF0000) << 16) | ((value & 0xFF000000) << 24);
}

/**
 * @brief Check for UBX ACK-ACK or ACK-NAK response (non-blocking)
 * Returns: 1=ACK received, 0=NAK or timeout
 */
static uint8_t M10S_WaitForACK(I2C_HandleTypeDef *hi2c, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint16_t bytes_avail;
    uint8_t response[64];

    while ((HAL_GetTick() - start) < timeout_ms) {
        if (M10S_GetBytesAvailable(hi2c, &bytes_avail) && bytes_avail >= 8) {
            uint16_t read_size = (bytes_avail > 64) ? 64 : bytes_avail;
            if (M10S_ReadDataStream(hi2c, response, read_size)) {
                /* Scan for UBX-ACK-ACK (B5 62 05 01) or UBX-ACK-NAK (B5 62 05 00)
                 * anywhere in the read chunk - it may not be at the start */
                for (uint16_t i = 0; i + 3 < read_size; i++) {
                    if (response[i] == 0xB5 && response[i+1] == 0x62 && response[i+2] == 0x05) {
                        if (response[i+3] == 0x01) {
                            return 1;  /* ACK-ACK */
                        } else if (response[i+3] == 0x00) {
                            return 0;  /* ACK-NAK */
                        }
                    }
                }
            }
        }
        HAL_Delay(10);
    }
    return 0;  /* Timeout */
}

/**
 * @brief Send UBX-CFG-VALSET to configure NMEA sentence output rate via I2C
 * Proper u-blox CFG-VALSET format with transaction and little-endian encoding
 * Message: B5 62 06 8A <len_lo> <len_hi> | version(1) layers(1) transaction(1) reserved(1) | key(4-LE) value(1-8) | CK_A CK_B
 *
 * Configuration keys (I2C-specific, size encoded in top nibble):
 *   0x209102ac = RMC_I2C (1-byte value)
 *   0x209102b1 = GGA_I2C (1-byte value)
 *   0x209102b6 = GSV_I2C (1-byte value, set to 0 to disable)
 *   0x209102c1 = GSA_I2C (1-byte value, set to 0 to disable)
 *   0x209102cc = GLL_I2C (1-byte value, set to 0 to disable)
 */
static void M10S_ConfigureNMEAViaValset(I2C_HandleTypeDef *hi2c, uint32_t key, uint8_t value)
{
    const char *msg_name;
    if (key == 0x209102ac) msg_name = "RMC_I2C";
    else if (key == 0x209102b1) msg_name = "GGA_I2C";
    else if (key == 0x209102b6) msg_name = "GSV_I2C";
    else if (key == 0x209102c1) msg_name = "GSA_I2C";
    else if (key == 0x209102cc) msg_name = "GLL_I2C";
    else if (key == 0x10720001) msg_name = "I2COUTPROT-UBX";
    else if (key == 0x10720002) msg_name = "I2COUTPROT-NMEA";
    else if (key == 0x20910006) msg_name = "MSGOUT-UBX_NAV_PVT_I2C";
    else if (key == 0x20110021) msg_name = "NAVSPG-DYNMODEL";
    else msg_name = "UNKNOWN";

    /* UBX-CFG-VALSET message format (proper u-blox implementation):
     * B5 62 06 8A | len_lo len_hi | version layers transaction reserved | key(4-LE) | value(1-8) | CK_A CK_B
     * Total: 2 (sync) + 2 (class/id) + 2 (length) + 4 (header) + 4 (key) + 1 (value) + 2 (checksum) = 17 bytes
     */
    uint8_t msg[17];
    int idx = 0;

    /* Frame header */
    msg[idx++] = 0xB5;           /* Sync 1 */
    msg[idx++] = 0x62;           /* Sync 2 */
    msg[idx++] = 0x06;           /* Class: CFG */
    msg[idx++] = 0x8A;           /* ID: VALSET */
    msg[idx++] = 0x09;           /* Length low byte (9 payload bytes) */
    msg[idx++] = 0x00;           /* Length high byte */

    /* Payload header */
    msg[idx++] = 0x00;           /* version 0x00: simple valset, no transaction */
    msg[idx++] = 0x01;           /* layers: bit0 = RAM (apply immediately) */
    msg[idx++] = 0x00;           /* reserved */
    msg[idx++] = 0x00;           /* reserved */

    /* Configuration key (4 bytes, little-endian byte order) */
    msg[idx++] = (key >> 0) & 0xFF;    /* key byte 0 (LSB) */
    msg[idx++] = (key >> 8) & 0xFF;    /* key byte 1 */
    msg[idx++] = (key >> 16) & 0xFF;   /* key byte 2 */
    msg[idx++] = (key >> 24) & 0xFF;   /* key byte 3 (MSB) */

    /* Value (1 byte for U1 type, which all our NMEA keys are) */
    msg[idx++] = value;

    /* Calculate checksum (Fletcher's algorithm per UBX protocol)
     * CK_A = sum of bytes [2..n-1]
     * CK_B = sum of CK_A values
     */
    uint8_t ck_a = 0, ck_b = 0;
    for (int i = 2; i < idx; i++) {  /* Start from class byte (index 2), skip sync */
        ck_a += msg[i];
        ck_b += ck_a;
    }

    msg[idx++] = ck_a;
    msg[idx++] = ck_b;

    /* Debug output */
    char dbg[128];
    int len = snprintf(dbg, sizeof(dbg),
        "[M10S] CFG-VALSET %s=%d\r\n", msg_name, value);
    DebugLog_WriteN((uint8_t*)dbg, len);

    /* Send via I2C */
    uint8_t result = M10S_WriteDataStream(hi2c, msg, idx);

    if (!result) {
        len = snprintf(dbg, sizeof(dbg), "[M10S]   ERROR: Failed to send command\r\n");
        DebugLog_WriteN((uint8_t*)dbg, len);
        return;
    }

    /* Wait for ACK/NAK response */
    HAL_Delay(100);  /* Give GPS time to process */
    uint8_t ack = M10S_WaitForACK(hi2c, 300);

    if (ack) {
        len = snprintf(dbg, sizeof(dbg), "[M10S]   [OK] ACK received\r\n");
    } else {
        len = snprintf(dbg, sizeof(dbg), "[M10S]   [WARN] No ACK (key may be unsupported on I2C)\r\n");
    }
    DebugLog_WriteN((uint8_t*)dbg, len);

    HAL_Delay(100);
}

/**
 * @brief Send UBX-CFG-MSG command to configure NMEA sentence output rate (legacy but works)
 */
static void M10S_ConfigureNMEAMessage(I2C_HandleTypeDef *hi2c, uint8_t msg_id, uint8_t rate)
{
    const char *msg_name = (msg_id == 0x04) ? "GNRMC" : (msg_id == 0x0A) ? "GNGGA" : "UNKNOWN";

    uint8_t full_msg[11];
    full_msg[0] = 0xB5;           /* Sync Char 1 */
    full_msg[1] = 0x62;           /* Sync Char 2 */
    full_msg[2] = 0x06;           /* Class: CFG */
    full_msg[3] = 0x01;           /* ID: MSG */
    full_msg[4] = 0x03;           /* Length low byte */
    full_msg[5] = 0x00;           /* Length high byte */
    full_msg[6] = 0xF0;           /* msgClass: NMEA */
    full_msg[7] = msg_id;         /* msgID */
    full_msg[8] = rate;           /* rate (1 = 1Hz) */

    /* Calculate checksum (both A and B required for proper UBX protocol) */
    uint8_t ck_a = 0, ck_b = 0;
    for (int i = 2; i < 9; i++) {
        ck_a += full_msg[i];
        ck_b += ck_a;
    }

    full_msg[9] = ck_a;
    full_msg[10] = ck_b;

    /* Debug: print UBX command being sent */
    char dbg[128];
    int len = snprintf(dbg, sizeof(dbg),
        "[M10S] Sending CFG-MSG for %s at %dHz (msg_id=0x%02X, ck_a=0x%02X, ck_b=0x%02X)\r\n",
        msg_name, rate, msg_id, ck_a, ck_b);
    DebugLog_WriteN((uint8_t*)dbg, len);

    /* Send via I2C */
    uint8_t result = M10S_WriteDataStream(hi2c, full_msg, 11);

    if (result) {
        len = snprintf(dbg, sizeof(dbg), "[M10S]   [OK] %s command sent\r\n", msg_name);
        DebugLog_WriteN((uint8_t*)dbg, len);
    } else {
        len = snprintf(dbg, sizeof(dbg), "[M10S]   [FAIL] Failed to send %s\r\n", msg_name);
        DebugLog_WriteN((uint8_t*)dbg, len);
        return;
    }

    HAL_Delay(200);  /* Wait for GPS to process and generate ACK */
}

/* ========================================================================== */
/* NMEA Sentence Parsing                                                     */
/* ========================================================================== */

/**
 * @brief Parse a NMEA field (comma-delimited)
 * Returns pointer to field value, or empty string if field missing
 */
static char* M10S_GetNMEAField(char *sentence, int field_num)
{
    static char field[32];
    int current_field = 0;
    int i = 0;

    memset(field, 0, sizeof(field));

    for (int pos = 0; pos < strlen(sentence) && i < sizeof(field) - 1; pos++) {
        if (sentence[pos] == ',') {
            if (current_field == field_num) break;
            current_field++;
            i = 0;
        } else if (current_field == field_num) {
            field[i++] = sentence[pos];
        }
    }
    field[i] = '\0';
    return field;
}

/**
 * @brief Parse GPRMC sentence (Recommended Minimum Navigation Information)
 * $GPRMC,hhmmss.ss,A,llll.ll,a,yyyyy.yy,a,x.x,x.x,ddmmyy,...*hh
 */
static void M10S_ParseGPRMC(char *sentence, M10S_NavPVT *pvt)
{
    char *field;

    /* UTC time (field 1: hhmmss.ss) */
    field = M10S_GetNMEAField(sentence, 1);
    if (field[0]) {
        pvt->utc_time = (uint32_t)atof(field);
    }

    /* Extract all GPRMC fields for debugging */
    char f1[32], f2[32], f3[32], f4[32], f5[32], f6[32], f7[32], f8[32];
    strncpy(f1, M10S_GetNMEAField(sentence, 1), sizeof(f1)-1); f1[sizeof(f1)-1] = '\0';
    strncpy(f2, M10S_GetNMEAField(sentence, 2), sizeof(f2)-1); f2[sizeof(f2)-1] = '\0';
    strncpy(f3, M10S_GetNMEAField(sentence, 3), sizeof(f3)-1); f3[sizeof(f3)-1] = '\0';
    strncpy(f4, M10S_GetNMEAField(sentence, 4), sizeof(f4)-1); f4[sizeof(f4)-1] = '\0';
    strncpy(f5, M10S_GetNMEAField(sentence, 5), sizeof(f5)-1); f5[sizeof(f5)-1] = '\0';
    strncpy(f6, M10S_GetNMEAField(sentence, 6), sizeof(f6)-1); f6[sizeof(f6)-1] = '\0';
    strncpy(f7, M10S_GetNMEAField(sentence, 7), sizeof(f7)-1); f7[sizeof(f7)-1] = '\0';
    strncpy(f8, M10S_GetNMEAField(sentence, 8), sizeof(f8)-1); f8[sizeof(f8)-1] = '\0';

    /* Check status (field 2: A=valid, V=void) */
    if (f2[0] != 'A') {
        pvt->fix_type = 0;  /* No fix */
        return;
    }

    pvt->fix_type = 2;  /* 2D fix (GPRMC status A = active fix) */

    /* Latitude (field 3: ddmm.mmmm) - use extracted f3 */
    if (f3[0]) {
        double lat_deg = atof(f3) / 100.0;
        int lat_d = (int)lat_deg;
        double lat_m = (lat_deg - lat_d) * 100.0;
        pvt->latitude = lat_d + (lat_m / 60.0);

        /* Check N/S (field 4) */
        if (f4[0] == 'S') pvt->latitude = -pvt->latitude;
    }

    /* Longitude (field 5: dddmm.mmmm) - use extracted f5 */
    if (f5[0]) {
        double lon_deg = atof(f5) / 100.0;
        int lon_d = (int)lon_deg;
        double lon_m = (lon_deg - lon_d) * 100.0;
        pvt->longitude = lon_d + (lon_m / 60.0);

        /* Check E/W (field 6) */
        if (f6[0] == 'W') pvt->longitude = -pvt->longitude;
    }

    /* Speed in knots (field 7) -> convert to m/s - use extracted f7 */
    if (f7[0]) {
        float speed_knots = atof(f7);
        pvt->speed = speed_knots * 0.51444f;  /* 1 knot = 0.51444 m/s */
    }

    /* True heading (field 8) - use extracted f8 */
    if (f8[0]) {
        pvt->heading = atof(f8);
    }
}

/**
 * @brief Parse GPGGA sentence (Global Positioning System Fix Data)
 * Contains position, altitude, and number of satellites
 * $GNGGA,hhmmss.ss,llll.llll,a,yyyyy.yyyy,a,x,xx,x.x,x.x,M,x.x,M,x.x,xxxx*hh
 */
static void M10S_ParseGPGGA(char *sentence, M10S_NavPVT *pvt)
{
    char *field;

    /* Fix quality (field 6: 0=invalid, 1=GPS, 2=DGPS, etc) */
    field = M10S_GetNMEAField(sentence, 6);
    int fix_quality = atoi(field);
    if (fix_quality == 0) {
        pvt->fix_type = 0;  /* No fix */
        return;
    }
    pvt->fix_type = (fix_quality >= 1) ? 2 : 0;  /* 2D fix if quality >= 1 */

    /* Latitude (field 2: ddmm.mmmm) */
    field = M10S_GetNMEAField(sentence, 2);
    if (field[0]) {
        double lat_deg = atof(field) / 100.0;
        int lat_d = (int)lat_deg;
        double lat_m = (lat_deg - lat_d) * 100.0;
        pvt->latitude = lat_d + (lat_m / 60.0);

        /* Check N/S (field 3) */
        char *ns = M10S_GetNMEAField(sentence, 3);
        if (ns[0] == 'S') pvt->latitude = -pvt->latitude;
    }

    /* Longitude (field 4: dddmm.mmmm) */
    field = M10S_GetNMEAField(sentence, 4);
    if (field[0]) {
        double lon_deg = atof(field) / 100.0;
        int lon_d = (int)lon_deg;
        double lon_m = (lon_deg - lon_d) * 100.0;
        pvt->longitude = lon_d + (lon_m / 60.0);

        /* Check E/W (field 5) */
        char *ew = M10S_GetNMEAField(sentence, 5);
        if (ew[0] == 'W') pvt->longitude = -pvt->longitude;
    }

    /* Number of satellites in use (field 7) */
    field = M10S_GetNMEAField(sentence, 7);
    if (field[0]) {
        pvt->num_satellites = atoi(field);
    }

    /* Altitude above mean sea level in meters (field 9) */
    field = M10S_GetNMEAField(sentence, 9);
    if (field[0]) {
        pvt->altitude = (int32_t)atof(field);
    }
}

/**
 * @brief Calculate vertical velocity from altitude changes over time
 */
static void M10S_CalculateVerticalVelocity(M10S_NavPVT *pvt)
{
    uint32_t now = HAL_GetTick();

    if (s_last_altitude_update_ms == 0) {
        /* First altitude reading */
        s_last_altitude_m = pvt->altitude;
        s_last_altitude_update_ms = now;
        pvt->vel_down = 0.0f;
        return;
    }

    uint32_t time_delta_ms = now - s_last_altitude_update_ms;
    if (time_delta_ms < 100) {
        /* Not enough time has passed, use previous value */
        return;
    }

    float time_delta_s = time_delta_ms / 1000.0f;
    int32_t altitude_delta = pvt->altitude - s_last_altitude_m;
    pvt->vel_down = -(float)altitude_delta / time_delta_s;  /* Negative because down is negative altitude change */

    s_last_altitude_m = pvt->altitude;
    s_last_altitude_update_ms = now;
}

/**
 * @brief Extract and process a complete NMEA sentence from buffer
 */
static uint8_t M10S_ProcessSentence(char *sentence)
{
    M10S_NavPVT temp_pvt = {0};

    /* Filter out high-volume messages that flood the buffer (software filtering since CFG-VALSET doesn't work) */
    /* These messages aren't needed and consume significant I2C bandwidth */
    if (strstr(sentence, "GSV") ||      /* Satellite list (hundreds of bytes per cycle) */
        strstr(sentence, "GSA") ||      /* Active satellites (not needed for position) */
        strstr(sentence, "GLL") ||      /* Geographic position (duplicate of RMC/GGA) */
        strstr(sentence, "VTG")) {      /* Course/speed (duplicate of RMC data) */
        return 0;  /* Silently ignore */
    }

    /* Accept both GP (GPS) and GN (GNSS multi-constellation) prefixes */
    if ((strncmp(sentence, "$GPRMC", 6) == 0) || (strncmp(sentence, "$GNRMC", 6) == 0)) {
        M10S_ParseGPRMC(sentence, &temp_pvt);
        /* Preserve altitude from previous GNGGA sentence (GPRMC has no altitude field) */
        temp_pvt.altitude = s_last_pvt.altitude;
        temp_pvt.num_satellites = s_last_pvt.num_satellites;  /* Preserve satellite count from GNGGA */
        s_last_pvt = temp_pvt;
        s_last_pvt.timestamp = HAL_GetTick();
        return 1;
    }
    else if ((strncmp(sentence, "$GPGGA", 6) == 0) || (strncmp(sentence, "$GNGGA", 6) == 0)) {
        M10S_ParseGPGGA(sentence, &s_last_pvt);
        M10S_CalculateVerticalVelocity(&s_last_pvt);
        return 1;
    }

    return 0;
}

/* ========================================================================== */
/* Public API                                                                */
/* ========================================================================== */

uint8_t M10S_Begin(I2C_HandleTypeDef *hi2c)
{
    char dbg[128];
    int len;

    /* Initialize I2C if needed */
    if (hi2c->State != HAL_I2C_STATE_READY) {
        HAL_I2C_DeInit(hi2c);
        HAL_Delay(100);
        HAL_I2C_Init(hi2c);
        HAL_Delay(100);
    }

    len = snprintf(dbg, sizeof(dbg), "[M10S] Initializing MAX-M10S at I2C 0x42...\r\n");
    DebugLog_WriteN((uint8_t*)dbg, len);

    /* Check device presence */
    uint16_t bytes_avail = 0;
    if (!M10S_GetBytesAvailable(hi2c, &bytes_avail)) {
        len = snprintf(dbg, sizeof(dbg), "[M10S] ERROR: Device not found at I2C 0x42\r\n");
        DebugLog_WriteN((uint8_t*)dbg, len);
        return 0;
    }

    len = snprintf(dbg, sizeof(dbg), "[M10S] Device found. Testing UBX communication...\r\n");
    DebugLog_WriteN((uint8_t*)dbg, len);

    /* Query version to verify GPS responds to UBX commands */
    M10S_QueryVersion(hi2c);
    HAL_Delay(500);

    /* Perform controlled software reset to clear any stuck state */
    len = snprintf(dbg, sizeof(dbg), "[M10S] Performing controlled software reset...\r\n");
    DebugLog_WriteN((uint8_t*)dbg, len);

    uint8_t reset_msg[10];
    reset_msg[0] = 0xB5;           /* Sync 1 */
    reset_msg[1] = 0x62;           /* Sync 2 */
    reset_msg[2] = 0x06;           /* Class: CFG */
    reset_msg[3] = 0x04;           /* ID: RST (Reset) */
    reset_msg[4] = 0x04;           /* Length low (4 bytes payload) */
    reset_msg[5] = 0x00;           /* Length high */
    reset_msg[6] = 0x00;           /* navBbrMask: don't clear BBR */
    reset_msg[7] = 0x01;           /* resetMode: 0x01 = Controlled software reset */
    reset_msg[8] = 0x00;           /* reserved */
    reset_msg[9] = 0x00;           /* reserved */

    /* Calculate checksum */
    uint8_t ck_a = 0, ck_b = 0;
    for (int i = 2; i < 10; i++) {
        ck_a += reset_msg[i];
        ck_b += ck_a;
    }

    /* Append checksum (overwrite reserved bytes) */
    reset_msg[8] = ck_a;
    reset_msg[9] = ck_b;

    M10S_WriteDataStream(hi2c, reset_msg, 10);
    HAL_Delay(2000);  /* Wait for reset to complete */

    /* M10 modules do NOT support legacy UBX-CFG-PRT / UBX-CFG-MSG.
     * All configuration must go through UBX-CFG-VALSET (configuration interface).
     * Keys (from u-blox M10 interface description):
     *   CFG-I2COUTPROT-UBX        0x10720001 (L)  - enable UBX output on I2C
     *   CFG-I2COUTPROT-NMEA       0x10720002 (L)  - NMEA output on I2C
     *   CFG-MSGOUT-UBX_NAV_PVT_I2C 0x20910006 (U1) - NAV-PVT rate on I2C
     */
    len = snprintf(dbg, sizeof(dbg), "[M10S] Configuring via VALSET: UBX out, NMEA off, NAV-PVT 1Hz...\r\n");
    DebugLog_WriteN((uint8_t*)dbg, len);

    M10S_ConfigureNMEAViaValset(hi2c, 0x10720001, 1);  /* CFG-I2COUTPROT-UBX = 1 */
    M10S_ConfigureNMEAViaValset(hi2c, 0x10720002, 0);  /* CFG-I2COUTPROT-NMEA = 0 */
    M10S_ConfigureNMEAViaValset(hi2c, 0x20910006, 1);  /* CFG-MSGOUT-UBX_NAV_PVT_I2C = 1 */
    M10S_ConfigureNMEAViaValset(hi2c, 0x20110021, 6);  /* CFG-NAVSPG-DYNMODEL = 6 (Airborne <1g) */

    len = snprintf(dbg, sizeof(dbg), "[M10S] UBX-NAV-PVT configured. Waiting for data...\r\n");
    DebugLog_WriteN((uint8_t*)dbg, len);

    /* Wait for GPS to start sending configured data */
    HAL_Delay(2000);

    /* Check if we're receiving data */
    uint8_t data_detected = 0;
    len = snprintf(dbg, sizeof(dbg), "[M10S] Checking for incoming UBX data...\r\n");
    DebugLog_WriteN((uint8_t*)dbg, len);

    /* Clear any initial buffered data and reset internal buffer pointers */
    uint8_t flush_buf[64];
    for (int i = 0; i < 10; i++) {
        if (M10S_GetBytesAvailable(hi2c, &bytes_avail) && bytes_avail > 0) {
            data_detected = 1;
            len = snprintf(dbg, sizeof(dbg), "[M10S]   Received %u bytes\r\n", bytes_avail);
            DebugLog_WriteN((uint8_t*)dbg, len);

            if (bytes_avail > 64) bytes_avail = 64;
            M10S_ReadDataStream(hi2c, flush_buf, bytes_avail);
        }
        HAL_Delay(100);
    }

    if (!data_detected) {
        len = snprintf(dbg, sizeof(dbg), "[M10S] WARNING: No UBX data detected after configuration!\r\n");
        DebugLog_WriteN((uint8_t*)dbg, len);
    } else {
        len = snprintf(dbg, sizeof(dbg), "[M10S] [OK] UBX data detected successfully\r\n");
        DebugLog_WriteN((uint8_t*)dbg, len);
    }

    /* Clear the internal sentence buffer completely */
    s_rx_index = 0;
    memset(s_rx_buffer, 0, M10S_BUFFER_SIZE);

    s_initialized = 1;
    len = snprintf(dbg, sizeof(dbg), "[M10S] Initialization complete. UBX-NAV-PVT at 1Hz...\r\n");
    DebugLog_WriteN((uint8_t*)dbg, len);

    return 1;
}

uint16_t M10S_CheckUblox(I2C_HandleTypeDef *hi2c)
{
    if (!s_initialized) return 0;

    uint16_t bytes_avail = 0;
    if (!M10S_GetBytesAvailable(hi2c, &bytes_avail) || bytes_avail == 0) {
        return 0;
    }

#if M10S_VERBOSE_RUNTIME_LOGS
    static uint32_t last_debug = 0;
    if (HAL_GetTick() - last_debug > 3000) {
        char dbg[100];
        int len = snprintf(dbg, sizeof(dbg), "[M10S_CHECK] %u bytes available, buffer fill: %d/%d\r\n",
            bytes_avail, s_rx_index, M10S_BUFFER_SIZE);
        DebugLog_WriteN((uint8_t*)dbg, len);
        last_debug = HAL_GetTick();
    }
#endif

    /* Cap to remaining buffer space (no wrap - parser assumes linear buffer) */
    uint16_t space = M10S_BUFFER_SIZE - s_rx_index;
    if (space == 0) {
        /* Buffer full of unparseable data - reset for recovery */
        s_rx_index = 0;
        space = M10S_BUFFER_SIZE;
    }
    if (bytes_avail > space) {
        bytes_avail = space;
    }

    /* Drain up to 256 bytes per call in 64-byte chunks */
    uint16_t total_read = 0;
    uint8_t temp_buf[64];
    while (total_read < bytes_avail && total_read < 256) {
        uint16_t chunk = bytes_avail - total_read;
        if (chunk > 64) chunk = 64;

        if (!M10S_ReadDataStream(hi2c, temp_buf, chunk)) break;

        memcpy(&s_rx_buffer[s_rx_index], temp_buf, chunk);
        s_rx_index += chunk;
        total_read += chunk;
    }

    if (total_read > 0U) {
        s_diagnostics.last_i2c_data_ms = HAL_GetTick();
        s_diagnostics.i2c_bytes_received += total_read;
    }

    return total_read;
}

uint8_t M10S_Read(I2C_HandleTypeDef *hi2c, M10S_NavPVT *pvt)
{
    (void)hi2c;  /* Not used in UBX mode */

    if (!s_initialized || s_rx_index < 100) {
#if M10S_VERBOSE_RUNTIME_LOGS
        static uint32_t last_debug = 0;
        if (HAL_GetTick() - last_debug > 5000) {
            char dbg[100];
            int len = snprintf(dbg, sizeof(dbg), "[M10S_READ] Buffer: %d bytes (need 100+)\r\n", s_rx_index);
            DebugLog_WriteN((uint8_t*)dbg, len);
            last_debug = HAL_GetTick();
        }
#endif
        return 0;
    }

#if M10S_VERBOSE_RUNTIME_LOGS
    /* Debug: once per 3s, show what message types are sitting in the buffer */
    static uint32_t last_sync_debug = 0;
    if (HAL_GetTick() - last_sync_debug > 3000) {
        char dbg[100];
        int len = snprintf(dbg, sizeof(dbg), "[M10S_READ] Scanning %d bytes, first: %02X %02X %02X %02X\r\n",
            s_rx_index, s_rx_buffer[0], s_rx_buffer[1], s_rx_buffer[2], s_rx_buffer[3]);
        DebugLog_WriteN((uint8_t*)dbg, len);
        last_sync_debug = HAL_GetTick();
    }
#endif

    /* Look for UBX-NAV-PVT message: B5 62 01 07 */
    for (uint16_t i = 0; i < s_rx_index - 100; i++) {
        if (s_rx_buffer[i] == 0xB5 && s_rx_buffer[i+1] == 0x62 &&
            s_rx_buffer[i+2] == 0x01 && s_rx_buffer[i+3] == 0x07) {

            char dbg[160];
            int len;

            /* Found UBX-NAV-PVT sync chars and class/ID */
            uint8_t len_low = s_rx_buffer[i+4];
            uint8_t len_high = s_rx_buffer[i+5];
            uint16_t payload_len = (len_high << 8) | len_low;
            uint16_t total_msg_len = 8 + payload_len;  /* Sync(2) + Class(1) + ID(1) + Len(2) + Payload + CK(2) */

            /* Sanity check: NAV-PVT payload is 92 bytes; reject corrupt lengths */
            if (payload_len != 92) {
                /* Corrupt header - drop everything through this sync and rescan next call */
                s_rx_index -= (i + 2);
                memmove(s_rx_buffer, &s_rx_buffer[i + 2], s_rx_index);
                return 0;
            }

            if (i + total_msg_len > s_rx_index) {
                return 0;  /* Message incomplete - wait for more data */
            }

            /* Verify checksum */
            uint8_t ck_a = 0, ck_b = 0;
            for (uint16_t j = 2; j < 6 + payload_len; j++) {
                ck_a += s_rx_buffer[i+j];
                ck_b += ck_a;
            }

            if (ck_a != s_rx_buffer[i + 6 + payload_len] ||
                ck_b != s_rx_buffer[i + 7 + payload_len]) {
                s_diagnostics.bad_checksum_count++;
                len = snprintf(dbg, sizeof(dbg), "[M10S_READ] Bad checksum, dropping message\r\n");
                DebugLog_WriteN((uint8_t*)dbg, len);
                /* Drop everything through this sync so we don't rescan it forever */
                s_rx_index -= (i + 2);
                memmove(s_rx_buffer, &s_rx_buffer[i + 2], s_rx_index);
                return 0;
            }

            /* Checksum valid - parse UBX-NAV-PVT payload (92 bytes).
             * Field offsets per u-blox M10 interface description. */
            uint8_t *payload = &s_rx_buffer[i + 6];

            #define LE_U32(o) ((uint32_t)payload[o] | ((uint32_t)payload[(o)+1]<<8) | ((uint32_t)payload[(o)+2]<<16) | ((uint32_t)payload[(o)+3]<<24))
            #define LE_I32(o) ((int32_t)LE_U32(o))

            uint32_t iTOW   = LE_U32(0);       /* GPS time of week (ms) */
            uint8_t  hour   = payload[8];
            uint8_t  minute = payload[9];
            uint8_t  second = payload[10];
            uint8_t  fixType = payload[20];
            uint8_t  flags  = payload[21];      /* gnssFixOK, diffSoln, ... */
            uint8_t  numSV  = payload[23];
            int32_t  lon    = LE_I32(24);       /* deg 1e-7 */
            int32_t  lat    = LE_I32(28);       /* deg 1e-7 */
            int32_t  hMSL   = LE_I32(36);       /* mm above mean sea level */
            int32_t  velD   = LE_I32(56);       /* mm/s */
            int32_t  gSpeed = LE_I32(60);       /* ground speed mm/s */
            int32_t  heading = LE_I32(64);      /* heading of motion deg 1e-5 */

            /* Store in PVT structure (units: deg*1e-7 -> deg, mm -> m, mm/s -> m/s, deg*1e-5 -> deg) */
            s_last_pvt.latitude = lat / 10000000.0;
            s_last_pvt.longitude = lon / 10000000.0;
            s_last_pvt.altitude = hMSL / 1000;
            s_last_pvt.speed = gSpeed / 1000.0f;
            s_last_pvt.vel_down = velD / 1000.0f;
            s_last_pvt.heading = heading / 100000.0f;
            s_last_pvt.utc_time = ((uint32_t)hour * 10000) + ((uint32_t)minute * 100) + second;
            s_last_pvt.num_satellites = numSV;
            s_last_pvt.fix_type = fixType;
            s_last_pvt.timestamp = HAL_GetTick();
            s_diagnostics.last_nav_pvt_ms = s_last_pvt.timestamp;
            s_diagnostics.nav_pvt_count++;
            if (fixType > 0U && numSV > 0U && (flags & 0x01U) != 0U)
                s_diagnostics.last_valid_fix_ms = s_last_pvt.timestamp;

            /* A compact, rate-limited summary replaces four synchronous lines
             * per 1 Hz NAV-PVT message. Full fields remain in s_last_pvt and
             * the 10-second [SYS_STAT] reports parser/fix freshness. */
            static uint32_t last_pvt_log_ms = 0U;
            if ((uint32_t)(s_last_pvt.timestamp - last_pvt_log_ms) >= 10000U)
            {
            len = snprintf(dbg, sizeof(dbg),
                "[GPS_PVT] t=%lu iTOW=%lu fix=%u flags=0x%02X sv=%u lat_e7=%ld lon_e7=%ld hmsl_mm=%ld\r\n",
                (unsigned long)s_last_pvt.timestamp, (unsigned long)iTOW,
                (unsigned int)fixType, (unsigned int)flags, (unsigned int)numSV,
                (long)lat, (long)lon, (long)hMSL);
                DebugLog_WriteN(dbg, len);
                last_pvt_log_ms = s_last_pvt.timestamp;
            }

            #undef LE_U32
            #undef LE_I32

            /* Remove processed message (and anything before it) from buffer */
            s_rx_index -= (i + total_msg_len);
            memmove(s_rx_buffer, &s_rx_buffer[i + total_msg_len], s_rx_index);

            if (pvt) *pvt = s_last_pvt;
            return 1;
        }
    }

    return 0;
}

uint8_t M10S_GetLastPVT(M10S_NavPVT *pvt)
{
    if (!s_initialized || s_last_pvt.timestamp == 0) return 0;
    if (pvt) *pvt = s_last_pvt;
    return 1;
}

uint8_t M10S_IsInitialized(void)
{
    return s_initialized;
}

uint16_t M10S_GetBufferFillLevel(void)
{
    return s_rx_index;
}

void M10S_GetDiagnostics(M10S_Diagnostics_t *diagnostics)
{
    if (diagnostics != NULL)
        *diagnostics = s_diagnostics;
}

void M10S_ClearBufferedData(void)
{
    s_rx_index = 0;
    memset(s_rx_buffer, 0, sizeof(s_rx_buffer));
}

void M10S_RequestPVT(I2C_HandleTypeDef *hi2c)
{
    /* Polling disabled - using NMEA streaming mode only */
    (void)hi2c;
}

