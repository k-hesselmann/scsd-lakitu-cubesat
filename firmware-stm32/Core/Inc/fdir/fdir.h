#ifndef FDIR_FDIR_H
#define FDIR_FDIR_H

#include "datapool.h"

/* Debounce limits (superloop cycles) — see docs/FMECA.md Section 4.
 * All SCV timeout counters saturate at UINT8_MAX during a continuous fault
 * (never wrap — a wrap would clear the fault spuriously) and reset to zero
 * as soon as valid data returns. */
#define FDIR_GPS_TIMEOUT_LIMIT        30U
#define FDIR_IMU_TIMEOUT_LIMIT        3U
#define FDIR_BARO_TIMEOUT_LIMIT       3U
#define FDIR_CORAL_TIMEOUT_LIMIT      5U
#define FDIR_BATT_TIMEOUT_LIMIT       3U
#define FDIR_LORA_TX_FAILURE_LIMIT    3U
#define FDIR_FRESHNESS_LIMIT          2U
#define FDIR_SD_FAULT_LIMIT           3U
#define FDIR_WATCHDOG_RESET_LIMIT     3U

/* Cooldowns between recovery requests for the same equipment. GPS gets a
 * longer one: re-initialising the module too often disturbs fix acquisition
 * and most GPS outages are environmental (FMECA C1). */
#define FDIR_REINIT_PERIOD_MS         10000U
#define FDIR_GPS_REINIT_PERIOD_MS     60000U

/* Withhold the IWDG kick (forcing a reset) once the datapool has been stale
 * this many cycles despite bus-restart attempts — L3 escalation of FMECA C10. */
#define FDIR_FRESHNESS_KICK_LIMIT     30U

/* Reduced-mode order (FMECA F2/L4): each watchdog reset beyond
 * FDIR_WATCHDOG_RESET_LIMIT disables the next subsystem's update call. */
typedef enum {
    FDIR_SUBSYS_SD = 0,
    FDIR_SUBSYS_CDH,
    FDIR_SUBSYS_TTC,
    FDIR_SUBSYS_FSW,
    FDIR_SUBSYS_COUNT
} FDIR_Subsystem_t;

void FDIR_Init(SCV_t *scv);
void FDIR_Update(SensorData_t *dp, SCV_t *scv);

/* Recovery request transport: FDIR sets EQUIPMENT_x bits, the owning
 * subsystem executes its own recovery and acknowledges the bit. FDIR never
 * touches hardware (see docs/FMECA.md Section 2, L1/L2). */
uint16_t FDIR_GetReinitRequests(void);
void FDIR_AcknowledgeReinit(uint16_t mask);

/* Single shared writer for g_scv.equipment_faults bits. Subsystems that
 * detect their own faults (TTC, SD logger) report through this instead of
 * writing the field directly. */
void FDIR_SetEquipmentFault(uint16_t mask, uint8_t active);

/* 0 while the subsystem is disabled by staged reduced mode. Evaluated once
 * at boot from watchdog_reset_count; consulted by main() around each
 * *_Init/_Update call. FDIR itself and the IWDG kick always run. */
uint8_t FDIR_SubsystemEnabled(FDIR_Subsystem_t subsystem);

uint8_t FDIR_SystemHealthyEnoughToKickWatchdog(void);

#endif /* FDIR_FDIR_H */
