#ifndef CDH_CDH_H
#define CDH_CDH_H

#include "datapool.h"

/* Initialise all sensors and record the ground pressure baseline.
 * Must be called once before the superloop starts. */
void CDH_Init(void);

/* Read all sensors, update g_datapool, update g_scv health fields.
 * Called once per 1 Hz superloop tick. */
void CDH_Update(SensorData_t *dp, SCV_t *scv);

/* Request an I2C bus restart. Called by FDIR when it decides recovery is due;
 * CDH owns the bus hardware and executes the (multi-tick) restart in CDH_Update. */
void CDH_RequestBusRestart(void);

#endif /* CDH_CDH_H */
