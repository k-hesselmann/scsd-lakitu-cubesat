#include "main.h"
#include "usb_device.h"
#include "datapool.h"
#include "fsw/fsm.h"
#include "cdh/cdh.h"
#include "ttc/ttc.h"
#include <string.h>

#define FSW_USE_DUMMY_DATAPOOL 1

I2C_HandleTypeDef hi2c1;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
#if FSW_USE_DUMMY_DATAPOOL
static void FSW_UpdateDummyDatapool(SensorData_t *dp);
#endif

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_I2C1_Init();
    MX_USB_DEVICE_Init();

#if !FSW_USE_DUMMY_DATAPOOL
    CDH_Init();
#endif
    FSW_Init();
    TTC_Init();

    TelemetryPacket_t tx_packet = {0};

    while (1)
    {
#if FSW_USE_DUMMY_DATAPOOL
        FSW_UpdateDummyDatapool(&g_datapool);
#else
        CDH_Update(&g_datapool, &g_scv);
#endif
        FSW_Update(&g_datapool);
        FSW_BuildTelemetryPacket(&g_datapool, &tx_packet);
        TTC_Transmit(&tx_packet);
        /* TODO: HAL_IWDG_Refresh(&hiwdg); */
        HAL_Delay(1000);
    }
}

#if FSW_USE_DUMMY_DATAPOOL
static void FSW_UpdateDummyDatapool(SensorData_t *dp)
{
    uint32_t now_ms = HAL_GetTick();
    uint32_t t_s = now_ms / 1000U;

    memset(dp, 0, sizeof(*dp));

    dp->timestamp_ms = now_ms;
    dp->gps_valid = 1U;
    dp->imu_valid = 1U;
    dp->baro_valid = 1U;
    dp->batt_valid = 1U;
    dp->coral_valid = 1U;
    dp->batt_voltage_mv = 7400U;
    dp->imu_accel_z_g = 1.0f;
    dp->imu_accel_mag_g = 1.0f;

    if (t_s < 3U)
    {
        dp->baro_alt_m = 0.0f;
        dp->gps_vvel_mps = 0.0f;
        dp->gps_speed_mps = 0.0f;
    }
    else if (t_s < 8U)
    {
        dp->imu_accel_z_g = 1.8f;
        dp->imu_accel_mag_g = 1.8f;
        dp->baro_alt_m = 5.0f + (float)(t_s - 3U) * 20.0f;
        dp->gps_vvel_mps = 4.0f;
        dp->gps_speed_mps = 4.0f;
    }
    else if (t_s < 40U)
    {
        dp->baro_alt_m = 120.0f + (float)(t_s - 8U) * 60.0f;
        dp->gps_alt_m = dp->baro_alt_m;
        dp->gps_vvel_mps = 6.0f;
        dp->gps_speed_mps = 6.0f;
    }
    else if (t_s < 80U)
    {
        dp->baro_alt_m = 2040.0f;
        dp->gps_alt_m = dp->baro_alt_m;
        dp->gps_vvel_mps = 0.1f;
        dp->gps_speed_mps = 0.2f;
    }
    else if (t_s < 105U)
    {
        dp->imu_accel_z_g = 1.7f;
        dp->imu_accel_mag_g = 1.7f;
        dp->baro_alt_m = 2040.0f - (float)(t_s - 80U) * 75.0f;
        dp->gps_alt_m = dp->baro_alt_m;
        dp->gps_vvel_mps = -5.0f;
        dp->gps_speed_mps = 8.0f;
    }
    else
    {
        dp->baro_alt_m = 100.0f;
        dp->gps_alt_m = dp->baro_alt_m;
        dp->gps_vvel_mps = 0.0f;
        dp->gps_speed_mps = 0.3f;
    }
}
#endif

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
        Error_Handler();

    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_MSI;
    RCC_OscInitStruct.MSIState            = RCC_MSI_ON;
    RCC_OscInitStruct.MSICalibrationValue = 0;
    RCC_OscInitStruct.MSIClockRange       = RCC_MSIRANGE_6;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_MSI;
    RCC_OscInitStruct.PLL.PLLM            = 1;
    RCC_OscInitStruct.PLL.PLLN            = 40;
    RCC_OscInitStruct.PLL.PLLP            = RCC_PLLP_DIV7;
    RCC_OscInitStruct.PLL.PLLQ            = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR            = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
        Error_Handler();

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
        Error_Handler();
}

static void MX_I2C1_Init(void)
{
    hi2c1.Instance             = I2C1;
    hi2c1.Init.Timing          = 0x10D19CE4;
    hi2c1.Init.OwnAddress1     = 0;
    hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2     = 0;
    hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK)
        Error_Handler();

    if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
        Error_Handler();

    if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
        Error_Handler();
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin  = B1_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin   = LD2_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
}
#endif
