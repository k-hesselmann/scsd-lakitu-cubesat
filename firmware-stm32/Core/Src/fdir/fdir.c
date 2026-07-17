#include "fdir/fdir.h"

#include "cdh/cdh.h"
#include "fdir/fdir_hooks.h"
#include "fdir/scv.h"
#include "fsw/fsm.h"
#include "main.h"

/* FDIR owns health policy only: it detects faults, sets recovery request
 * bits, and escalates. Hardware recovery is executed by the owning
 * subsystem (CDH, TTC, SD logger) — no HAL handles in this file. */

typedef struct {
    uint16_t mask;                      /* EQUIPMENT_x fault/request bit    */
    uint32_t timeout_ms;                /* elapsed ms of invalid data -> fault */
    const uint8_t *valid_flag;          /* into g_datapool                  */
    uint8_t *timeout_count;             /* into g_scv, or RAM-only (seconds stale, telemetry only) */
    uint32_t reinit_period_ms;          /* 0 = detection only, no recovery  */
    uint32_t last_request_ms;
    uint32_t last_ok_ms;                /* tick of last valid sample        */
} FDIR_Monitor_t;

static uint16_t s_reinit_requests;
static uint8_t  s_batt_timeout_count;   /* RAM-only: no SCV field for batt  */
static uint8_t  s_subsys_enabled[FDIR_SUBSYS_COUNT];
static uint32_t s_last_bus_restart_ms;
static uint32_t s_prev_datapool_timestamp_ms;
static uint32_t s_stale_since_ms;       /* 0 = datapool timestamp currently advancing */

/* One row per FMECA Section 4 debounce monitor. Escalation rules
 * (IMU ∧ baro -> bus restart, freshness, LoRa) stay explicit below.
 * TODO(FMECA C4/C6): IMU plausibility and GPS/baro altitude cross-check
 * monitors pending threshold agreement with the CDH owner. */
static FDIR_Monitor_t s_monitors[] = {
    { EQUIPMENT_GPS,     FDIR_GPS_TIMEOUT_MS,   &g_datapool.gps_valid,
      &g_scv.gps_timeout_count,   FDIR_GPS_REINIT_PERIOD_MS, 0U, 0U },
    { EQUIPMENT_IMU,     FDIR_IMU_TIMEOUT_MS,   &g_datapool.imu_valid,
      &g_scv.imu_timeout_count,   FDIR_REINIT_PERIOD_MS,     0U, 0U },
    { EQUIPMENT_BARO,    FDIR_BARO_TIMEOUT_MS,  &g_datapool.baro_valid,
      &g_scv.baro_timeout_count,  FDIR_REINIT_PERIOD_MS,     0U, 0U },
    { EQUIPMENT_CORAL,   FDIR_CORAL_TIMEOUT_MS, &g_datapool.coral_valid,
      &g_scv.coral_timeout_count, 0U,                        0U, 0U },
    { EQUIPMENT_EPS_ADC, FDIR_BATT_TIMEOUT_MS,  &g_datapool.batt_valid,
      &s_batt_timeout_count,      0U,                        0U, 0U },
};

#define FDIR_MONITOR_COUNT (sizeof(s_monitors) / sizeof(s_monitors[0]))

void FDIR_SetEquipmentFault(uint16_t mask, uint8_t active)
{
    if (active)
        g_scv.equipment_faults |= mask;
    else
        g_scv.equipment_faults &= (uint16_t)~mask;
}

uint16_t FDIR_GetReinitRequests(void)
{
    return s_reinit_requests;
}

void FDIR_AcknowledgeReinit(uint16_t mask)
{
    s_reinit_requests &= (uint16_t)~mask;
}

uint8_t FDIR_SubsystemEnabled(FDIR_Subsystem_t subsystem)
{
    if (subsystem >= FDIR_SUBSYS_COUNT)
        return 0U;

    return s_subsys_enabled[subsystem];
}

static uint8_t FDIR_CooldownElapsed(uint32_t now_ms, uint32_t *last_ms,
                                    uint32_t period_ms)
{
    if ((*last_ms == 0U) || ((uint32_t)(now_ms - *last_ms) >= period_ms))
    {
        *last_ms = now_ms;
        return 1U;
    }

    return 0U;
}

/* Staleness expressed in whole seconds, saturated to fit the uint8_t SCV
 * counters (kept for SD-log/telemetry compatibility). */
static uint8_t FDIR_StaleSeconds(uint32_t now_ms, uint32_t last_ok_ms)
{
    uint32_t seconds = (now_ms - last_ok_ms) / 1000U;

    return (seconds > UINT8_MAX) ? UINT8_MAX : (uint8_t)seconds;
}

static void FDIR_InitDefaults(SCV_t *scv)
{
    scv->magic = SCV_MAGIC;
    scv->boot_count = 0U;
    scv->mission_elapsed_ms = 0U;
    scv->flight_phase = PHASE_STANDBY;
    scv->reset_reason = RESET_REASON_UNKNOWN;
    scv->equipment_enabled = EQUIPMENT_ALL_NOMINAL;
    scv->equipment_faults = 0U;
    scv->gps_timeout_count = 0U;
    scv->imu_timeout_count = 0U;
    scv->baro_timeout_count = 0U;
    scv->coral_timeout_count = 0U;
    scv->lora_timeout_count = 0U;
    scv->sd_fault_count = 0U;
    scv->watchdog_reset_count = 0U;
    scv->last_batt_mv = SCV_INVALID_U16;
    scv->baro_ground_alt_cm = SCV_INVALID_I32;
    scv->crc16 = 0U;
}

static uint8_t FDIR_ClassifyResetReason(void)
{
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET)
        return RESET_REASON_WATCHDOG;

    if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) != RESET)
        return RESET_REASON_SOFTWARE;

    if ((__HAL_RCC_GET_FLAG(RCC_FLAG_BORRST) != RESET) ||
        (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST) != RESET))
        return RESET_REASON_POWER_ON;

    return RESET_REASON_UNKNOWN;
}

/* Equipment owned by each subsystem, for expressing reduced mode in the
 * persisted/downlinked equipment_enabled field. FSW owns no equipment; its
 * stage is visible via watchdog_reset_count instead. */
static const uint16_t s_subsys_equipment[FDIR_SUBSYS_COUNT] = {
    [FDIR_SUBSYS_SD]  = EQUIPMENT_SD,
    [FDIR_SUBSYS_CDH] = EQUIPMENT_GPS | EQUIPMENT_IMU | EQUIPMENT_BARO |
                        EQUIPMENT_CORAL | EQUIPMENT_EPS_ADC,
    [FDIR_SUBSYS_TTC] = EQUIPMENT_LORA,
    [FDIR_SUBSYS_FSW] = 0U,
};

/* Staged reduced mode (FMECA F2): after FDIR_WATCHDOG_RESET_LIMIT
 * consecutive watchdog resets, each further reset disables the next
 * subsystem update — SD, then CDH, then TTC, then FSW. Every disabled
 * subsystem removes a potential hang source; FDIR and the IWDG kick stay
 * alive at every stage, so a stage that stops the resets is where the
 * system settles.
 *
 * The stage is also expressed in equipment_enabled, so the degraded
 * configuration is visible in telemetry and the post-flight log. The mask
 * is rebuilt from ALL_NOMINAL each boot (a non-watchdog reset re-arms
 * everything); a future give-up isolation policy that also clears enabled
 * bits must keep its state elsewhere or it will be undone here. */
static void FDIR_ApplyReducedMode(SCV_t *scv)
{
    uint8_t resets = scv->watchdog_reset_count;

    scv->equipment_enabled = EQUIPMENT_ALL_NOMINAL;
    for (uint8_t i = 0U; i < (uint8_t)FDIR_SUBSYS_COUNT; i++)
    {
        s_subsys_enabled[i] =
            (resets < (uint8_t)(FDIR_WATCHDOG_RESET_LIMIT + i)) ? 1U : 0U;
        if (!s_subsys_enabled[i])
            scv->equipment_enabled &= (uint16_t)~s_subsys_equipment[i];
    }
}

void FDIR_Init(SCV_t *scv)
{
    uint8_t reset_reason;

    if (!SCV_Load(scv) || scv->magic != SCV_MAGIC)
        FDIR_InitDefaults(scv);

    reset_reason = FDIR_ClassifyResetReason();
    scv->reset_reason = reset_reason;

    /* watchdog_reset_count tracks *consecutive* watchdog resets: any other
     * reset reason proves the boot loop is broken and re-arms all stages. */
    if (reset_reason == RESET_REASON_WATCHDOG)
    {
        if (scv->watchdog_reset_count < UINT8_MAX)
            scv->watchdog_reset_count++;
    }
    else
    {
        scv->watchdog_reset_count = 0U;
    }

    if (scv->boot_count < SCV_INVALID_U32)
        scv->boot_count++;

    FDIR_ApplyReducedMode(scv);

    s_reinit_requests = 0U;
    s_batt_timeout_count = 0U;
    s_last_bus_restart_ms = 0U;
    s_prev_datapool_timestamp_ms = 0U;
    s_stale_since_ms = 0U;
    for (uint32_t i = 0U; i < FDIR_MONITOR_COUNT; i++)
    {
        s_monitors[i].last_request_ms = 0U;
        s_monitors[i].last_ok_ms = HAL_GetTick();
    }

    __HAL_RCC_CLEAR_RESET_FLAGS();

    /* Persist boot_count/reset_reason (and reduced-mode evidence) now. */
    SCV_Backup(scv);
}

static void FDIR_RunMonitor(FDIR_Monitor_t *m, const SCV_t *scv,
                            uint32_t now_ms)
{
    if (*m->valid_flag)
    {
        m->last_ok_ms = now_ms;
        *m->timeout_count = 0U;
        FDIR_SetEquipmentFault(m->mask, 0U);
        return;
    }

    *m->timeout_count = FDIR_StaleSeconds(now_ms, m->last_ok_ms);

    if ((now_ms - m->last_ok_ms) < m->timeout_ms)
        return;

    FDIR_SetEquipmentFault(m->mask, 1U);

    if ((m->reinit_period_ms != 0U) &&
        (scv->equipment_enabled & m->mask) &&
        FDIR_CooldownElapsed(now_ms, &m->last_request_ms, m->reinit_period_ms))
    {
        s_reinit_requests |= m->mask;
    }
}

void FDIR_Update(SensorData_t *dp, SCV_t *scv)
{
    uint32_t now_ms = HAL_GetTick();

    /* Sensor monitors only run while CDH is sampling — with CDH disabled by
     * reduced mode every valid flag is permanently 0, and faulting equipment
     * nobody reads would put phantom faults in telemetry.
     *
     * TODO(give-up policy, optional next step): after N reinit requests with
     * no recovery, clear the equipment_enabled bit to stop requesting and
     * bothering the bus. Needs its own persisted mask (or a rule merged
     * here) so FDIR_ApplyReducedMode's rebuild doesn't undo it, plus a
     * re-arm condition (e.g. power-on reset). */
    if (s_subsys_enabled[FDIR_SUBSYS_CDH])
    {
        for (uint32_t i = 0U; i < FDIR_MONITOR_COUNT; i++)
            FDIR_RunMonitor(&s_monitors[i], scv, now_ms);
    }

    if (dp->batt_valid)
        scv->last_batt_mv = dp->batt_voltage_mv;

    /* L2 escalation (FMECA C7): both fast I2C monitors faulted means the
     * shared bus is suspect, not the devices. CDH owns the bus and executes
     * the restart; GPS shares the bus and recovers with it. */
    if (((scv->equipment_faults & (EQUIPMENT_IMU | EQUIPMENT_BARO)) ==
         (EQUIPMENT_IMU | EQUIPMENT_BARO)) &&
        FDIR_CooldownElapsed(now_ms, &s_last_bus_restart_ms,
                             FDIR_REINIT_PERIOD_MS))
    {
        CDH_RequestBusRestart();
    }

    /* LoRa (FMECA T1): TTC reports send results per attempt; FDIR debounces
     * consecutive failures and requests a radio reinit (RST pulse), which
     * TTC executes at its next transmit slot. Skipped when reduced mode
     * disabled TTC — no attempts are being made. */
    if (s_subsys_enabled[FDIR_SUBSYS_TTC])
    {
        uint8_t tx_failures = TTC_ConsecutiveTxFailures();

        scv->lora_timeout_count = tx_failures;   /* persisted via SCV backup */

        if (tx_failures >= FDIR_LORA_TX_FAILURE_LIMIT)
        {
            static uint32_t s_last_lora_request_ms;
            if (FDIR_CooldownElapsed(now_ms, &s_last_lora_request_ms,
                                     FDIR_REINIT_PERIOD_MS))
                s_reinit_requests |= EQUIPMENT_LORA;
        }
    }

    /* CDH freshness (FMECA C10): CDH_Update writes timestamp_ms every cycle;
     * a stall means CDH is blocked or the datapool is stale. Escalate to a
     * bus restart; if that never helps, the health gate below stops kicking
     * the IWDG. Skipped when reduced mode disabled CDH on purpose. */
    if (s_subsys_enabled[FDIR_SUBSYS_CDH])
    {
        if (dp->timestamp_ms == s_prev_datapool_timestamp_ms)
        {
            if (s_stale_since_ms == 0U)
                s_stale_since_ms = (now_ms != 0U) ? now_ms : 1U; /* keep 0 = "not stale" */
        }
        else
        {
            s_stale_since_ms = 0U;
        }
        s_prev_datapool_timestamp_ms = dp->timestamp_ms;

        if ((s_stale_since_ms != 0U) &&
            ((now_ms - s_stale_since_ms) >= FDIR_FRESHNESS_MS))
        {
            FDIR_SetEquipmentFault(EQUIPMENT_CDH, 1U);
            if (FDIR_CooldownElapsed(now_ms, &s_last_bus_restart_ms,
                                     FDIR_REINIT_PERIOD_MS))
                CDH_RequestBusRestart();
        }
        else
        {
            FDIR_SetEquipmentFault(EQUIPMENT_CDH, 0U);
        }
    }
}

uint8_t FDIR_SystemHealthyEnoughToKickWatchdog(void)
{
    /* CDH intentionally off in reduced mode: staleness is expected. */
    if (!s_subsys_enabled[FDIR_SUBSYS_CDH])
        return 1U;

    /* Datapool stale for this long despite bus restarts: let the IWDG fire
     * (L3) — reset plus SCV restore is the remaining recovery (FMECA C10). */
    if (s_stale_since_ms == 0U)
        return 1U;

    return ((HAL_GetTick() - s_stale_since_ms) < FDIR_FRESHNESS_KICK_MS) ? 1U : 0U;
}
