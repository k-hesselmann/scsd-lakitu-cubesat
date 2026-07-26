/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "cdh/cdh.h"
#include "cdh/coral.h"
#include "cdh/gps_diag.h"
#include "cdh/baro_diag.h"
#include "datapool.h"
#include "debug_log.h"
#include "fdir/fdir.h"
#include "fdir/fdir_test_hooks.h"
#include "fdir/scv.h"
#include "fsw/fsm.h"
#include "observability.h"
#include "sd_logger.h"
#include "ttc/ttc.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* Superloop scheduling periods. FDIR runs every iteration (ungated). */
#define LOOP_CDH_FSW_PERIOD_MS  100U   /* CDH + FSW at 10 Hz */
#define LOOP_SD_PERIOD_MS        100U  /* SD logging at 10 Hz, batched in logger */
#define BOARD_BUTTON_DEBOUNCE_MS  50U
#define BOARD_SCV_RESET_HOLD_MS 1500U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
I2C_HandleTypeDef hi2c1;

UART_HandleTypeDef huart5;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart3;

/* USER CODE BEGIN PV */
/* SPI1/SPI2 are now in the .ioc; a CubeMX regen also declares these handles
 * in the generated section above. Duplicate tentative definitions are legal C,
 * so these stay here to keep the pre-regen sources buildable. */
SPI_HandleTypeDef hspi1;
SPI_HandleTypeDef hspi2;
/* IWDG (FMECA F1) is configured in the .ioc; like the SPI handles above,
 * this duplicate tentative definition keeps pre-regen sources buildable. */
IWDG_HandleTypeDef hiwdg;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_UART5_Init(void);
static void MX_ADC1_Init(void);
/* USER CODE BEGIN PFP */
/* Hand-added SPI init functions; see bodies in the USER CODE 4 block. */
static void SPI1_UserInit(void);
static void SPI2_UserInit(void);
static void IWDG_UserInit(void);
static void BoardButton_Init(void);
static void BoardButton_Update(uint32_t now_ms);
static void BoardResetScvAndReboot(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static uint8_t  s_button_last_raw_pressed;
static uint8_t  s_button_stable_pressed;
static uint8_t  s_button_reset_armed;
static uint32_t s_button_last_change_ms;
static uint32_t s_button_pressed_since_ms;

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  /* Arm the independent watchdog before clock, peripheral, and subsystem init.
   * Once started it cannot be stopped; any init hang now resets the MCU. */
  IWDG_UserInit();
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  (void)HAL_IWDG_Refresh(&hiwdg);
  /* FDIR first: restores the SCV from flash and evaluates staged reduced
   * mode (FMECA F2) before subsystem-specific init begins. */
  FDIR_Init(&g_scv);
  (void)HAL_IWDG_Refresh(&hiwdg);
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  BoardButton_Init();
  (void)HAL_IWDG_Refresh(&hiwdg);
  MX_I2C1_Init();
  (void)HAL_IWDG_Refresh(&hiwdg);
  MX_USB_DEVICE_Init();
  (void)HAL_IWDG_Refresh(&hiwdg);
  MX_USART3_UART_Init();
  (void)HAL_IWDG_Refresh(&hiwdg);
  MX_USART2_UART_Init();
  DebugLog_Init(&huart2);
  Observability_Init();
  (void)HAL_IWDG_Refresh(&hiwdg);
  MX_UART5_Init();
  (void)HAL_IWDG_Refresh(&hiwdg);
  MX_ADC1_Init();
  (void)HAL_IWDG_Refresh(&hiwdg);
  /* USER CODE BEGIN 2 */
  /* The gates below skip subsystems disabled after repeated watchdog resets.
   * FDIR and the IWDG kick always run. */
  if (FDIR_SubsystemEnabled(FDIR_SUBSYS_SD))
  {
    SPI2_UserInit();   /* hand-added SPI2 for the SD card */
    (void)HAL_IWDG_Refresh(&hiwdg);
  }
  if (FDIR_SubsystemEnabled(FDIR_SUBSYS_TTC))
  {
    SPI1_UserInit();   /* hand-added SPI1 for the LoRa radio */
    (void)HAL_IWDG_Refresh(&hiwdg);
  }

  if (FDIR_SubsystemEnabled(FDIR_SUBSYS_CDH))
  {
    CDH_Init();
    (void)HAL_IWDG_Refresh(&hiwdg);
    GPS_Diag_Test(&hi2c1);
    (void)HAL_IWDG_Refresh(&hiwdg);
    Baro_Diag_Test(&hi2c1);
    (void)HAL_IWDG_Refresh(&hiwdg);
  }
  if (FDIR_SubsystemEnabled(FDIR_SUBSYS_FSW))
  {
    FSW_Init();
    (void)HAL_IWDG_Refresh(&hiwdg);
  }
  if (FDIR_SubsystemEnabled(FDIR_SUBSYS_TTC))
  {
    TTC_Init();
    (void)HAL_IWDG_Refresh(&hiwdg);
  }
  if (FDIR_SubsystemEnabled(FDIR_SUBSYS_SD))
  {
    SD_Logger_Init(&g_scv);
    (void)HAL_IWDG_Refresh(&hiwdg);
  }

  /* Bench-only fault-injection console (docs/FMECA.md Section 5); compiles
   * to nothing unless FDIR_TEST_HOOKS is defined (testhooks build env). */
  FDIR_TestHooks_Init();
  (void)HAL_IWDG_Refresh(&hiwdg);

  TelemetryPacket_t tx_packet = {0};

  /* Superloop schedule: the loop free-runs (no delay) so FDIR executes at the
   * maximum rate; CDH/FSW and SD are gated by elapsed-time slots below. TTC
   * additionally rate-limits itself via TTC_TelemetryDue(). */
  uint32_t last_cdh_fsw_ms = 0U;
  uint32_t last_sd_ms = 0U;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    uint32_t now_ms = HAL_GetTick();

    DebugLog_Service();
    Observability_Update(now_ms, &g_datapool);

    BoardButton_Update(now_ms);

    if ((now_ms - last_cdh_fsw_ms) >= LOOP_CDH_FSW_PERIOD_MS)
    {
      last_cdh_fsw_ms = now_ms;
      if (FDIR_SubsystemEnabled(FDIR_SUBSYS_CDH))
        CDH_Update(&g_datapool, &g_scv);
      if (FDIR_SubsystemEnabled(FDIR_SUBSYS_FSW))
        FSW_Update(&g_datapool);
    }

    FDIR_Update(&g_datapool, &g_scv);

    if (FDIR_SubsystemEnabled(FDIR_SUBSYS_CDH))
      Coral_Update(&g_datapool);

    if ((now_ms - last_sd_ms) >= LOOP_SD_PERIOD_MS)
    {
      last_sd_ms = now_ms;
      if (FDIR_SubsystemEnabled(FDIR_SUBSYS_SD))
        SD_Logger_Update(&g_datapool, &g_scv);
    }

    /* TTC_Service() must run every tick, even while FDIR_SUBSYS_TTC is
     * disabled: it is what carries out FDIR-requested isolation/recovery/
     * rx-restart actions to completion (see TTC_FDIR_Request*() in ttc.h). */
    TTC_Service();

    if (FDIR_SubsystemEnabled(FDIR_SUBSYS_TTC) && TTC_TelemetryDue())
    {
      TTC_BuildTelemetryPacket(&g_datapool, &g_scv, &tx_packet);
      TTC_Transmit(&tx_packet);
    }
    SCV_Update(&g_scv);   /* periodic + event-driven flash backup */

    /* Kick at the END of the loop, so a blocked task above is recovered by
     * reset; FDIR withholds the kick to escalate a stuck datapool (C10).
     * IWDG timeout is nominally 10 s (FR-011). TTC_Transmit()/TTC_Service() are
     * now non-blocking (queued, advanced by LoRa_Service() over subsequent
     * ticks), so correctness here depends on TTC_Service() being called
     * every loop iteration rather than tolerating one long blocking send. */
    if (FDIR_SystemHealthyEnoughToKickWatchdog())
      (void)HAL_IWDG_Refresh(&hiwdg);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSE|RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 40;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable MSI Auto calibration
  */
  HAL_RCCEx_EnableMSIPLLMode();
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x10D19CE4;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{
  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment
  * and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_5;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_640CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */
  if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief UART5 Initialization Function
  * @param None
  * @retval None
  */
static void MX_UART5_Init(void)
{

  /* USER CODE BEGIN UART5_Init 0 */

  /* USER CODE END UART5_Init 0 */

  /* USER CODE BEGIN UART5_Init 1 */

  /* USER CODE END UART5_Init 1 */
  huart5.Instance = UART5;
  huart5.Init.BaudRate = 115200;
  huart5.Init.WordLength = UART_WORDLENGTH_8B;
  huart5.Init.StopBits = UART_STOPBITS_1;
  huart5.Init.Parity = UART_PARITY_NONE;
  huart5.Init.Mode = UART_MODE_TX_RX;
  huart5.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart5.Init.OverSampling = UART_OVERSAMPLING_16;
  huart5.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart5.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart5) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN UART5_Init 2 */

  /* USER CODE END UART5_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* B1 is polled by the board service; no EXTI/NVIC handler is required. */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PC12 PD2 for UART5 */
  GPIO_InitStruct.Pin = GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF8_UART5;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF8_UART5;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
static uint8_t BoardButton_IsPressed(void)
{
  return (HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

static void BoardButton_Init(void)
{
  uint32_t now_ms = HAL_GetTick();
  uint8_t pressed = BoardButton_IsPressed();

  s_button_last_raw_pressed = pressed;
  s_button_stable_pressed = pressed;
  s_button_reset_armed = 0U;
  s_button_last_change_ms = now_ms;
  s_button_pressed_since_ms = pressed ? now_ms : 0U;
}

static void BoardButton_Update(uint32_t now_ms)
{
  uint8_t raw_pressed = BoardButton_IsPressed();

  if (raw_pressed != s_button_last_raw_pressed)
  {
    s_button_last_raw_pressed = raw_pressed;
    s_button_last_change_ms = now_ms;
    return;
  }

  if ((uint32_t)(now_ms - s_button_last_change_ms) < BOARD_BUTTON_DEBOUNCE_MS)
    return;

  if (raw_pressed != s_button_stable_pressed)
  {
    s_button_stable_pressed = raw_pressed;
    s_button_reset_armed = 0U;
    s_button_pressed_since_ms = raw_pressed ? now_ms : 0U;
  }

  if (s_button_stable_pressed &&
      !s_button_reset_armed &&
      ((uint32_t)(now_ms - s_button_pressed_since_ms) >= BOARD_SCV_RESET_HOLD_MS))
  {
    s_button_reset_armed = 1U;
    BoardResetScvAndReboot();
  }
}

static void BoardResetScvAndReboot(void)
{
  (void)HAL_IWDG_Refresh(&hiwdg);
  SCV_Erase();
  (void)HAL_IWDG_Refresh(&hiwdg);
  NVIC_SystemReset();
}

static void SPI1_UserInit(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_SPI1_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  HAL_GPIO_WritePin(LORA_CS_GPIO_Port, LORA_CS_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET);

  GPIO_InitStruct.Pin = LORA_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(LORA_CS_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LORA_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(LORA_RST_GPIO_Port, &GPIO_InitStruct);

  /* SPI1: PA5=SCK, PA6=MISO, PA7=MOSI */
  GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
        Error_Handler();
}

/* SPI2 initialisation for the SD card. SPI2 is also configured in the .ioc,
 * so after a CubeMX regen MX_SPI2_Init() performs the same setup; this
 * user-code copy keeps the pre-regen sources self-contained and is idempotent,
 * doing its own clock + GPIO setup. */
static void SPI2_UserInit(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_SPI2_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* SPI2: PB13=SCK, PB14=MISO, PB15=MOSI */
  GPIO_InitStruct.Pin = GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 7;
  hspi2.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi2.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
        Error_Handler();
}

/* Independent watchdog. Also configured in the .ioc (IWDG_PRESCALER_256,
 * reload 1249 — keep BOTH in sync!), so a CubeMX regen emits an identical
 * MX_IWDG_Init(); this user-code copy keeps pre-regen sources self-contained
 * and is idempotent alongside it, same pattern as SPIx_UserInit above.
 * LSI ~32 kHz / 256 = 125 Hz; reload 1249 gives a nominal 10 s timeout. */
static void IWDG_UserInit(void)
{
  hiwdg.Instance = IWDG;
  hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
  hiwdg.Init.Reload = 1249U;
  hiwdg.Init.Window = IWDG_WINDOW_DISABLE;
  if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
    Error_Handler();

  /* Freeze the IWDG while the core is halted by a debugger, so breakpoint
   * sessions don't reset mid-inspection. No effect in flight. */
  __HAL_DBGMCU_FREEZE_IWDG();
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
