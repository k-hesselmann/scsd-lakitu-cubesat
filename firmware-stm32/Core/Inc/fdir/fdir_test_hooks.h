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
 */

void FDIR_TestHooks_Init(void);

/* Call once per superloop iteration, after CDH_Update/FSW_Update (if they ran
 * this iteration) and before FDIR_Update — so an injected value survives the
 * cycle's real sensor read and is what FDIR actually evaluates. No-op build
 * (empty function) when FDIR_TEST_HOOKS is not defined. */
void FDIR_TestHooks_Poll(SensorData_t *dp, SCV_t *scv);

#endif /* FDIR_FDIR_TEST_HOOKS_H */
