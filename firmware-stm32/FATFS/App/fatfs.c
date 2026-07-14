/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file   fatfs.c
  * @brief  Code for fatfs applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
#include "fatfs.h"
#include "main.h"
#include <stdlib.h>
#include <string.h>

uint8_t retUSER;    /* Return value for USER */
char USERPath[4];   /* USER logical drive path */
FATFS USERFatFS;    /* File system object for USER logical drive */
FIL USERFile;       /* File object for USER */

/* USER CODE BEGIN Variables */

/* USER CODE END Variables */

void MX_FATFS_Init(void)
{
  /*## FatFS: Link the USER driver ###########################*/
  retUSER = FATFS_LinkDriver(&USER_Driver, USERPath);

  /* USER CODE BEGIN Init */
  /* additional user code for init */
  /* USER CODE END Init */
}

/**
  * @brief  Gets Time from RTC
  * @param  None
  * @retval Time in DWORD
  */
DWORD get_fattime(void)
{
  /* USER CODE BEGIN get_fattime */
  static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  static const uint8_t month_days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  uint32_t elapsed_s = HAL_GetTick() / 1000U;
  uint32_t second = (uint32_t)atoi(&__TIME__[6]) + elapsed_s;
  uint32_t minute = (uint32_t)atoi(&__TIME__[3]) + (second / 60U);
  uint32_t hour = (uint32_t)atoi(&__TIME__[0]) + (minute / 60U);
  uint32_t day = (uint32_t)atoi(&__DATE__[4]) + (hour / 24U);
  uint32_t year = (uint32_t)atoi(&__DATE__[7]);
  uint32_t month = 1U;

  second %= 60U;
  minute %= 60U;
  hour %= 24U;

  for (uint32_t i = 0U; i < 12U; i++)
  {
    if (strncmp(__DATE__, &months[i * 3U], 3U) == 0)
    {
      month = i + 1U;
      break;
    }
  }

  for (;;)
  {
    uint32_t days = month_days[month - 1U];
    if (month == 2U && ((year % 4U) == 0U) &&
        (((year % 100U) != 0U) || ((year % 400U) == 0U)))
      days = 29U;
    if (day <= days)
      break;
    day -= days;
    month++;
    if (month > 12U)
    {
      month = 1U;
      year++;
    }
  }

  if (year < 1980U) year = 1980U;
  if (year > 2107U) year = 2107U;

  /* No RTC is configured. Use firmware build time plus uptime so FatFs
   * receives valid, non-zero timestamps until RTC/GPS UTC is integrated. */
  return ((DWORD)(year - 1980U) << 25) |
         ((DWORD)month << 21) |
         ((DWORD)day << 16) |
         ((DWORD)hour << 11) |
         ((DWORD)minute << 5) |
         ((DWORD)second / 2U);
  /* USER CODE END get_fattime */
}

/* USER CODE BEGIN Application */

/* USER CODE END Application */
