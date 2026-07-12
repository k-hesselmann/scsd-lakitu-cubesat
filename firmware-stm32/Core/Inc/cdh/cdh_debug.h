#ifndef CDH_DEBUG_H
#define CDH_DEBUG_H

#include "cdh/gps_equipment_handler.h"
#include "cdh/ms5607_equipment_handler.h"

void CDH_Debug_PrintGPS(GPS_EquipmentHandler *gps);
void CDH_Debug_PrintBaro(MS5607_EquipmentHandler *baro);

#endif
