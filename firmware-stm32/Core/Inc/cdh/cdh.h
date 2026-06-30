#ifndef CDH_CDH_H
#define CDH_CDH_H

#include "datapool.h"

/* Initialise all sensors and record the ground pressure baseline.
 * Must be called once before the superloop starts. */
void CDH_Init(void);

/* Read all sensors, update g_datapool, update g_scv health fields.
 * Called once per 1 Hz superloop tick. */
void CDH_Update(SensorData_t *dp, SCV_t *scv);

#endif /* CDH_CDH_H */
