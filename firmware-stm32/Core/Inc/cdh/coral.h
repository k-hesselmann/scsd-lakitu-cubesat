/* ============================================================================
 * coral.h  —  OBC <-> Coral Dev Board Micro USART3 interface header
 *
 * Protocol reference: UART_PROTOCOL.md v1.0 (interface_docs/)
 *
 * Transport : USART3  PC10=TX (OBC->Coral)   PC11=RX (Coral->OBC)  115200 8N1
 *             PC10 (TX) is OPEN-DRAIN + external 2.2k pull-up to the Coral 1.8 V
 *             rail (3.3 V/1.8 V level shift). See UART_PROTOCOL.md section 1.1.
 * ========================================================================== */

#ifndef CDH_CORAL_H
#define CDH_CORAL_H

#include "datapool.h"
#include <stdint.h>

/* ---- SOF: two-byte start-of-frame marker (Coral -> OBC) ---------------- */
#define CORAL_SOF_0              0xAAU   /* first byte  */
#define CORAL_SOF_1              0x55U   /* second byte */

/* ---- Image packet type -------------------------------------------------- */
#define CORAL_TYPE_IMAGE         0x01U

/* ---- OBC -> Coral command bytes (no SOF prefix, UART_PROTOCOL.md section 4) */
#define CORAL_CMD_TRIGGER        0x10U   /* trigger immediate inference         */
#define CORAL_CMD_SET_INTERVAL   0x11U   /* set periodic interval (uint32 ms)  */
#define CORAL_CMD_SLEEP          0x17U   /* suspend inference (auto-wakes 5 min) */
#define CORAL_CMD_WAKE           0x18U   /* resume inference                    */

/* ---- Image geometry (224x224 Y8 grayscale) ------------------------------ */
#define CORAL_IMAGE_W            224U
#define CORAL_IMAGE_H            224U
#define CORAL_IMAGE_PIXELS       (CORAL_IMAGE_W * CORAL_IMAGE_H)  /* 50 176  */

/* ---- Default capture cadence -------------------------------------------- */
#define CORAL_DEFAULT_INTERVAL_MS   10000UL   /* 10 s */

/* ---- coral_block[16] layout (all little-endian, native STM32) -----------
 *   [0..3]   SEQ        uint32 -- sequence number from Coral
 *   [4..5]   FRAC_RAW   uint16 -- raw fraction 0-65535
 *   [6]      FRAC_PCT   uint8  -- cloud cover 0-100 %
 *   [7]      STATUS     uint8  -- bitmask (see flags below)
 *   [8..11]  RX_TICK    uint32 -- HAL_GetTick() at time of receipt (ms)
 *   [12..13] FRAME_CNT  uint16 -- good-frame counter (wraps at 65535)
 *   [14..15] reserved   zero
 *
 * Telemetry v8 carries SEQ low 16 bits, FRAC_RAW, STATUS, and result age;
 * the full block remains available in the SD log.
 * ---------------------------------------------------------------------- */
#define CORAL_STATUS_OK          0x00U
#define CORAL_STATUS_TIMEOUT     0x01U   /* UART receive timeout               */
#define CORAL_STATUS_CRC_ERR     0x02U   /* CRC-16 mismatch                    */
#define CORAL_STATUS_SD_ERR      0x04U   /* FatFS write error                  */
#define CORAL_STATUS_NO_UART     0x80U   /* Coral_Init() not yet called        */

/* ---- Public API --------------------------------------------------------- */

/* Call once after MX_USART3_UART_Init() -- before the superloop. */
void Coral_Init(void);

/* Call every CDH_Update() tick. Non-blocking poll; blocks only when a frame
 * SOF is detected (~4.4 s for 224x224 at 115200 baud). */
void Coral_Update(SensorData_t *dp);

/* Send an immediate-inference trigger command to the Coral. */
void Coral_SendTrigger(void);

/* Change the Coral autonomous inference interval (milliseconds). */
void Coral_SendSetInterval(uint32_t interval_ms);

/* Suspend the Coral's inference (SLEEP). The Coral auto-wakes after 5 min if no
 * WAKE arrives, so to keep it asleep re-send this within that window. */
void Coral_SendSleep(void);

/* Resume the Coral's inference (WAKE); triggers one immediate capture. */
void Coral_SendWake(void);

#endif /* CDH_CORAL_H */
