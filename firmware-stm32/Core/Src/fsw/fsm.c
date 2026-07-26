#include "fsw/fsm.h"
#include "fsw/fsm_thresholds.h"
#include "cdh/m10s.h"
#include "main.h"
#include "ttc/ttc.h"

#include <math.h>

static FlightPhase_t s_phase = PHASE_STANDBY;
/* Tick at which each transition condition first became (and stayed) true;
 * 0 = condition not currently held. */
static uint32_t s_standby_to_launch_since_ms = 0U;
static uint32_t s_launch_to_ascent_since_ms = 0U;
static uint32_t s_ascent_to_cruise_since_ms = 0U;
static uint32_t s_descent_since_ms = 0U;
static uint32_t s_landing_since_ms = 0U;
static uint8_t s_cruise_baro_ref_valid = 0U;
static float s_cruise_alt_min_m = 0.0f;
static float s_cruise_alt_max_m = 0.0f;
static float s_float_accel_baseline_g = 1.0f;

/* Equipment feeds phase decisions only when it is enabled (policy plane:
 * reduced mode, future give-up isolation) AND not currently faulted
 * (detection plane). An isolated device whose data happens to look valid
 * must not steer phase transitions. */
static uint8_t FSW_EquipmentUsable(uint16_t equipment)
{
    return ((g_scv.equipment_enabled & equipment) == equipment) &&
           ((g_scv.equipment_faults & equipment) == 0U);
}

static uint8_t FSW_GpsUsable(const SensorData_t *dp)
{
    return dp->gps_valid && dp->gps_fix_type == M10S_FIX_3D &&
           FSW_EquipmentUsable(EQUIPMENT_GPS);
}

static uint8_t FSW_ImuUsable(const SensorData_t *dp)
{
    return dp->imu_valid && FSW_EquipmentUsable(EQUIPMENT_IMU);
}

static uint8_t FSW_BaroUsable(const SensorData_t *dp)
{
    return dp->baro_valid && FSW_EquipmentUsable(EQUIPMENT_BARO);
}

static uint8_t FSW_IsValidPhase(uint8_t phase)
{
    return phase <= (uint8_t)PHASE_LANDING;
}

static void FSW_ResetTransitionCounters(void)
{
    s_standby_to_launch_since_ms = 0U;
    s_launch_to_ascent_since_ms = 0U;
    s_ascent_to_cruise_since_ms = 0U;
    s_descent_since_ms = 0U;
    s_landing_since_ms = 0U;
    s_cruise_baro_ref_valid = 0U;
}

static void FSW_SetPhase(FlightPhase_t phase)
{
    if (s_phase == phase)
        return;

    s_phase = phase;
    g_scv.flight_phase = (uint8_t)phase;
    FSW_ResetTransitionCounters();
    /* State changes are operationally significant: report them immediately. */
    TTC_RequestTelemetry();
}

/* Elapsed-time debounce: returns 1 once `condition` has held continuously for
 * `window_ms`. Rate-independent — FSW_Update may be called at any frequency.
 * A single false sample releases the latch and restarts the window. */
static uint8_t FSW_ConditionHeld(uint8_t condition, uint32_t *since_ms, uint32_t window_ms)
{
    uint32_t now_ms = HAL_GetTick();

    if (!condition)
    {
        *since_ms = 0U;
        return 0U;
    }

    if (*since_ms == 0U)
        *since_ms = (now_ms != 0U) ? now_ms : 1U; /* keep 0 = "not held" */

    return (now_ms - *since_ms) >= window_ms;
}

void FSW_Init(void)
{
    if (FSW_IsValidPhase(g_scv.flight_phase))
        s_phase = (FlightPhase_t)g_scv.flight_phase;
    else
        FSW_SetPhase(PHASE_STANDBY);
}

void FSW_Update(const SensorData_t *dp)
{
    uint8_t launch_condition;
    uint8_t ascent_condition;
    uint8_t cruise_condition;
    uint8_t descent_condition;
    uint8_t landing_condition;
    float accel_delta_g;
    uint8_t gps_usable;
    uint8_t imu_usable;
    uint8_t baro_usable;

    if (dp == NULL)
        return;

    gps_usable = FSW_GpsUsable(dp);
    imu_usable = FSW_ImuUsable(dp);
    baro_usable = FSW_BaroUsable(dp);

    switch (s_phase)
    {
    case PHASE_STANDBY:
        launch_condition = ((imu_usable && dp->imu_accel_mag_g > FSM_LAUNCH_ACCEL_G) ||
                            (baro_usable && dp->baro_alt_m > FSM_LAUNCH_BARO_RISE_M));
        if (FSW_ConditionHeld(launch_condition, &s_standby_to_launch_since_ms, FSM_LAUNCH_WINDOW_MS))
            FSW_SetPhase(PHASE_LAUNCH);
        break;

    case PHASE_LAUNCH:
        ascent_condition = ((baro_usable && dp->baro_alt_m > FSM_ASCENT_ALT_M) ||
                            (gps_usable && dp->gps_vel_down_mps < FSM_ASCENT_VEL_DOWN_MPS));
        if (FSW_ConditionHeld(ascent_condition, &s_launch_to_ascent_since_ms, FSM_ASCENT_WINDOW_MS))
            FSW_SetPhase(PHASE_ASCENT);
        break;

    case PHASE_ASCENT:
        accel_delta_g = imu_usable ? fabsf(dp->imu_accel_mag_g - s_float_accel_baseline_g) : 0.0f;
        descent_condition = ((imu_usable && accel_delta_g > FSM_DESCENT_ACCEL_DELTA_G) ||
                             (gps_usable && dp->gps_vel_down_mps > FSM_DESCENT_VEL_DOWN_MPS));
        if (FSW_ConditionHeld(descent_condition, &s_descent_since_ms, FSM_DESCENT_WINDOW_MS))
        {
            FSW_SetPhase(PHASE_DESCENT);
            break;
        }

        /* Baro band: track the min/max altitude seen since the window last
         * reset and require the full spread to stay under the band for the
         * whole debounce window -- this approximates "altitude rate of
         * change bounded" (FR-007) with two floats instead of a fixed
         * reference point. A fixed reference (the previous approach) can
         * never be re-satisfied once steady ascent carries the balloon past
         * it, which made this branch permanently dead. */
        if (baro_usable)
        {
            if (!s_cruise_baro_ref_valid)
            {
                s_cruise_alt_min_m = dp->baro_alt_m;
                s_cruise_alt_max_m = dp->baro_alt_m;
                s_cruise_baro_ref_valid = 1U;
            }
            else
            {
                if (dp->baro_alt_m < s_cruise_alt_min_m) { s_cruise_alt_min_m = dp->baro_alt_m; }
                if (dp->baro_alt_m > s_cruise_alt_max_m) { s_cruise_alt_max_m = dp->baro_alt_m; }
            }
        }

        cruise_condition = ((gps_usable && dp->gps_vel_down_mps > FSM_CRUISE_VEL_DOWN_MPS) ||
                            (baro_usable && s_cruise_baro_ref_valid &&
                             (s_cruise_alt_max_m - s_cruise_alt_min_m) < FSM_CRUISE_ALT_BAND_M));
        if (FSW_ConditionHeld(cruise_condition, &s_ascent_to_cruise_since_ms, FSM_CRUISE_WINDOW_MS))
        {
            FSW_SetPhase(PHASE_CRUISE);
            break;
        }
        if (!cruise_condition && baro_usable)
        {
            /* Band exceeded (steady ascent, or a real gust): FSW_ConditionHeld
             * already restarted the debounce timer above -- restart the
             * watermarks from the current sample too, so the next window
             * measures forward from here instead of an ever-growing spread. */
            s_cruise_alt_min_m = dp->baro_alt_m;
            s_cruise_alt_max_m = dp->baro_alt_m;
        }
        break;

    case PHASE_CRUISE:
        if (imu_usable)
            s_float_accel_baseline_g = ((1.0f - FSM_CRUISE_BASELINE_ALPHA) * s_float_accel_baseline_g) +
                                        (FSM_CRUISE_BASELINE_ALPHA * dp->imu_accel_mag_g);

        accel_delta_g = imu_usable ? fabsf(dp->imu_accel_mag_g - s_float_accel_baseline_g) : 0.0f;
        descent_condition = ((imu_usable && accel_delta_g > FSM_DESCENT_ACCEL_DELTA_G) ||
                             (gps_usable && dp->gps_vel_down_mps > FSM_DESCENT_VEL_DOWN_MPS));
        if (FSW_ConditionHeld(descent_condition, &s_descent_since_ms, FSM_DESCENT_WINDOW_MS))
            FSW_SetPhase(PHASE_DESCENT);
        break;

    case PHASE_DESCENT:
        landing_condition = ((gps_usable && dp->gps_speed_mps < FSM_LANDING_SPEED_MPS) ||
                             (baro_usable && dp->baro_alt_m < FSM_LANDING_ALT_M));
        if (FSW_ConditionHeld(landing_condition, &s_landing_since_ms, FSM_LANDING_WINDOW_MS))
            FSW_SetPhase(PHASE_LANDING);
        break;

    case PHASE_LANDING:
    default:
        break;
    }
}

FlightPhase_t FSW_GetPhase(void)
{
    return s_phase;
}
