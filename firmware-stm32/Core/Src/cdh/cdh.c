#include "cdh/cdh.h"
#include "fsw/fsm.h"
#include "fsw/fsm_thresholds.h"
#include "cdh/mpu6050_equipment_handler.h"
#include "cdh/ms5607_equipment_handler.h"
#include "cdh/gps_equipment_handler.h"
#include "cdh/battery_adc.h"
#include "cdh/datapool_nvm.h"
#include "cdh/cdh_debug.h"
#include "cdh/cdh_fdir.h"
#include "cdh/coral.h"
#include "cdh/sensor_validation.h"
#include "fdir/fdir.h"
#include "fdir/fdir_test_hooks.h"

#include "main.h"
#include <math.h>

extern I2C_HandleTypeDef hi2c1;
extern ADC_HandleTypeDef hadc1;

static MPU6050_EquipmentHandler s_imu;
static MS5607_EquipmentHandler  s_baro;
static GPS_EquipmentHandler     s_gps;
static CDH_FDIR_Context         s_fdir;
static float                    s_ground_baro_alt_m = 0.0f;
/* Set once the ground baseline stops moving (either restored from a
 * mid-flight reboot, or frozen the tick the FSM leaves Standby). While
 * locked, CDH_Update never touches s_ground_baro_alt_m again. */
static uint8_t                  s_baro_baseline_locked = 0U;

/*  CDH_Init                                                           */
/* ------------------------------------------------------------------ */
void CDH_Init(void)
{
    s_imu  = MPU6050_EquipmentHandler_Init(&hi2c1);
    s_baro = MS5607_EquipmentHandler_Init(&hi2c1);
    s_gps  = GPS_EquipmentHandler_Init(&hi2c1);
    CDH_FDIR_Init(&s_fdir);
    SensorValidation_Init();

    /* Ground baro baseline. FDIR_Init() (called before CDH_Init() in main.c)
     * has already restored g_scv from flash, so g_scv.flight_phase and
     * g_scv.baro_ground_alt_cm reflect the state before this boot.
     *
     * If that state says we were already past Standby, this is a mid-flight
     * reboot: restore the persisted baseline instead of recapturing it here.
     * Recapturing at the current (much higher) altitude would zero
     * dp->baro_alt_m regardless of true altitude, which can fake a
     * within-cruise-band or below-landing-altitude reading immediately after
     * the reboot -- see docs/bench_session_notes.md follow-up. */
    if (g_scv.flight_phase != (uint8_t)PHASE_STANDBY &&
        g_scv.baro_ground_alt_cm != SCV_INVALID_I32)
    {
        s_ground_baro_alt_m = (float)g_scv.baro_ground_alt_cm / 100.0f;
        s_baro_baseline_locked = 1U;
    }
    else if (s_baro.baro_valid)
    {
        s_ground_baro_alt_m = s_baro.data.altitude;
    }

    Coral_Init();   /* USART3 already init by MX_USART3_UART_Init() in main.c */

    DatapoolNVM_Init();   /* internal-flash backup of the SD housekeeping log */
}

/* ------------------------------------------------------------------ */
/*  CDH_Update  (called at 1 Hz from the superloop)                   */
/* ------------------------------------------------------------------ */
void CDH_Update(SensorData_t *dp, SCV_t *scv)
{
    dp->timestamp_ms = HAL_GetTick();

    /* Consume FDIR-requested I2C bus restart (FMECA C7/C10 escalation).
     * Fire-and-forget: ack on acceptance, CDH_FDIR_BusRestart_Process below
     * advances the (already in-progress-safe) state machine every tick. */
    if (FDIR_GetReinitRequests() & FDIR_REQUEST_I2C_BUS_RESTART)
    {
        CDH_FDIR_BusRestart_Start(&s_fdir);
        FDIR_AcknowledgeReinit(FDIR_REQUEST_I2C_BUS_RESTART);
    }

    /* Consume FDIR-requested single-device reinit (FMECA C1-C3, C5): distinct
     * from the bus-wide restart above, this runs when only one sensor has
     * timed out. Done before the per-sensor _Update() calls below, so a
     * recovered device can deliver valid data in the same tick. */
    {
        uint16_t device_requests = FDIR_GetReinitRequests();
        if (device_requests & EQUIPMENT_IMU)
        {
            CDH_FDIR_RecoverIMU(&hi2c1);
            FDIR_AcknowledgeReinit(EQUIPMENT_IMU);
        }
        if (device_requests & EQUIPMENT_BARO)
        {
            CDH_FDIR_RecoverBaro(&hi2c1);
            FDIR_AcknowledgeReinit(EQUIPMENT_BARO);
        }
        if (device_requests & EQUIPMENT_GPS)
        {
            s_gps = GPS_EquipmentHandler_Init(&hi2c1);
            FDIR_AcknowledgeReinit(EQUIPMENT_GPS);
        }
    }

    /* ---- IMU ---- */
    s_imu = MPU6050_EquipmentHandler_Update(s_imu, &hi2c1);
    if (s_imu.imu_valid)
    {
        dp->imu_accel_x_g   = s_imu.data.accel_x;
        dp->imu_accel_y_g   = s_imu.data.accel_y;
        dp->imu_accel_z_g   = s_imu.data.accel_z;
        dp->imu_accel_mag_g = sqrtf((s_imu.data.accel_x * s_imu.data.accel_x) +
                                    (s_imu.data.accel_y * s_imu.data.accel_y) +
                                    (s_imu.data.accel_z * s_imu.data.accel_z));
        dp->imu_gyro_x_dps  = s_imu.data.gyro_x;
        dp->imu_gyro_y_dps  = s_imu.data.gyro_y;
        dp->imu_gyro_z_dps  = s_imu.data.gyro_z;
        dp->imu_valid       = 1U;
    }
    else { dp->imu_valid = 0U; }

    /* ---- Barometer ---- */
    s_baro = MS5607_EquipmentHandler_Update(s_baro, &hi2c1);
    if (s_baro.baro_valid)
    {
        if (!s_baro_baseline_locked)
        {
            if (FSW_GetPhase() == PHASE_STANDBY)
            {
                /* Still on the ground: slow-track weather-driven pressure
                 * drift without absorbing a genuine launch rise inside the
                 * debounce window -- hard-resetting the baseline to the live
                 * reading every tick pinned dp->baro_alt_m at exactly 0.0
                 * the whole time in Standby, permanently defeating the
                 * Standby->Launch baro branch (see FSM_STANDBY_BARO_BASELINE_ALPHA
                 * rationale in fsm_thresholds.h). */
                s_ground_baro_alt_m += FSM_STANDBY_BARO_BASELINE_ALPHA *
                                       (s_baro.data.altitude - s_ground_baro_alt_m);
            }
            else
            {
                /* FSW left Standby this cycle (CDH_Update runs before
                 * FSW_Update, so this fires one tick after the transition):
                 * freeze the baseline and persist it so a later mid-flight
                 * reboot restores it instead of recapturing at altitude.
                 * SCV_Update() flushes to flash this same tick -- it treats
                 * any flight_phase change as dirty and backs up immediately. */
                scv->baro_ground_alt_cm = (int32_t)(s_ground_baro_alt_m * 100.0f);
                s_baro_baseline_locked = 1U;
            }
        }

        dp->baro_pressure_pa = s_baro.data.pressure * 100.0f;
        dp->baro_alt_m       = s_baro.data.altitude - s_ground_baro_alt_m;
        dp->baro_temp_c      = s_baro.data.temperature;
        dp->baro_valid       = 1U;
    }
    else { dp->baro_valid = 0U; }

    /* ---- GPS ---- */
    s_gps = GPS_EquipmentHandler_Update(s_gps, &hi2c1);
    /* Always copy GPS data to datapool (for logging), but validation sets VALID flag */
    dp->gps_lat_deg        = s_gps.data.latitude;
    dp->gps_lon_deg        = s_gps.data.longitude;
    dp->gps_alt_m          = s_gps.data.altitude;
    dp->gps_speed_mps      = s_gps.data.speed;
    dp->gps_vel_down_mps   = s_gps.data.vel_down;
    dp->gps_heading_deg    = s_gps.data.heading;
    dp->gps_utc_time       = s_gps.data.utc_time;
    dp->gps_num_satellites = s_gps.data.num_satellites;
    dp->gps_fix_type       = s_gps.data.fix_type;
    dp->gps_valid          = s_gps.gps_valid ? 1U : 0U;

    /* Bench-only: apply any active IMU/baro fault injection before
     * validation runs, so FMECA C4/C6 can actually be exercised (see
     * fdir_test_hooks.h for why this must run here, not after CDH_Update
     * returns). No-op in the flight build. */
    FDIR_TestHooks_PreValidation(dp);

    /* Validate sensor data and handle faults (before printing so validation matches printed values) */
    SensorValidation_Update(dp, scv);

    CDH_Debug_PrintDatapool(dp);
    CDH_Debug_PrintBaro(&s_baro);

    /* CDH owns the I2C bus: advance any pending restart requested by FDIR and
     * publish the resulting bus state. CDH is the sole writer of i2c_bus_state. */
    CDH_FDIR_BusRestart_Process(&s_fdir, &hi2c1);
    dp->i2c_bus_state = s_fdir.bus_state;

    /* ---- Battery voltage (PA0 = ADC1_IN5, 22k/10k divider) ---- */
    dp->batt_valid = BatteryADC_Read(&hadc1, &dp->batt_voltage_mv);

    /* Bench-only: apply an active ADC fault injection after the real read,
     * so it isn't immediately overwritten. No-op in the flight build. */
    FDIR_TestHooks_PostAdcRead(dp);

    /* Periodically mirror the finished datapool to on-chip flash: a
     * redundant, SD-independent copy that survives an SD-card failure
     * (rate-limited to DATAPOOL_NVM_PERIOD_MS for flash endurance). Runs
     * unconditionally, so it captures the datapool -- validity flags and all
     * -- regardless of sensor or SD health. Coral_Update runs later in the
     * superloop, so the snapshot carries the previous cycle's coral block.
     * Ordered after FDIR_TestHooks_PostAdcRead() so the flash mirror
     * reflects the same final dp state as everything else this tick. */
    DatapoolNVM_Update(dp);
}

/* ------------------------------------------------------------------ */
/*  CDH_RequestBusRestart                                              */
/* ------------------------------------------------------------------ */
void CDH_RequestBusRestart(void)
{
    CDH_FDIR_BusRestart_Start(&s_fdir);
}
