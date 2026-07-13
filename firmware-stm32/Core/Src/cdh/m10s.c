/**
 * @file m10s.c
 * @brief SparkFun u-blox GNSS (MAX-M10S) I2C Driver
 *
 * Complete port of the SparkFun u-blox GNSS Arduino Library to STM32 C.
 * Implements exact same behavior: buffering, parsing, validation, and data access.
 *
 * Key behaviors:
 * - begin(): Check I2C device, configure output protocols, enable NAV-PVT
 * - checkUblox(): Check bytes available, buffer incoming data, parse messages
 * - getPVT(): Search for complete UBX-NAV-PVT, validate, parse, return new messages only
 */

#include "cdh/m10s.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern UART_HandleTypeDef huart2;

/* ========================================================================== */
/* UBX Protocol Constants                                                    */
/* ========================================================================== */

#define UBX_SYNC_CHAR_1        0xB5
#define UBX_SYNC_CHAR_2        0x62

#define UBX_CLASS_NAV          0x01
#define UBX_CLASS_CFG          0x06

#define UBX_ID_NAV_PVT         0x07

#define UBX_ID_CFG_PRT         0x00  /* Port configuration */
#define UBX_ID_CFG_MSG         0x01  /* Message output rate */
#define UBX_ID_CFG_CFG         0x09  /* Save configuration */

/* NAV-PVT Payload is exactly 92 bytes */
#define UBX_NAV_PVT_PAYLOAD_LEN  92

/* I2C Registers for MAX-M10S */
#define M10S_I2C_REG_BYTES_AVAIL  0xFD  /* Number of bytes available to read */
#define M10S_I2C_REG_DATA_STREAM   0xFF  /* Data stream register */

/* Circular buffer size - must hold multiple complete messages */
#define M10S_BUFFER_SIZE           512

/* ========================================================================== */
/* Message Structure                                                         */
/* ========================================================================== */

/**
 * @brief Complete UBX message frame
 * Format: [0xB5][0x62][Class][ID][Length_L][Length_H][Payload...][CK_A][CK_B]
 */
typedef struct {
    uint8_t sync1;              /* 0xB5 */
    uint8_t sync2;              /* 0x62 */
    uint8_t msg_class;
    uint8_t msg_id;
    uint16_t payload_length;
    uint8_t payload[UBX_NAV_PVT_PAYLOAD_LEN];
    uint8_t checksum_a;
    uint8_t checksum_b;
} UBX_Message;

/* ========================================================================== */
/* Static Module Data                                                        */
/* ========================================================================== */

static uint8_t s_rx_buffer[M10S_BUFFER_SIZE];
static uint16_t s_rx_index = 0;

/* Last successfully parsed PVT data (internal copy) */
static M10S_NavPVT s_last_pvt = {0};

/* Initialization state */
static uint8_t s_initialized = 0;

/* ========================================================================== */
/* Checksum Calculation (UBX Protocol)                                       */
/* ========================================================================== */

/**
 * @brief Calculate UBX message checksum (Fletcher-16 variant)
 * @param data Pointer to message data (after sync bytes)
 * @param len Length of data to checksum (class + id + len_l + len_h + payload)
 * @param ck_a Output: first checksum byte
 * @param ck_b Output: second checksum byte
 */
static void M10S_CalculateChecksum(const uint8_t *data, uint16_t len,
                                   uint8_t *ck_a, uint8_t *ck_b)
{
    *ck_a = 0;
    *ck_b = 0;

    for (uint16_t i = 0; i < len; i++) {
        *ck_a += data[i];
        *ck_b += *ck_a;
    }
}

/**
 * @brief Verify UBX message checksum
 * @param data Pointer to message data (after sync bytes)
 * @param len Length of data to verify (class + id + len_l + len_h + payload)
 * @param ck_a Expected first checksum byte
 * @param ck_b Expected second checksum byte
 * @return 1 if valid, 0 if invalid
 */
static uint8_t M10S_VerifyChecksum(const uint8_t *data, uint16_t len,
                                   uint8_t ck_a, uint8_t ck_b)
{
    uint8_t calc_a = 0, calc_b = 0;
    M10S_CalculateChecksum(data, len, &calc_a, &calc_b);
    return (calc_a == ck_a) && (calc_b == ck_b);
}

/* ========================================================================== */
/* NAV-PVT Message Parsing                                                   */
/* ========================================================================== */

/**
 * @brief Parse UBX-NAV-PVT payload into M10S_NavPVT structure
 *
 * NAV-PVT Payload Structure (92 bytes):
 * Offset  Size   Name            Format    Description
 * ------  ----   ----            ------    -----------
 * 0-3     U4     iTOW            ms        GPS time of week
 * 4-7     I4     year            year      Year (UTC)
 * 8       U1     month           month     Month (1=Jan)
 * 9       U1     day             day       Day of month
 * 10      U1     hour            hour      Hour of day (0-23)
 * 11      U1     min             minute    Minute of hour (0-59)
 * 12      U1     sec             second    Seconds of minute (0-59)
 * 13      U1     valid           bitfield  Validity bits
 * 14-17   U4     tAcc            ns        Time accuracy estimate (ns)
 * 18-21   I4     nano            ns        Fraction of second (-1e9..1e9)
 * 22      U1     fixType         enum      GNSS fix type (0=None,1=2D,2=3D,3=DGPS,4=RTK-F,5=RTK-F)
 * 23      U1     flags           bitfield  Validity/correction flags [GNSS Fix OK = bit 0]
 * 24      U1     numSV           count     Number of satellites used in solution
 * 25-26   I2     reserved1       -         Reserved
 * 27-30   I4     lon             1e-7 deg  Longitude
 * 31-34   I4     lat             1e-7 deg  Latitude
 * 35-38   I4     height          mm        Height above ellipsoid
 * 39-42   I4     hMSL            mm        Height above mean sea level
 * 43-46   U4     hAcc            mm        Horizontal accuracy estimate
 * 47-50   U4     vAcc            mm        Vertical accuracy estimate
 * 51-54   I4     velN            mm/s      NED velocity North component
 * 55-58   I4     velE            mm/s      NED velocity East component
 * 59-62   I4     velD            mm/s      NED velocity Down component
 * 63-66   I4     gSpeed          mm/s      Ground speed (2D)
 * 67-70   I4     heading         1e-5 deg  Heading of motion (2D)
 * 71-74   U4     sAcc            mm/s      Speed accuracy estimate
 * 75-78   U4     headingAcc      1e-5 deg  Heading accuracy estimate (both 2D and 3D)
 * 79-80   U2     pDOP            1e-2      Position dilution of precision
 * 81      U1     reserved2       -         Reserved
 * 82      U1     reserved3       -         Reserved
 * 83-86   I4     headVeh         1e-5 deg  Heading of vehicle (from gyroscope)
 * 87-88   I2     magDec          1e-2 deg  Magnetic declination
 * 89-90   U2     magAcc          1e-2 deg  Magnetic declination accuracy
 * 91      U1     reserved4       -         Reserved
 *
 * @param payload Pointer to 92-byte NAV-PVT payload
 * @param len Length of payload (must be 92)
 * @param pvt Pointer to output structure
 * @return 1 if data is valid and parsed successfully, 0 otherwise
 */
static uint8_t M10S_ParseNAVPVT(const uint8_t *payload, uint16_t len, M10S_NavPVT *pvt)
{
    /* Validate payload length */
    if (len < UBX_NAV_PVT_PAYLOAD_LEN)
        return 0;

    /* Extract fix type and flags */
    uint8_t fix_type = payload[22];
    uint8_t flags = payload[23];
    uint8_t num_sv = payload[24];

    /* Extract raw coordinate values (little-endian) */
    int32_t lon_raw = (payload[27]) |
                      (payload[28] << 8) |
                      (payload[29] << 16) |
                      (payload[30] << 24);

    int32_t lat_raw = (payload[31]) |
                      (payload[32] << 8) |
                      (payload[33] << 16) |
                      (payload[34] << 24);

    int32_t height_raw = (payload[35]) |
                         (payload[36] << 8) |
                         (payload[37] << 16) |
                         (payload[38] << 24);

    int32_t gspeed_raw = (payload[63]) |
                         (payload[64] << 8) |
                         (payload[65] << 16) |
                         (payload[66] << 24);

    /* Extract velocity components (NED - North/East/Down) */
    int32_t vel_north_raw = (payload[51]) |
                            (payload[52] << 8) |
                            (payload[53] << 16) |
                            (payload[54] << 24);

    int32_t vel_east_raw = (payload[55]) |
                           (payload[56] << 8) |
                           (payload[57] << 16) |
                           (payload[58] << 24);

    int32_t vel_down_raw = (payload[59]) |
                           (payload[60] << 8) |
                           (payload[61] << 16) |
                           (payload[62] << 24);

    /* Extract heading of motion */
    int32_t heading_raw = (payload[67]) |
                          (payload[68] << 8) |
                          (payload[69] << 16) |
                          (payload[70] << 24);

    /* Extract validity flags */
    uint8_t gnss_fix_ok = (flags & 0x01);  /* Bit 0: GNSS fix valid */

    /* Validate data quality */
    if (!gnss_fix_ok || fix_type == 0 || num_sv == 0)
        return 0;

    /* Reject invalid/placeholder values */
    if (lat_raw == 0x7FFFFFFF || lon_raw == 0x7FFFFFFF)
        return 0;

    /* Convert and store data
     * LAT/LON: divide by 1e7 to convert from 1e-7 deg to degrees
     * Height: divide by 1000 to convert from mm to meters
     * Speed: divide by 1000 to convert from mm/s to m/s
     * VVel: velD is NED Down (mm/s); negate for datapool's positive-up convention
     * Velocity: divide by 1000 to convert from mm/s to m/s
     * Heading: divide by 1e5 to convert from 1e-5 deg to degrees
     */
    pvt->latitude = lat_raw / 10000000.0;
    pvt->longitude = lon_raw / 10000000.0;
    pvt->altitude = height_raw / 1000.0;
    pvt->speed = gspeed_raw / 1000.0;
    pvt->vel_north = vel_north_raw / 1000.0;
    pvt->vel_east = vel_east_raw / 1000.0;
    pvt->vel_down = vel_down_raw / 1000.0;
    pvt->vvel = -pvt->vel_down;
    pvt->heading = heading_raw / 100000.0;
    pvt->fix_type = fix_type;
    pvt->num_satellites = num_sv;
    pvt->timestamp = HAL_GetTick();

    return 1;
}

/* ========================================================================== */
/* I2C Communication                                                         */
/* ========================================================================== */

/**
 * @brief Check if MAX-M10S device is present at I2C address 0x42
 * @param hi2c I2C handle
 * @return 1 if device responds, 0 if not
 */
static uint8_t M10S_CheckPresence(I2C_HandleTypeDef *hi2c)
{
    uint8_t dummy = 0;

    /* Try to read 1 byte from the device */
    HAL_StatusTypeDef status = HAL_I2C_Master_Receive(hi2c,
                                                       (M10S_I2C_ADDR << 1),
                                                       &dummy, 1, 100);

    return (status == HAL_OK) ? 1 : 0;
}

/**
 * @brief Send UBX configuration command via I2C
 * @param hi2c I2C handle
 * @param msg Pointer to complete UBX message (including sync and checksum)
 * @param msg_len Total message length
 * @return 1 if sent successfully, 0 otherwise
 */
static uint8_t M10S_SendCommand(I2C_HandleTypeDef *hi2c,
                                const uint8_t *msg, uint16_t msg_len)
{
    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(hi2c,
                                                        (M10S_I2C_ADDR << 1),
                                                        (uint8_t*)msg,
                                                        msg_len,
                                                        500);
    return (status == HAL_OK) ? 1 : 0;
}

/**
 * @brief Read bytes available at I2C register 0xFD
 * @param hi2c I2C handle
 * @param bytes_avail Pointer to store available byte count
 * @return 1 if read successfully, 0 if I2C error
 */
static uint8_t M10S_GetBytesAvailable(I2C_HandleTypeDef *hi2c, uint8_t *bytes_avail)
{
    uint8_t reg = M10S_I2C_REG_BYTES_AVAIL;

    /* Read the bytes available register */
    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(hi2c,
                                                        (M10S_I2C_ADDR << 1),
                                                        &reg, 1, 50);
    if (status != HAL_OK)
        return 0;

    status = HAL_I2C_Master_Receive(hi2c,
                                     (M10S_I2C_ADDR << 1),
                                     bytes_avail, 1, 50);

    return (status == HAL_OK) ? 1 : 0;
}

/**
 * @brief Read data from I2C data stream register
 * @param hi2c I2C handle
 * @param buffer Pointer to buffer
 * @param len Number of bytes to read
 * @return 1 if read successfully, 0 if I2C error
 */
static uint8_t M10S_ReadDataStream(I2C_HandleTypeDef *hi2c, uint8_t *buffer, uint16_t len)
{
    uint8_t reg = M10S_I2C_REG_DATA_STREAM;

    /* Write register address */
    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(hi2c,
                                                        (M10S_I2C_ADDR << 1),
                                                        &reg, 1, 50);
    if (status != HAL_OK)
        return 0;

    /* Read data from that register */
    status = HAL_I2C_Master_Receive(hi2c,
                                     (M10S_I2C_ADDR << 1),
                                     buffer, len, 50);

    return (status == HAL_OK) ? 1 : 0;
}

/* ========================================================================== */
/* Buffer Management                                                         */
/* ========================================================================== */

/**
 * @brief Clear receive buffer
 */
static void M10S_ClearBuffer(void)
{
    memset(s_rx_buffer, 0, M10S_BUFFER_SIZE);
    s_rx_index = 0;
}

/**
 * @brief Add bytes to circular buffer
 * @param data Pointer to data
 * @param len Number of bytes to add
 * @return Number of bytes actually added (may be less if buffer full)
 */
static uint16_t M10S_BufferAdd(const uint8_t *data, uint16_t len)
{
    uint16_t bytes_added = 0;

    for (uint16_t i = 0; i < len; i++) {
        if (s_rx_index < M10S_BUFFER_SIZE) {
            s_rx_buffer[s_rx_index++] = data[i];
            bytes_added++;
        } else {
            /* Buffer full - discard oldest data */
            memmove(&s_rx_buffer[0], &s_rx_buffer[1], M10S_BUFFER_SIZE - 1);
            s_rx_buffer[M10S_BUFFER_SIZE - 1] = data[i];
        }
    }

    return bytes_added;
}

/**
 * @brief Remove n bytes from front of buffer
 * @param n Number of bytes to remove
 */
static void M10S_BufferRemoveFront(uint16_t n)
{
    if (n >= s_rx_index) {
        M10S_ClearBuffer();
        return;
    }

    memmove(&s_rx_buffer[0], &s_rx_buffer[n], s_rx_index - n);
    s_rx_index -= n;
}

/* ========================================================================== */
/* Public API - Initialization                                               */
/* ========================================================================== */

/**
 * @brief Initialize MAX-M10S GPS module
 *
 * Performs the following:
 * 1. Checks if device responds at I2C address 0x42
 * 2. Sends UBX-CFG-PRT to configure I2C port (enable UBX and NMEA output)
 * 3. Sends UBX-CFG-MSG to enable NAV-PVT messages at 1 Hz
 * 4. Sends UBX-CFG-CFG to save configuration to flash
 * 5. Clears receive buffer and waits for data
 *
 * @param hi2c I2C handle
 * @return 1 if initialization successful, 0 if device not found or init failed
 */
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
    HAL_UART_Transmit(&huart2, (uint8_t*)dbg, len, 100);
    HAL_Delay(500);

    /* Step 0: Check device presence */
    if (!M10S_CheckPresence(hi2c)) {
        len = snprintf(dbg, sizeof(dbg), "[M10S] ERROR: Device not found at I2C 0x42\r\n");
        HAL_UART_Transmit(&huart2, (uint8_t*)dbg, len, 100);
        return 0;
    }

    len = snprintf(dbg, sizeof(dbg), "[M10S] Device found at I2C 0x42\r\n");
    HAL_UART_Transmit(&huart2, (uint8_t*)dbg, len, 100);

    /* Step 1: Configure I2C port to output only UBX (like SparkFun library)
     * UBX-CFG-PRT (0x06 0x00)
     * Structure:
     *   Port ID: 0 (I2C)
     *   Reserved: 0
     *   TX Ready: 0
     *   In proto: 1 (UBX only)
     *   Out proto: 1 (UBX only)
     *   Flags: 0
     */
    uint8_t cfg_prt[] = {
        0xB5, 0x62,           /* UBX Sync */
        0x06, 0x00,           /* CFG-PRT class/id */
        0x14, 0x00,           /* Length = 20 bytes (little-endian) */
        0x00,                 /* Port ID = 0 (I2C) */
        0x00,                 /* Reserved */
        0x00, 0x00,           /* Reserved */
        0x00, 0x00,           /* TX Ready = 0 */
        0x01, 0x00,           /* In proto: UBX only (1) */
        0x01, 0x00,           /* Out proto: UBX only (1) */
        0x00, 0x00,           /* Flags = 0 */
        0x00, 0x00,           /* Reserved */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* Reserved */
        0x00, 0x00            /* Checksum placeholder */
    };

    uint8_t ck_a = 0, ck_b = 0;
    M10S_CalculateChecksum(&cfg_prt[2], 18, &ck_a, &ck_b);
    cfg_prt[20] = ck_a;
    cfg_prt[21] = ck_b;

    if (!M10S_SendCommand(hi2c, cfg_prt, sizeof(cfg_prt))) {
        len = snprintf(dbg, sizeof(dbg), "[M10S] ERROR: Failed to send CFG-PRT\r\n");
        HAL_UART_Transmit(&huart2, (uint8_t*)dbg, len, 100);
        return 0;
    }

    len = snprintf(dbg, sizeof(dbg), "[M10S] CFG-PRT sent (CK=%02X %02X)\r\n", ck_a, ck_b);
    HAL_UART_Transmit(&huart2, (uint8_t*)dbg, len, 100);
    HAL_Delay(300);

    /* Step 2: Enable NAV-PVT messages at 1 Hz
     * UBX-CFG-MSG (0x06 0x01)
     * Structure:
     *   Class: 0x01 (NAV)
     *   ID: 0x07 (PVT)
     *   Rate: 1 (every fix)
     */
    uint8_t cfg_msg_pvt[] = {
        0xB5, 0x62,           /* UBX Sync */
        0x06, 0x01,           /* CFG-MSG class/id */
        0x03, 0x00,           /* Length = 3 bytes */
        0x01, 0x07,           /* NAV-PVT (Class 0x01, ID 0x07) */
        0x01,                 /* Rate = 1 (every fix) */
        0x00, 0x00            /* Checksum placeholder */
    };

    ck_a = 0;
    ck_b = 0;
    M10S_CalculateChecksum(&cfg_msg_pvt[2], 5, &ck_a, &ck_b);
    cfg_msg_pvt[7] = ck_a;
    cfg_msg_pvt[8] = ck_b;

    if (!M10S_SendCommand(hi2c, cfg_msg_pvt, sizeof(cfg_msg_pvt))) {
        len = snprintf(dbg, sizeof(dbg), "[M10S] ERROR: Failed to send CFG-MSG (NAV-PVT)\r\n");
        HAL_UART_Transmit(&huart2, (uint8_t*)dbg, len, 100);
        return 0;
    }

    len = snprintf(dbg, sizeof(dbg), "[M10S] CFG-MSG (NAV-PVT) sent (CK=%02X %02X)\r\n", ck_a, ck_b);
    HAL_UART_Transmit(&huart2, (uint8_t*)dbg, len, 100);
    HAL_Delay(300);

    /* Step 3: Save configuration to flash
     * UBX-CFG-CFG (0x06 0x09)
     * Structure:
     *   Clear mask: 0x1F (all sections)
     *   Save mask: 0x1F (all sections)
     *   Load mask: 0x1F (all sections)
     */
    uint8_t cfg_save[] = {
        0xB5, 0x62,           /* UBX Sync */
        0x06, 0x09,           /* CFG-CFG class/id */
        0x0D, 0x00,           /* Length = 13 bytes */
        0x1F, 0x1F, 0x00, 0x00,  /* Clear mask = all sections */
        0x1F, 0x1F, 0x00, 0x00,  /* Save mask = all sections */
        0x1F, 0x1F, 0x00, 0x00,  /* Load mask = all sections */
        0x00,                    /* Reserved */
        0x00, 0x00            /* Checksum placeholder */
    };

    ck_a = 0;
    ck_b = 0;
    M10S_CalculateChecksum(&cfg_save[2], 15, &ck_a, &ck_b);
    cfg_save[17] = ck_a;
    cfg_save[18] = ck_b;

    if (!M10S_SendCommand(hi2c, cfg_save, sizeof(cfg_save))) {
        len = snprintf(dbg, sizeof(dbg), "[M10S] ERROR: Failed to send CFG-CFG (SAVE)\r\n");
        HAL_UART_Transmit(&huart2, (uint8_t*)dbg, len, 100);
        return 0;
    }

    len = snprintf(dbg, sizeof(dbg), "[M10S] CFG-CFG (SAVE) sent (CK=%02X %02X)\r\n", ck_a, ck_b);
    HAL_UART_Transmit(&huart2, (uint8_t*)dbg, len, 100);
    HAL_Delay(300);

    /* Clear buffer and mark as initialized */
    M10S_ClearBuffer();
    s_initialized = 1;

    len = snprintf(dbg, sizeof(dbg), "[M10S] Initialization complete. Waiting for NAV-PVT data...\r\n");
    HAL_UART_Transmit(&huart2, (uint8_t*)dbg, len, 100);

    return 1;
}

/* ========================================================================== */
/* Public API - Data Reading                                                 */
/* ========================================================================== */

/**
 * @brief Request new PVT data from GPS (poll mode)
 *
 * Sends a UBX-NAV-PVT poll request to the GPS module.
 * The GPS will respond with a new NAV-PVT message containing current position/velocity/time data.
 *
 * @param hi2c I2C handle
 */
void M10S_RequestPVT(I2C_HandleTypeDef *hi2c)
{
    if (!s_initialized)
        return;

    /* UBX-NAV-PVT Poll request: no payload, just the header */
    uint8_t request[] = {
        0xB5, 0x62,           /* UBX Sync bytes */
        0x01, 0x07,           /* Class NAV, ID PVT */
        0x00, 0x00,           /* Length = 0 (no payload) */
        0x00, 0x00            /* Checksum placeholder */
    };

    /* Calculate checksum */
    uint8_t ck_a = 0, ck_b = 0;
    M10S_CalculateChecksum(&request[2], 4, &ck_a, &ck_b);
    request[6] = ck_a;
    request[7] = ck_b;

    /* Send the poll request */
    HAL_I2C_Master_Transmit(hi2c, (M10S_I2C_ADDR << 1), request, sizeof(request), 100);
}

/**
 * @brief Check for available data and buffer it
 *
 * This function must be called frequently (e.g., every 10-100 ms) to maintain
 * responsiveness. It:
 * 1. Reads bytes available from register 0xFD (only if not 0xFF)
 * 2. If bytes available, reads data from register 0xFF
 * 3. Adds received bytes to the circular buffer
 * 4. Does NOT parse messages (that's done by M10S_Read)
 *
 * @param hi2c I2C handle
 * @return Number of bytes added to buffer (0 if nothing received or error)
 */
uint16_t M10S_CheckUblox(I2C_HandleTypeDef *hi2c)
{
    if (!s_initialized)
        return 0;

    uint8_t bytes_avail = 0;

    /* Read how many bytes are available */
    if (!M10S_GetBytesAvailable(hi2c, &bytes_avail))
        return 0;

    /* Sanity checks:
     * - 0x00: No data available
     * - 0xFF: Register read error or garbage - ignore
     * - > 127: Limit to avoid huge reads
     */
    if (bytes_avail == 0 || bytes_avail == 0xFF)
        return 0;

    if (bytes_avail > 127)
        bytes_avail = 127;

    /* Read the data */
    uint8_t chunk[128];
    if (!M10S_ReadDataStream(hi2c, chunk, bytes_avail))
        return 0;

    /* Add to buffer */
    uint16_t bytes_added = M10S_BufferAdd(chunk, bytes_avail);

    return bytes_added;
}

/* ========================================================================== */
/* NMEA Parsing (Fallback)                                                   */
/* ========================================================================== */

static uint8_t M10S_ParseNMEA_GGA(const uint8_t *sentence, uint16_t len, M10S_NavPVT *pvt)
{
    if (len < 30)
        return 0;

    /* Parse: $GPGGA,hhmmss.ss,llll.lllll,a,yyyyy.yyyyy,a,x,xx,x.x,x.x,M,x.x,M,,*hh */
    char str[128];
    memcpy(str, sentence, len < 127 ? len : 127);
    str[len < 127 ? len : 127] = '\0';

    char *fields[15];
    int field_count = 0;

    char *p = str;
    for (int i = 0; i < 15 && p; i++) {
        fields[i] = p;
        p = strchr(p, ',');
        if (p) {
            *p = '\0';
            p++;
            field_count++;
        }
    }

    if (field_count < 10)
        return 0;

    int fix_quality = atoi(fields[6]);
    if (fix_quality == 0)
        return 0;

    /* NMEA: ddmm.mmmmm format - convert to decimal degrees */
    double lat_raw = atof(fields[2]);
    double lon_raw = atof(fields[4]);

    int lat_deg = (int)(lat_raw / 100.0);
    double lat_min = lat_raw - (lat_deg * 100.0);
    double lat = lat_deg + (lat_min / 60.0);

    int lon_deg = (int)(lon_raw / 100.0);
    double lon_min = lon_raw - (lon_deg * 100.0);
    double lon = lon_deg + (lon_min / 60.0);

    double alt = atof(fields[9]);
    int num_sats = atoi(fields[7]);

    if (fields[3][0] == 'S')
        lat = -lat;
    if (fields[5][0] == 'W')
        lon = -lon;

    pvt->latitude = lat;
    pvt->longitude = lon;
    pvt->altitude = (int32_t)alt;
    pvt->num_satellites = num_sats;
    pvt->fix_type = (fix_quality == 2) ? 2 : 1;
    pvt->speed = 0;
    pvt->vvel = 0;   /* NMEA GGA carries no vertical velocity */
    pvt->vel_north = 0;
    pvt->vel_east = 0;
    pvt->vel_down = 0;
    pvt->heading = 0;
    pvt->timestamp = HAL_GetTick();

    return 1;
}

static uint8_t M10S_ParseNMEA_RMC(const uint8_t *sentence, uint16_t len, M10S_NavPVT *pvt)
{
    if (len < 30)
        return 0;

    /* Parse: $GPRMC,hhmmss.ss,A,llll.lllll,a,yyyyy.yyyyy,a,x.x,x.x,ddmmyy,,*hh */
    char str[128];
    memcpy(str, sentence, len < 127 ? len : 127);
    str[len < 127 ? len : 127] = '\0';

    char *fields[15];
    int field_count = 0;

    char *p = str;
    for (int i = 0; i < 15 && p; i++) {
        fields[i] = p;
        p = strchr(p, ',');
        if (p) {
            *p = '\0';
            p++;
            field_count++;
        }
    }

    if (field_count < 8)
        return 0;

    if (fields[2][0] != 'A')
        return 0;

    /* NMEA: ddmm.mmmmm format - convert to decimal degrees */
    double lat_raw = atof(fields[3]);
    double lon_raw = atof(fields[5]);

    int lat_deg = (int)(lat_raw / 100.0);
    double lat_min = lat_raw - (lat_deg * 100.0);
    double lat = lat_deg + (lat_min / 60.0);

    int lon_deg = (int)(lon_raw / 100.0);
    double lon_min = lon_raw - (lon_deg * 100.0);
    double lon = lon_deg + (lon_min / 60.0);

    double speed = atof(fields[7]);

    if (fields[4][0] == 'S')
        lat = -lat;
    if (fields[6][0] == 'W')
        lon = -lon;

    pvt->latitude = lat;
    pvt->longitude = lon;
    pvt->speed = speed;
    pvt->fix_type = 1;
    pvt->vvel = 0;   /* NMEA RMC carries no vertical velocity */
    pvt->vel_north = 0;
    pvt->vel_east = 0;
    pvt->vel_down = 0;
    pvt->heading = 0;
    pvt->timestamp = HAL_GetTick();

    return 1;
}

/**
 * @brief Search buffer for complete UBX-NAV-PVT message and parse it
 *
 * This function implements the exact behavior of SparkFun library's getPVT():
 * 1. Searches buffer for UBX sync characters (0xB5 0x62)
 * 2. Validates class/id (0x01 0x07 for NAV-PVT)
 * 3. Validates payload length (92 bytes)
 * 4. Checks if complete message is in buffer
 * 5. Verifies checksum
 * 6. Parses payload
 * 7. Stores in pvt structure
 * 8. Removes processed message from buffer
 * 9. Returns 1 ONLY if NEW complete message was parsed
 * 10. Returns 0 if no new message or error
 *
 * @param hi2c I2C handle (not used for parsing, kept for API compatibility)
 * @param pvt Pointer to M10S_NavPVT structure to receive parsed data
 * @return 1 if NEW complete NAV-PVT message was found and parsed, 0 otherwise
 */
uint8_t M10S_Read(I2C_HandleTypeDef *hi2c, M10S_NavPVT *pvt)
{
    (void)hi2c;  /* Parameter not used */

    if (!s_initialized || !pvt)
        return 0;

    /* Need at least: sync(2) + class(1) + id(1) + len(2) + ck(2) = 8 bytes minimum */
    if (s_rx_index < 8)
        return 0;

    /* Search for sync character pair in buffer */
    for (uint16_t i = 0; i < s_rx_index - 7; i++) {
        if (s_rx_buffer[i] == UBX_SYNC_CHAR_1 && s_rx_buffer[i+1] == UBX_SYNC_CHAR_2) {

            /* Extract message header */
            uint8_t msg_class = s_rx_buffer[i+2];
            uint8_t msg_id = s_rx_buffer[i+3];
            uint16_t msg_len = s_rx_buffer[i+4] | (s_rx_buffer[i+5] << 8);

            /* Check if this is a NAV-PVT message */
            if (msg_class != UBX_CLASS_NAV || msg_id != UBX_ID_NAV_PVT)
                continue;

            /* NAV-PVT payload must be exactly 92 bytes */
            if (msg_len != UBX_NAV_PVT_PAYLOAD_LEN)
                continue;

            /* Calculate total message size:
             * sync(2) + class(1) + id(1) + len(2) + payload(92) + checksum(2) = 100 bytes
             */
            uint16_t total_msg_size = 2 + 1 + 1 + 2 + msg_len + 2;

            /* Check if complete message is in buffer */
            if (i + total_msg_size > s_rx_index)
                return 0;  /* Incomplete message - wait for more data */

            /* Extract checksum bytes */
            uint8_t msg_ck_a = s_rx_buffer[i + 6 + msg_len];
            uint8_t msg_ck_b = s_rx_buffer[i + 6 + msg_len + 1];

            /* Verify checksum (data is from class byte to end of payload)
             * This is: class + id + len_l + len_h + payload
             */
            uint8_t ck_data_len = 2 + 2 + msg_len;  /* class + id + len + payload */
            if (!M10S_VerifyChecksum(&s_rx_buffer[i+2], ck_data_len, msg_ck_a, msg_ck_b)) {
                /* Checksum failed - skip this sync and continue searching */
                continue;
            }

            /* Extract payload (92 bytes after the 6-byte header) */
            uint8_t payload[UBX_NAV_PVT_PAYLOAD_LEN];
            memcpy(payload, &s_rx_buffer[i+6], UBX_NAV_PVT_PAYLOAD_LEN);

            /* Try to parse the payload */
            if (M10S_ParseNAVPVT(payload, UBX_NAV_PVT_PAYLOAD_LEN, pvt)) {
                /* Successfully parsed! Copy to internal storage and remove from buffer */
                memcpy(&s_last_pvt, pvt, sizeof(M10S_NavPVT));
                M10S_BufferRemoveFront(i + total_msg_size);
                return 1;  /* NEW message found and parsed */
            } else {
                /* Parse validation failed - skip this message and continue searching */
                continue;
            }
        }
    }

    /* FALLBACK: Try NMEA format ($GPGGA, $GPRMC) */
    for (uint16_t i = 0; i < s_rx_index; i++) {
        if (s_rx_buffer[i] == '$') {
            /* Found potential NMEA sentence start */
            uint16_t sent_len = 0;
            uint8_t sentence[128] = {0};

            /* Copy sentence until newline or end of buffer */
            uint16_t sent_end = i;
            for (uint16_t j = i; j < s_rx_index && sent_len < 127; j++) {
                sentence[sent_len++] = s_rx_buffer[j];
                sent_end = j;
                if (s_rx_buffer[j] == '\n')
                    break;
            }

            if (sent_len > 10) {  /* Minimum sentence length */
                /* Try to parse NMEA */
                if ((sentence[0] == '$') &&
                    ((strncmp((char*)sentence, "$GPGGA", 6) == 0 ||
                      strncmp((char*)sentence, "$GNGGA", 6) == 0))) {

                    /* Parse GGA sentence for position data */
                    if (M10S_ParseNMEA_GGA(sentence, sent_len, pvt)) {
                        memcpy(&s_last_pvt, pvt, sizeof(M10S_NavPVT));
                        M10S_BufferRemoveFront(sent_end + 1);
                        return 1;
                    }
                } else if ((sentence[0] == '$') &&
                           ((strncmp((char*)sentence, "$GPRMC", 6) == 0 ||
                             strncmp((char*)sentence, "$GNRMC", 6) == 0))) {

                    /* Parse RMC sentence for position/speed data */
                    if (M10S_ParseNMEA_RMC(sentence, sent_len, pvt)) {
                        memcpy(&s_last_pvt, pvt, sizeof(M10S_NavPVT));
                        M10S_BufferRemoveFront(sent_end + 1);
                        return 1;
                    }
                }
            }

            i += sent_len;
        }
    }

    return 0;  /* No new complete message found */
}

/**
 * @brief Get the last successfully parsed PVT data
 * @param pvt Pointer to M10S_NavPVT structure to receive data
 * @return 1 if data is available, 0 if no data has been parsed yet
 */
uint8_t M10S_GetLastPVT(M10S_NavPVT *pvt)
{
    if (!s_initialized || !pvt)
        return 0;

    if (s_last_pvt.timestamp == 0)
        return 0;  /* No data has been parsed yet */

    memcpy(pvt, &s_last_pvt, sizeof(M10S_NavPVT));
    return 1;
}

/**
 * @brief Check if GPS module is initialized
 * @return 1 if initialized, 0 otherwise
 */
uint8_t M10S_IsInitialized(void)
{
    return s_initialized;
}

/**
 * @brief Get current buffer fill level for debugging
 * @return Number of bytes currently in receive buffer
 */
uint16_t M10S_GetBufferFillLevel(void)
{
    return s_rx_index;
}

/**
 * @brief Clear all buffered data (for recovery/reset)
 */
void M10S_ClearBufferedData(void)
{
    M10S_ClearBuffer();
}
