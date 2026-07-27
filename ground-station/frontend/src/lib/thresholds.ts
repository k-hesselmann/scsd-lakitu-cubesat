export type AlertThresholds = {
  staleTelemetryWarningS: number
  staleTelemetryCriticalS: number

  batteryWarningV: number
  batteryCriticalV: number
  batteryHighWarningV: number
  batteryHighCriticalV: number

  rssiWarningDbm: number
  rssiCriticalDbm: number

  snrWarningDb: number
  snrCriticalDb: number

  packetLossWarningCount: number

  alarmRepeatS: number
}

export const defaultAlertThresholds: AlertThresholds = {
  // Flight transmits every 10 s. Allow one jitter margin before warning and
  // three missed nominal opportunities before declaring a critical outage.
  staleTelemetryWarningS: 15,
  staleTelemetryCriticalS: 30,

  batteryWarningV: 3.5,
  batteryCriticalV: 3.3,
  batteryHighWarningV: 4.2,
  batteryHighCriticalV: 4.4,

  rssiWarningDbm: -110,
  rssiCriticalDbm: -120,

  snrWarningDb: 0,
  snrCriticalDb: -10,

  packetLossWarningCount: 1,

  alarmRepeatS: 8,
}

// v2 resets legacy browser values that only represented low-voltage limits;
// merging those into the two-sided model can classify a healthy battery as
// critical until localStorage is manually cleared.
const STORAGE_KEY = "mission-alert-thresholds-v2"
export const ALERT_THRESHOLDS_UPDATED_EVENT = "thresholds-updated"

export function loadAlertThresholds(): AlertThresholds {
  try {
    const raw = localStorage.getItem(STORAGE_KEY)

    if (!raw) return defaultAlertThresholds

    const stored = JSON.parse(raw) as Partial<AlertThresholds>
    const merged = {
      ...defaultAlertThresholds,
      ...stored,
    }

    const valuesAreFinite = Object.values(merged).every(
      (value) => typeof value === "number" && Number.isFinite(value),
    )
    const rangesAreOrdered =
      merged.staleTelemetryWarningS < merged.staleTelemetryCriticalS &&
      merged.batteryCriticalV <= merged.batteryWarningV &&
      merged.batteryWarningV <= merged.batteryHighWarningV &&
      merged.batteryHighWarningV <= merged.batteryHighCriticalV &&
      merged.rssiCriticalDbm <= merged.rssiWarningDbm &&
      merged.snrCriticalDb <= merged.snrWarningDb &&
      merged.packetLossWarningCount >= 1 &&
      merged.alarmRepeatS > 0

    return valuesAreFinite && rangesAreOrdered
      ? merged
      : defaultAlertThresholds
  } catch {
    return defaultAlertThresholds
  }
}

export function saveAlertThresholds(thresholds: AlertThresholds) {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(thresholds))
  window.dispatchEvent(new CustomEvent<AlertThresholds>(
    ALERT_THRESHOLDS_UPDATED_EVENT,
    { detail: { ...thresholds } },
  ))
}
