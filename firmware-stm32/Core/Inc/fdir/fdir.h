#ifndef FDIR_FDIR_H
#define FDIR_FDIR_H

#include "datapool.h"

/* Data-staleness fault thresholds, in elapsed milliseconds since the last
 * valid sample — see docs/FMECA.md Section 4. Rate-independent: the superloop
 * runs FDIR_Update every iteration (no fixed tick), so a cycle-count debounce
 * would trip at whatever rate the loop happens to spin at. The persisted SCV
 * timeout_count fields report elapsed seconds instead (saturated at
 * UINT8_MAX) purely for telemetry/SD-log compatibility; they are not compared
 * against these thresholds directly. */
#define FDIR_GPS_TIMEOUT_MS           30000U
#define FDIR_IMU_TIMEOUT_MS           3000U
#define FDIR_BARO_TIMEOUT_MS          3000U
#define FDIR_CORAL_TIMEOUT_MS         5000U
#define FDIR_BATT_TIMEOUT_MS          3000U
#define FDIR_LORA_TX_FAILURE_LIMIT    5U      /* consecutive TX attempts, not time */
/* LoRa TX failure-rate windows (FMECA T1): ring-buffered pass/fail over the
 * last N TX attempts. Recovery trips on either the consecutive-failure limit
 * above or a sustained failure rate; the SCV fault uses a tighter, shorter
 * window so it lags recovery and reflects "still bad after trying." Plain
 * integer counts (not percentages) to avoid floating point. */
#define FDIR_LORA_RECOVERY_WINDOW        20U
#define FDIR_LORA_RECOVERY_WINDOW_FAILS  12U   /* 60% of 20 */
#define FDIR_LORA_FAULT_WINDOW           10U
#define FDIR_LORA_FAULT_WINDOW_FAILS      9U   /* 90% of 10 */
#define FDIR_FRESHNESS_MS             2000U
#define FDIR_SD_REINIT_PERIOD_MS      5000U
#define FDIR_WATCHDOG_RESET_LIMIT     3U      /* consecutive boots, not time */

/* Cooldowns between recovery requests for the same equipment. GPS gets a
 * longer one: re-initialising the module too often disturbs fix acquisition
 * and most GPS outages are environmental (FMECA C1). */
#define FDIR_REINIT_PERIOD_MS         10000U
#define FDIR_GPS_REINIT_PERIOD_MS     60000U

/* Withhold the IWDG kick (forcing a reset) once the datapool has been stale
 * this long despite bus-restart attempts — L3 escalation of FMECA C10. */
#define FDIR_FRESHNESS_KICK_MS        30000U

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
 * touches hardware (see docs/FMECA.md Section 2, L1/L2). LoRa recovery
 * (EQUIPMENT_LORA) and I2C bus restart (FDIR_REQUEST_I2C_BUS_RESTART below)
 * both go through this same mechanism, polled from TTC_Service()/CDH_Update()
 * respectively. */
uint16_t FDIR_GetReinitRequests(void);
void FDIR_AcknowledgeReinit(uint16_t mask);

/* Request-only pseudo bit in the FDIR_GetReinitRequests() bitmask: not an
 * equipment identity, never part of EQUIPMENT_ALL_NOMINAL/equipment_faults
 * (same idea as EQUIPMENT_CDH in datapool.h, one bit lower). Set by FDIR's
 * I2C bus-wedge/freshness escalation, consumed by CDH_Update(), which calls
 * CDH_FDIR_BusRestart_Start() and acknowledges. */
#define FDIR_REQUEST_I2C_BUS_RESTART  (1U << 14)

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
