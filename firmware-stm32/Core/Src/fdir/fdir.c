#include "fdir/fdir.h"

#include "cdh/cdh_fdir.h"
#include "fsw/fsm.h"
#include "main.h"

extern I2C_HandleTypeDef hi2c1;

static CDH_FDIR_Context s_cdh_fdir;
static uint32_t s_last_imu_reinit_ms;
static uint32_t s_last_baro_reinit_ms;
static uint32_t s_last_i2c_bus_restart_ms;

static void FDIR_SetFault(SCV_t *scv, uint16_t mask, uint8_t active)
{
    if (active)
        scv->equipment_faults |= mask;
    else
        scv->equipment_faults &= (uint16_t)~mask;
}

static uint8_t FDIR_ReinitDue(uint32_t now_ms, uint32_t *last_ms)
{
    if ((*last_ms == 0U) || ((now_ms - *last_ms) >= FDIR_REINIT_PERIOD_MS))
    {
        *last_ms = now_ms;
        return 1U;
    }

    return 0U;
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

void FDIR_Init(SCV_t *scv)
{
    uint8_t reset_reason;

    if (scv->magic != SCV_MAGIC)
        FDIR_InitDefaults(scv);

    reset_reason = FDIR_ClassifyResetReason();
    scv->reset_reason = reset_reason;
    if (reset_reason == RESET_REASON_WATCHDOG &&
        scv->watchdog_reset_count < UINT8_MAX)
    {
        scv->watchdog_reset_count++;
    }

    if (scv->boot_count < SCV_INVALID_U32)
        scv->boot_count++;

    CDH_FDIR_Init(&s_cdh_fdir);
    s_last_imu_reinit_ms = 0U;
    s_last_baro_reinit_ms = 0U;
    s_last_i2c_bus_restart_ms = 0U;

    __HAL_RCC_CLEAR_RESET_FLAGS();
}

void FDIR_Update(SensorData_t *dp, SCV_t *scv)
{
    uint32_t now_ms = HAL_GetTick();

    CDH_FDIR_BusRestart_Process(&s_cdh_fdir, &hi2c1);
    dp->i2c_bus_state = s_cdh_fdir.bus_state;

    if (dp->gps_valid)
    {
        scv->gps_timeout_count = 0U;
        FDIR_SetFault(scv, EQUIPMENT_GPS, 0U);
    }
    else if (scv->gps_timeout_count < UINT8_MAX)
    {
        scv->gps_timeout_count++;
        if (scv->gps_timeout_count >= FDIR_GPS_TIMEOUT_LIMIT)
            FDIR_SetFault(scv, EQUIPMENT_GPS, 1U);
    }

    if (dp->imu_valid)
    {
        scv->imu_timeout_count = 0U;
        FDIR_SetFault(scv, EQUIPMENT_IMU, 0U);
    }
    else
    {
        if (scv->imu_timeout_count < UINT8_MAX)
            scv->imu_timeout_count++;

        if (scv->imu_timeout_count >= FDIR_IMU_TIMEOUT_LIMIT)
        {
            FDIR_SetFault(scv, EQUIPMENT_IMU, 1U);
            if ((scv->equipment_enabled & EQUIPMENT_IMU) &&
                FDIR_ReinitDue(now_ms, &s_last_imu_reinit_ms))
            {
                CDH_FDIR_RecoverIMU(&hi2c1);
            }
        }
    }

    if (dp->baro_valid)
    {
        scv->baro_timeout_count = 0U;
        FDIR_SetFault(scv, EQUIPMENT_BARO, 0U);
    }
    else
    {
        if (scv->baro_timeout_count < UINT8_MAX)
            scv->baro_timeout_count++;

        if (scv->baro_timeout_count >= FDIR_BARO_TIMEOUT_LIMIT)
        {
            FDIR_SetFault(scv, EQUIPMENT_BARO, 1U);
            if ((scv->equipment_enabled & EQUIPMENT_BARO) &&
                FDIR_ReinitDue(now_ms, &s_last_baro_reinit_ms))
            {
                CDH_FDIR_RecoverBaro(&hi2c1);
            }
        }
    }

    if (((scv->equipment_faults & (EQUIPMENT_IMU | EQUIPMENT_BARO)) ==
         (EQUIPMENT_IMU | EQUIPMENT_BARO)) &&
        FDIR_ReinitDue(now_ms, &s_last_i2c_bus_restart_ms))
    {
        CDH_FDIR_BusRestart_Start(&s_cdh_fdir);
    }

    if (dp->coral_valid)
    {
        scv->coral_timeout_count = 0U;
        FDIR_SetFault(scv, EQUIPMENT_CORAL, 0U);
    }
    else if (scv->coral_timeout_count < UINT8_MAX)
    {
        scv->coral_timeout_count++;
        if (scv->coral_timeout_count >= FDIR_CORAL_TIMEOUT_LIMIT)
            FDIR_SetFault(scv, EQUIPMENT_CORAL, 1U);
    }

    if (dp->batt_valid)
    {
        scv->last_batt_mv = dp->batt_voltage_mv;
        FDIR_SetFault(scv, EQUIPMENT_EPS_ADC, 0U);
    }
    else
    {
        FDIR_SetFault(scv, EQUIPMENT_EPS_ADC, 1U);
    }

    scv->mission_elapsed_ms = now_ms;
}

uint8_t FDIR_SystemHealthyEnoughToKickWatchdog(void)
{
    return 1U;
}
