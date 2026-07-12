#ifndef CDH_FDIR_H
#define CDH_FDIR_H

#include "main.h"

#define CDH_FDIR_BUS_IDLE 0
#define CDH_FDIR_BUS_RESTART_IN_PROGRESS 1
#define CDH_FDIR_BUS_RESTART_HOLD 2
#define CDH_FDIR_BUS_RESTART_HOLD_TIME_MS 50

typedef struct {
  uint8_t bus_state;
  uint32_t bus_restart_start_ms;
} CDH_FDIR_Context;

void CDH_FDIR_Init(CDH_FDIR_Context *fdir);
void CDH_FDIR_RecoverIMU(I2C_HandleTypeDef *hi2c);
void CDH_FDIR_RecoverBaro(I2C_HandleTypeDef *hi2c);
void CDH_FDIR_BusRestart_Start(CDH_FDIR_Context *fdir);
void CDH_FDIR_BusRestart_Process(CDH_FDIR_Context *fdir, I2C_HandleTypeDef *hi2c);

#endif
