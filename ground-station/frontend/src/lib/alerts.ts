import type { BackendStatus, TelemetryRow } from "@/types/telemetry"
import { packetAgeSeconds } from "@/lib/format"
import {
  EQUIPMENT_BARO,
  EQUIPMENT_CORAL,
  EQUIPMENT_EPS_ADC,
  EQUIPMENT_GPS,
  EQUIPMENT_IMU,
  EQUIPMENT_LORA,
  EQUIPMENT_SD,
  LORA_EVENT_CONFIG_FAIL,
  gnssFixIsValid,
  hasEquipmentFault,
  isLoraFailureEvent,
  rawFlagIsValid,
} from "@/lib/telemetryHealth"
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
  nowMs = Date.now(),
}: {
  latest: TelemetryRow | null
  backendStatus: BackendStatus | null
  connected: boolean
  frontendError: string | null
  thresholds?: AlertThresholds
  nowMs?: number
}): MissionAlert[] {
  const alerts: MissionAlert[] = []
  const receiver = backendStatus?.receiver

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

  if (receiver?.radio_enabled && !receiver.radio_initialized) {
    alerts.push({
      id: "radio-not-ready",
      level: "warning",
      title: "Ground radio not initialized",
      message: "The backend has not confirmed that the ground RFM95W is ready.",
    })
  }

  if (!latest) {
    alerts.push({
      id: "no-telemetry",
      level: "warning",
      title: "No v8 telemetry received",
      message: "The backend has not received a valid protocol-v8 telemetry packet.",
    })
    return alerts
  }

  const age = packetAgeSeconds(latest.pc_receive_time_unix, nowMs)
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

  if (latest.packet_type_ok === false || latest.protocol_version_ok === false) {
    alerts.push({
      id: "unsupported-packet",
      level: "critical",
      title: "Unsupported telemetry packet",
      message: "The packet is not the expected protocol-v8 telemetry layout.",
    })
  }

  if (!gnssFixIsValid(latest)) {
    alerts.push({
      id: "gnss-no-3d-fix",
      level: "warning",
      title: "No usable GNSS 3D fix",
      message: `Receiver reports fix type ${latest.gnss_fix_type ?? "unknown"} with ${latest.gnss_satellites_used ?? "unknown"} satellites; retained position fields are not current.`,
    })
  }

  for (const [id, title, value] of [
    ["imu-invalid", "IMU data invalid", latest.imu_valid_raw],
    ["baro-invalid", "Barometer data invalid", latest.baro_valid_raw],
    ["battery-invalid", "Battery measurement invalid", latest.batt_valid_raw],
    ["coral-invalid", "Coral payload invalid", latest.coral_valid_raw],
  ] as const) {
    if (!rawFlagIsValid(value)) {
      alerts.push({
        id,
        level: "warning",
        title,
        message: "The corresponding v8 validity flag is not set.",
      })
    }
  }

  if (typeof latest.battery_v === "number") {
    if (
      latest.battery_v < thresholds.batteryCriticalV ||
      latest.battery_v > thresholds.batteryHighCriticalV
    ) {
      const direction = latest.battery_v < thresholds.batteryCriticalV
        ? "low"
        : "high"
      alerts.push({
        id: `battery-critical-${direction}`,
        level: "critical",
        title: `Battery critically ${direction}`,
        message: `Battery voltage is ${latest.battery_v.toFixed(2)} V; critical limits are ${thresholds.batteryCriticalV.toFixed(2)}–${thresholds.batteryHighCriticalV.toFixed(2)} V.`,
      })
    } else if (
      latest.battery_v < thresholds.batteryWarningV ||
      latest.battery_v > thresholds.batteryHighWarningV
    ) {
      const direction = latest.battery_v < thresholds.batteryWarningV
        ? "low"
        : "high"
      alerts.push({
        id: `battery-warning-${direction}`,
        level: "warning",
        title: `Battery ${direction}`,
        message: `Battery voltage is ${latest.battery_v.toFixed(2)} V; normal range is ${thresholds.batteryWarningV.toFixed(2)}–${thresholds.batteryHighWarningV.toFixed(2)} V.`,
      })
    }
  }

  for (const [id, title, equipment] of [
    ["gps-fault", "GPS equipment fault", EQUIPMENT_GPS],
    ["imu-fault", "IMU equipment fault", EQUIPMENT_IMU],
    ["baro-fault", "Barometer equipment fault", EQUIPMENT_BARO],
    ["battery-adc-fault", "Battery ADC equipment fault", EQUIPMENT_EPS_ADC],
    ["coral-fault", "Coral equipment fault", EQUIPMENT_CORAL],
    ["sd-fault", "SD equipment fault", EQUIPMENT_SD],
  ] as const) {
    if (hasEquipmentFault(latest, equipment)) {
      alerts.push({
        id,
        level: "critical",
        title,
        message: "The v8 equipment-fault mask reports this subsystem fault.",
      })
    }
  }

  const loraFailures = latest.lora_consecutive_failures
  const loraEventFailed = isLoraFailureEvent(latest.lora_last_event)
  const loraConfigFailed = latest.lora_last_event === LORA_EVENT_CONFIG_FAIL
  const loraFault = hasEquipmentFault(latest, EQUIPMENT_LORA)

  if (loraConfigFailed) {
    alerts.push({
      id: "spacecraft-lora-config",
      level: "warning",
      title: "Spacecraft LoRa configuration verification failed",
      message: `The onboard readback found an unexpected essential RFM95W register value after initialization. This packet was received, but the pre-transmission snapshot reports ${loraFailures ?? "unknown"} consecutive failure(s) and ${latest.lora_recovery_count ?? "unknown"} recovery attempt(s).`,
    })
  } else if (loraFault) {
    alerts.push({
      id: "spacecraft-lora-fault",
      level: "warning",
      title: "Spacecraft LoRa fault flag reported",
      message: `This packet arrived successfully, but its pre-transmission SCV snapshot has EQUIPMENT_LORA set. Last event: ${latest.lora_last_event_name ?? "UNKNOWN"}; consecutive failures: ${loraFailures ?? "unknown"}.`,
    })
  } else if (loraEventFailed || (typeof loraFailures === "number" && loraFailures > 0)) {
    alerts.push({
      id: "spacecraft-lora-history",
      level: "info",
      title: "Prior spacecraft LoRa failures",
      message: `This packet arrived successfully. Its pre-transmission snapshot reports event ${latest.lora_last_event_name ?? "UNKNOWN"} and ${loraFailures ?? "unknown"} preceding consecutive failure(s).`,
    })
  }

  if (
    typeof latest.lora_ack_timeout_count === "number" &&
    latest.lora_ack_timeout_count >= thresholds.packetLossWarningCount
  ) {
    alerts.push({
      id: "telemetry-ack-timeouts",
      level: "warning",
      title: "Telemetry acknowledgements missed",
      message: `Flight received no matching ground ACK within five seconds ${latest.lora_ack_timeout_count} time(s) since boot (warning threshold: ${thresholds.packetLossWarningCount}).`,
    })
  }

  if (!rawFlagIsValid(latest.lora_rx_mode_active)) {
    alerts.push({
      id: "flight-rx-inactive",
      level: "critical",
      title: "Flight receiver is not in RX mode",
      message: "The latest onboard readback says continuous LoRa RX mode was inactive before the downlink.",
    })
  } else if (latest.lora_last_rx_status === 5 || latest.lora_last_rx_status === 6) {
    alerts.push({
      id: "flight-rx-hardware",
      level: "critical",
      title: "Flight receiver hardware/configuration error",
      message: `The onboard RX status is ${latest.lora_last_rx_status_name ?? "UNKNOWN"}.`,
    })
  } else if (latest.lora_last_rx_status === 3 || latest.lora_last_rx_status === 4) {
    alerts.push({
      id: "flight-rx-packet-error",
      level: "warning",
      title: "Flight receiver rejected an uplink packet",
      message: `The latest onboard RX status is ${latest.lora_last_rx_status_name ?? "UNKNOWN"}.`,
    })
  }

  if (latest.is_duplicate_packet === true) {
    alerts.push({
      id: "duplicate-telemetry",
      level: "warning",
      title: "Duplicate telemetry — ACK not confirmed by flight",
      message: `Flight retransmitted sequence ${latest.sequence_number ?? "unknown"} because it did not receive or process the previous ground ACK before timeout. The ground station has automatically sent the ACK again.`,
    })
  }

  if (latest.uplink_last_ack_status_name === "UNEXPECTED_ACK") {
    alerts.push({
      id: "unexpected-telemetry-ack",
      level: "warning",
      title: "Flight received an unexpected telemetry ACK",
      message: "The ACK did not match the outstanding flight telemetry sequence.",
    })
  }

  if (receiver?.last_telemetry_ack_ok === false) {
    alerts.push({
      id: "ground-ack-tx-failed",
      level: "warning",
      title: "Ground telemetry ACK transmission failed",
      message: `The ground radio did not report TxDone for ACK sequence ${receiver.last_telemetry_ack_sequence ?? "unknown"}. Flight may retransmit that telemetry packet.`,
    })
  }

  if (receiver?.last_command_outcome === "retrying") {
    alerts.push({
      id: "flight-command-retrying",
      level: "warning",
      title: "Flight command acknowledgement pending",
      message: `Command ${receiver.last_command_id ?? "unknown"} was not confirmed after attempt ${receiver.last_command_attempt ?? "unknown"}; ground is retrying the same command ID.`,
    })
  } else if (receiver?.last_command_outcome === "unacknowledged") {
    alerts.push({
      id: "flight-command-unacknowledged",
      level: "warning",
      title: "Flight command was not acknowledged",
      message: `Command ${receiver.last_command_id ?? "unknown"} was not confirmed after ${receiver.last_command_attempt ?? "unknown"} attempt(s). This indicates an unconfirmed uplink path or a lost telemetry response, not necessarily a proven RX hardware failure.`,
    })
  }

  if (
    typeof latest.lost_packets_since_previous === "number" &&
    latest.lost_packets_since_previous >= thresholds.packetLossWarningCount
  ) {
    alerts.push({
      id: "packet-loss",
      level: "warning",
      title: "Packet loss detected",
      message: `${latest.lost_packets_since_previous} packet(s) were missed before the latest packet (warning threshold: ${thresholds.packetLossWarningCount}).`,
    })
  }

  if (typeof latest.lora_downlink_rssi_dbm === "number") {
    if (latest.lora_downlink_rssi_dbm < thresholds.rssiCriticalDbm) {
      alerts.push({ id: "rssi-critical", level: "critical", title: "Very weak downlink RSSI", message: `Ground radio RSSI is ${latest.lora_downlink_rssi_dbm} dBm.` })
    } else if (latest.lora_downlink_rssi_dbm < thresholds.rssiWarningDbm) {
      alerts.push({ id: "rssi-warning", level: "warning", title: "Weak downlink RSSI", message: `Ground radio RSSI is ${latest.lora_downlink_rssi_dbm} dBm.` })
    }
  }

  if (typeof latest.lora_downlink_snr_db === "number") {
    if (latest.lora_downlink_snr_db < thresholds.snrCriticalDb) {
      alerts.push({ id: "snr-critical", level: "critical", title: "Very low downlink SNR", message: `Ground radio SNR is ${latest.lora_downlink_snr_db.toFixed(1)} dB.` })
    } else if (latest.lora_downlink_snr_db < thresholds.snrWarningDb) {
      alerts.push({ id: "snr-warning", level: "warning", title: "Low downlink SNR", message: `Ground radio SNR is ${latest.lora_downlink_snr_db.toFixed(1)} dB.` })
    }
  }

  return alerts
}
