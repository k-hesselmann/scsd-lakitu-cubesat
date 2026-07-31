#!/usr/bin/env node

/*
 * Reproducible post-flight analysis for the Lakitu 2026-07-28 flight.
 *
 * Inputs are never modified. The script reads the public on-board flight
 * window plus both ground-station downlink logs, reconstructs a sub-second
 * timeline independently for each OBC boot, and writes compact plot-driving
 * CSVs, a machine-readable summary, and LaTeX value macros.
 */

import fs from "node:fs";
import path from "node:path";
import crypto from "node:crypto";
import { fileURLToPath } from "node:url";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const flightDataDir = path.resolve(scriptDir, "..");
const repoRoot = path.resolve(flightDataDir, "..");
const processedDir = path.join(scriptDir, "processed");
fs.mkdirSync(processedDir, { recursive: true });

const flightFile = path.join(
  flightDataDir,
  "flight_data_2026-07-28_1429-1732_CEST.csv",
);
const fullArchiveFile = path.join(flightDataDir, "flight_data.csv");
const telemetryDir = path.join(
  flightDataDir,
  "telemetry ground station logs",
  "logs",
);
const telemetryFiles = [
  path.join(telemetryDir, "telemetry_20260728_132651.csv"),
  path.join(telemetryDir, "telemetry_20260728_165643.csv"),
];
const eventFiles = [
  path.join(telemetryDir, "ground_station_events_20260728_132651.csv"),
  path.join(telemetryDir, "ground_station_events_20260728_165643.csv"),
];

const phaseNames = [
  "Standby",
  "Launch",
  "Ascent",
  "Cruise",
  "Descent",
  "Landing",
];
const flightStartUtcMs = Date.parse("2026-07-28T12:29:00Z");
const flightEndUtcMs = Date.parse("2026-07-28T15:32:00Z");

function sha256(file) {
  return crypto.createHash("sha256").update(fs.readFileSync(file)).digest("hex");
}

function csvEscape(value) {
  if (value === null || value === undefined) return "";
  const text = String(value);
  if (/[",\r\n]/.test(text)) return `"${text.replaceAll('"', '""')}"`;
  return text;
}

function writeCsv(file, headers, rows) {
  const lines = [
    headers.map(csvEscape).join(","),
    ...rows.map((row) => headers.map((header) => csvEscape(row[header])).join(",")),
  ];
  fs.writeFileSync(file, `${lines.join("\n")}\n`);
}

function parseCsvLine(line) {
  const fields = [];
  let field = "";
  let quoted = false;
  for (let i = 0; i < line.length; i += 1) {
    const char = line[i];
    if (quoted) {
      if (char === '"' && line[i + 1] === '"') {
        field += '"';
        i += 1;
      } else if (char === '"') {
        quoted = false;
      } else {
        field += char;
      }
    } else if (char === '"') {
      quoted = true;
    } else if (char === ",") {
      fields.push(field);
      field = "";
    } else {
      field += char;
    }
  }
  fields.push(field);
  return fields;
}

function readCsv(file) {
  const lines = fs.readFileSync(file, "utf8").trimEnd().split(/\r?\n/);
  const header = parseCsvLine(lines[0]);
  const index = Object.fromEntries(header.map((name, i) => [name, i]));
  const rows = lines.slice(1).filter(Boolean).map(parseCsvLine);
  return { header, index, rows };
}

function number(value) {
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : null;
}

function bool(value) {
  return String(value).toLowerCase() === "true" || value === "1";
}

function hmsToSeconds(value) {
  const h = Math.floor(value / 10000);
  const m = Math.floor((value % 10000) / 100);
  const s = value % 100;
  return h * 3600 + m * 60 + s;
}

function median(values) {
  return quantile(values, 0.5);
}

function quantile(values, probability) {
  const sorted = values.filter(Number.isFinite).toSorted((a, b) => a - b);
  if (!sorted.length) return null;
  const position = (sorted.length - 1) * probability;
  const lower = Math.floor(position);
  const upper = Math.ceil(position);
  if (lower === upper) return sorted[lower];
  return sorted[lower] + (sorted[upper] - sorted[lower]) * (position - lower);
}

function mean(values) {
  const clean = values.filter(Number.isFinite);
  return clean.length ? clean.reduce((sum, value) => sum + value, 0) / clean.length : null;
}

function standardDeviation(values) {
  const clean = values.filter(Number.isFinite);
  if (clean.length < 2) return null;
  const average = mean(clean);
  return Math.sqrt(
    clean.reduce((sum, value) => sum + (value - average) ** 2, 0) /
      (clean.length - 1),
  );
}

function minBy(items, accessor) {
  return items.reduce(
    (best, item) =>
      best === null || accessor(item) < accessor(best) ? item : best,
    null,
  );
}

function maxBy(items, accessor) {
  return items.reduce(
    (best, item) =>
      best === null || accessor(item) > accessor(best) ? item : best,
    null,
  );
}

function round(value, digits = 3) {
  if (!Number.isFinite(value)) return null;
  const factor = 10 ** digits;
  return Math.round(value * factor) / factor;
}

function formatClockFromElapsed(elapsedS, includeSeconds = true) {
  const localMs = flightStartUtcMs + elapsedS * 1000 + 2 * 3600 * 1000;
  const date = new Date(localMs);
  const hh = String(date.getUTCHours()).padStart(2, "0");
  const mm = String(date.getUTCMinutes()).padStart(2, "0");
  const ss = String(date.getUTCSeconds()).padStart(2, "0");
  return includeSeconds ? `${hh}:${mm}:${ss}` : `${hh}:${mm}`;
}

function littleEndian(hex, start, length) {
  if (!/^[0-9A-Fa-f]{32}$/.test(hex)) return null;
  let value = 0;
  for (let i = 0; i < length; i += 1) {
    value += Number.parseInt(hex.slice((start + i) * 2, (start + i + 1) * 2), 16) *
      256 ** i;
  }
  return value;
}

function coralBlock(hex) {
  if (!/^[0-9A-Fa-f]{32}$/.test(hex)) return null;
  return {
    sequence: littleEndian(hex, 0, 4),
    fractionRaw: littleEndian(hex, 4, 2),
    fractionPercentByte: littleEndian(hex, 6, 1),
    status: littleEndian(hex, 7, 1),
    rxTickMs: littleEndian(hex, 8, 4),
    frameCount: littleEndian(hex, 12, 2),
  };
}

function haversineKm(lat1, lon1, lat2, lon2) {
  const toRadians = (degrees) => degrees * Math.PI / 180;
  const earthRadiusKm = 6371.0088;
  const dLat = toRadians(lat2 - lat1);
  const dLon = toRadians(lon2 - lon1);
  const a =
    Math.sin(dLat / 2) ** 2 +
    Math.cos(toRadians(lat1)) *
      Math.cos(toRadians(lat2)) *
      Math.sin(dLon / 2) ** 2;
  return 2 * earthRadiusKm * Math.asin(Math.sqrt(a));
}

function slantRangeKm(lat1, lon1, altM1, lat2, lon2, altM2) {
  const horizontalKm = haversineKm(lat1, lon1, lat2, lon2);
  const verticalKm = (altM2 - altM1) / 1000;
  return Math.hypot(horizontalKm, verticalKm);
}

function webMercatorTileX(longitudeDeg, zoom) {
  return (longitudeDeg + 180) / 360 * 2 ** zoom;
}

function webMercatorNorthing(latitudeDeg, zoom) {
  const latitudeRad = latitudeDeg * Math.PI / 180;
  const tileY =
    (1 - Math.asinh(Math.tan(latitudeRad)) / Math.PI) / 2 * 2 ** zoom;
  return -tileY;
}

function nearestValidPosition(rows, elapsedS) {
  return minBy(
    rows.filter((row) => row.gpsFix === 3 && row.gpsLat !== 0 && row.gpsLon !== 0),
    (row) => Math.abs(row.elapsedS - elapsedS),
  );
}

function binRows(rows, widthS, build) {
  const bins = new Map();
  for (const row of rows) {
    const key = Math.floor(row.elapsedS / widthS);
    if (!bins.has(key)) bins.set(key, []);
    bins.get(key).push(row);
  }
  return [...bins.entries()]
    .sort(([a], [b]) => a - b)
    .map(([key, values]) => build(values, (key + 0.5) * widthS));
}

function contiguousIntervals(rows, predicate, maxStepS = 0.5) {
  const intervals = [];
  let current = null;
  for (let i = 0; i < rows.length; i += 1) {
    const row = rows[i];
    const active = predicate(row);
    const previous = i > 0 ? rows[i - 1] : null;
    const discontinuity = previous && row.elapsedS - previous.elapsedS > maxStepS;
    if (active && (!current || discontinuity)) {
      if (current) intervals.push(current);
      current = { startS: row.elapsedS, endS: row.elapsedS, rows: 1 };
    } else if (active && current) {
      current.endS = row.elapsedS;
      current.rows += 1;
    } else if (!active && current) {
      intervals.push(current);
      current = null;
    }
  }
  if (current) intervals.push(current);
  return intervals;
}

const flightCsv = readCsv(flightFile);
const fi = flightCsv.index;

/*
 * gps_utc_time has only one-second resolution and repeats across roughly ten
 * 10 Hz rows. Interpolate within each equal-time run using the HAL tick. This
 * preserves the authoritative GNSS second at every time update, yet supplies
 * a monotonic sub-second axis. It also preserves the visible nine-second GNSS
 * jump caused by the 81 rows omitted from the public cut.
 */
const rawTiming = flightCsv.rows.map((fields, sourceIndex) => ({
  fields,
  sourceIndex,
  boot: number(fields[fi.scv_boot_count]),
  tickMs: number(fields[fi.record_timestamp_ms]),
  gpsUtc: number(fields[fi.gps_utc_time]),
}));
const reconstructedUtcS = new Array(rawTiming.length);
let runStart = 0;
while (runStart < rawTiming.length) {
  let runEnd = runStart + 1;
  while (
    runEnd < rawTiming.length &&
    rawTiming[runEnd].boot === rawTiming[runStart].boot &&
    rawTiming[runEnd].gpsUtc === rawTiming[runStart].gpsUtc
  ) {
    runEnd += 1;
  }
  const first = rawTiming[runStart];
  const next = rawTiming[runEnd];
  const baseS = hmsToSeconds(first.gpsUtc);
  const hasNextAnchor = next && next.boot === first.boot;
  let clockDeltaS = hasNextAnchor ? hmsToSeconds(next.gpsUtc) - baseS : null;
  if (clockDeltaS !== null && clockDeltaS < -12 * 3600) clockDeltaS += 24 * 3600;
  const tickDeltaMs = hasNextAnchor ? next.tickMs - first.tickMs : null;
  for (let i = runStart; i < runEnd; i += 1) {
    const withinRunMs = rawTiming[i].tickMs - first.tickMs;
    const fractionS =
      hasNextAnchor && tickDeltaMs > 0 && clockDeltaS >= 0
        ? withinRunMs / tickDeltaMs * clockDeltaS
        : withinRunMs / 1000;
    reconstructedUtcS[i] = baseS + fractionS;
  }
  runStart = runEnd;
}

const flightRows = rawTiming.map(({ fields, sourceIndex, boot, tickMs }, timingIndex) => {
  const utcS = reconstructedUtcS[timingIndex];
  const elapsedS = utcS - 12 * 3600 - 29 * 60;
  const coral = coralBlock(fields[fi.coral_block_hex]);
  return {
    sourceIndex,
    fields,
    boot,
    tickMs,
    utcS,
    elapsedS,
    phase: number(fields[fi.scv_flight_phase]),
    resetReason: number(fields[fi.scv_reset_reason]),
    missionElapsedMs: number(fields[fi.scv_mission_elapsed_ms]),
    gpsLat: number(fields[fi.gps_lat_e7]) / 1e7,
    gpsLon: number(fields[fi.gps_lon_e7]) / 1e7,
    gpsAltM: number(fields[fi.gps_alt_cm]) / 100,
    gpsSpeedMps: number(fields[fi.gps_speed_cms]) / 100,
    gpsVelDownMps: number(fields[fi.gps_vel_down_cms]) / 100,
    gpsHeadingDeg: number(fields[fi.gps_heading_cdeg]) / 100,
    gpsSatellites: number(fields[fi.gps_satellites]),
    gpsFix: number(fields[fi.gps_fix_type]),
    gpsValid: number(fields[fi.gps_valid]),
    accelXG: number(fields[fi.imu_accel_x_mg]) / 1000,
    accelYG: number(fields[fi.imu_accel_y_mg]) / 1000,
    accelZG: number(fields[fi.imu_accel_z_mg]) / 1000,
    accelMagG: number(fields[fi.imu_accel_mag_mg]) / 1000,
    gyroXDegS: number(fields[fi.imu_gyro_x_mdps]) / 1000,
    gyroYDegS: number(fields[fi.imu_gyro_y_mdps]) / 1000,
    gyroZDegS: number(fields[fi.imu_gyro_z_mdps]) / 1000,
    imuValid: number(fields[fi.imu_valid]),
    pressurePa: number(fields[fi.baro_pressure_pa]),
    baroAltM: number(fields[fi.baro_alt_cm]) / 100,
    baroTempC: number(fields[fi.baro_temp_centi_c]) / 100,
    baroValid: number(fields[fi.baro_valid]),
    i2cState: number(fields[fi.i2c_bus_state]),
    batteryV: number(fields[fi.batt_voltage_mv]) / 1000,
    batteryValid: number(fields[fi.batt_valid]),
    coralValid: number(fields[fi.coral_valid]),
    coral,
    equipmentEnabled: number(fields[fi.scv_equipment_enabled]),
    equipmentFaults: number(fields[fi.scv_equipment_faults]),
    gpsTimeouts: number(fields[fi.scv_gps_timeout_count]),
    imuTimeouts: number(fields[fi.scv_imu_timeout_count]),
    baroTimeouts: number(fields[fi.scv_baro_timeout_count]),
    coralTimeouts: number(fields[fi.scv_coral_timeout_count]),
    loraTimeouts: number(fields[fi.scv_lora_timeout_count]),
    loraTxFaults: number(fields[fi.scv_lora_tx_fault_counter]),
    sdFaults: number(fields[fi.scv_sd_fault_count]),
    watchdogResets: number(fields[fi.scv_watchdog_reset_count]),
  };
}).toSorted((a, b) => a.elapsedS - b.elapsedS || a.sourceIndex - b.sourceIndex);

const firstElapsedS = flightRows[0].elapsedS;
for (const row of flightRows) row.elapsedS -= firstElapsedS;

const flightDurationS = flightRows.at(-1).elapsedS;
const validGpsRows = flightRows.filter(
  (row) => row.gpsFix === 3 && row.gpsLat !== 0 && row.gpsLon !== 0,
);
const firstFix = validGpsRows[0];
const apex = maxBy(validGpsRows, (row) => row.gpsAltM);
const baroApex = maxBy(flightRows, (row) => row.baroAltM);
const maxDescent = maxBy(validGpsRows, (row) => row.gpsVelDownMps);
const maxClimb = minBy(validGpsRows, (row) => row.gpsVelDownMps);
const maxGroundSpeed = maxBy(validGpsRows, (row) => row.gpsSpeedMps);
const maxAcceleration = maxBy(flightRows, (row) => row.accelMagG);

const transitions = [];
for (let i = 1; i < flightRows.length; i += 1) {
  if (flightRows[i].phase !== flightRows[i - 1].phase) {
    transitions.push({
      elapsedS: flightRows[i].elapsedS,
      from: flightRows[i - 1].phase,
      to: flightRows[i].phase,
      row: flightRows[i],
    });
  }
}
const launchTransition = transitions.find((event) => event.to === 1);
const ascentTransition = transitions.find((event) => event.to === 2);
const descentTransition = transitions.find((event) => event.to === 4);
const landingTransition = transitions.find((event) => event.to === 5);

const bootTransitions = [];
for (let i = 1; i < flightRows.length; i += 1) {
  if (flightRows[i].boot !== flightRows[i - 1].boot) {
    bootTransitions.push({
      elapsedS: flightRows[i].elapsedS,
      priorElapsedS: flightRows[i - 1].elapsedS,
      gapS: flightRows[i].elapsedS - flightRows[i - 1].elapsedS,
      from: flightRows[i - 1].boot,
      to: flightRows[i].boot,
      resetReason: flightRows[i].resetReason,
    });
  }
}

const launchPosition = nearestValidPosition(flightRows, launchTransition.elapsedS);
const landingPosition = nearestValidPosition(flightRows, landingTransition.elapsedS);
const apexPosition = nearestValidPosition(flightRows, apex.elapsedS);
const landingDistanceKm = haversineKm(
  launchPosition.gpsLat,
  launchPosition.gpsLon,
  landingPosition.gpsLat,
  landingPosition.gpsLon,
);
const maximumRangeKm = Math.max(
  ...validGpsRows.map((row) =>
    haversineKm(
      launchPosition.gpsLat,
      launchPosition.gpsLon,
      row.gpsLat,
      row.gpsLon,
    )
  ),
);

const sampleStepsS = [];
for (let i = 1; i < flightRows.length; i += 1) {
  if (flightRows[i].boot === flightRows[i - 1].boot) {
    sampleStepsS.push((flightRows[i].tickMs - flightRows[i - 1].tickMs) / 1000);
  }
}
const positiveSampleStepsS = sampleStepsS.filter((value) => value > 0);
const samplingStalls = positiveSampleStepsS.filter((value) => value >= 1);
const largestGapS = Math.max(...positiveSampleStepsS);
const genuineSamplingStalls = positiveSampleStepsS.filter(
  (value) => value >= 1 && value < 2,
);

const phaseDurationsS = Object.fromEntries(phaseNames.map((name) => [name, 0]));
for (let i = 0; i < flightRows.length - 1; i += 1) {
  const delta = Math.max(0, flightRows[i + 1].elapsedS - flightRows[i].elapsedS);
  phaseDurationsS[phaseNames[flightRows[i].phase]] += delta;
}

const gpsFixRows = {
  fix3: flightRows.filter((row) => row.gpsFix === 3).length,
  fix2: flightRows.filter((row) => row.gpsFix === 2).length,
  other: flightRows.filter((row) => row.gpsFix !== 3 && row.gpsFix !== 2).length,
};

const batteryValues = flightRows.map((row) => row.batteryV);
const batteryOutliers = flightRows.filter(
  (row) => row.batteryV < 3.2 || row.batteryV > 4.2,
);
const temperatures = flightRows.map((row) => row.baroTempC);
const pressures = flightRows.map((row) => row.pressurePa);

const uniqueCoral = [];
const coralKeys = new Set();
for (const row of flightRows) {
  if (!row.coral) continue;
  const key = `${row.boot}:${row.coral.sequence}:${row.coral.rxTickMs}:${row.coral.status}`;
  if (coralKeys.has(key)) continue;
  coralKeys.add(key);
  uniqueCoral.push({
    ...row.coral,
    boot: row.boot,
    elapsedS: row.elapsedS,
    coralValid: row.coralValid,
    equipmentFaults: row.equipmentFaults,
  });
}
const goodCoralFrames = uniqueCoral.filter(
  (frame) => frame.status === 0 && frame.coralValid === 1,
);
const cloudFractions = goodCoralFrames.map((frame) => frame.fractionRaw / 65535 * 100);
const coralFaultIntervals = contiguousIntervals(
  flightRows,
  (row) => (row.equipmentFaults & 0x0008) !== 0,
);
for (const interval of coralFaultIntervals) {
  interval.durationS = Math.max(0, interval.endS - interval.startS);
}
const coralInvalidRows = flightRows.filter((row) => row.coralValid !== 1).length;
const coralTimeoutRows = flightRows.filter(
  (row) => row.coral && (row.coral.status & 0x01) !== 0,
).length;

const equipmentFaultValues = [...new Set(flightRows.map((row) => row.equipmentFaults))]
  .toSorted((a, b) => a - b);
const nonCoralFaultRows = flightRows.filter(
  (row) => (row.equipmentFaults & ~0x0008) !== 0,
).length;

const ascentRateMps =
  (apex.gpsAltM - ascentTransition.row.gpsAltM) /
  (apex.elapsedS - ascentTransition.elapsedS);
const descentRateMps =
  (apex.gpsAltM - landingTransition.row.gpsAltM) /
  (landingTransition.elapsedS - apex.elapsedS);

const profileRows = binRows(flightRows, 5, (rows, elapsedS) => ({
  elapsed_min: round(elapsedS / 60, 5),
  gps_alt_km: round(median(rows.map((row) => row.gpsAltM)) / 1000, 5),
  baro_alt_km: round(median(rows.map((row) => row.baroAltM)) / 1000, 5),
  phase: rows.at(-1).phase,
}));
writeCsv(
  path.join(processedDir, "flight_profile.csv"),
  ["elapsed_min", "gps_alt_km", "baro_alt_km", "phase"],
  profileRows,
);

const dynamicsRows = binRows(flightRows, 5, (rows, elapsedS) => ({
  elapsed_min: round(elapsedS / 60, 5),
  vertical_up_mps: round(median(rows.map((row) => -row.gpsVelDownMps)), 4),
  ground_speed_mps: round(median(rows.map((row) => row.gpsSpeedMps)), 4),
  accel_mag_g: round(median(rows.map((row) => row.accelMagG)), 4),
  accel_mag_max_g: round(Math.max(...rows.map((row) => row.accelMagG)), 4),
}));
writeCsv(
  path.join(processedDir, "vertical_dynamics.csv"),
  [
    "elapsed_min",
    "vertical_up_mps",
    "ground_speed_mps",
    "accel_mag_g",
    "accel_mag_max_g",
  ],
  dynamicsRows,
);

const environmentRows = binRows(flightRows, 10, (rows, elapsedS) => ({
  elapsed_min: round(elapsedS / 60, 5),
  pressure_hpa: round(median(rows.map((row) => row.pressurePa)) / 100, 5),
  temperature_c: round(median(rows.map((row) => row.baroTempC)), 4),
  gps_alt_km: round(median(rows.map((row) => row.gpsAltM)) / 1000, 5),
}));
writeCsv(
  path.join(processedDir, "environment.csv"),
  ["elapsed_min", "pressure_hpa", "temperature_c", "gps_alt_km"],
  environmentRows,
);

const powerRows = binRows(flightRows, 2, (rows, elapsedS) => ({
  elapsed_min: round(elapsedS / 60, 5),
  battery_v: round(median(rows.map((row) => row.batteryV)), 4),
  battery_min_v: round(Math.min(...rows.map((row) => row.batteryV)), 4),
  battery_max_v: round(Math.max(...rows.map((row) => row.batteryV)), 4),
  coral_fault: rows.some((row) => (row.equipmentFaults & 0x0008) !== 0) ? 1 : 0,
}));
writeCsv(
  path.join(processedDir, "power_health.csv"),
  ["elapsed_min", "battery_v", "battery_min_v", "battery_max_v", "coral_fault"],
  powerRows,
);
writeCsv(
  path.join(processedDir, "battery_outliers.csv"),
  ["elapsed_min", "battery_v"],
  batteryOutliers.map((row) => ({
    elapsed_min: round(row.elapsedS / 60, 5),
    battery_v: round(row.batteryV, 4),
  })),
);

const trajectoryRows = binRows(validGpsRows, 10, (rows, elapsedS) => ({
  elapsed_min: round(elapsedS / 60, 5),
  latitude_deg: round(median(rows.map((row) => row.gpsLat)), 7),
  longitude_deg: round(median(rows.map((row) => row.gpsLon)), 7),
  altitude_km: round(median(rows.map((row) => row.gpsAltM)) / 1000, 5),
  map_x: round(webMercatorTileX(median(rows.map((row) => row.gpsLon)), 11), 7),
  map_y: round(webMercatorNorthing(median(rows.map((row) => row.gpsLat)), 11), 7),
}));
writeCsv(
  path.join(processedDir, "trajectory.csv"),
  [
    "elapsed_min",
    "latitude_deg",
    "longitude_deg",
    "altitude_km",
    "map_x",
    "map_y",
  ],
  trajectoryRows,
);

writeCsv(
  path.join(processedDir, "payload_frames.csv"),
  ["elapsed_min", "sequence", "cloud_fraction_percent", "status", "valid"],
  uniqueCoral.map((frame) => ({
    elapsed_min: round(frame.elapsedS / 60, 5),
    sequence: frame.sequence,
    cloud_fraction_percent: round(frame.fractionRaw / 65535 * 100, 4),
    status: frame.status,
    valid: frame.coralValid,
  })),
);
writeCsv(
  path.join(processedDir, "coral_fault_intervals.csv"),
  ["start_min", "end_min", "duration_s"],
  coralFaultIntervals.map((interval) => ({
    start_min: round(interval.startS / 60, 5),
    end_min: round(interval.endS / 60, 5),
    duration_s: round(interval.durationS, 3),
  })),
);

const fixRows = binRows(flightRows, 2, (rows, elapsedS) => ({
  elapsed_min: round(elapsedS / 60, 5),
  fix_type: rows.at(-1).gpsFix,
  satellites: round(median(rows.map((row) => row.gpsSatellites)), 1),
}));
writeCsv(
  path.join(processedDir, "gps_fix.csv"),
  ["elapsed_min", "fix_type", "satellites"],
  fixRows,
);

const intervalBins = [
  [0, 0.095],
  [0.095, 0.105],
  [0.105, 0.2],
  [0.2, 0.5],
  [0.5, 1],
  [1, 2],
  [2, 10],
  [10, Number.POSITIVE_INFINITY],
];
const intervalHistogram = intervalBins.map(([low, high]) => ({
  bin_label: high === Number.POSITIVE_INFINITY
    ? `>${low}`
    : `${low.toFixed(3)}-${high.toFixed(3)}`,
  lower_s: low,
  upper_s: Number.isFinite(high) ? high : "",
  count: positiveSampleStepsS.filter((step) => step >= low && step < high).length,
}));
writeCsv(
  path.join(processedDir, "sample_interval_histogram.csv"),
  ["bin_label", "lower_s", "upper_s", "count"],
  intervalHistogram,
);

const ttcRows = [];
const ttcAllRows = [];
for (let session = 0; session < telemetryFiles.length; session += 1) {
  const parsed = readCsv(telemetryFiles[session]);
  for (const fields of parsed.rows) {
    const receiveMs = Date.parse(fields[parsed.index.pc_receive_time_iso]);
    const row = {
      session: session + 1,
      receiveMs,
      elapsedS: (receiveMs - flightStartUtcMs) / 1000,
      telemetryValid: bool(fields[parsed.index.telemetry_valid]),
      crcOk: bool(fields[parsed.index.crc_ok]),
      rssiDbm: number(fields[parsed.index.lora_downlink_rssi_dbm]),
      snrDb: number(fields[parsed.index.lora_downlink_snr_db]),
      sequence: number(fields[parsed.index.sequence_number]),
      lost: number(fields[parsed.index.lost_packets_since_previous]) ?? 0,
      duplicate: bool(fields[parsed.index.is_duplicate_packet]),
      flightState: number(fields[parsed.index.flight_state]),
      batteryV: number(fields[parsed.index.battery_v]),
      latitudeDeg: number(fields[parsed.index.latitude_deg]),
      longitudeDeg: number(fields[parsed.index.longitude_deg]),
      altitudeM: number(fields[parsed.index.gnss_altitude_m]),
    };
    ttcAllRows.push(row);
    if (
      receiveMs >= flightStartUtcMs &&
      receiveMs <= flightEndUtcMs &&
      row.telemetryValid
    ) {
      ttcRows.push(row);
    }
  }
}
writeCsv(
  path.join(processedDir, "ttc_link.csv"),
  ["elapsed_min", "rssi_dbm", "snr_db", "session", "sequence", "lost_packets"],
  ttcRows.map((row) => ({
    elapsed_min: round(row.elapsedS / 60, 5),
    rssi_dbm: row.rssiDbm,
    snr_db: row.snrDb,
    session: row.session,
    sequence: row.sequence,
    lost_packets: row.lost,
  })),
);

for (const session of [1, 2]) {
  writeCsv(
    path.join(processedDir, `ttc_link_session${session}.csv`),
    ["elapsed_min", "rssi_dbm", "snr_db", "sequence", "lost_packets"],
    ttcRows.filter((row) => row.session === session).map((row) => ({
      elapsed_min: round(row.elapsedS / 60, 5),
      rssi_dbm: row.rssiDbm,
      snr_db: row.snrDb,
      sequence: row.sequence,
      lost_packets: row.lost,
    })),
  );
}

const groundEventCounts = {};
const groundEvents = [];
for (let session = 0; session < eventFiles.length; session += 1) {
  const file = eventFiles[session];
  const parsed = readCsv(file);
  for (const fields of parsed.rows) {
    const eventType = fields[parsed.index.event_type];
    groundEventCounts[eventType] = (groundEventCounts[eventType] ?? 0) + 1;
    groundEvents.push({
      session: session + 1,
      eventType,
      timeMs: Date.parse(fields[parsed.index.pc_time_iso]),
    });
  }
}

const failedReceptionEventsInWindow = groundEvents.filter(
  (event) =>
    event.timeMs >= flightStartUtcMs &&
    event.timeMs <= flightEndUtcMs &&
    ["lora_crc_error", "non_telemetry_packet"].includes(event.eventType),
);
for (const [eventType, fileStem] of [
  ["lora_crc_error", "ttc_failures_crc"],
  ["non_telemetry_packet", "ttc_failures_length"],
]) {
  writeCsv(
    path.join(processedDir, `${fileStem}.csv`),
    ["elapsed_min"],
    failedReceptionEventsInWindow
      .filter((event) => event.eventType === eventType)
      .map((event) => ({
        elapsed_min: round((event.timeMs - flightStartUtcMs) / 60000, 5),
      })),
  );
}

/*
 * Session 1 range is a GNSS-derived 3D slant range to a fixed station at the
 * CubeSat's first valid GNSS position, matching the field setup. Session 2
 * contains no ground-station GNSS. Its range is therefore an explicitly
 * labelled operational estimate: the recovery station approaches linearly
 * from 1 km at radio initialisation to co-location at the last valid packet.
 * A 0.05 km plotting floor keeps co-location visible on the logarithmic axis.
 */
const stationReference = {
  latitudeDeg: firstFix.gpsLat,
  longitudeDeg: firstFix.gpsLon,
  altitudeM: firstFix.gpsAltM,
};
const session1StopEvent = groundEvents.find(
  (event) => event.session === 1 && event.eventType === "receiver_stopped",
);
const session1StopElapsedS = session1StopEvent
  ? (session1StopEvent.timeMs - flightStartUtcMs) / 1000
  : ttcRows.filter((row) => row.session === 1).at(-1).elapsedS;
const session1RangeRows = binRows(
  validGpsRows.filter((row) => row.elapsedS <= session1StopElapsedS),
  10,
  (rows, elapsedS) => {
    const latitudeDeg = median(rows.map((row) => row.gpsLat));
    const longitudeDeg = median(rows.map((row) => row.gpsLon));
    const altitudeM = median(rows.map((row) => row.gpsAltM));
    const rangeKm = slantRangeKm(
      stationReference.latitudeDeg,
      stationReference.longitudeDeg,
      stationReference.altitudeM,
      latitudeDeg,
      longitudeDeg,
      altitudeM,
    );
    return {
      elapsed_min: round(elapsedS / 60, 5),
      range_km: round(Math.max(rangeKm, 0.05), 5),
    };
  },
);
writeCsv(
  path.join(processedDir, "ttc_range_session1.csv"),
  ["elapsed_min", "range_km"],
  session1RangeRows,
);

const session2StartEvent = groundEvents.find(
  (event) => event.session === 2 && event.eventType === "radio_initialized",
);
const session2ValidRows = ttcRows.filter((row) => row.session === 2);
const session2StartElapsedS =
  (session2StartEvent.timeMs - flightStartUtcMs) / 1000;
const session2EndElapsedS = session2ValidRows.at(-1).elapsedS;
const session2RangeRows = [
  {
    elapsed_min: round(session2StartElapsedS / 60, 5),
    range_km: 1,
  },
  {
    elapsed_min: round(session2EndElapsedS / 60, 5),
    range_km: 0.05,
  },
];
writeCsv(
  path.join(processedDir, "ttc_range_session2.csv"),
  ["elapsed_min", "range_km"],
  session2RangeRows,
);

const ttcRssi = ttcRows.map((row) => row.rssiDbm).filter(Number.isFinite);
const ttcSnr = ttcRows.map((row) => row.snrDb).filter(Number.isFinite);
const ttcInvalidInWindow = ttcAllRows.filter(
  (row) =>
    row.receiveMs >= flightStartUtcMs &&
    row.receiveMs <= flightEndUtcMs &&
    !row.telemetryValid,
);

const summary = {
  generatedAt: new Date().toISOString(),
  scope: {
    primaryFile: path.relative(repoRoot, flightFile).replaceAll("\\", "/"),
    publicRows: flightRows.length,
    fullArchiveRows: fs.readFileSync(fullArchiveFile, "utf8").trimEnd().split(/\r?\n/)
      .length - 1,
    startCest: "2026-07-28 14:29:00 CEST",
    endCest: "2026-07-28 17:32:00 CEST",
    durationS: flightDurationS,
    sha256: {
      publicFlightCsv: sha256(flightFile),
      fullArchiveCsv: sha256(fullArchiveFile),
      telemetrySession1: sha256(telemetryFiles[0]),
      telemetrySession2: sha256(telemetryFiles[1]),
    },
  },
  timeline: {
    transitions: transitions.map((event) => ({
      timeCest: formatClockFromElapsed(event.elapsedS),
      elapsedS: round(event.elapsedS, 3),
      from: phaseNames[event.from],
      to: phaseNames[event.to],
      gpsAltitudeM: round(event.row.gpsAltM, 2),
      baroAltitudeM: round(event.row.baroAltM, 2),
      verticalDownMps: round(event.row.gpsVelDownMps, 2),
    })),
    phaseDurationsS: Object.fromEntries(
      Object.entries(phaseDurationsS).map(([name, value]) => [name, round(value, 3)]),
    ),
    bootTransitions: bootTransitions.map((event) => ({
      ...event,
      elapsedS: round(event.elapsedS, 3),
      priorElapsedS: round(event.priorElapsedS, 3),
      gapS: round(event.gapS, 3),
      timeCest: formatClockFromElapsed(event.elapsedS),
    })),
  },
  flightPerformance: {
    first3dFixTimeCest: formatClockFromElapsed(firstFix.elapsedS),
    first3dFixElapsedS: round(firstFix.elapsedS, 3),
    apexGpsAltitudeM: round(apex.gpsAltM, 2),
    apexTimeCest: formatClockFromElapsed(apex.elapsedS),
    apexElapsedS: round(apex.elapsedS, 3),
    apexLatitudeDeg: round(apex.gpsLat, 7),
    apexLongitudeDeg: round(apex.gpsLon, 7),
    apexBaroAltitudeM: round(apex.baroAltM, 2),
    maximumBaroAltitudeM: round(baroApex.baroAltM, 2),
    maximumBaroTimeCest: formatClockFromElapsed(baroApex.elapsedS),
    altitudeDisagreementAtGpsApexM: round(apex.gpsAltM - apex.baroAltM, 2),
    maximumClimbMps: round(-maxClimb.gpsVelDownMps, 2),
    maximumClimbTimeCest: formatClockFromElapsed(maxClimb.elapsedS),
    maximumDescentMps: round(maxDescent.gpsVelDownMps, 2),
    maximumDescentTimeCest: formatClockFromElapsed(maxDescent.elapsedS),
    maximumGroundSpeedMps: round(maxGroundSpeed.gpsSpeedMps, 2),
    meanAscentRateMps: round(ascentRateMps, 3),
    meanDescentRateMps: round(descentRateMps, 3),
    launchLatitudeDeg: round(launchPosition.gpsLat, 7),
    launchLongitudeDeg: round(launchPosition.gpsLon, 7),
    landingLatitudeDeg: round(landingPosition.gpsLat, 7),
    landingLongitudeDeg: round(landingPosition.gpsLon, 7),
    launchToLandingDistanceKm: round(landingDistanceKm, 3),
    maximumRangeFromLaunchKm: round(maximumRangeKm, 3),
  },
  environment: {
    minimumPressureHpa: round(Math.min(...pressures) / 100, 3),
    minimumPressureTimeCest: formatClockFromElapsed(minBy(flightRows, (r) => r.pressurePa).elapsedS),
    temperatureMinimumC: round(Math.min(...temperatures), 2),
    temperatureMaximumC: round(Math.max(...temperatures), 2),
  },
  powerAndImu: {
    batteryMedianV: round(median(batteryValues), 3),
    batteryP05V: round(quantile(batteryValues, 0.05), 3),
    batteryP95V: round(quantile(batteryValues, 0.95), 3),
    batteryMinimumV: round(Math.min(...batteryValues), 3),
    batteryMaximumV: round(Math.max(...batteryValues), 3),
    batteryOutlierRows: batteryOutliers.length,
    maximumAccelerationG: round(maxAcceleration.accelMagG, 3),
    maximumAccelerationTimeCest: formatClockFromElapsed(maxAcceleration.elapsedS),
    maximumAccelerationComponentsG: [
      round(maxAcceleration.accelXG, 3),
      round(maxAcceleration.accelYG, 3),
      round(maxAcceleration.accelZG, 3),
    ],
  },
  payload: {
    uniqueCoralRecords: uniqueCoral.length,
    goodFrames: goodCoralFrames.length,
    cloudFractionMeanPercent: round(mean(cloudFractions), 3),
    cloudFractionMedianPercent: round(median(cloudFractions), 3),
    cloudFractionP90Percent: round(quantile(cloudFractions, 0.9), 3),
    cloudFractionZeroOrLessPercent: round(
      goodCoralFrames.filter((frame) => frame.fractionRaw === 0).length /
        goodCoralFrames.length * 100,
      3,
    ),
    coralInvalidRows,
    coralInvalidPercent: round(coralInvalidRows / flightRows.length * 100, 3),
    coralTimeoutRows,
    faultIntervals: coralFaultIntervals.length,
    longestFaultS: round(Math.max(...coralFaultIntervals.map((i) => i.durationS)), 3),
    totalFaultTelemetryDurationS: round(
      coralFaultIntervals.reduce((sum, interval) => sum + interval.durationS, 0),
      3,
    ),
  },
  health: {
    equipmentEnabledValues: [...new Set(flightRows.map((row) => row.equipmentEnabled))],
    equipmentFaultValues,
    nonCoralFaultRows,
    gpsTimeoutMax: Math.max(...flightRows.map((row) => row.gpsTimeouts)),
    imuTimeoutMax: Math.max(...flightRows.map((row) => row.imuTimeouts)),
    baroTimeoutMax: Math.max(...flightRows.map((row) => row.baroTimeouts)),
    loraTimeoutMax: Math.max(...flightRows.map((row) => row.loraTimeouts)),
    loraTxFaultMax: Math.max(...flightRows.map((row) => row.loraTxFaults)),
    sdFaultMax: Math.max(...flightRows.map((row) => row.sdFaults)),
    watchdogResetMax: Math.max(...flightRows.map((row) => row.watchdogResets)),
    i2cStateValues: [...new Set(flightRows.map((row) => row.i2cState))],
  },
  quality: {
    medianSampleIntervalS: round(median(positiveSampleStepsS), 4),
    meanSampleIntervalS: round(mean(positiveSampleStepsS), 4),
    sampleIntervalStdS: round(standardDeviation(positiveSampleStepsS), 4),
    p95SampleIntervalS: round(quantile(positiveSampleStepsS, 0.95), 4),
    largestRetainedGapS: round(largestGapS, 3),
    stallsAtLeast1s: samplingStalls.length,
    genuineStallsOneToTwoSeconds: genuineSamplingStalls.length,
    longestGenuineStallS: round(Math.max(...genuineSamplingStalls), 3),
    rowsFix3: gpsFixRows.fix3,
    rowsFix2: gpsFixRows.fix2,
    rowsOtherFix: gpsFixRows.other,
    fix3Percent: round(gpsFixRows.fix3 / flightRows.length * 100, 3),
    gpsValidPercent: round(
      flightRows.filter((row) => row.gpsValid === 1).length / flightRows.length * 100,
      3,
    ),
    imuValidPercent: round(
      flightRows.filter((row) => row.imuValid === 1).length / flightRows.length * 100,
      3,
    ),
    baroValidPercent: round(
      flightRows.filter((row) => row.baroValid === 1).length / flightRows.length * 100,
      3,
    ),
    batteryValidPercent: round(
      flightRows.filter((row) => row.batteryValid === 1).length /
        flightRows.length * 100,
      3,
    ),
  },
  ttc: {
    totalRowsAllSessions: ttcAllRows.length,
    validPacketsInFlightWindow: ttcRows.length,
    invalidRowsInFlightWindow: ttcInvalidInWindow.length,
    lostPacketsReportedInFlightWindow: ttcRows.reduce((sum, row) => sum + row.lost, 0),
    duplicatePacketsInFlightWindow: ttcRows.filter((row) => row.duplicate).length,
    firstValidPacketCest: ttcRows.length ? formatClockFromElapsed(ttcRows[0].elapsedS) : null,
    lastValidPacketCest: ttcRows.length ? formatClockFromElapsed(ttcRows.at(-1).elapsedS) : null,
    rssiMedianDbm: round(median(ttcRssi), 2),
    rssiMinimumDbm: round(Math.min(...ttcRssi), 2),
    rssiMaximumDbm: round(Math.max(...ttcRssi), 2),
    snrMedianDb: round(median(ttcSnr), 2),
    snrMinimumDb: round(Math.min(...ttcSnr), 2),
    snrMaximumDb: round(Math.max(...ttcSnr), 2),
    invalidReceptionPercent: round(
      ttcInvalidInWindow.length /
        (ttcRows.length + ttcInvalidInWindow.length) * 100,
      2,
    ),
    session1LastValidSlantRangeKm: round(
      (() => {
        const last = ttcRows.filter((row) => row.session === 1).at(-1);
        const position = nearestValidPosition(validGpsRows, last.elapsedS);
        return slantRangeKm(
          stationReference.latitudeDeg,
          stationReference.longitudeDeg,
          stationReference.altitudeM,
          position.gpsLat,
          position.gpsLon,
          position.gpsAltM,
        );
      })(),
      2,
    ),
    session1ReceiverStopSlantRangeKm: round(
      (() => {
        const position = nearestValidPosition(validGpsRows, session1StopElapsedS);
        return slantRangeKm(
          stationReference.latitudeDeg,
          stationReference.longitudeDeg,
          stationReference.altitudeM,
          position.gpsLat,
          position.gpsLon,
          position.gpsAltM,
        );
      })(),
      2,
    ),
    session2ApproachStartKm: 1,
    session2ApproachStartCest: formatClockFromElapsed(session2StartElapsedS),
    session2ApproachEndCest: formatClockFromElapsed(session2EndElapsedS),
    crcErrorEventsAllSessions: groundEventCounts.lora_crc_error ?? 0,
    unexpectedLengthPacketsAllSessions: groundEventCounts.non_telemetry_packet ?? 0,
    crcErrorEventsInFlightWindow: failedReceptionEventsInWindow.filter(
      (event) => event.eventType === "lora_crc_error",
    ).length,
    unexpectedLengthPacketsInFlightWindow: failedReceptionEventsInWindow.filter(
      (event) => event.eventType === "non_telemetry_packet",
    ).length,
    eventCounts: groundEventCounts,
  },
};

fs.writeFileSync(
  path.join(scriptDir, "analysis_summary.json"),
  `${JSON.stringify(summary, null, 2)}\n`,
);

const macros = {
  PublicRows: summary.scope.publicRows.toLocaleString("en-US"),
  FullArchiveRows: summary.scope.fullArchiveRows.toLocaleString("en-US"),
  FlightDurationHours: round(summary.scope.durationS / 3600, 3),
  FlightEndMinute: round(summary.scope.durationS / 60, 5),
  FirstFixTime: summary.flightPerformance.first3dFixTimeCest,
  FirstFixMinutes: round(summary.flightPerformance.first3dFixElapsedS / 60, 2),
  LaunchTime: summary.timeline.transitions.find((event) => event.to === "Launch").timeCest,
  AscentTime: summary.timeline.transitions.find((event) => event.to === "Ascent").timeCest,
  DescentTime: summary.timeline.transitions.find((event) => event.to === "Descent").timeCest,
  LandingTime: summary.timeline.transitions.find((event) => event.to === "Landing").timeCest,
  LaunchMinute: round(launchTransition.elapsedS / 60, 4),
  AscentMinute: round(ascentTransition.elapsedS / 60, 4),
  DescentMinute: round(descentTransition.elapsedS / 60, 4),
  LandingMinute: round(landingTransition.elapsedS / 60, 4),
  ApexMinute: round(apex.elapsedS / 60, 4),
  ResetMinute: round(bootTransitions[0].elapsedS / 60, 4),
  StandbyDurationMin: round(summary.timeline.phaseDurationsS.Standby / 60, 2),
  LaunchDurationS: round(summary.timeline.phaseDurationsS.Launch, 1),
  AscentDurationMin: round(summary.timeline.phaseDurationsS.Ascent / 60, 2),
  DescentDurationMin: round(summary.timeline.phaseDurationsS.Descent / 60, 2),
  LandingDurationMin: round(summary.timeline.phaseDurationsS.Landing / 60, 2),
  ApexGpsKm: round(summary.flightPerformance.apexGpsAltitudeM / 1000, 3),
  ApexGpsM: summary.flightPerformance.apexGpsAltitudeM.toLocaleString("en-US"),
  ApexBaroKm: round(summary.flightPerformance.apexBaroAltitudeM / 1000, 3),
  MaxBaroKm: round(summary.flightPerformance.maximumBaroAltitudeM / 1000, 3),
  ApexTime: summary.flightPerformance.apexTimeCest,
  AltitudeDifferenceKm: round(
    summary.flightPerformance.altitudeDisagreementAtGpsApexM / 1000,
    3,
  ),
  MaxClimb: summary.flightPerformance.maximumClimbMps,
  MaxDescent: summary.flightPerformance.maximumDescentMps,
  MeanAscent: summary.flightPerformance.meanAscentRateMps,
  MeanDescent: summary.flightPerformance.meanDescentRateMps,
  MaxGroundSpeed: summary.flightPerformance.maximumGroundSpeedMps,
  LaunchLatitude: summary.flightPerformance.launchLatitudeDeg,
  LaunchLongitude: summary.flightPerformance.launchLongitudeDeg,
  ApexLatitude: summary.flightPerformance.apexLatitudeDeg,
  ApexLongitude: summary.flightPerformance.apexLongitudeDeg,
  LandingLatitude: summary.flightPerformance.landingLatitudeDeg,
  LandingLongitude: summary.flightPerformance.landingLongitudeDeg,
  LaunchMapX: round(
    webMercatorTileX(summary.flightPerformance.launchLongitudeDeg, 11),
    7,
  ),
  LaunchMapY: round(
    webMercatorNorthing(summary.flightPerformance.launchLatitudeDeg, 11),
    7,
  ),
  ApexMapX: round(
    webMercatorTileX(summary.flightPerformance.apexLongitudeDeg, 11),
    7,
  ),
  ApexMapY: round(
    webMercatorNorthing(summary.flightPerformance.apexLatitudeDeg, 11),
    7,
  ),
  LandingMapX: round(
    webMercatorTileX(summary.flightPerformance.landingLongitudeDeg, 11),
    7,
  ),
  LandingMapY: round(
    webMercatorNorthing(summary.flightPerformance.landingLatitudeDeg, 11),
    7,
  ),
  LandingDistanceKm: summary.flightPerformance.launchToLandingDistanceKm,
  MaxRangeKm: summary.flightPerformance.maximumRangeFromLaunchKm,
  MinPressureHpa: summary.environment.minimumPressureHpa,
  MinTemperatureC: summary.environment.temperatureMinimumC,
  MaxTemperatureC: summary.environment.temperatureMaximumC,
  BatteryMedianV: summary.powerAndImu.batteryMedianV,
  BatteryPZeroFiveV: summary.powerAndImu.batteryP05V,
  BatteryPNinetyFiveV: summary.powerAndImu.batteryP95V,
  BatteryMinV: summary.powerAndImu.batteryMinimumV,
  BatteryMaxV: summary.powerAndImu.batteryMaximumV,
  BatteryOutlierRows: summary.powerAndImu.batteryOutlierRows,
  MaxAccelerationG: summary.powerAndImu.maximumAccelerationG,
  MaxAccelerationTime: summary.powerAndImu.maximumAccelerationTimeCest,
  GoodCoralFrames: summary.payload.goodFrames,
  CoralInvalidRows: summary.payload.coralInvalidRows.toLocaleString("en-US"),
  CoralInvalidPercent: summary.payload.coralInvalidPercent,
  CoralTimeoutRows: summary.payload.coralTimeoutRows.toLocaleString("en-US"),
  CoralFaultIntervals: summary.payload.faultIntervals,
  CoralLongestFaultS: summary.payload.longestFaultS,
  CoralTotalFaultS: summary.payload.totalFaultTelemetryDurationS,
  CloudMeanPercent: summary.payload.cloudFractionMeanPercent,
  CloudMedianPercent: summary.payload.cloudFractionMedianPercent,
  CloudPNinetyPercent: summary.payload.cloudFractionP90Percent,
  CloudZeroPercent: summary.payload.cloudFractionZeroOrLessPercent,
  MedianSampleMs: round(summary.quality.medianSampleIntervalS * 1000, 1),
  SamplePNinetyFiveMs: round(summary.quality.p95SampleIntervalS * 1000, 1),
  LargestGapS: summary.quality.largestRetainedGapS,
  SamplingStalls: summary.quality.stallsAtLeast1s,
  GenuineSamplingStalls: summary.quality.genuineStallsOneToTwoSeconds,
  LongestGenuineStallS: summary.quality.longestGenuineStallS,
  FixThreePercent: summary.quality.fix3Percent,
  FixThreeRows: summary.quality.rowsFix3.toLocaleString("en-US"),
  ValidDownlinks: summary.ttc.validPacketsInFlightWindow,
  InvalidDownlinks: summary.ttc.invalidRowsInFlightWindow,
  ReportedLostPackets: summary.ttc.lostPacketsReportedInFlightWindow,
  DuplicateDownlinks: summary.ttc.duplicatePacketsInFlightWindow,
  RssiMedian: summary.ttc.rssiMedianDbm,
  RssiMin: summary.ttc.rssiMinimumDbm,
  RssiMax: summary.ttc.rssiMaximumDbm,
  SnrMedian: summary.ttc.snrMedianDb,
  SnrMin: summary.ttc.snrMinimumDb,
  SnrMax: summary.ttc.snrMaximumDb,
  InvalidDownlinkPercent: summary.ttc.invalidReceptionPercent,
  SessionOneLastRangeKm: summary.ttc.session1LastValidSlantRangeKm,
  SessionOneStopRangeKm: summary.ttc.session1ReceiverStopSlantRangeKm,
  SessionTwoStartRangeKm: summary.ttc.session2ApproachStartKm,
  SessionTwoStartTime: summary.ttc.session2ApproachStartCest,
  SessionTwoEndTime: summary.ttc.session2ApproachEndCest,
  TtcCrcErrors: summary.ttc.crcErrorEventsAllSessions,
  TtcUnexpectedLengthPackets: summary.ttc.unexpectedLengthPacketsAllSessions,
  TtcCrcErrorsInWindow: summary.ttc.crcErrorEventsInFlightWindow,
  TtcUnexpectedLengthPacketsInWindow:
    summary.ttc.unexpectedLengthPacketsInFlightWindow,
  TtcFirstTime: summary.ttc.firstValidPacketCest,
  TtcLastTime: summary.ttc.lastValidPacketCest,
  BootGapS: summary.timeline.bootTransitions[0].gapS,
};
const texLines = [
  "% Generated by generate_analysis.mjs. Do not edit by hand.",
  ...Object.entries(macros).map(
    ([name, value]) => `\\newcommand{\\${name}}{${value}}`,
  ),
  "",
];
fs.writeFileSync(path.join(scriptDir, "report_values.tex"), texLines.join("\n"));

console.log(
  JSON.stringify(
    {
      summary: path.relative(repoRoot, path.join(scriptDir, "analysis_summary.json")),
      processedFiles: fs.readdirSync(processedDir).length,
      publicRows: flightRows.length,
      goodCoralFrames: goodCoralFrames.length,
      validDownlinks: ttcRows.length,
    },
    null,
    2,
  ),
);
