#ifndef CDH_BATTERY_ADC_H
#define CDH_BATTERY_ADC_H

#include "main.h"
#include <stdint.h>

/* Reads the EPS battery voltage via ADC1 (PA0 = ADC1_IN5), which sits at the
 * midpoint of a 22k (to VBAT) / 10k (to GND) divider. Fills *batt_mv and
 * returns 1 on a successful conversion; returns 0 and leaves *batt_mv
 * unchanged if the ADC did not complete in time. */
uint8_t BatteryADC_Read(ADC_HandleTypeDef *hadc, uint16_t *batt_mv);

#endif /* CDH_BATTERY_ADC_H */
