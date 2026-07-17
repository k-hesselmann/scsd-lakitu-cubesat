#include "cdh/battery_adc.h"

/* V_batt = V_pa0 * (22k + 10k) / 10k */
#define BATTERY_DIVIDER_NUMERATOR      32U
#define BATTERY_DIVIDER_DENOMINATOR    10U

#define ADC_VREF_MV                    3300U
#define ADC_FULL_SCALE                 4095U
#define ADC_CONV_TIMEOUT_MS            10U

uint8_t BatteryADC_Read(ADC_HandleTypeDef *hadc, uint16_t *batt_mv)
{
    if (HAL_ADC_Start(hadc) != HAL_OK)
        return 0U;

    if (HAL_ADC_PollForConversion(hadc, ADC_CONV_TIMEOUT_MS) != HAL_OK)
    {
        HAL_ADC_Stop(hadc);
        return 0U;
    }

    uint32_t raw = HAL_ADC_GetValue(hadc);
    HAL_ADC_Stop(hadc);

    uint32_t pa0_mv = (raw * ADC_VREF_MV) / ADC_FULL_SCALE;
    *batt_mv = (uint16_t)((pa0_mv * BATTERY_DIVIDER_NUMERATOR) / BATTERY_DIVIDER_DENOMINATOR);
    return 1U;
}
