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
 * Typical balloon release jerk: 1.5–3 g. Lowered from 1.5 g to 1.3 g (still
 * >5x the ~0.05 g noise floor) to catch softer releases sooner; still well
 * above 1 g (gravity) plus noise so ground handling does not false-trigger.
 * DEVIATION: FR-005 as written specifies 1.5 g / 2 s (see FSM_LAUNCH_WINDOW_MS
 * below). This constant intentionally no longer matches that wording —
 * update FR-005 or record the deviation rationale in the traceability doc
 * before flight sign-off. */
#define FSM_LAUNCH_ACCEL_G        1.3f   /* g */

/* Barometric altitude rise over the persistence window confirming upward motion.
 * At 1 Hz with 2 s window: balloon must rise at least this much to confirm launch.
 * Typical ascent rate 3–5 m/s → expect ~6–10 m over 2 s. Set low to catch slow
 * releases. Adjust if false triggers occur near windy ground conditions. */
#define FSM_LAUNCH_BARO_RISE_M    3.0f   /* m over window — TBD from bench test */

/* Persistence window: the OR condition must hold continuously for this long
 * (elapsed-time debounce, sample-rate independent). Shortened from 2 s to 1 s
 * alongside FSM_LAUNCH_ACCEL_G — the jerk itself is sub-second, so the 2 s
 * requirement was already relying on the baro branch to actually fire.
 * DEVIATION: FR-005 as written specifies 2 s — see FSM_LAUNCH_ACCEL_G note. */
#define FSM_LAUNCH_WINDOW_MS      1000U  /* ms */


/* ── Launch → Ascent ──────────────────────────────────────────────────────── */

/* Barometric altitude above ground baseline confirming balloon has left ground.
 * FR-006 specifies 100 m. GPS unreliable at cold start; baro is primary here. */
#define FSM_ASCENT_ALT_M          100.0f /* m above Standby baseline */

/* GPS NED down velocity confirming sustained climb.
 * FR-006 specifies 5 m/s. Typical ascent rate 3–5 m/s — threshold sits at the
 * fast end to avoid premature trigger; lower if test flights ascend slower. */
#define FSM_ASCENT_VEL_DOWN_MPS  -5.0f   /* m/s NED down; negative = upward */

/* Persistence window. No hard requirement in FR-006 beyond both conditions met.
 * A few seconds avoids triggering on a momentary GPS spike. TBD from test. */
#define FSM_ASCENT_WINDOW_MS      3000U  /* ms — TBD */


/* ── Ascent → Cruise ──────────────────────────────────────────────────────── */

/* GPS NED down velocity above which float is detected.
 * FR-007 specifies <0.5 m/s. Typical float oscillation ±0.1–0.3 m/s.
 * u-blox velocity noise ~0.05 m/s RMS, so 0.5 m/s provides 10× noise margin. */
#define FSM_CRUISE_VEL_DOWN_MPS  -0.5f   /* m/s NED down; climb has slowed */

/* Baro altitude change band over the persistence window.
 * Confirms altitude is bounded, not just momentarily slow.
 * At 30 km, MS5607 altitude noise ~1–2 m; ±10 m band gives comfortable margin.
 * TBD from actual float telemetry. */
#define FSM_CRUISE_ALT_BAND_M     10.0f  /* m peak-to-peak over window — TBD */

/* Persistence window. FR-007 specifies 30 s. Float develops gradually;
 * long window avoids premature trigger during ascent slowdowns. */
#define FSM_CRUISE_WINDOW_MS      30000U /* ms */


/* ── Ascent/Cruise → Descent (burst detection) ───────────────────────────── */

/* IMU acceleration deviation from float baseline above which burst is detected.
 * During Cruise, a running median baseline is maintained. Burst causes a sharp
 * jerk; even with parachute deployment, expect >0.5 g delta. MPU-6050 noise
 * ~0.05 g at 1 Hz; 0.5 g gives 10× margin. TBD from burst simulation test. */
#define FSM_DESCENT_ACCEL_DELTA_G 0.5f   /* g deviation from Cruise baseline — TBD */

/* GPS NED down velocity above which rapid descent is confirmed.
 * FR-008 specifies <-2 m/s. Typical post-burst descent 8–15 m/s with parachute.
 * 2 m/s threshold is conservative; increase if false triggers occur near float. */
#define FSM_DESCENT_VEL_DOWN_MPS   2.0f  /* m/s NED down; positive = downward */

/* Persistence window. FR-008 specifies 10 s. Burst is sharp but parachute
 * deployment may temporarily slow descent. 10 s confirms sustained fall. */
#define FSM_DESCENT_WINDOW_MS     10000U /* ms */


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
#define FSM_LANDING_WINDOW_MS     5000U  /* ms */


/* ── Cross-cutting ────────────────────────────────────────────────────────── */

/* EMA smoothing factor for the Cruise IMU accel-magnitude baseline (0..1),
 * updated once per FSW_Update() call. FSW_Update runs at LOOP_CDH_FSW_PERIOD_MS
 * (10 Hz, see main.c), not the 1 Hz this file originally assumed elsewhere —
 * at 10 Hz the previous 0.9/0.1 EMA had a ~1 s time constant, so it absorbed a
 * genuine burst-magnitude acceleration deviation faster than the 10 s
 * FSM_DESCENT_WINDOW_MS debounce could confirm it, silently defeating the
 * Cruise->Descent IMU branch. This value gives a time constant of roughly
 * 1 / (FSM_CRUISE_BASELINE_ALPHA * 10 Hz) ~= 100 s — about 10x the debounce
 * window, so a real burst stays visible across many debounce cycles before
 * the baseline "catches up" and erases it. */
#define FSM_CRUISE_BASELINE_ALPHA 0.001f

/* EMA smoothing factor for the Standby ground-baro baseline (0..1), updated
 * once per CDH_Update() call (10 Hz, see main.c). Replaces a hard reset
 * (`s_ground_baro_alt_m = current altitude`, every tick while in Standby)
 * that pinned dp->baro_alt_m at exactly 0.0 for the whole time on the pad,
 * permanently defeating the Standby->Launch baro branch regardless of true
 * altitude change (see docs/bench_session_notes.md — bench ground test with
 * a genuine ~11 m rise never crossed FSM_LAUNCH_BARO_RISE_M). This value
 * gives a time constant of roughly 1 / (FSM_STANDBY_BARO_BASELINE_ALPHA *
 * 10 Hz) ~= 10 s — about 10x FSM_LAUNCH_WINDOW_MS, so a genuine launch rise
 * stays visible across the full debounce window before the baseline
 * "catches up" and erases it, while still absorbing weather-driven pressure
 * drift (which moves on a scale of hours, not seconds) well before launch. */
#define FSM_STANDBY_BARO_BASELINE_ALPHA 0.01f

/* Watchdog kick period. Must be < 10 s (FR-011). The main superloop free-runs
 * (no fixed tick) and the IWDG is kicked once per iteration when FDIR reports
 * the system healthy; this constant is not currently read in code. */
#define FSM_WATCHDOG_KICK_S       1      /* seconds */

#endif /* FSW_FSM_THRESHOLDS_H */
