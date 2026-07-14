export type AlertThresholds = {
  staleTelemetryWarningS: number
  staleTelemetryCriticalS: number

  batteryWarningV: number
  batteryCriticalV: number

  rssiWarningDbm: number
  rssiCriticalDbm: number

  snrWarningDb: number
  snrCriticalDb: number

  packetLossWarningCount: number

  alarmRepeatS: number
}

export const defaultAlertThresholds: AlertThresholds = {
  staleTelemetryWarningS: 6,
  staleTelemetryCriticalS: 12,

  batteryWarningV: 3.5,
  batteryCriticalV: 3.3,

  rssiWarningDbm: -110,
  rssiCriticalDbm: -120,

  snrWarningDb: 0,
  snrCriticalDb: -10,

  packetLossWarningCount: 1,

  alarmRepeatS: 8,
}

const STORAGE_KEY = "mission-alert-thresholds"

export function loadAlertThresholds(): AlertThresholds {
  try {
    const raw = localStorage.getItem(STORAGE_KEY)

    if (!raw) return defaultAlertThresholds

    return {
      ...defaultAlertThresholds,
      ...JSON.parse(raw),
    }
  } catch {
    return defaultAlertThresholds
  }
}

export function saveAlertThresholds(thresholds: AlertThresholds) {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(thresholds))
  window.dispatchEvent(new CustomEvent("thresholds-updated"))
}