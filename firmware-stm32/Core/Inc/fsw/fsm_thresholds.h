#ifndef FSW_FSM_THRESHOLDS_H
#define FSW_FSM_THRESHOLDS_H

/*
 * FSM transition thresholds.
 *
 * All values are placeholders pending research and first-flight calibration.
 * Each constant includes a rationale comment and source reference so that
 * changes can be justified and traced.
 *
 * Sources to consult:
 *   - UKHAS flight database: https://tracker.habhub.org (historical ascent rates)
 *   - CUSF landing predictor telemetry logs
 *   - MPU-6050 datasheet (noise floor ~0.05 g at 1 Hz)
 *   - MS5607 datasheet (altitude resolution ~20 cm at sea level, ~1 m at 30 km)
 *   - u-blox NEO-M8N datasheet (velocity accuracy ~0.05 m/s RMS in Airborne mode)
 */

/* ── Standby → Launch ─────────────────────────────────────────────────────── */

/* Total IMU acceleration magnitude above which launch jerk is detected.
 * Typical balloon release jerk: 1.5–3 g. Set conservatively above 1 g (gravity)
 * plus noise floor (~0.05 g) to avoid false trigger from ground handling.
 * FR-005 specifies 1.5 g minimum. Refine after bench shake test. */
#define FSM_LAUNCH_ACCEL_G        1.5f   /* g */

/* Barometric altitude rise over the persistence window confirming upward motion.
 * At 1 Hz with 2 s window: balloon must rise at least this much to confirm launch.
 * Typical ascent rate 3–5 m/s → expect ~6–10 m over 2 s. Set low to catch slow
 * releases. Adjust if false triggers occur near windy ground conditions. */
#define FSM_LAUNCH_BARO_RISE_M    3.0f   /* m over window — TBD from bench test */

/* Persistence window: both OR conditions evaluated over this many consecutive
 * 1 Hz samples. FR-005 specifies 2 s. Short because the launch jerk is brief. */
#define FSM_LAUNCH_WINDOW_S       2      /* seconds */


/* ── Launch → Ascent ──────────────────────────────────────────────────────── */

/* Barometric altitude above ground baseline confirming balloon has left ground.
 * FR-006 specifies 100 m. GPS unreliable at cold start; baro is primary here. */
#define FSM_ASCENT_ALT_M          100.0f /* m above Standby baseline */

/* GPS vertical velocity confirming sustained climb.
 * FR-006 specifies 5 m/s. Typical ascent rate 3–5 m/s — threshold sits at the
 * fast end to avoid premature trigger; lower if test flights ascend slower. */
#define FSM_ASCENT_VVEL_MPS       5.0f   /* m/s upward */

/* Persistence window. No hard requirement in FR-006 beyond both conditions met.
 * A few seconds avoids triggering on a momentary GPS spike. TBD from test. */
#define FSM_ASCENT_WINDOW_S       3      /* seconds — TBD */


/* ── Ascent → Cruise ──────────────────────────────────────────────────────── */

/* GPS vertical velocity below which float is detected.
 * FR-007 specifies <0.5 m/s. Typical float oscillation ±0.1–0.3 m/s.
 * u-blox velocity noise ~0.05 m/s RMS, so 0.5 m/s provides 10× noise margin. */
#define FSM_CRUISE_VVEL_MPS       0.5f   /* m/s — balloon considered stationary */

/* Baro altitude change band over the persistence window.
 * Confirms altitude is bounded, not just momentarily slow.
 * At 30 km, MS5607 altitude noise ~1–2 m; ±10 m band gives comfortable margin.
 * TBD from actual float telemetry. */
#define FSM_CRUISE_ALT_BAND_M     10.0f  /* m peak-to-peak over window — TBD */

/* Persistence window. FR-007 specifies 30 s. Float develops gradually;
 * long window avoids premature trigger during ascent slowdowns. */
#define FSM_CRUISE_WINDOW_S       30     /* seconds */


/* ── Ascent/Cruise → Descent (burst detection) ───────────────────────────── */

/* IMU acceleration deviation from float baseline above which burst is detected.
 * During Cruise, a running median baseline is maintained. Burst causes a sharp
 * jerk; even with parachute deployment, expect >0.5 g delta. MPU-6050 noise
 * ~0.05 g at 1 Hz; 0.5 g gives 10× margin. TBD from burst simulation test. */
#define FSM_DESCENT_ACCEL_DELTA_G 0.5f   /* g deviation from Cruise baseline — TBD */

/* GPS vertical velocity below which rapid descent is confirmed.
 * FR-008 specifies <-2 m/s. Typical post-burst descent 8–15 m/s with parachute.
 * -2 m/s threshold is conservative; increase if false triggers occur near float. */
#define FSM_DESCENT_VVEL_MPS      -2.0f  /* m/s (negative = downward) */

/* Persistence window. FR-008 specifies 10 s. Burst is sharp but parachute
 * deployment may temporarily slow descent. 10 s confirms sustained fall. */
#define FSM_DESCENT_WINDOW_S      10     /* seconds */


/* ── Descent → Landing ────────────────────────────────────────────────────── */

/* GPS 3D speed below which the system is considered stationary.
 * FR-009 specifies <1 m/s. Includes horizontal drift; more complete than
 * vertical-only. u-blox speed accuracy ~0.05 m/s RMS gives 20× margin. */
#define FSM_LANDING_SPEED_MPS     1.0f   /* m/s 3D speed magnitude */

/* Barometric altitude above ground baseline below which landing is plausible.
 * FR-009 specifies <200 m. Prevents false landing trigger during slow descent.
 * MS5607 is accurate to <1 m at low altitude; 200 m threshold is coarse on
 * purpose — the speed condition is the primary discriminator. */
#define FSM_LANDING_ALT_M         200.0f /* m above Standby baseline */

/* Persistence window. FR-009 specifies 5 s. Touchdown is unambiguous;
 * short window is sufficient. */
#define FSM_LANDING_WINDOW_S      5      /* seconds */


/* ── Cross-cutting ────────────────────────────────────────────────────────── */

/* Median filter window applied to all sensor values before threshold comparison.
 * 5 samples at 1 Hz = 5 s smoothing. Removes single-sample spikes.
 * Increase if noise is worse than expected; decrease if phase detection is sluggish. */
#define FSM_MEDIAN_WINDOW         5      /* samples */

/* Watchdog kick period. Must be < 10 s (FR-011). Kicked in the main superloop. */
#define FSM_WATCHDOG_KICK_S       1      /* seconds — once per superloop tick */

#endif /* FSW_FSM_THRESHOLDS_H */
