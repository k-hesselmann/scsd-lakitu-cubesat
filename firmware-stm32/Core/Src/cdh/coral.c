/* ============================================================================
 * coral.c  --  Coral Dev Board Micro USART3 interface  (STM32 OBC side)
 *
 * Protocol reference: UART_PROTOCOL.md v1.0 (interface_docs/)
 * ============================================================================
 *
 * Wire format (Coral -> OBC image packet, 50196 bytes total):
 *
 *   Offset  Size  Field      Notes
 *   ------  ----  ---------  -------------------------------------------------
 *        0     2  SOF        {0xAA, 0x55}  -- NOT included in CRC
 *        2     1  TYPE       0x01
 *        3     4  SEQ        uint32 big-endian, starts at 0
 *        7     4  LEN        uint32 big-endian = 7 + W*H
 *       11     2  FRAC       uint16 big-endian, cloud fraction * 65535
 *       13     2  WIDTH      uint16 big-endian = 224
 *       15     2  HEIGHT     uint16 big-endian = 224
 *       17     1  FORMAT     0x01 = Y8
 *       18  W*H   PIXELS     raw uint8, row-major
 *   18+W*H     2  CRC16      uint16 big-endian, CRC over bytes 2..(18+W*H-1)
 *                            (TYPE through last pixel; SOF and CRC excluded)
 *
 * CRC-16/CCITT-FALSE: poly=0x1021, init=0xFFFF, no reflect, no XOR-out.
 *
 * Commands (OBC -> Coral), no SOF prefix:
 *   TRIGGER (3 bytes):      {0x10, crc_hi, crc_lo}
 *   SET_INTERVAL (7 bytes): {0x11, ms_b3, ms_b2, ms_b1, ms_b0, crc_hi, crc_lo}
 *   SLEEP (3 bytes):        {0x17, crc_hi, crc_lo}   suspend inference
 *   WAKE  (3 bytes):        {0x18, crc_hi, crc_lo}   resume inference
 *   CRC covers all bytes before the CRC field.
 *
 * SD output:  F<SEQ8>.RAW  -- raw 224x224 Y8 grayscale, no header.
 *
 * DEBUG OUTPUT
 * ------------
 *  All [CORAL] lines sent over USART2 (ST-LINK virtual COM, 115200 baud).
 *  Open a terminal at 115200 8N1 on the ST-LINK COM port to see them.
 * ========================================================================== */

#include "cdh/coral.h"
#include "main.h"        /* huart3, huart2, Error_Handler */
#include "fatfs.h"       /* f_open, f_write, f_close, f_unlink, FIL, FR_OK */
#include <stdio.h>       /* snprintf */
#include <string.h>      /* strlen */

/* ---- External handles (declared in main.c) ------------------------------ */
/* Coral wired to USART3: PC10=TX (OBC->Coral cmds), PC11=RX (Coral->OBC data),
 * AF7, 115200 8N1. PC10 is open-drain + external 2.2k pull-up to the Coral
 * 1.8 V rail (3.3 V/1.8 V level shift) -- see interface_docs/UART_PROTOCOL.md 1.1.
 * Pins configured by MX_USART3_UART_Init() / HAL_UART_MspInit().  */
extern UART_HandleTypeDef huart3;   /* Coral UART (USART3)                   */
extern UART_HandleTypeDef huart2;   /* debug console (USART2, ST-LINK)       */

#define CORAL_HUART  huart3

/* ---- Debug helpers ------------------------------------------------------- */
static void dbg(const char *msg)
{
    HAL_UART_Transmit(&huart2, (const uint8_t *)msg,
                      (uint16_t)strlen(msg), 100U);
}

static void dbg_hex(const char *label, uint8_t val)
{
    const char hex[] = "0123456789ABCDEF";
    char buf[48];
    uint8_t i = 0;
    while (label[i]) { buf[i] = label[i]; i++; }
    buf[i++] = '0'; buf[i++] = 'x';
    buf[i++] = hex[(val >> 4) & 0xFU];
    buf[i++] = hex[val & 0xFU];
    buf[i++] = '\r'; buf[i++] = '\n'; buf[i] = '\0';
    dbg(buf);
}

static void coral_clear_uart_errors(void)
{
    uint32_t flags = CORAL_HUART.Instance->ISR;
    uint32_t clear = 0U;

    if (flags & USART_ISR_ORE) { clear |= UART_CLEAR_OREF; }
    if (flags & USART_ISR_FE)  { clear |= UART_CLEAR_FEF; }
    if (flags & USART_ISR_NE)  { clear |= UART_CLEAR_NEF; }
    if (flags & USART_ISR_PE)  { clear |= UART_CLEAR_PEF; }

    if (clear != 0U)
    {
        __HAL_UART_CLEAR_FLAG(&CORAL_HUART, clear);
        CORAL_HUART.ErrorCode = HAL_UART_ERROR_NONE;
    }
}

/* ---- Module state ------------------------------------------------------- */
static uint8_t  s_uart_ready  = 0;
static uint16_t s_frame_count = 0;

#define CORAL_RX_RING_SIZE 16384U
#define CORAL_RX_RING_MASK (CORAL_RX_RING_SIZE - 1U)

static uint8_t s_rx_irq_byte;
static volatile uint8_t s_rx_ring[CORAL_RX_RING_SIZE];
static volatile uint16_t s_rx_head = 0U;
static volatile uint16_t s_rx_tail = 0U;

/* Live-expression debug counters (add to STM32CubeIDE Live Expressions) */
volatile uint32_t coral_sof_count     = 0;  /* incremented on each confirmed SOF   */
volatile uint32_t coral_good_frames   = 0;  /* incremented on CRC-OK frame         */
volatile uint32_t coral_timeout_count = 0;  /* UART timeout errors                 */
volatile uint32_t coral_crc_err_count = 0;  /* CRC mismatch count                  */
volatile uint32_t coral_sd_err_count  = 0;  /* SD write error count                */
volatile uint32_t coral_rx_overflow_count = 0; /* RX ring overflow count             */
volatile uint8_t  coral_last_status   = 0;  /* STATUS byte of last frame           */
volatile uint8_t  coral_last_fraction = 0;  /* cloud fraction % of last frame      */

/* 512-byte chunk buffer -- frame is streamed, never fully buffered in RAM */
static uint8_t s_chunk[512];

/* ---- Timing parameters -------------------------------------------------- */
/* At 115200 baud a byte takes ~87 us.  512 bytes ~ 44 ms. */
#define SOF1_TIMEOUT_MS    50U   /* 0x55 must follow 0xAA within 50 ms        */
#define HDR_TIMEOUT_MS    500U   /* 16-byte header                             */
#define CHUNK_TIMEOUT_MS  200U   /* 512-byte chunk (44 ms + 4.5x margin)       */
#define CRC_TIMEOUT_MS    100U   /* 2-byte CRC trailer                         */

static void coral_rx_ring_reset(void)
{
    __disable_irq();
    s_rx_head = 0U;
    s_rx_tail = 0U;
    __enable_irq();
}

static void coral_rx_ring_push(uint8_t byte)
{
    uint16_t next = (uint16_t)((s_rx_head + 1U) & CORAL_RX_RING_MASK);

    if (next == s_rx_tail)
    {
        s_rx_tail = (uint16_t)((s_rx_tail + 1U) & CORAL_RX_RING_MASK);
        coral_rx_overflow_count++;
    }

    s_rx_ring[s_rx_head] = byte;
    s_rx_head = next;
}

static uint8_t coral_rx_ring_pop(uint8_t *byte)
{
    if (s_rx_tail == s_rx_head)
        return 0U;

    *byte = s_rx_ring[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1U) & CORAL_RX_RING_MASK);
    return 1U;
}

static void coral_rx_start_it(void)
{
    coral_clear_uart_errors();

    if (CORAL_HUART.RxState == HAL_UART_STATE_READY)
    {
        (void)HAL_UART_Receive_IT(&CORAL_HUART, &s_rx_irq_byte, 1U);
    }
}

static uint8_t coral_read_bytes(uint8_t *dst, uint16_t len, uint32_t timeout_ms)
{
    uint16_t pos = 0U;
    uint32_t start = HAL_GetTick();

    while (pos < len)
    {
        if (coral_rx_ring_pop(&dst[pos]))
        {
            pos++;
            continue;
        }

        if ((HAL_GetTick() - start) >= timeout_ms)
            return 0U;

        coral_rx_start_it();
    }

    return 1U;
}

/* ============================================================================
 * CRC-16/CCITT-FALSE  (poly=0x1021, init=0xFFFF, no reflect, no XOR-out)
 * ========================================================================== */
static uint16_t crc16_update(uint16_t crc, uint8_t byte)
{
    crc ^= (uint16_t)((uint16_t)byte << 8);
    for (int i = 0; i < 8; i++)
    {
        crc = (crc & 0x8000U)
              ? (uint16_t)((crc << 1) ^ 0x1021U)
              : (uint16_t)(crc << 1);
    }
    return crc;
}

static uint16_t crc16_buf(uint16_t crc, const uint8_t *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
        crc = crc16_update(crc, buf[i]);
    return crc;
}

/* ============================================================================
 * coral_receive_frame
 *   Called after SOF {0xAA,0x55} confirmed.  Blocks until the full packet
 *   is received or an error occurs.
 * ========================================================================== */
static uint8_t coral_receive_frame(SensorData_t *dp)
{
    uint8_t  status = 0;
    uint16_t crc    = 0xFFFFU;

    /* Start with coral_valid = 0; set to 1 only on clean frame */
    dp->coral_valid = 0U;

    /* ---- 1. Read 16-byte header ---------------------------------------- */
    /* Header layout (all fields big-endian):
     *   hdr[0]      TYPE    (1 byte)
     *   hdr[1..4]   SEQ     (4 bytes)
     *   hdr[5..8]   LEN     (4 bytes) = 7 + W*H
     *   hdr[9..10]  FRAC    (2 bytes)
     *   hdr[11..12] WIDTH   (2 bytes)
     *   hdr[13..14] HEIGHT  (2 bytes)
     *   hdr[15]     FORMAT  (1 byte)  */
    uint8_t hdr[16];
    if (!coral_read_bytes(hdr, 16U, HDR_TIMEOUT_MS))
    {
        dbg("[CORAL] !!! Header timeout\r\n");
        coral_timeout_count++;
        return CORAL_STATUS_TIMEOUT;
    }

    /* CRC starts at TYPE byte (hdr[0]); SOF is excluded from CRC */
    crc = crc16_buf(crc, hdr, 16U);

    /* Decode header -- big-endian */
    uint8_t  type   =  hdr[0];
    uint32_t seq    = ((uint32_t)hdr[1]  << 24) | ((uint32_t)hdr[2]  << 16)
                    | ((uint32_t)hdr[3]  <<  8) |  (uint32_t)hdr[4];
    /* hdr[5..8] = LEN -- not used; we derive pixel count from width*height */
    uint16_t frac   = ((uint16_t)hdr[9]  <<  8) |  hdr[10];
    uint16_t width  = ((uint16_t)hdr[11] <<  8) |  hdr[12];
    uint16_t height = ((uint16_t)hdr[13] <<  8) |  hdr[14];
    /* hdr[15] = FORMAT (0x01 = Y8) -- informational only */

    if (type != CORAL_TYPE_IMAGE)
    {
        dbg_hex("[CORAL] !!! Bad TYPE: ", type);
        coral_crc_err_count++;
        return CORAL_STATUS_CRC_ERR;
    }
    if (width != CORAL_IMAGE_W || height != CORAL_IMAGE_H)
    {
        dbg("[CORAL] !!! Unexpected image geometry\r\n");
        coral_crc_err_count++;
        return CORAL_STATUS_CRC_ERR;
    }

    {
        char buf[72];
        snprintf(buf, sizeof(buf),
                 "[CORAL] Header OK  SEQ=%lu  FRAC=%u (%u%%)\r\n",
                 (unsigned long)seq, (unsigned)frac,
                 (unsigned)((uint32_t)frac * 100UL / 65535UL));
        dbg(buf);
    }

    /* ---- 2. Open SD file ---------------------------------------------- */
    char   fname[16];
    snprintf(fname, sizeof(fname), "F%08lu.RAW", (unsigned long)seq);

    FIL     fframe;
    uint8_t sd_ok = 0;
    if (f_open(&fframe, fname, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
    {
        sd_ok = 1;
        dbg("[CORAL] SD file opened OK\r\n");
    }
    else
    {
        dbg("[CORAL] !!! SD f_open FAILED -- draining UART without saving\r\n");
    }

    /* ---- 3. Stream WIDTH*HEIGHT pixel bytes to SD ---------------------- */
    uint32_t remaining  = (uint32_t)width * height;  /* 50 176 */
    uint32_t chunk_num  = 0;

    while (remaining > 0U)
    {
        uint32_t take = (remaining > sizeof(s_chunk))
                        ? (uint32_t)sizeof(s_chunk) : remaining;

        if (!coral_read_bytes(s_chunk, (uint16_t)take, CHUNK_TIMEOUT_MS))
        {
            char buf[72];
            snprintf(buf, sizeof(buf),
                     "[CORAL] !!! Chunk %lu timeout (remaining=%lu)\r\n",
                     (unsigned long)chunk_num, (unsigned long)remaining);
            dbg(buf);
            if (sd_ok) { f_close(&fframe); }
            coral_timeout_count++;
            return CORAL_STATUS_TIMEOUT;
        }

        /* Accumulate CRC over pixel data */
        crc = crc16_buf(crc, s_chunk, take);

        if (sd_ok)
        {
            UINT written;
            if (f_write(&fframe, s_chunk, (UINT)take, &written) != FR_OK
                || written != (UINT)take)
            {
                dbg("[CORAL] !!! SD write error -- continuing UART drain\r\n");
                f_close(&fframe);
                sd_ok  = 0;
                status |= CORAL_STATUS_SD_ERR;
                coral_sd_err_count++;
            }
        }

        remaining -= take;
        chunk_num++;
    }

    if (sd_ok)
    {
        f_close(&fframe);
        dbg("[CORAL] Pixel stream done, SD file closed\r\n");
    }

    /* ---- 4. Read and verify 2-byte big-endian CRC16 ------------------- */
    uint8_t crc_bytes[2] = {0U, 0U};
    if (!coral_read_bytes(crc_bytes, 2U, CRC_TIMEOUT_MS))
    {
        dbg("[CORAL] !!! CRC bytes timeout\r\n");
        coral_timeout_count++;
        return (uint8_t)(status | CORAL_STATUS_TIMEOUT);
    }

    uint16_t rx_crc = ((uint16_t)crc_bytes[0] << 8) | crc_bytes[1];

    if (crc != rx_crc)
    {
        char buf[72];
        snprintf(buf, sizeof(buf),
                 "[CORAL] !!! CRC MISMATCH: computed=0x%04X received=0x%04X\r\n",
                 (unsigned)crc, (unsigned)rx_crc);
        dbg(buf);
        status |= CORAL_STATUS_CRC_ERR;
        coral_crc_err_count++;
        f_unlink(fname);   /* delete corrupt file */
    }
    else
    {
        char buf[56];
        snprintf(buf, sizeof(buf),
                 "[CORAL] CRC OK  frac=%u%%  file=%s\r\n",
                 (unsigned)((uint32_t)frac * 100UL / 65535UL), fname);
        dbg(buf);
    }

    /* ---- 5. Populate coral_block[16] ---------------------------------- */
    /*  [0..3]   SEQ        uint32 LE
     *  [4..5]   FRAC_RAW   uint16 LE
     *  [6]      FRAC_PCT   uint8  0-100
     *  [7]      STATUS     uint8
     *  [8..11]  RX_TICK    uint32 LE (ms)
     *  [12..13] FRAME_CNT  uint16 LE
     *  [14..15] reserved              */
    uint32_t now      = HAL_GetTick();
    uint8_t  frac_pct = (uint8_t)((uint32_t)frac * 100UL / 65535UL);

    dp->coral_block[0]  = (uint8_t)( seq         & 0xFFU);
    dp->coral_block[1]  = (uint8_t)((seq >>  8)  & 0xFFU);
    dp->coral_block[2]  = (uint8_t)((seq >> 16)  & 0xFFU);
    dp->coral_block[3]  = (uint8_t)((seq >> 24)  & 0xFFU);
    dp->coral_block[4]  = (uint8_t)( frac        & 0xFFU);
    dp->coral_block[5]  = (uint8_t)((frac >> 8)  & 0xFFU);
    dp->coral_block[6]  = frac_pct;
    dp->coral_block[7]  = status;
    dp->coral_block[8]  = (uint8_t)( now         & 0xFFU);
    dp->coral_block[9]  = (uint8_t)((now >>  8)  & 0xFFU);
    dp->coral_block[10] = (uint8_t)((now >> 16)  & 0xFFU);
    dp->coral_block[11] = (uint8_t)((now >> 24)  & 0xFFU);
    dp->coral_block[12] = (uint8_t)( s_frame_count        & 0xFFU);
    dp->coral_block[13] = (uint8_t)((s_frame_count >> 8)  & 0xFFU);
    dp->coral_block[14] = 0U;
    dp->coral_block[15] = 0U;

    coral_last_status   = status;
    coral_last_fraction = frac_pct;

    if (status == 0U)
    {
        s_frame_count++;
        coral_good_frames++;
        dp->coral_valid = 1U;
        dbg("[CORAL] ===== Frame GOOD =====\r\n");
    }
    else
    {
        dbg("[CORAL] ===== Frame ERROR (check status byte) =====\r\n");
    }

    return status;
}

/* ============================================================================
 * Public API
 * ========================================================================== */

void Coral_Init(void)
{
    /* huart3 must already be initialised by MX_USART3_UART_Init() in main.c. */
    s_uart_ready  = 1U;
    s_frame_count = 0U;
    coral_rx_ring_reset();

    dbg("\r\n[CORAL] ===== Init start =====\r\n");
    coral_rx_start_it();

    /* Brief pause so the Coral has time to boot before we send a command.    */
    HAL_Delay(200U);
    Coral_SendSetInterval(CORAL_DEFAULT_INTERVAL_MS);
    dbg("[CORAL] SET_INTERVAL 10000 ms sent\r\n");
}

void Coral_Update(SensorData_t *dp)
{
    if (!s_uart_ready)
    {
        dp->coral_block[7] = CORAL_STATUS_NO_UART;
        dbg("[CORAL] !!! UART not ready -- call Coral_Init() first\r\n");
        return;
    }

    coral_rx_start_it();

    uint8_t b0 = 0U;
    uint8_t scanned = 0U;

    while (scanned < 64U)
    {
        if (!coral_rx_ring_pop(&b0))
            return;

        scanned++;

        if (b0 == CORAL_SOF_0)
            break;
    }

    if (b0 != CORAL_SOF_0)
        return;

    /* Got 0xAA -- read 0x55 with short deadline */
    uint8_t b1 = 0U;
    if (!coral_read_bytes(&b1, 1U, SOF1_TIMEOUT_MS) || b1 != CORAL_SOF_1)
    {
        dbg("[CORAL] !!! SOF_1 (0x55) missing or wrong -- discarding\r\n");
        return;
    }

    coral_sof_count++;
    dbg("[CORAL] SOF 0xAA55 confirmed -- receiving frame...\r\n");

    coral_receive_frame(dp);
}

void Coral_SendTrigger(void)
{
    if (!s_uart_ready) { return; }

    /* TRIGGER = {CMD, CRC_hi, CRC_lo}   CRC covers byte 0 only */
    uint8_t  pkt[3];
    pkt[0] = CORAL_CMD_TRIGGER;
    uint16_t crc = crc16_update(0xFFFFU, pkt[0]);
    pkt[1] = (uint8_t)(crc >> 8);     /* big-endian CRC */
    pkt[2] = (uint8_t)(crc & 0xFFU);
    HAL_UART_Transmit(&CORAL_HUART, pkt, 3U, 100U);
    dbg("[CORAL] TRIGGER sent\r\n");
}

void Coral_SendSetInterval(uint32_t interval_ms)
{
    if (!s_uart_ready) { return; }

    /* SET_INTERVAL = {CMD, ms_b3, ms_b2, ms_b1, ms_b0, CRC_hi, CRC_lo}
     * interval_ms is a uint32 big-endian; CRC covers bytes 0-4.            */
    uint8_t pkt[7];
    pkt[0] = CORAL_CMD_SET_INTERVAL;
    pkt[1] = (uint8_t)(interval_ms >> 24);
    pkt[2] = (uint8_t)(interval_ms >> 16);
    pkt[3] = (uint8_t)(interval_ms >>  8);
    pkt[4] = (uint8_t)(interval_ms);
    uint16_t crc = 0xFFFFU;
    for (uint8_t i = 0U; i < 5U; i++) { crc = crc16_update(crc, pkt[i]); }
    pkt[5] = (uint8_t)(crc >> 8);
    pkt[6] = (uint8_t)(crc & 0xFFU);
    HAL_UART_Transmit(&CORAL_HUART, pkt, 7U, 100U);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != CORAL_HUART.Instance)
        return;

    coral_rx_ring_push(s_rx_irq_byte);
    (void)HAL_UART_Receive_IT(&CORAL_HUART, &s_rx_irq_byte, 1U);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != CORAL_HUART.Instance)
        return;

    coral_clear_uart_errors();
    (void)HAL_UART_Receive_IT(&CORAL_HUART, &s_rx_irq_byte, 1U);
}

/* SLEEP / WAKE share the 3-byte {CMD, CRC_hi, CRC_lo} framing of TRIGGER;
 * CRC covers byte 0 only. */
static void coral_send_simple_cmd(uint8_t cmd)
{
    if (!s_uart_ready) { return; }
    uint8_t  pkt[3];
    pkt[0] = cmd;
    uint16_t crc = crc16_update(0xFFFFU, pkt[0]);
    pkt[1] = (uint8_t)(crc >> 8);     /* big-endian CRC */
    pkt[2] = (uint8_t)(crc & 0xFFU);
    HAL_UART_Transmit(&CORAL_HUART, pkt, 3U, 100U);
}

void Coral_SendSleep(void)
{
    coral_send_simple_cmd(CORAL_CMD_SLEEP);
    dbg("[CORAL] SLEEP sent\r\n");
}

void Coral_SendWake(void)
{
    coral_send_simple_cmd(CORAL_CMD_WAKE);
    dbg("[CORAL] WAKE sent\r\n");
}
