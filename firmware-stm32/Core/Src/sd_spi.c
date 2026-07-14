#include "sd_spi.h"
#include <string.h>

#if __has_include("spi.h")
#include "spi.h"
#endif

extern SPI_HandleTypeDef  hspi2;
extern UART_HandleTypeDef huart2;   /* NUCLEO ST-LINK virtual COM port */

#ifndef SD_SPI_DEBUG
#define SD_SPI_DEBUG 0
#endif

/* ------------------------------------------------------------------ */
/*  Debug UART helper - reads on any 115200-8N1 serial terminal        */
/* ------------------------------------------------------------------ */
static void dbg(const char *msg)
{
#if SD_SPI_DEBUG
  HAL_UART_Transmit(&huart2, (const uint8_t *)msg, (uint16_t)strlen(msg), 100);
#else
  (void)msg;
#endif
}

static void dbg_hex(const char *label, uint8_t val)
{
#if SD_SPI_DEBUG
  const char hex[] = "0123456789ABCDEF";
  char buf[64];
  uint8_t i = 0;
  while (label[i]) { buf[i] = label[i]; i++; }
  buf[i++] = '0'; buf[i++] = 'x';
  buf[i++] = hex[(val >> 4) & 0xF];
  buf[i++] = hex[val & 0xF];
  buf[i++] = '\r'; buf[i++] = '\n'; buf[i] = '\0';
  dbg(buf);
#else
  (void)label;
  (void)val;
#endif
}

/* ------------------------------------------------------------------ */

#define CMD0   (0)
#define CMD1   (1)
#define CMD8   (8)
#define CMD9   (9)
#define CMD16  (16)
#define CMD17  (17)
#define CMD24  (24)
#define CMD55  (55)
#define CMD58  (58)
#define ACMD41 (0x80 + 41)

#define CT_MMC   0x01
#define CT_SD1   0x02
#define CT_SD2   0x04
#define CT_BLOCK 0x08

static uint8_t card_type = 0;

static void sd_cs_high(void)
{
  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);
}

static void sd_cs_low(void)
{
  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET);
}

volatile uint32_t spi_error_count = 0;
volatile uint32_t spi_hal_error_last = 0;
volatile uint32_t spi_hal_state_last = 0;
volatile uint8_t  spi_rx_last = 0;

static uint8_t spi_xchg(uint8_t data)
{
  uint8_t rx = 0xFF;
  HAL_StatusTypeDef status;

  status = HAL_SPI_TransmitReceive(&hspi2, &data, &rx, 1, 1000);

  spi_rx_last = rx;

  if (status != HAL_OK)
  {
    spi_error_count++;
    spi_hal_error_last = HAL_SPI_GetError(&hspi2);
    spi_hal_state_last = hspi2.State;
    /* Force HAL state machine back to ready so next call can proceed */
    hspi2.State = HAL_SPI_STATE_READY;
    __HAL_SPI_CLEAR_OVRFLAG(&hspi2);
    return 0xFF;
  }

  return rx;
}

static void spi_multi_xchg(uint8_t *buff, uint32_t count)
{
  while (count--)
  {
    *buff++ = spi_xchg(0xFF);
  }
}

static uint8_t wait_ready(uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();
  uint8_t  r;
  do
  {
    r = spi_xchg(0xFF);
    if (r == 0xFF) { return 1; }
  } while ((HAL_GetTick() - start) < timeout_ms);
  return 0;
}

static void deselect_card(void)
{
  sd_cs_high();
  spi_xchg(0xFF);
}

static uint8_t select_card(void)
{
  sd_cs_low();
  spi_xchg(0xFF);
  if (wait_ready(500)) { return 1; }
  deselect_card();
  return 0;
}

static uint8_t receive_data_block(uint8_t *buff, uint32_t len)
{
  uint8_t  token;
  uint32_t start = HAL_GetTick();
  do
  {
    token = spi_xchg(0xFF);
    if (token == 0xFE)
    {
      spi_multi_xchg(buff, len);
      spi_xchg(0xFF); spi_xchg(0xFF);   /* discard CRC */
      return 1;
    }
  } while ((HAL_GetTick() - start) < 200);
  return 0;
}

static uint8_t transmit_data_block(const uint8_t *buff, uint8_t token)
{
  uint8_t response;
  if (!wait_ready(500)) { return 0; }
  spi_xchg(token);
  if (token != 0xFD)
  {
    for (uint32_t i = 0; i < 512; i++) { spi_xchg(buff[i]); }
    spi_xchg(0xFF); spi_xchg(0xFF);    /* dummy CRC */
    response = spi_xchg(0xFF);
    if ((response & 0x1F) != 0x05) { return 0; }
  }
  return 1;
}

static uint8_t send_command(uint8_t cmd, uint32_t arg)
{
  uint8_t response;
  uint8_t crc = 0x01;

  if (cmd & 0x80)
  {
    cmd &= 0x7F;
    response = send_command(CMD55, 0);
    if (response > 1) { return response; }
  }

  deselect_card();
  if (!select_card()) { return 0xFF; }

  spi_xchg(0x40 | cmd);
  spi_xchg((uint8_t)(arg >> 24));
  spi_xchg((uint8_t)(arg >> 16));
  spi_xchg((uint8_t)(arg >>  8));
  spi_xchg((uint8_t) arg);

  if      (cmd == CMD0) { crc = 0x95; }
  else if (cmd == CMD8) { crc = 0x87; }
  spi_xchg(crc);

  for (uint8_t n = 0; n < 10; n++)
  {
    response = spi_xchg(0xFF);
    if ((response & 0x80) == 0) { return response; }
  }
  return response;
}

/* ------------------------------------------------------------------ */
/*  SD_SPI_Init                                                        */
/* ------------------------------------------------------------------ */
uint8_t SD_SPI_Init(void)
{
  uint8_t  n, ty = 0, ocr[4], r;
  uint32_t start;

  /* ---- Ensure CS pin (PB1) is configured as push-pull output. ---- */
  {
    GPIO_InitTypeDef _cs = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);
    _cs.Pin   = SD_CS_Pin;
    _cs.Mode  = GPIO_MODE_OUTPUT_PP;
    _cs.Pull  = GPIO_NOPULL;
    _cs.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(SD_CS_GPIO_Port, &_cs);
  }

  dbg("\r\n[SD] ===== Init start =====\r\n");

  /* --- reset SPI error counter so a fresh run is visible --- */
  spi_error_count = 0;

  sd_cs_high();

  /* Power-up delay: VCC must be stable before the first clock. */
  HAL_Delay(500);

  /* At least 74 dummy clocks with CS HIGH are required. Send extra clocks. */
  for (n = 0; n < 20; n++) { spi_xchg(0xFF); }

  /* CMD0: reset / enter SPI mode. Retry because some cards wake slowly. */
  r = 0xFF;
  for (n = 0; n < 10; n++)
  {
    r = send_command(CMD0, 0);
    dbg_hex("[SD] CMD0 response: ", r);
    if (r == 1) { break; }
    HAL_Delay(20);
  }

  if (spi_error_count > 0)
  {
    dbg("[SD] !!! SPI HAL errors detected - SPI peripheral problem !!!\r\n");
  }

  if (r != 1)
  {
    dbg("[SD] CMD0 failed - card not responding. Check wiring / power.\r\n");
    card_type = 0;
    deselect_card();
    return 1;
  }

  /* ---- CMD8: check SD v2 / voltage range ---- */
  r = send_command(CMD8, 0x1AA);
  dbg_hex("[SD] CMD8 response: ", r);

  if (r == 1)
  {
    /* SD v2: read 4-byte R7 response */
    for (n = 0; n < 4; n++) { ocr[n] = spi_xchg(0xFF); }
    dbg("[SD] CMD8 R7 bytes received\r\n");

    if (ocr[2] == 0x01 && ocr[3] == 0xAA)
    {
      dbg("[SD] Voltage OK - running ACMD41 (SDHC probe)...\r\n");
      start = HAL_GetTick();
      do
      {
        r = send_command(ACMD41, 1UL << 30);
        if (r == 0) { break; }
      } while ((HAL_GetTick() - start) < 5000);

      dbg_hex("[SD] ACMD41 final response: ", r);

      if ((HAL_GetTick() - start) >= 5000)
      {
        dbg("[SD] ACMD41 timed out after 5000 ms\r\n");
      }
      else if (send_command(CMD58, 0) == 0)
      {
        for (n = 0; n < 4; n++) { ocr[n] = spi_xchg(0xFF); }
        ty = (ocr[0] & 0x40) ? (CT_SD2 | CT_BLOCK) : CT_SD2;
        dbg_hex("[SD] CMD58 OCR[0]: ", ocr[0]);
        dbg(ty & CT_BLOCK ? "[SD] Card type: SDHC/SDXC (block)\r\n"
                          : "[SD] Card type: SD v2 (byte)\r\n");
      }
      else
      {
        dbg("[SD] CMD58 failed\r\n");
      }
    }
    else
    {
      dbg("[SD] Voltage pattern mismatch in R7\r\n");
    }
  }
  else
  {
    /* CMD8 returned 0x05 (illegal cmd) - SD v1 or MMC */
    dbg("[SD] CMD8 illegal - SD v1 or MMC path\r\n");

    r = send_command(ACMD41, 0);
    dbg_hex("[SD] ACMD41 probe: ", r);

    if (r <= 1)
    {
      ty = CT_SD1;
      dbg("[SD] SD v1 detected\r\n");
      start = HAL_GetTick();
      do
      {
        r = send_command(ACMD41, 0);
        if (r == 0) { break; }
      } while ((HAL_GetTick() - start) < 5000);
      dbg_hex("[SD] ACMD41 final: ", r);
    }
    else
    {
      ty = CT_MMC;
      dbg("[SD] MMC detected\r\n");
      start = HAL_GetTick();
      do
      {
        r = send_command(CMD1, 0);
        if (r == 0) { break; }
      } while ((HAL_GetTick() - start) < 5000);
      dbg_hex("[SD] CMD1 final: ", r);
    }

    if ((HAL_GetTick() - start) >= 5000)
    {
      dbg("[SD] Init loop timed out\r\n");
      ty = 0;
    }
    else
    {
      r = send_command(CMD16, 512);
      dbg_hex("[SD] CMD16 response: ", r);
      if (r != 0) { ty = 0; }
    }
  }

  card_type = ty;
  deselect_card();

  if (card_type)
  {
    dbg("[SD] ===== Init SUCCESS =====\r\n");
    return 0;
  }
  else
  {
    dbg("[SD] ===== Init FAILED =====\r\n");
    return 1;
  }
}

/* ------------------------------------------------------------------ */
uint8_t SD_SPI_ReadBlocks(uint8_t *buff, uint32_t sector, uint32_t count)
{
  if (!(card_type & CT_BLOCK)) { sector *= 512; }
  while (count--)
  {
    if (send_command(CMD17, sector) != 0) { deselect_card(); return 1; }
    if (!receive_data_block(buff, 512))   { deselect_card(); return 1; }
    buff   += 512;
    sector++;
  }
  deselect_card();
  return 0;
}

uint8_t SD_SPI_WriteBlocks(const uint8_t *buff, uint32_t sector, uint32_t count)
{
  if (!(card_type & CT_BLOCK)) { sector *= 512; }
  while (count--)
  {
    if (send_command(CMD24, sector) != 0)       { deselect_card(); return 1; }
    if (!transmit_data_block(buff, 0xFE))        { deselect_card(); return 1; }
    buff   += 512;
    sector++;
  }
  deselect_card();
  return 0;
}

uint8_t SD_SPI_Sync(void)
{
  uint8_t ok;
  if (!select_card()) { return 0; }
  ok = wait_ready(500);
  deselect_card();
  return ok;
}

/* Switch SPI2 from the slow init prescaler (/256 = 312 kHz) to /8 (10 MHz).
 * Call once after SD_SPI_Init() succeeds and before any data transfers. */
void SD_SPI_SetHighSpeed(void)
{
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
  HAL_SPI_Init(&hspi2);
}

/* Read the CSD register via CMD9 and return the actual card sector count.
 * Returns 0 on failure.  Supports CSD v1 (SD/MMC) and CSD v2 (SDHC/SDXC). */
uint32_t SD_SPI_GetSectorCount(void)
{
  uint8_t  csd[16];
  uint8_t  token;
  uint32_t start;
  uint8_t  i;

  if (card_type == 0) { return 0; }

  if (send_command(CMD9, 0) != 0) { deselect_card(); return 0; }

  /* Wait for data token 0xFE */
  start = HAL_GetTick();
  do
  {
    token = spi_xchg(0xFF);
    if (token == 0xFE) { break; }
  } while ((HAL_GetTick() - start) < 200);

  if (token != 0xFE) { deselect_card(); return 0; }

  for (i = 0; i < 16; i++) { csd[i] = spi_xchg(0xFF); }
  spi_xchg(0xFF); spi_xchg(0xFF);  /* discard CRC */
  deselect_card();

  if ((csd[0] >> 6) == 1)
  {
    /* CSD v2 (SDHC/SDXC): C_SIZE at bits[69:48] spans bytes 7-9 */
    uint32_t c_size = ((uint32_t)(csd[7] & 0x3FU) << 16) |
                      ((uint32_t) csd[8]            <<  8) |
                      ((uint32_t) csd[9]);
    return (c_size + 1UL) * 1024UL;
  }
  else
  {
    /* CSD v1: C_SIZE[11:0], C_SIZE_MULT[2:0], READ_BL_LEN[3:0] */
    uint32_t c_size      = ((uint32_t)(csd[6] & 0x03U) << 10) |
                           ((uint32_t) csd[7]            <<  2) |
                           ((uint32_t)(csd[8] >> 6));
    uint32_t c_size_mult = ((uint32_t)(csd[9]  & 0x03U) << 1) |
                           ((uint32_t)(csd[10] >> 7));
    uint32_t read_bl_len = (uint32_t)(csd[5] & 0x0FU);
    uint32_t block_len   = 1UL << read_bl_len;
    uint32_t block_count = (c_size + 1UL) * (1UL << (c_size_mult + 2UL));
    return block_count * (block_len / 512UL);
  }
}

