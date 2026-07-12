#ifndef FDIR_FDIR_H
#define FDIR_FDIR_H

#include "datapool.h"

#define FDIR_GPS_TIMEOUT_LIMIT        30U
#define FDIR_IMU_TIMEOUT_LIMIT        3U
#define FDIR_BARO_TIMEOUT_LIMIT       3U
#define FDIR_CORAL_TIMEOUT_LIMIT      5U
#define FDIR_SD_FAULT_LIMIT           3U
#define FDIR_WATCHDOG_RESET_LIMIT     3U
#define FDIR_REINIT_PERIOD_MS         10000U

void FDIR_Init(SCV_t *scv);
void FDIR_Update(SensorData_t *dp, SCV_t *scv);
uint8_t FDIR_SystemHealthyEnoughToKickWatchdog(void);

#endif /* FDIR_FDIR_H */
