#ifndef OBSERVABILITY_H
#define OBSERVABILITY_H

#include "datapool.h"
#include <stdint.h>

void Observability_Init(void);
void Observability_Update(uint32_t now_ms, const SensorData_t *datapool);

#endif /* OBSERVABILITY_H */
