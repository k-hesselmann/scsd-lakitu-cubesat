import type { BackendStatus, TelemetryRow } from "@/types/telemetry"
import { packetAgeSeconds } from "@/lib/format"
import {
  defaultAlertThresholds,
  type AlertThresholds,
} from "@/lib/thresholds"

export type AlertLevel = "info" | "warning" | "critical"

export type MissionAlert = {
  id: string
  level: AlertLevel
  title: string
  message: string
}

export function buildMissionAlerts({
  latest,
  backendStatus,
  connected,
  frontendError,
  thresholds = defaultAlertThresholds,
}: {
  latest: TelemetryRow | null
  backendStatus: BackendStatus | null
  connected: boolean
  frontendError: string | null
  thresholds?: AlertThresholds
}): MissionAlert[] {
  const alerts: MissionAlert[] = []
  const receiver = backendStatus?.receiver
  const stats = backendStatus?.stats

  if (!connected) {
    alerts.push({
      id: "ws-disconnected",
      level: "critical",
      title: "Frontend disconnected",
      message: "The browser is not connected to the backend WebSocket.",
    })
  }

  if (frontendError) {
    alerts.push({
      id: "frontend-error",
      level: "critical",
      title: "Frontend/WebSocket error",
      message: frontendError,
    })
  }

  if (receiver?.last_error) {
    alerts.push({
      id: "receiver-error",
      level: "critical",
      title: "Backend receiver error",
      message: receiver.last_error,
    })
  }

  if (!receiver?.radio_initialized) {
    alerts.push({
      id: "radio-not-ready",
      level: "warning",
      title: "Radio not initialized",
      message: "The backend has not confirmed that the RFM95W radio is initialized.",
    })
  }

  if (!latest) {
    alerts.push({
      id: "no-telemetry",
      level: "warning",
      title: "No telemetry received",
      message: "The backend is running, but no valid telemetry packet has been received yet.",
    })

    return alerts
  }

  const age = packetAgeSeconds(latest.pc_receive_time_unix)

  if (age !== null) {
    if (age > thresholds.staleTelemetryCriticalS) {
      alerts.push({
        id: "stale-telemetry-critical",
        level: "critical",
        title: "No recent telemetry",
        message: `Last packet was received ${age.toFixed(1)} seconds ago.`,
      })
    } else if (age > thresholds.staleTelemetryWarningS) {
      alerts.push({
        id: "stale-telemetry-warning",
        level: "warning",
        title: "Telemetry becoming stale",
        message: `Last packet was received ${age.toFixed(1)} seconds ago.`,
      })
    }
  }

  if (latest.crc_ok === false) {
    alerts.push({
      id: "telemetry-crc",
      level: "critical",
      title: "Telemetry CRC mismatch",
      message: `Received ${latest.received_crc16}, calculated ${latest.calculated_crc16}.`,
    })
  }

  if (latest.packet_type_ok === false) {
    alerts.push({
      id: "packet-type",
      level: "warning",
      title: "Unexpected packet type",
      message: `Packet type is ${latest.packet_type}.`,
    })
  }

  if (latest.protocol_version_ok === false) {
    alerts.push({
      id: "protocol-version",
      level: "warning",
      title: "Unexpected protocol version",
      message: `Protocol version is ${latest.protocol_version}.`,
    })
  }

  const battery = latest.battery_v

  if (typeof battery === "number") {
    if (battery < thresholds.batteryCriticalV) {
      alerts.push({
        id: "battery-critical",
        level: "critical",
        title: "Battery critical",
        message: `Battery voltage is ${battery.toFixed(2)} V.`,
      })
    } else if (battery < thresholds.batteryWarningV) {
      alerts.push({
        id: "battery-low",
        level: "warning",
        title: "Battery low",
        message: `Battery voltage is ${battery.toFixed(2)} V.`,
      })
    }
  }

  if (!latest.GNSS_FIX_VALID) {
    alerts.push({
      id: "gnss-fix",
      level: "warning",
      title: "GNSS fix invalid",
      message: "GNSS fix valid flag is OFF.",
    })
  }

  if (latest.SD_LOGGING_OK === false) {
    alerts.push({
      id: "sd-logging",
      level: "warning",
      title: "SD logging not OK",
      message: "The SD_LOGGING_OK flag is OFF.",
    })
  }

  const lost = latest.lost_packets_since_previous

  if (
    typeof lost === "number" &&
    lost >= thresholds.packetLossWarningCount
  ) {
    alerts.push({
      id: "packet-loss",
      level: "warning",
      title: "Packet loss detected",
      message: `${lost} telemetry packet(s) were missed since the previous packet.`,
    })
  }

  const rssi = latest.lora_downlink_rssi_dbm

  if (typeof rssi === "number") {
    if (rssi < thresholds.rssiCriticalDbm) {
      alerts.push({
        id: "rssi-critical",
        level: "critical",
        title: "Very weak downlink RSSI",
        message: `Ground radio RSSI is ${rssi} dBm.`,
      })
    } else if (rssi < thresholds.rssiWarningDbm) {
      alerts.push({
        id: "rssi-warning",
        level: "warning",
        title: "Weak downlink RSSI",
        message: `Ground radio RSSI is ${rssi} dBm.`,
      })
    }
  }

  const snr = latest.lora_downlink_snr_db

  if (typeof snr === "number") {
    if (snr < thresholds.snrCriticalDb) {
      alerts.push({
        id: "snr-critical",
        level: "critical",
        title: "Very low downlink SNR",
        message: `Ground radio SNR is ${snr.toFixed(1)} dB.`,
      })
    } else if (snr < thresholds.snrWarningDb) {
      alerts.push({
        id: "snr-warning",
        level: "warning",
        title: "Low downlink SNR",
        message: `Ground radio SNR is ${snr.toFixed(1)} dB.`,
      })
    }
  }

  const errorFlags = [
    ["GPS_ERROR", latest.GPS_ERROR],
    ["IMU_ERROR", latest.IMU_ERROR],
    ["BARO_ERROR", latest.BARO_ERROR],
    ["SD_ERROR", latest.SD_ERROR],
  ] as const

  for (const [name, active] of errorFlags) {
    if (active) {
      alerts.push({
        id: name,
        level: "critical",
        title: `${name} active`,
        message: `The ${name} status flag is active.`,
      })
    }
  }

  if (receiver && receiver.decode_errors > 0) {
    alerts.push({
      id: "decode-errors",
      level: "warning",
      title: "Telemetry decode errors",
      message: `${receiver.decode_errors} telemetry decode error(s) reported by backend.`,
    })
  }

  if (receiver && receiver.lora_crc_errors > 0) {
    alerts.push({
      id: "lora-crc-errors",
      level: "warning",
      title: "LoRa CRC errors",
      message: `${receiver.lora_crc_errors} LoRa packet CRC error(s) reported by backend.`,
    })
  }

  if (stats && stats.total_lost_packets > 0) {
    alerts.push({
      id: "total-lost",
      level: "warning",
      title: "Total packet loss",
      message: `${stats.total_lost_packets} total packet(s) estimated lost.`,
    })
  }

  return alerts
}