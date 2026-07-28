#!/usr/bin/env node

/*
 * Post-flight archive audit and correlation tool.
 *
 * The firmware stores no GNSS calendar date in its CSV (only HHMMSS).  The
 * supplied --flight-date is therefore an assumed acquisition date used to turn the
 * GNSS time-of-day into a CEST filename.  Frame times are matched to the
 * nearest CSV row carrying the corresponding Coral sequence; if that row has
 * no GNSS time, the script interpolates/extrapolates from valid GNSS rows in
 * the same boot.
 */

import fs from 'node:fs';
import path from 'node:path';

const root = process.cwd();
const memoryRoot = path.join(root, 'FLIGHT INTERNAL MEMORY');
const logsRoot = path.join(memoryRoot, 'LOGS');
const coralRoot = path.join(memoryRoot, 'CORAL');
const pngRoot = path.join(root, 'png');
const outputRoot = path.join(root, 'flight-data');

const args = new Set(process.argv.slice(2));
const flightDateArg = process.argv.find((arg) => arg.startsWith('--flight-date='));
const flightDate = flightDateArg?.slice('--flight-date='.length) ?? '2026-07-28';
const renamePng = args.has('--rename-png');

const expectedHeader = 'session,record_timestamp_ms,gps_lat_e7,gps_lon_e7,gps_alt_cm,gps_speed_cms,gps_vel_down_cms,gps_heading_cdeg,gps_utc_time,gps_satellites,gps_fix_type,gps_valid,imu_accel_x_mg,imu_accel_y_mg,imu_accel_z_mg,imu_accel_mag_mg,imu_gyro_x_mdps,imu_gyro_y_mdps,imu_gyro_z_mdps,imu_valid,baro_pressure_pa,baro_alt_cm,baro_temp_centi_c,baro_valid,i2c_bus_state,batt_voltage_mv,batt_valid,coral_block_hex,coral_valid,scv_magic,scv_boot_count,scv_mission_elapsed_ms,scv_flight_phase,scv_reset_reason,scv_equipment_enabled,scv_equipment_faults,scv_gps_timeout_count,scv_imu_timeout_count,scv_baro_timeout_count,scv_coral_timeout_count,scv_lora_timeout_count,scv_lora_tx_fault_counter,scv_sd_fault_count,scv_watchdog_reset_count,scv_last_batt_mv,scv_baro_ground_alt_cm,scv_crc16';
const columns = expectedHeader.split(',');
const indexOf = Object.fromEntries(columns.map((column, index) => [column, index]));

function walk(directory, predicate) {
  const result = [];
  for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
    const name = path.join(directory, entry.name);
    if (entry.isDirectory()) result.push(...walk(name, predicate));
    else if (predicate(name)) result.push(name);
  }
  return result;
}

function bootFromPath(file) {
  const match = file.match(/B(\d{8})/);
  return match ? Number(match[1]) : null;
}

function sortByBootThenName(a, b) {
  return (bootFromPath(a) - bootFromPath(b)) || a.localeCompare(b, undefined, { numeric: true });
}

function parseLogName(file) {
  const match = path.basename(file).match(/^LOG_(\d+)_START_(\d+)\.CSV$/i);
  return match ? { session: Number(match[1]), startS: Number(match[2]) } : null;
}

function parseCoralName(file) {
  const match = path.basename(file).match(/^F(\d+)_cloud(\d+)\.RAW$/i);
  return match ? { sequence: Number(match[1]), cloudPercent: Number(match[2]) } : null;
}

function parsePngName(file) {
  const match = path.basename(file).match(/^B(\d+)_D(\d+)_F(\d+)_cloud(\d+)\.png$/i);
  return match ? { boot: Number(match[1]), directory: Number(match[2]), sequence: Number(match[3]), cloudPercent: Number(match[4]) } : null;
}

function timestampNameWithoutCloud(file) {
  return path.basename(file).replace(/_cloud\d+\.png$/i, '.png');
}

/* After a timestamp rename, retain the RAW-derived identity from the prior
 * report so the tool can be rerun safely if the assumed date is corrected. */
const priorPngInfoByPath = new Map();
const priorPngInfoByBasename = new Map();
const priorAuditFile = path.join(outputRoot, 'flight_data_audit.json');
if (fs.existsSync(priorAuditFile)) {
  try {
    const prior = JSON.parse(fs.readFileSync(priorAuditFile, 'utf8'));
    for (const entry of prior.pngRenames ?? []) {
      const raw = parseCoralName(entry.rawSource ?? '');
      const boot = bootFromPath(entry.rawSource ?? '');
      if (raw && boot !== null) {
        const info = { boot, directory: null, sequence: raw.sequence, cloudPercent: raw.cloudPercent };
        priorPngInfoByPath.set(path.resolve(root, entry.from), info);
        priorPngInfoByPath.set(path.resolve(root, entry.to), info);
        priorPngInfoByBasename.set(path.basename(entry.from), info);
        priorPngInfoByBasename.set(path.basename(entry.to), info);
      }
    }
  } catch {
    /* A report is auxiliary; a malformed prior report must not block the audit. */
  }
}

function coralData(hex) {
  if (!/^[0-9A-Fa-f]{32}$/.test(hex)) return null;
  const bytes = Buffer.from(hex, 'hex');
  return {
    sequence: bytes.readUInt32LE(0),
    status: bytes[7],
    rxTickMs: bytes.readUInt32LE(8),
    frameCount: bytes.readUInt16LE(12),
  };
}

function hmsToSeconds(value) {
  const h = Math.floor(value / 10000);
  const m = Math.floor((value % 10000) / 100);
  const s = value % 100;
  return h * 3600 + m * 60 + s;
}

function secondsToCestName(seconds) {
  /* Format a UTC time-of-day as CEST (+02:00) without depending on the
   * workstation time zone.  The flight-date represents the local CEST date. */
  const date = new Date(`${flightDate}T00:00:00Z`);
  const milliseconds = Math.round(seconds * 1000);
  date.setUTCMilliseconds(date.getUTCMilliseconds() + milliseconds + 2 * 3600 * 1000);
  const y = date.getUTCFullYear();
  const m = String(date.getUTCMonth() + 1).padStart(2, '0');
  const d = String(date.getUTCDate()).padStart(2, '0');
  const hh = String(date.getUTCHours()).padStart(2, '0');
  const mm = String(date.getUTCMinutes()).padStart(2, '0');
  const ss = String(date.getUTCSeconds()).padStart(2, '0');
  const ms = String(date.getUTCMilliseconds()).padStart(3, '0');
  return `${y}-${m}-${d}_${hh}-${mm}-${ss}.${ms}_CEST.png`;
}

function interpolateTime(anchors, tickMs) {
  if (anchors.length === 0) return null;
  let before = null;
  let after = null;
  for (const anchor of anchors) {
    if (anchor.tickMs <= tickMs) before = anchor;
    if (anchor.tickMs >= tickMs) { after = anchor; break; }
  }
  if (before && after && before !== after) {
    return before.seconds + (tickMs - before.tickMs) * (after.seconds - before.seconds) / (after.tickMs - before.tickMs);
  }
  const nearest = before ?? after;
  return nearest.seconds + (tickMs - nearest.tickMs) / 1000;
}

const issues = [];
const csvFiles = walk(logsRoot, (file) => file.toUpperCase().endsWith('.CSV')).sort(sortByBootThenName);
const rawFiles = walk(coralRoot, (file) => file.toUpperCase().endsWith('.RAW')).sort(sortByBootThenName);
const pngFiles = walk(pngRoot, (file) => file.toLowerCase().endsWith('.png')).sort();
const rows = [];
const csvSummary = [];

for (const file of csvFiles) {
  const relative = path.relative(root, file);
  const name = parseLogName(file);
  if (!name) issues.push(`Unexpected CSV name: ${relative}`);
  const contents = fs.readFileSync(file, 'utf8');
  if (contents.includes('\0')) issues.push(`NUL byte in CSV: ${relative}`);
  const lines = contents.split(/\r?\n/);
  if (lines.at(-1) === '') lines.pop();
  if (lines[0] !== expectedHeader) issues.push(`Header mismatch: ${relative}`);
  let previousTick = null;
  let rowCount = 0;
  for (let i = 1; i < lines.length; i++) {
    const fields = lines[i].split(',');
    if (fields.length !== columns.length) {
      issues.push(`Malformed CSV row (${fields.length}/${columns.length} fields): ${relative}:${i + 1}`);
      continue;
    }
    const tickMs = Number(fields[indexOf.record_timestamp_ms]);
    if (!Number.isFinite(tickMs)) {
      issues.push(`Invalid record timestamp: ${relative}:${i + 1}`);
      continue;
    }
    if (previousTick !== null && tickMs <= previousTick) issues.push(`Non-increasing timestamp: ${relative}:${i + 1}`);
    if (previousTick !== null && tickMs - previousTick > 2000) issues.push(`CSV sample gap ${(tickMs - previousTick) / 1000}s: ${relative}:${i + 1}`);
    previousTick = tickMs;
    const coral = coralData(fields[indexOf.coral_block_hex]);
    if (!coral) issues.push(`Invalid Coral block: ${relative}:${i + 1}`);
    rows.push({ fields, tickMs, boot: bootFromPath(file), file, line: i + 1, coral, gpsUtc: Number(fields[indexOf.gps_utc_time]) });
    rowCount++;
  }
  if (rowCount === 0) issues.push(`CSV has no data rows: ${relative}`);
  csvSummary.push({ file: relative, boot: bootFromPath(file), session: name?.session, rows: rowCount, firstTick: rows.at(-rowCount)?.tickMs ?? null, lastTick: previousTick });
}

rows.sort((a, b) => a.boot - b.boot || a.tickMs - b.tickMs || a.file.localeCompare(b.file) || a.line - b.line);

const rowsByBoot = new Map();
for (const row of rows) {
  if (!rowsByBoot.has(row.boot)) rowsByBoot.set(row.boot, []);
  rowsByBoot.get(row.boot).push(row);
}

/* Boot directories are not a unique timeline in this archive: some contain
 * separate recordings whose HAL ticks restart.  GNSS interpolation must stay
 * inside the CSV file containing the Coral completion record.  GNSS UTC time
 * remains useful even when the receiver has no position fix. */
const anchorsByFile = new Map();
for (const file of csvFiles) {
  const fileRows = rows.filter((row) => row.file === file);
  const anchors = [];
  let previousSeconds = null;
  let dayOffset = 0;
  for (const row of fileRows) {
    if (row.gpsUtc > 0 && row.gpsUtc <= 235959) {
      let seconds = hmsToSeconds(row.gpsUtc);
      if (previousSeconds !== null && seconds < previousSeconds - 12 * 3600) dayOffset += 24 * 3600;
      seconds += dayOffset;
      const previous = anchors.at(-1);
      if (!previous || row.tickMs - previous.tickMs > 1000 || seconds !== previous.seconds) anchors.push({ tickMs: row.tickMs, seconds });
      previousSeconds = seconds - dayOffset;
    }
  }
  anchorsByFile.set(file, anchors);
}

const rawByBootSequence = new Map();
for (const file of rawFiles) {
  const info = parseCoralName(file);
  const boot = bootFromPath(file);
  if (!info || boot === null) {
    issues.push(`Unexpected RAW name: ${path.relative(root, file)}`);
    continue;
  }
  const size = fs.statSync(file).size;
  if (size !== 224 * 224) issues.push(`RAW size ${size}, expected 50176: ${path.relative(root, file)}`);
  rawByBootSequence.set(`${boot}:${info.sequence}`, { ...info, boot, file, size });
}

const rowByBootSequence = new Map();
for (const row of rows) {
  if (!row.coral || row.coral.status !== 0 || row.fields[indexOf.coral_valid] !== '1') continue;
  const key = `${row.boot}:${row.coral.sequence}`;
  const prior = rowByBootSequence.get(key);
  if (!prior || Math.abs(row.tickMs - row.coral.rxTickMs) < Math.abs(prior.tickMs - prior.coral.rxTickMs)) rowByBootSequence.set(key, row);
}

const frameSummary = [];
for (const [key, frame] of rawByBootSequence) {
  const row = rowByBootSequence.get(key);
  if (!row) {
    issues.push(`RAW frame has no successful matching CSV Coral record: ${path.relative(root, frame.file)}`);
    frameSummary.push({ ...frame, status: 'unmatched' });
    continue;
  }
  const seconds = interpolateTime(anchorsByFile.get(row.file) ?? [], row.coral.rxTickMs);
  if (seconds === null) issues.push(`No GNSS time anchor for RAW frame: ${path.relative(root, frame.file)}`);
  frameSummary.push({ ...frame, status: 'matched', recordTickMs: row.tickMs, coralRxTickMs: row.coral.rxTickMs, utcSeconds: seconds, timeMethod: seconds === null ? null : 'CSV-local GNSS-time interpolation', cestFilename: seconds === null ? null : secondsToCestName(seconds) });
}

/* A CRC-valid RAW frame may be written just before a reset prevents its
 * completion record from reaching the CSV.  If it is immediately adjacent to
 * a correlated Coral sequence in the same boot, estimate it from that boot's
 * observed capture cadence. */
for (const [boot] of rowsByBoot) {
  const bootFrames = frameSummary.filter((frame) => frame.boot === boot).sort((a, b) => a.sequence - b.sequence);
  const cadenceSamples = [];
  for (let i = 1; i < bootFrames.length; i++) {
    const earlier = bootFrames[i - 1];
    const later = bootFrames[i];
    if (earlier.utcSeconds !== null && later.utcSeconds !== null && later.sequence > earlier.sequence) {
      cadenceSamples.push((later.utcSeconds - earlier.utcSeconds) / (later.sequence - earlier.sequence));
    }
  }
  cadenceSamples.sort((a, b) => a - b);
  const cadence = cadenceSamples.length ? cadenceSamples[Math.floor(cadenceSamples.length / 2)] : null;
  if (cadence === null) continue;
  for (let i = 0; i < bootFrames.length; i++) {
    const frame = bootFrames[i];
    if (frame.utcSeconds !== undefined && frame.utcSeconds !== null) continue;
    const before = bootFrames[i - 1];
    const after = bootFrames[i + 1];
    if (before?.utcSeconds !== null && before?.sequence === frame.sequence - 1) {
      frame.utcSeconds = before.utcSeconds + cadence;
    } else if (after?.utcSeconds !== null && after?.sequence === frame.sequence + 1) {
      frame.utcSeconds = after.utcSeconds - cadence;
    } else {
      continue;
    }
    frame.timeMethod = 'Coral-sequence cadence extrapolation';
    frame.cestFilename = secondsToCestName(frame.utcSeconds);
  }
}

for (const [boot, bootRows] of rowsByBoot) {
  const uniqueFrames = [...rowByBootSequence.values()].filter((row) => row.boot === boot).map((row) => row.coral.sequence).sort((a, b) => a - b);
  const rawSequences = [...rawByBootSequence.values()].filter((frame) => frame.boot === boot).map((frame) => frame.sequence).sort((a, b) => a - b);
  for (const sequence of uniqueFrames) if (!rawByBootSequence.has(`${boot}:${sequence}`)) issues.push(`Successful CSV Coral frame missing RAW file: boot ${boot}, sequence ${sequence}`);
  for (let i = 1; i < rawSequences.length; i++) {
    if (rawSequences[i] > rawSequences[i - 1] + 1) issues.push(`RAW Coral sequence gap: boot ${boot}, ${rawSequences[i - 1]} to ${rawSequences[i]}`);
  }
  for (let i = 1; i < bootRows.length; i++) {
    const delta = bootRows[i].tickMs - bootRows[i - 1].tickMs;
    if (delta > 2000) issues.push(`CSV inter-file sample gap ${delta / 1000}s: boot ${boot}`);
  }
}

/* Compatibility map for timestamp names made by the first (boot-wide)
 * correlation. It lets a later audit recover the image identity even if a
 * user has moved the PNG into a subfolder. */
const legacyAnchorsByBoot = new Map();
for (const [boot, bootRows] of rowsByBoot) {
  const anchors = [];
  for (const row of bootRows) {
    if (row.gpsUtc > 0 && row.gpsUtc <= 235959 && row.fields[indexOf.gps_fix_type] === '3') {
      const seconds = hmsToSeconds(row.gpsUtc);
      const previous = anchors.at(-1);
      if (!previous || row.tickMs - previous.tickMs > 1000 || seconds !== previous.seconds) anchors.push({ tickMs: row.tickMs, seconds });
    }
  }
  legacyAnchorsByBoot.set(boot, anchors);
}
const legacyPngInfoByBasename = new Map();
for (const frame of frameSummary) {
  if (frame.coralRxTickMs === undefined) continue;
  const seconds = interpolateTime(legacyAnchorsByBoot.get(frame.boot) ?? [], frame.coralRxTickMs);
  if (seconds !== null) legacyPngInfoByBasename.set(secondsToCestName(seconds), {
    boot: frame.boot, directory: null, sequence: frame.sequence, cloudPercent: frame.cloudPercent,
  });
}

const pngByKey = new Map();
for (const file of pngFiles) {
  const normalizedTimestampName = timestampNameWithoutCloud(file);
  const info = parsePngName(file) ?? priorPngInfoByPath.get(file) ??
    priorPngInfoByBasename.get(path.basename(file)) ??
    priorPngInfoByBasename.get(normalizedTimestampName) ??
    legacyPngInfoByBasename.get(path.basename(file)) ??
    legacyPngInfoByBasename.get(normalizedTimestampName);
  if (!info) {
    issues.push(`Unexpected PNG name: ${path.relative(root, file)}`);
    continue;
  }
  const key = `${info.boot}:${info.sequence}`;
  if (pngByKey.has(key)) issues.push(`Duplicate PNG Coral key: ${key}`);
  pngByKey.set(key, { ...info, file });
}
for (const [key, frame] of rawByBootSequence) if (!pngByKey.has(key)) issues.push(`RAW frame has no PNG conversion: ${path.relative(root, frame.file)}`);
for (const [key, png] of pngByKey) if (!rawByBootSequence.has(key)) issues.push(`PNG has no RAW source: ${path.relative(root, png.file)}`);

const pngRenames = [];
for (const [key, png] of pngByKey) {
  const frame = frameSummary.find((item) => `${item.boot}:${item.sequence}` === key);
  if (!frame) continue;
  let targetName;
  if (frame.cestFilename) {
    targetName = frame.cestFilename.replace(/\.png$/i, `_cloud${frame.cloudPercent}.png`);
  } else {
    const rawDirectory = path.basename(path.dirname(frame.file));
    targetName = `B${String(frame.boot).padStart(8, '0')}_${rawDirectory}_${path.basename(frame.file, '.RAW')}.png`;
  }
  pngRenames.push({ old: png.file, new: path.join(path.dirname(png.file), targetName), source: path.relative(root, frame.file), estimated: png.cloudPercent !== frame.cloudPercent });
}
const nameCounts = new Map();
for (const entry of pngRenames) nameCounts.set(entry.new, (nameCounts.get(entry.new) ?? 0) + 1);
for (const [name, count] of nameCounts) if (count > 1) issues.push(`Timestamp filename collision (${count} frames): ${path.basename(name)}`);

fs.mkdirSync(outputRoot, { recursive: true });
const mergedFile = path.join(outputRoot, 'flight_data_merged.csv');
const mergedRows = [expectedHeader, ...rows.map((row) => row.fields.join(','))];
fs.writeFileSync(mergedFile, `${mergedRows.join('\n')}\n`);

const report = {
  assumedFlightDate: flightDate,
  dateProvenance: `The flight CSV records GNSS HHMMSS only. Its calendar date was discarded by the firmware; ${flightDate} is an archive-date assumption, not GNSS-derived.`,
  generatedAt: new Date().toISOString(),
  firmwareNaming: {
    bootDirectory: 'B######## is scv.boot_count; it changes on each OBC boot.',
    batchDirectory: 'D#### is floor(session/32) for LOGS and floor(frame_count/32) for CORAL.',
    csv: 'LOG_######_START_##########.CSV: session and HAL tick (seconds) at file open; files rotate every 60 seconds.',
    raw: 'F########_cloud#.RAW: Coral-supplied sequence and truncated cloud fraction percent; each valid RAW frame is 224×224 grayscale bytes (50,176 bytes).',
  },
  totals: { csvFiles: csvFiles.length, csvRows: rows.length, rawFrames: rawFiles.length, pngFiles: pngFiles.length, matchedFrames: frameSummary.filter((frame) => frame.status === 'matched').length, pngRenameCandidates: pngRenames.length },
  issues,
  csvFiles: csvSummary,
  frames: frameSummary.map((frame) => ({ boot: frame.boot, sequence: frame.sequence, cloudPercent: frame.cloudPercent, source: path.relative(root, frame.file), status: frame.status, recordTimestampMs: frame.recordTickMs, coralRxTickMs: frame.coralRxTickMs, utcSeconds: frame.utcSeconds, timeMethod: frame.timeMethod, renamedPng: frame.cestFilename })),
  pngRenames: pngRenames.map((entry) => ({ from: path.relative(root, entry.old), to: path.relative(root, entry.new), rawSource: entry.source })),
};
fs.writeFileSync(path.join(outputRoot, 'flight_data_audit.json'), `${JSON.stringify(report, null, 2)}\n`);

if (renamePng) {
  for (const entry of pngRenames) {
    if (entry.old === entry.new) continue;
    if (fs.existsSync(entry.new)) throw new Error(`Refusing to overwrite existing file: ${entry.new}`);
    fs.renameSync(entry.old, entry.new);
  }
}

console.log(JSON.stringify({ mergedFile: path.relative(root, mergedFile), reportFile: path.relative(root, path.join(outputRoot, 'flight_data_audit.json')), ...report.totals, issueCount: issues.length, renamed: renamePng ? pngRenames.length : 0 }, null, 2));
