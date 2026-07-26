#include "sd_spi.h"
#include <string.h>

#if __has_include("spi.h")
#include "spi.h"
#endif

extern SPI_HandleTypeDef  hspi2;
extern UART_HandleTypeDef huart2;   /* NUCLEO ST-LINK virtual COM port */

#ifndef SD_SPI_DEBUG
#define SD_SPI_DEBUG 0   /* set to 1 to enable the SD init trace on UART2 */
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

#define SD_SPI_BSY_TIMEOUT_MS 10U

static uint8_t card_type = 0;
static void sd_cs_high(void);

volatile uint32_t spi_error_count = 0;
volatile uint32_t spi_hal_error_last = 0;
volatile uint32_t spi_hal_state_last = 0;
volatile uint8_t  spi_rx_last = 0;

static uint8_t sd_spi_wait_not_busy(uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();

  while (__HAL_SPI_GET_FLAG(&hspi2, SPI_FLAG_BSY))
  {
    if ((uint32_t)(HAL_GetTick() - start) >= timeout_ms)
    {
      spi_error_count++;
      spi_hal_error_last = HAL_SPI_GetError(&hspi2);
      spi_hal_state_last = hspi2.State;
      return 0U;
    }
  }

  return 1U;
}

static void sd_spi_configure_cs(void)
{
  GPIO_InitTypeDef cs = {0};

  __HAL_RCC_GPIOB_CLK_ENABLE();
  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);

  cs.Pin   = SD_CS_Pin;
  cs.Mode  = GPIO_MODE_OUTPUT_PP;
  cs.Pull  = GPIO_NOPULL;
  cs.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SD_CS_GPIO_Port, &cs);
}

static void sd_spi_configure_handle(uint32_t prescaler)
{
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = prescaler;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 7;
  hspi2.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi2.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
}

static uint8_t sd_spi_set_prescaler(uint32_t prescaler)
{
  sd_cs_high();

  if (!sd_spi_wait_not_busy(SD_SPI_BSY_TIMEOUT_MS))
  {
    (void)HAL_SPI_Abort(&hspi2);
    return 0U;
  }

  __HAL_SPI_DISABLE(&hspi2);
  MODIFY_REG(hspi2.Instance->CR1, SPI_CR1_BR, prescaler);
  hspi2.Init.BaudRatePrescaler = prescaler;
  __HAL_SPI_ENABLE(&hspi2);

  return 1U;
}

static void sd_spi_set_high_speed(void)
{
  /* Initialization must stay below 400 kHz. After the card leaves idle,
   * raise SPI2 from /256 (312.5 kHz) to /64 (1.25 MHz at 80 MHz PCLK1).
   * Telemetry logging is a handful of rows/sec, so this keeps plenty of
   * headroom while cutting SPI clock harmonics by 8x vs. the previous
   * /8 (10 MHz) setting to reduce radiated EMI near the radio front end. */
  (void)sd_spi_set_prescaler(SPI_BAUDRATEPRESCALER_64);
}

static void sd_cs_high(void)
{
  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);
}

static void sd_cs_low(void)
{
  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET);
}

static uint8_t sd_spi_recover_bus(void)
{
  card_type = 0U;
  spi_rx_last = 0xFFU;

  sd_spi_configure_cs();
  sd_cs_high();

  hspi2.Instance = SPI2;
  if (__HAL_RCC_SPI2_IS_CLK_ENABLED())
  {
    (void)HAL_SPI_Abort(&hspi2);
    __HAL_SPI_DISABLE(&hspi2);
    __HAL_SPI_CLEAR_OVRFLAG(&hspi2);
    (void)HAL_SPI_DeInit(&hspi2);
  }

  __HAL_RCC_SPI2_FORCE_RESET();
  __NOP();
  __HAL_RCC_SPI2_RELEASE_RESET();

  sd_spi_configure_handle(SPI_BAUDRATEPRESCALER_256);
  hspi2.State = HAL_SPI_STATE_RESET;
  hspi2.ErrorCode = HAL_SPI_ERROR_NONE;

  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    spi_error_count++;
    spi_hal_error_last = HAL_SPI_GetError(&hspi2);
    spi_hal_state_last = hspi2.State;
    return 1U;
  }

  sd_cs_high();
  return 0U;
}

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

  dbg("\r\n[SD] ===== Init start =====\r\n");

  /* Reset SPI error counter so a fresh init/recovery run is visible. */
  spi_error_count = 0;

  /* FDIR only requests SD recovery; the SD subsystem owns the hardware action.
   * Every mount retry gets one deterministic SPI2/card restart: CS high,
   * peripheral reset, GPIO/MSP re-init, and <=400 kHz clock before CMD0. */
  if (sd_spi_recover_bus() != 0U)
  {
    dbg("[SD] SPI2 recovery failed\r\n");
    card_type = 0U;
    return 1U;
  }

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
    sd_spi_set_high_speed();
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

uint8_t SD_SPI_GetSectorCount(uint32_t *sector_count)
{
  uint8_t csd[16];
  uint64_t sectors;

  if (sector_count == NULL || card_type == 0U)
    return 1U;

  if (send_command(CMD9, 0U) != 0U || !receive_data_block(csd, sizeof(csd)))
  {
    deselect_card();
    return 1U;
  }
  deselect_card();

  if ((csd[0] & 0xC0U) == 0x40U)
  {
    uint32_t c_size = ((uint32_t)(csd[7] & 0x3FU) << 16) |
                      ((uint32_t)csd[8] << 8) |
                      (uint32_t)csd[9];
    sectors = ((uint64_t)c_size + 1ULL) << 10;
  }
  else
  {
    uint32_t read_bl_len = csd[5] & 0x0FU;
    uint32_t c_size = ((uint32_t)(csd[6] & 0x03U) << 10) |
                      ((uint32_t)csd[7] << 2) |
                      ((uint32_t)(csd[8] & 0xC0U) >> 6);
    uint32_t c_size_mult = ((uint32_t)(csd[9] & 0x03U) << 1) |
                           ((uint32_t)(csd[10] & 0x80U) >> 7);
    uint64_t block_count = ((uint64_t)c_size + 1ULL) << (c_size_mult + 2U);
    uint64_t block_length = 1ULL << read_bl_len;
    sectors = (block_count * block_length) / 512ULL;
  }

  if (sectors == 0ULL || sectors > UINT32_MAX)
    return 1U;

  *sector_count = (uint32_t)sectors;
  return 0U;
}

