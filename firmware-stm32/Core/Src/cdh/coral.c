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
/* Tick of the last byte pushed by the UART ISR, regardless of parse state --
 * used to tell whether the wire has actually been silent for a while, since
 * s_rx_state == CORAL_RX_IDLE alone doesn't rule out a byte already sitting
 * unread in the ring (see coral_try_preopen()/Coral_IsQuiescent()). */
static volatile uint32_t s_rx_last_byte_ms = 0U;

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
/* Minimum RX silence, on top of s_rx_state == CORAL_RX_IDLE, before treating
 * the link as quiet enough for a slow SD operation (this module's own
 * pre-open, or another module's deferred maintenance -- see
 * Coral_IsQuiescent()). IDLE alone isn't enough: a byte can already be
 * sitting unread in the ring the instant IDLE is checked, since Coral starts
 * a new frame on its own schedule, asynchronously to this loop. */
#define CORAL_QUIET_MS    100U
/* Clear coral_valid at the expected frame cadence; FDIR adds its 5 s invalid
 * debounce before setting the Coral equipment fault.
 *
 * CORAL_DEFAULT_INTERVAL_MS alone is NOT the result-to-result period: per
 * cloud_regressor.cc's main loop, that interval is slept AFTER capture +
 * inference + UART transfer + local backup finish, not on a fixed clock.
 * That work (dominated by the ~4.4 s blocking UART transfer of the 50 KB
 * frame) measures ~6.5-7.5 s, so completed frames are actually
 * ~16.5-17.5 s apart. A bare CORAL_DEFAULT_INTERVAL_MS timeout was firing a
 * false "Frame freshness timeout" almost every cycle. */
#define CORAL_WORK_BUDGET_MS       8000UL  /* capture+inference+transfer+backup, with margin */
#define CORAL_FRESHNESS_MARGIN_MS  2000UL  /* extra slack against jitter                     */
#define FRAME_STALE_TIMEOUT_MS \
    (CORAL_DEFAULT_INTERVAL_MS + CORAL_WORK_BUDGET_MS + CORAL_FRESHNESS_MARGIN_MS)

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
    s_rx_last_byte_ms = HAL_GetTick();
}

static uint8_t coral_rx_ring_pop(uint8_t *byte)
{
    if (s_rx_tail == s_rx_head)
        return 0U;

    *byte = s_rx_ring[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1U) & CORAL_RX_RING_MASK);
    return 1U;
}

/* Non-blocking: pops whatever is currently available, up to max_len, and
 * returns the count actually popped (0..max_len). Never waits. */
static uint16_t coral_rx_ring_pop_n(uint8_t *dst, uint16_t max_len)
{
    uint16_t n = 0U;
    while (n < max_len && coral_rx_ring_pop(&dst[n]))
        n++;
    return n;
}

static void coral_rx_start_it(void)
{
    coral_clear_uart_errors();

    if (CORAL_HUART.RxState == HAL_UART_STATE_READY)
    {
        (void)HAL_UART_Receive_IT(&CORAL_HUART, &s_rx_irq_byte, 1U);
    }
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
 * Frame-receive state machine
 *
 *   coral_receive_frame() used to be a single function that looped over
 *   every header byte, every 512-byte pixel chunk, and the CRC trailer,
 *   blocking until the ~4.4 s (wire) + SD-write frame finished. Because
 *   Coral_Update() called it synchronously from inside the main.c superloop,
 *   that one call stalled the *entire* loop -- GPS/IMU servicing, FDIR,
 *   TTC_Service(), and the IWDG kick all included -- for the frame's
 *   duration. The Coral has no per-chunk handshake (UART_PROTOCOL.md 3):
 *   once triggered it streams the whole frame unprompted, so pacing can't
 *   be borrowed from a request/response exchange either.
 *
 *   Below, the same parsing/validation/SD-write logic is kept, but spread
 *   over many calls: each state advances by whatever is already sitting in
 *   s_rx_ring (filled independently by HAL_UART_RxCpltCallback(), so wire
 *   bytes are never lost regardless of call cadence) and, for CORAL_RX_PIXELS,
 *   by at most one 512-byte chunk (one f_write) per call. A call that finds
 *   nothing new simply returns. Every state tracks the tick of its last
 *   forward progress so a silent link (Coral wedged/disconnected mid-frame)
 *   is caught by a stall timeout instead of hanging forever.
 * ========================================================================== */
typedef enum {
    CORAL_RX_IDLE = 0,  /* scanning s_rx_ring for SOF byte 0 (0xAA)   */
    CORAL_RX_SOF1,      /* got 0xAA, waiting for 0x55                 */
    CORAL_RX_HEADER,    /* accumulating the 16-byte header            */
    CORAL_RX_PIXELS,    /* streaming W*H pixel bytes to SD, one chunk per call */
    CORAL_RX_CRC,       /* accumulating the 2-byte CRC trailer        */
} coral_rx_state_t;

static coral_rx_state_t s_rx_state = CORAL_RX_IDLE;
static SensorData_t    *s_rx_dp;
static uint32_t         s_rx_last_progress_ms;

static uint8_t  s_hdr[16];
static uint16_t s_hdr_pos;
static uint16_t s_rx_crc;      /* running CRC-16 over TYPE..last pixel */

static uint32_t s_seq;
static uint16_t s_frac;
static uint8_t  s_frac_pct;   /* s_frac scaled to 0-100; also folds into s_fname */

static uint32_t s_pixels_remaining;
static uint16_t s_chunk_target;   /* bytes wanted in the in-flight chunk */
static uint16_t s_chunk_fill;     /* bytes accumulated so far            */
static uint32_t s_chunk_num;

static uint8_t  s_crc_trailer[2];
static uint16_t s_crc_pos;

static uint8_t  s_status;   /* accumulated CORAL_STATUS_* flags for this frame */
static uint8_t  s_sd_ok;
static FIL      s_fframe;
static char     s_fname[32];        /* this frame's final desired name       */
static char     s_open_name[32];    /* name actually bound to s_fframe right now */
static uint8_t  s_using_staged_name; /* s_open_name is CORAL_STAGING_NAME, needs rename on success */
static uint8_t  s_seen_good_frame;
static uint8_t  s_frame_stale_reported;
static uint32_t s_last_good_frame_ms;

/* Pre-opened SD file, readied ahead of the next frame -- see coral_try_preopen(). */
static const char CORAL_STAGING_NAME[] = "CORAL.STG";
static uint8_t   s_preopen_ok;
static uint32_t  s_last_preopen_attempt_ms;

/* Abort the in-progress frame: close/delete any partial SD file, publish
 * the given status, log why, and return the state machine to IDLE so the
 * next SOF (the following frame, ~10 s later at the default interval) is
 * picked up cleanly. Mirrors the timeout-count/status semantics of the
 * three timeout returns in the original blocking coral_receive_frame(). */
static void coral_rx_abort(uint8_t status, const char *reason)
{
    dbg(reason);

    if (s_sd_ok)
    {
        f_close(&s_fframe);
        f_unlink(s_open_name);
        s_sd_ok = 0U;
    }

    coral_timeout_count++;

    if (s_rx_dp != NULL)
    {
        s_rx_dp->coral_block[7] = status;
    }
    coral_last_status = status;

    s_rx_dp    = NULL;
    s_rx_state = CORAL_RX_IDLE;
}

/* Opens a fresh SD file under a fixed staging name ahead of the next frame
 * and leaves it open. On this project's bench SD card, f_open() with
 * FA_CREATE_ALWAYS has been observed taking 2+ seconds (directory-slot and
 * first-cluster allocation) -- called synchronously from
 * coral_header_complete() as before, that stalls the main loop for longer
 * than the 16 KB RX ring takes to overflow (~1.4 s), on every single frame.
 * Doing it here instead, while CORAL_RX_IDLE (no frame in flight, so a
 * multi-second stall costs nothing), means coral_header_complete() usually
 * finds the file already open and pays no f_open() cost on the time-critical
 * path at all. Left open (not closed) so there is nothing left to redo when
 * it's claimed. Reused under the same name every time, so a slow/failed
 * attempt is retried (rate-limited, see s_last_preopen_attempt_ms) rather
 * than accumulating stale files. */
static void coral_try_preopen(void)
{
    if (s_preopen_ok)
        return;

    if (f_open(&s_fframe, CORAL_STAGING_NAME, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
    {
        (void)f_sync(&s_fframe);   /* durable even if power is lost before it's claimed */
        s_preopen_ok = 1U;
    }
}

/* 16-byte header complete: validate, open the SD file, and set up the
 * pixel-chunk state -- or drop back to IDLE on a bad TYPE/geometry, exactly
 * as the original did (pixel bytes are not drained in that case; the next
 * SOF is re-synced to on a later call, same as before this change). */
static void coral_header_complete(void)
{
    s_rx_crc = crc16_buf(s_rx_crc, s_hdr, 16U);

    uint8_t  type   =  s_hdr[0];
    s_seq   = ((uint32_t)s_hdr[1]  << 24) | ((uint32_t)s_hdr[2]  << 16)
            | ((uint32_t)s_hdr[3]  <<  8) |  (uint32_t)s_hdr[4];
    s_frac  = ((uint16_t)s_hdr[9]  <<  8) |  s_hdr[10];
    uint16_t width  = ((uint16_t)s_hdr[11] <<  8) |  s_hdr[12];
    uint16_t height = ((uint16_t)s_hdr[13] <<  8) |  s_hdr[14];
    s_frac_pct = (uint8_t)((uint32_t)s_frac * 100UL / 65535UL);

    if (type != CORAL_TYPE_IMAGE)
    {
        dbg_hex("[CORAL] !!! Bad TYPE: ", type);
        coral_crc_err_count++;
        if (s_rx_dp != NULL) { s_rx_dp->coral_block[7] = CORAL_STATUS_CRC_ERR; }
        coral_last_status = CORAL_STATUS_CRC_ERR;
        s_rx_dp    = NULL;
        s_rx_state = CORAL_RX_IDLE;
        return;
    }
    if (width != CORAL_IMAGE_W || height != CORAL_IMAGE_H)
    {
        dbg("[CORAL] !!! Unexpected image geometry\r\n");
        coral_crc_err_count++;
        if (s_rx_dp != NULL) { s_rx_dp->coral_block[7] = CORAL_STATUS_CRC_ERR; }
        coral_last_status = CORAL_STATUS_CRC_ERR;
        s_rx_dp    = NULL;
        s_rx_state = CORAL_RX_IDLE;
        return;
    }

    {
        char buf[72];
        snprintf(buf, sizeof(buf),
                 "[CORAL] Header OK  SEQ=%lu  FRAC=%u (%u%%)\r\n",
                 (unsigned long)s_seq, (unsigned)s_frac,
                 (unsigned)((uint32_t)s_frac * 100UL / 65535UL));
        dbg(buf);
    }

    /* Cloud percentage folded into the filename (UART_PROTOCOL.md section 6's
     * OBC-side naming convention) so it's visible without cross-referencing
     * the SD-logger CSV by SEQ. */
    snprintf(s_fname, sizeof(s_fname), "F%08lu_cloud%u.RAW",
             (unsigned long)s_seq, (unsigned)s_frac_pct);

    s_sd_ok = 0U;
    s_using_staged_name = 0U;
    strncpy(s_open_name, s_fname, sizeof(s_open_name) - 1U);
    s_open_name[sizeof(s_open_name) - 1U] = '\0';

    if (s_preopen_ok)
    {
        /* File is already open under the staging name -- reuse it instead of
         * paying for another f_open() here, on the time-critical path. */
        s_preopen_ok = 0U;
        s_using_staged_name = 1U;
        strncpy(s_open_name, CORAL_STAGING_NAME, sizeof(s_open_name) - 1U);
        s_open_name[sizeof(s_open_name) - 1U] = '\0';
        s_sd_ok = 1U;
        dbg("[CORAL] SD file ready (pre-opened)\r\n");
    }
    else if (f_open(&s_fframe, s_fname, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
    {
        s_sd_ok = 1U;
        dbg("[CORAL] SD file opened OK\r\n");
    }
    else
    {
        dbg("[CORAL] !!! SD f_open FAILED -- draining UART without saving\r\n");
    }

    s_pixels_remaining = (uint32_t)width * height;  /* 50 176 */
    s_chunk_num        = 0U;
    s_chunk_fill        = 0U;
    s_chunk_target       = (uint16_t)((s_pixels_remaining > sizeof(s_chunk))
                                      ? sizeof(s_chunk) : s_pixels_remaining);

    s_rx_state             = CORAL_RX_PIXELS;
    s_rx_last_progress_ms  = HAL_GetTick();
}

/* One 512-byte (or final, shorter) pixel chunk is fully buffered: CRC it,
 * write it (the one bounded SD op per Coral_Update() call), and either
 * start the next chunk or move on to the CRC trailer. */
static void coral_pixel_chunk_complete(void)
{
    s_rx_crc = crc16_buf(s_rx_crc, s_chunk, s_chunk_fill);

    if (s_sd_ok)
    {
        UINT written;
        if (f_write(&s_fframe, s_chunk, (UINT)s_chunk_fill, &written) != FR_OK
            || written != (UINT)s_chunk_fill)
        {
            dbg("[CORAL] !!! SD write error -- continuing UART drain\r\n");
            f_close(&s_fframe);
            s_sd_ok  = 0U;
            s_status |= CORAL_STATUS_SD_ERR;
            coral_sd_err_count++;
        }
    }

    s_pixels_remaining -= s_chunk_fill;
    s_chunk_num++;
    s_chunk_fill = 0U;

    if (s_pixels_remaining == 0U)
    {
        if (s_sd_ok)
        {
            f_close(&s_fframe);
            dbg("[CORAL] Pixel stream done, SD file closed\r\n");
        }
        s_crc_pos  = 0U;
        s_rx_state = CORAL_RX_CRC;
    }
    else
    {
        s_chunk_target = (uint16_t)((s_pixels_remaining > sizeof(s_chunk))
                                    ? sizeof(s_chunk) : s_pixels_remaining);
    }
    s_rx_last_progress_ms = HAL_GetTick();
}

/* 2-byte CRC trailer complete: verify, populate coral_block[16] exactly as
 * the original did, and return the state machine to IDLE. */
static void coral_frame_complete(void)
{
    uint16_t rx_crc = ((uint16_t)s_crc_trailer[0] << 8) | s_crc_trailer[1];

    if (s_rx_crc != rx_crc)
    {
        char buf[72];
        snprintf(buf, sizeof(buf),
                 "[CORAL] !!! CRC MISMATCH: computed=0x%04X received=0x%04X\r\n",
                 (unsigned)s_rx_crc, (unsigned)rx_crc);
        dbg(buf);
        s_status |= CORAL_STATUS_CRC_ERR;
        coral_crc_err_count++;
        f_unlink(s_open_name);   /* delete corrupt file (final or staged name) */
    }
    else
    {
        char buf[56];

        /* Staged file was written under CORAL_STAGING_NAME; give it its real
         * name now that the frame is confirmed good. The file is already
         * closed at this point (coral_pixel_chunk_complete() closes it once
         * the pixel stream finishes), so the rename is a quick metadata-only
         * operation, not another slow allocation like the original open. */
        if (s_using_staged_name)
        {
            if (f_rename(s_open_name, s_fname) != FR_OK)
            {
                dbg("[CORAL] !!! rename to final name failed -- discarding\r\n");
                f_unlink(s_open_name);
                s_status |= CORAL_STATUS_SD_ERR;
                coral_sd_err_count++;
            }
        }

        snprintf(buf, sizeof(buf),
                 "[CORAL] CRC OK  frac=%u%%  file=%s\r\n",
                 (unsigned)((uint32_t)s_frac * 100UL / 65535UL), s_fname);
        dbg(buf);
    }

    /* ---- Populate coral_block[16] --------------------------------------
     *  [0..3]   SEQ        uint32 LE
     *  [4..5]   FRAC_RAW   uint16 LE
     *  [6]      FRAC_PCT   uint8  0-100
     *  [7]      STATUS     uint8
     *  [8..11]  RX_TICK    uint32 LE (ms)
     *  [12..13] FRAME_CNT  uint16 LE
     *  [14..15] reserved              */
    uint32_t now      = HAL_GetTick();

    if (s_rx_dp != NULL)
    {
        SensorData_t *dp = s_rx_dp;

        dp->coral_block[0]  = (uint8_t)( s_seq         & 0xFFU);
        dp->coral_block[1]  = (uint8_t)((s_seq >>  8)  & 0xFFU);
        dp->coral_block[2]  = (uint8_t)((s_seq >> 16)  & 0xFFU);
        dp->coral_block[3]  = (uint8_t)((s_seq >> 24)  & 0xFFU);
        dp->coral_block[4]  = (uint8_t)( s_frac        & 0xFFU);
        dp->coral_block[5]  = (uint8_t)((s_frac >> 8)  & 0xFFU);
        dp->coral_block[6]  = s_frac_pct;
        dp->coral_block[7]  = s_status;
        dp->coral_block[8]  = (uint8_t)( now         & 0xFFU);
        dp->coral_block[9]  = (uint8_t)((now >>  8)  & 0xFFU);
        dp->coral_block[10] = (uint8_t)((now >> 16)  & 0xFFU);
        dp->coral_block[11] = (uint8_t)((now >> 24)  & 0xFFU);
        dp->coral_block[12] = (uint8_t)( s_frame_count        & 0xFFU);
        dp->coral_block[13] = (uint8_t)((s_frame_count >> 8)  & 0xFFU);
        dp->coral_block[14] = 0U;
        dp->coral_block[15] = 0U;

        if (s_status == 0U)
        {
            s_frame_count++;
            coral_good_frames++;
            dp->coral_valid = 1U;
            s_seen_good_frame = 1U;
            s_frame_stale_reported = 0U;
            s_last_good_frame_ms = now;
            dbg("[CORAL] ===== Frame GOOD =====\r\n");
        }
        else
        {
            dbg("[CORAL] ===== Frame ERROR (check status byte) =====\r\n");
        }
    }

    coral_last_status   = s_status;
    coral_last_fraction = s_frac_pct;

    s_rx_dp    = NULL;
    s_rx_state = CORAL_RX_IDLE;
}

/* ============================================================================
 * Public API
 * ========================================================================== */

void Coral_Init(void)
{
    /* huart3 must already be initialised by MX_USART3_UART_Init() in main.c. */
    s_uart_ready  = 1U;
    s_frame_count = 0U;
    s_seen_good_frame = 0U;
    s_frame_stale_reported = 0U;
    s_last_good_frame_ms = HAL_GetTick();
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

    /* Ring overflows happen inside HAL_UART_RxCpltCallback() (IRQ context),
     * where doing blocking UART debug I/O would be unsafe; surface/log the
     * condition here instead, in normal main-loop context. */
    static uint32_t s_last_reported_overflow = 0U;
    if (coral_rx_overflow_count != s_last_reported_overflow)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "[CORAL] !!! RX ring overflow (total=%lu)\r\n",
                 (unsigned long)coral_rx_overflow_count);
        dbg(buf);
        s_last_reported_overflow = coral_rx_overflow_count;
    }

    uint32_t now = HAL_GetTick();

    if (dp->coral_valid &&
        s_seen_good_frame &&
        !s_frame_stale_reported &&
        ((uint32_t)(now - s_last_good_frame_ms) >= FRAME_STALE_TIMEOUT_MS))
    {
        dp->coral_valid = 0U;
        dp->coral_block[7] = CORAL_STATUS_TIMEOUT;
        coral_last_status = CORAL_STATUS_TIMEOUT;
        coral_timeout_count++;
        s_frame_stale_reported = 1U;
        dbg("[CORAL] !!! Frame freshness timeout\r\n");
    }

    switch (s_rx_state)
    {
    case CORAL_RX_IDLE:
    {
        uint8_t b0 = 0U;
        uint8_t scanned = 0U;

        /* Get the next frame's file ready while nothing is time-critical.
         * Rate-limited to at most once/second so a persistently unavailable
         * SD card can't turn this into its own per-tick stall, and gated on
         * CORAL_QUIET_MS of RX silence so it can't start right in front of a
         * SOF that's already arrived but not yet popped this call. */
        if (!s_preopen_ok &&
            (uint32_t)(now - s_last_preopen_attempt_ms) >= 1000U &&
            (uint32_t)(now - s_rx_last_byte_ms) >= CORAL_QUIET_MS)
        {
            s_last_preopen_attempt_ms = now;
            coral_try_preopen();
        }

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

        s_rx_state             = CORAL_RX_SOF1;
        s_rx_last_progress_ms  = now;
        break;
    }

    case CORAL_RX_SOF1:
    {
        uint8_t b1;
        if (coral_rx_ring_pop_n(&b1, 1U) == 1U)
        {
            if (b1 == CORAL_SOF_1)
            {
                coral_sof_count++;
                dbg("[CORAL] SOF 0xAA55 confirmed -- receiving frame...\r\n");

                s_rx_dp    = dp;
                dp->coral_valid = 0U;   /* set to 1 only on a clean frame */
                s_hdr_pos  = 0U;
                s_rx_crc   = 0xFFFFU;
                s_status   = 0U;
                s_sd_ok    = 0U;
                s_rx_state = CORAL_RX_HEADER;
            }
            else
            {
                dbg("[CORAL] !!! SOF_1 (0x55) missing or wrong -- discarding\r\n");
                s_rx_state = CORAL_RX_IDLE;
            }
            s_rx_last_progress_ms = now;
        }
        else if ((now - s_rx_last_progress_ms) >= SOF1_TIMEOUT_MS)
        {
            dbg("[CORAL] !!! SOF_1 (0x55) missing or wrong -- discarding\r\n");
            s_rx_state = CORAL_RX_IDLE;
        }
        break;
    }

    case CORAL_RX_HEADER:
    {
        uint16_t got = coral_rx_ring_pop_n(&s_hdr[s_hdr_pos], (uint16_t)(16U - s_hdr_pos));
        if (got > 0U)
        {
            s_hdr_pos += got;
            s_rx_last_progress_ms = now;
        }

        if (s_hdr_pos < 16U)
        {
            if ((now - s_rx_last_progress_ms) >= HDR_TIMEOUT_MS)
                coral_rx_abort(CORAL_STATUS_TIMEOUT, "[CORAL] !!! Header timeout\r\n");
            break;
        }

        coral_header_complete();
        break;
    }

    case CORAL_RX_PIXELS:
    {
        if (s_chunk_fill < s_chunk_target)
        {
            uint16_t got = coral_rx_ring_pop_n(&s_chunk[s_chunk_fill],
                                                (uint16_t)(s_chunk_target - s_chunk_fill));
            if (got > 0U)
            {
                s_chunk_fill += got;
                s_rx_last_progress_ms = now;
            }
        }

        if (s_chunk_fill < s_chunk_target)
        {
            if ((now - s_rx_last_progress_ms) >= CHUNK_TIMEOUT_MS)
            {
                char buf[72];
                snprintf(buf, sizeof(buf),
                         "[CORAL] !!! Chunk %lu timeout (remaining=%lu)\r\n",
                         (unsigned long)s_chunk_num, (unsigned long)s_pixels_remaining);
                coral_rx_abort(CORAL_STATUS_TIMEOUT, buf);
            }
            break;
        }

        coral_pixel_chunk_complete();
        break;
    }

    case CORAL_RX_CRC:
    {
        uint16_t got = coral_rx_ring_pop_n(&s_crc_trailer[s_crc_pos],
                                            (uint16_t)(2U - s_crc_pos));
        if (got > 0U)
        {
            s_crc_pos += got;
            s_rx_last_progress_ms = now;
        }

        if (s_crc_pos < 2U)
        {
            if ((now - s_rx_last_progress_ms) >= CRC_TIMEOUT_MS)
                coral_rx_abort((uint8_t)(s_status | CORAL_STATUS_TIMEOUT),
                                "[CORAL] !!! CRC bytes timeout\r\n");
            break;
        }

        coral_frame_complete();
        break;
    }

    default:
        s_rx_state = CORAL_RX_IDLE;
        break;
    }
}

/* True when no frame is in flight and the wire has been silent for at least
 * CORAL_QUIET_MS -- the same condition this module uses to gate its own
 * pre-open. Exposed so another owner of slow SD operations (currently
 * sd_logger.c's periodic CSV rotation) can defer its own work to the same
 * safe window instead of colliding with an in-flight Coral transfer. This is
 * a best-effort signal, not a lock: a SOF can still land immediately after
 * a caller checks it, same as it always could for coral_try_preopen(). */
uint8_t Coral_IsQuiescent(void)
{
    uint32_t now = HAL_GetTick();

    return (s_rx_state == CORAL_RX_IDLE &&
            (uint32_t)(now - s_rx_last_byte_ms) >= CORAL_QUIET_MS) ? 1U : 0U;
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
