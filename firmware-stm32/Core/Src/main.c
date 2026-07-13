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
#include "fatfs.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi2;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
#define SHORT_SIM_SECONDS   600U
#define FULL_FLIGHT_SECONDS 21600U
#define LOG_BUFFER_SIZE     4096U

typedef enum
{
  FLIGHT_READY   = 0,
  FLIGHT_ASCENT  = 1,
  FLIGHT_CRUISE  = 2,
  FLIGHT_DESCENT = 3,
  FLIGHT_LANDED  = 4
} FlightState;

typedef struct
{
  uint32_t time_s;
  uint8_t  state;
  int32_t  pressure_pa;
  int16_t  temperature_c10;
  int32_t  altitude_m;
  int16_t  vertical_speed_cm_s;
  int16_t  acc_x_mg;
  int16_t  acc_y_mg;
  int16_t  acc_z_mg;
  int16_t  gyro_x_dps;
  int16_t  gyro_y_dps;
  int16_t  gyro_z_dps;
  uint16_t battery_mV;
  int32_t  gps_lat_e7;
  int32_t  gps_lon_e7;
  uint16_t valid_flags;
} FakeSample;

static char     log_line[256];
static char     log_buffer[LOG_BUFFER_SIZE];
static uint32_t log_buffer_used = 0;

volatile uint8_t  test_done          = 0;
volatile uint8_t  test_status        = 0;
volatile FRESULT  last_fatfs_error   = FR_OK;
volatile uint32_t rows_written       = 0;
volatile uint32_t log_bytes          = 0;
volatile uint64_t estimated_6h_bytes = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_SPI2_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static const char *state_to_text(uint8_t state)
{
  switch (state)
  {
    case FLIGHT_READY:   return "READY";
    case FLIGHT_ASCENT:  return "ASCENT";
    case FLIGHT_CRUISE:  return "CRUISE";
    case FLIGHT_DESCENT: return "DESCENT";
    case FLIGHT_LANDED:  return "LANDED";
    default:             return "UNKNOWN";
  }
}

static FakeSample make_fake_sample(uint32_t t)
{
  FakeSample s;
  int32_t altitude_m;
  int16_t vertical_speed_cm_s;
  uint8_t state;

  if (t < 60)
  {
    state                = FLIGHT_READY;
    altitude_m           = 520;
    vertical_speed_cm_s  = 0;
  }
  else if (t < 12060)
  {
    state                = FLIGHT_ASCENT;
    altitude_m           = 520 + (int32_t)(t - 60) * 5;
    vertical_speed_cm_s  = 500;
  }
  else if (t < 14460)
  {
    state                = FLIGHT_CRUISE;
    altitude_m           = 30520 + (int32_t)((t % 120) - 60);
    vertical_speed_cm_s  = 0;
  }
  else if (t < 19960)
  {
    state       = FLIGHT_DESCENT;
    altitude_m  = 30520 - (int32_t)(t - 14460) * 6;
    if (altitude_m < 520) { altitude_m = 520; }
    vertical_speed_cm_s = -600;
  }
  else
  {
    state               = FLIGHT_LANDED;
    altitude_m          = 520;
    vertical_speed_cm_s = 0;
  }

  s.time_s             = t;
  s.state              = state;
  s.altitude_m         = altitude_m;
  s.vertical_speed_cm_s = vertical_speed_cm_s;

  s.pressure_pa = 101325 - altitude_m * 3;
  if (s.pressure_pa < 1000) { s.pressure_pa = 1000; }

  s.temperature_c10 = 150 - (int16_t)((altitude_m * 65) / 1000);
  if (s.temperature_c10 < -600) { s.temperature_c10 = -600; }

  s.acc_x_mg = (int16_t)(((int32_t)(t % 20) - 10) * 2);
  s.acc_y_mg = (int16_t)(((int32_t)(t % 30) - 15) * 2);
  s.acc_z_mg = 1000;
  if (state == FLIGHT_ASCENT)  { s.acc_z_mg += 20; }
  if (state == FLIGHT_DESCENT) { s.acc_z_mg -= 20; }

  s.gyro_x_dps  = (int16_t)((int32_t)(t % 40) - 20);
  s.gyro_y_dps  = (int16_t)((int32_t)(t % 50) - 25);
  s.gyro_z_dps  = (int16_t)((int32_t)(t % 60) - 30);

  s.battery_mV  = (uint16_t)(4200 - (t / 30));
  s.gps_lat_e7  = 481350000 + (int32_t)(t / 10);
  s.gps_lon_e7  = 115820000 + (int32_t)(t / 20);
  s.valid_flags = 0x000F;

  return s;
}

static FRESULT flush_log_buffer(FIL *file)
{
  UINT    bytes_written_now = 0;
  FRESULT result;

  if (log_buffer_used == 0) { return FR_OK; }

  result = f_write(file, log_buffer, log_buffer_used, &bytes_written_now);
  if (result != FR_OK) { return result; }
  if (bytes_written_now != log_buffer_used) { return FR_DISK_ERR; }

  log_buffer_used = 0;
  return FR_OK;
}

static FRESULT append_to_log_buffer(FIL *file, const char *text)
{
  uint32_t len = (uint32_t)strlen(text);
  FRESULT  result;

  if ((log_buffer_used + len) > LOG_BUFFER_SIZE)
  {
    result = flush_log_buffer(file);
    if (result != FR_OK) { return result; }
  }

  memcpy(&log_buffer[log_buffer_used], text, len);
  log_buffer_used += len;
  return FR_OK;
}

static FRESULT write_summary_file(uint32_t sim_seconds)
{
  FIL  summary_file;
  UINT bytes_written_now = 0;
  char summary_text[256];
  FRESULT result;

  result = f_open(&summary_file, "SUMMARY.TXT", FA_CREATE_ALWAYS | FA_WRITE);
  if (result != FR_OK) { return result; }

  /* Note: %llu requires newlib full (not nano). If summary shows 0 for
     estimated bytes, add -u _printf_long or switch from newlib-nano. */
  snprintf(summary_text, sizeof(summary_text),
             "sim_seconds=%lu\r\n"
             "rows=%lu\r\n"
             "log_bytes=%lu\r\n"
             "estimated_6h_bytes=%lu\r\n"
             "estimated_6h_MB=%lu\r\n",
             (unsigned long)sim_seconds,
             (unsigned long)rows_written,
             (unsigned long)log_bytes,
             (unsigned long)(estimated_6h_bytes),
             (unsigned long)(estimated_6h_bytes / 1000000ULL));

  result = f_write(&summary_file, summary_text, strlen(summary_text), &bytes_written_now);
  if (result == FR_OK)
  {
    result = f_close(&summary_file);
  }
  else
  {
    f_close(&summary_file);
  }
  return result;
}

static FRESULT run_fake_flight_sd_test(uint32_t sim_seconds)
{
  FIL        log_file;
  FRESULT    result;
  FakeSample s;

  rows_written       = 0;
  log_bytes          = 0;
  estimated_6h_bytes = 0;
  log_buffer_used    = 0;

  result = f_mount(&USERFatFS, USERPath, 1);
  if (result != FR_OK) { return result; }

  result = f_open(&log_file, "FLIGHT.CSV", FA_CREATE_ALWAYS | FA_WRITE);
  if (result != FR_OK)
  {
    f_mount(NULL, USERPath, 0);
    return result;
  }

  result = append_to_log_buffer(&log_file,
      "time_s,state,pressure_pa,temp_c10,altitude_m,vertical_speed_cm_s,"
      "acc_x_mg,acc_y_mg,acc_z_mg,gyro_x_dps,gyro_y_dps,gyro_z_dps,"
      "battery_mV,gps_lat_e7,gps_lon_e7,valid_flags\r\n");
  if (result != FR_OK)
  {
    f_close(&log_file);
    f_mount(NULL, USERPath, 0);
    return result;
  }

  for (uint32_t t = 0; t < sim_seconds; t++)
  {
    s = make_fake_sample(t);

    snprintf(log_line, sizeof(log_line),
             "%lu,%s,%ld,%d,%ld,%d,%d,%d,%d,%d,%d,%d,%u,%ld,%ld,%u\r\n",
             (unsigned long)s.time_s,
             state_to_text(s.state),
             (long)s.pressure_pa,
             (int)s.temperature_c10,
             (long)s.altitude_m,
             (int)s.vertical_speed_cm_s,
             (int)s.acc_x_mg,
             (int)s.acc_y_mg,
             (int)s.acc_z_mg,
             (int)s.gyro_x_dps,
             (int)s.gyro_y_dps,
             (int)s.gyro_z_dps,
             (unsigned int)s.battery_mV,
             (long)s.gps_lat_e7,
             (long)s.gps_lon_e7,
             (unsigned int)s.valid_flags);

    result = append_to_log_buffer(&log_file, log_line);
    if (result != FR_OK)
    {
      f_close(&log_file);
      f_mount(NULL, USERPath, 0);
      return result;
    }

    rows_written++;

    if ((rows_written % 64U) == 0U)
    {
      result = flush_log_buffer(&log_file);
      if (result != FR_OK)
      {
        f_close(&log_file);
        f_mount(NULL, USERPath, 0);
        return result;
      }
      result = f_sync(&log_file);
      if (result != FR_OK)
      {
        f_close(&log_file);
        f_mount(NULL, USERPath, 0);
        return result;
      }
    }
  }

  result = flush_log_buffer(&log_file);
  if (result != FR_OK)
  {
    f_close(&log_file);
    f_mount(NULL, USERPath, 0);
    return result;
  }

  result = f_sync(&log_file);
  if (result != FR_OK)
  {
    f_close(&log_file);
    f_mount(NULL, USERPath, 0);
    return result;
  }

  log_bytes = (uint32_t)f_size(&log_file);

  result = f_close(&log_file);
  if (result != FR_OK)
  {
    f_mount(NULL, USERPath, 0);
    return result;
  }

  estimated_6h_bytes = ((uint64_t)log_bytes * FULL_FLIGHT_SECONDS) / sim_seconds;

  result = write_summary_file(sim_seconds);

  /* Unmount cleanly so the FAT is flushed before power-down */
  f_mount(NULL, USERPath, 0);

  return result;
}
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

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_FATFS_Init();
  MX_SPI2_Init();
  /* USER CODE BEGIN 2 */

  HAL_Delay(500);
  HAL_UART_Transmit(&huart2, (uint8_t *)"Starting SD test\r\n", 18, 100);

  last_fatfs_error = run_fake_flight_sd_test(FULL_FLIGHT_SECONDS);

  if (last_fatfs_error == FR_OK)
  {
    test_status = 1;
    HAL_UART_Transmit(&huart2, (uint8_t *)"SD TEST OK\r\n", 12, 100);
  }
  else
  {
    test_status = 2;
    HAL_UART_Transmit(&huart2, (uint8_t *)"SD TEST FAILED\r\n", 16, 100);
  }

  test_done = 1;

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

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

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 10;
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
}

/**
  * @brief SPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  /* SPI2 parameter configuration*/
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
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */

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

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : SD_CS_Pin */
  GPIO_InitStruct.Pin = SD_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SD_CS_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
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
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
