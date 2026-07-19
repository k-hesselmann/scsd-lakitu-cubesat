#ifndef FDIR_FDIR_TEST_HOOKS_H
#define FDIR_FDIR_TEST_HOOKS_H

#include "datapool.h"

/* Bench fault-injection console for docs/FMECA.md Section 5 rows marked
 * "(test hook)": IMU garbage/stuck values (C4), baro drift (C6), ADC
 * misconfiguration (C8), SCV erase (F3). Entirely compiled out unless the
 * build defines FDIR_TEST_HOOKS (see platformio.ini
 * [env:nucleo_l476rg_testhooks]) — never present in the flight image.
 *
 * Commands arrive as ASCII lines over the existing USART2 debug UART
 * (115200 8N1, same port as cdh_debug.c prints):
 *   HOOK IMU FREEZE <seconds>   stick imu_valid=1 but accel/gyro pinned to
 *                                an implausible constant, for <seconds>
 *   HOOK BARO OFFSET <meters>   add a persistent signed offset to baro_alt_m
 *                                (until HOOK CLEAR); simulates C6 drift
 *   HOOK ADC FAULT <seconds>    force batt_valid=0 for <seconds>
 *   HOOK SCV ERASE               erase the SCV flash page now (F3)
 *   HOOK STATUS                  print currently active hooks
 *   HOOK CLEAR                   cancel all active hooks
 *
 * Two call sites, both inside CDH_Update() (cdh.c) rather than main.c: the
 * IMU/baro overrides must land in the datapool *before*
 * SensorValidation_Update() runs, or the C4/C6 plausibility checks never see
 * the injected fault before the next real sensor read overwrites it; the ADC
 * override must land *after* BatteryADC_Read(), for the same reason in
 * reverse. A single post-CDH_Update call site cannot satisfy both. */

void FDIR_TestHooks_Init(void);

/* Call once per CDH_Update(), after the raw GPS/IMU/Baro copies and before
 * SensorValidation_Update(). Polls the UART command console and applies any
 * active IMU/baro override. No-op build when FDIR_TEST_HOOKS is not defined. */
void FDIR_TestHooks_PreValidation(SensorData_t *dp);

/* Call once per CDH_Update(), immediately after BatteryADC_Read(). Applies
 * an active ADC-fault override. No-op build when FDIR_TEST_HOOKS is not
 * defined. */
void FDIR_TestHooks_PostAdcRead(SensorData_t *dp);

#endif /* FDIR_FDIR_TEST_HOOKS_H */
