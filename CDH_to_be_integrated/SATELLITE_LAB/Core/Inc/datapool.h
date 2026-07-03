#ifndef DATAPOOL_H
#define DATAPOOL_H

#include "main.h"
#include "mpu6050_equipment_handler.h"
#include "ms5607_equipment_handler.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
  uint32_t test_counter;
  bool connection_ready;
  MPU6050_EquipmentHandler imu;
  MS5607_EquipmentHandler altimeter;
} Datapool;

extern Datapool g_datapool;

void Datapool_Init(I2C_HandleTypeDef *hi2c);
void Datapool_Update(I2C_HandleTypeDef *hi2c);
void Datapool_PrintDebug(void);

#endif
