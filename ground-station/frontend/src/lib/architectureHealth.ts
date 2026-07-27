import { packetAgeSeconds } from "@/lib/format"
import {
  EQUIPMENT_BARO,
  EQUIPMENT_CDH,
  EQUIPMENT_CORAL,
  EQUIPMENT_EPS_ADC,
  EQUIPMENT_GPS,
  EQUIPMENT_IMU,
  EQUIPMENT_LORA,
  EQUIPMENT_SD,
  gnssFixIsValid,
  hasEquipmentFault,
  isEquipmentEnabled,
  isLoraFailureEvent,
  parseEquipmentMask,
} from "@/lib/telemetryHealth"
import type { AlertThresholds } from "@/lib/thresholds"
import type { BackendStatus, TelemetryRow } from "@/types/telemetry"

export type HealthState =
  | "nominal"
  | "warning"
  | "critical"
  | "disabled"
  | "unknown"

export type HealthItem = {
  label: string
  value: string
}

export type ArchitectureNodeHealth = {
  state: HealthState
  badge?: string
  items: HealthItem[]
}

export type ArchitectureInterfaceHealth = {
  label: string
  state: HealthState
  evidence: string
}

export type ArchitectureHealth = {
  nodes: {
    obc: ArchitectureNodeHealth
    gnss: ArchitectureNodeHealth
    imu: ArchitectureNodeHealth
    barometer: ArchitectureNodeHealth
    battery: ArchitectureNodeHealth
    coral: ArchitectureNodeHealth
    sd: ArchitectureNodeHealth
    lora: ArchitectureNodeHealth
    downlink: ArchitectureNodeHealth
    uplink: ArchitectureNodeHealth
    groundRadio: ArchitectureNodeHealth
    groundStation: ArchitectureNodeHealth
  }
  interfaces: {
    i2c: ArchitectureInterfaceHealth
    uart: ArchitectureInterfaceHealth
    adc: ArchitectureInterfaceHealth
    sdSpi: ArchitectureInterfaceHealth
    loraSpi: ArchitectureInterfaceHealth
    usbSpi: ArchitectureInterfaceHealth
  }
}

function flagValue(value: unknown): boolean | undefined {
  if (value === true || value === 1) return true
  if (value === false || value === 0) return false
  return undefined
}

function yesNo(value: boolean | undefined) {
  return value === undefined ? "—" : value ? "Yes" : "No"
}

function activeInactive(value: boolean | undefined) {
  return value === undefined ? "—" : value ? "Active" : "Inactive"
}

function presentAbsent(value: boolean | undefined) {
  return value === undefined ? "—" : value ? "Present" : "None"
}

function equipmentState(
  enabled: boolean | undefined,
  valid: boolean | undefined,
  fault: boolean | undefined,
): HealthState {
  if (fault) return "critical"
  if (enabled === false) return "disabled"
  if (valid === false) return "warning"
  if (enabled === true && valid === true && fault === false) return "nominal"
  return "unknown"
}

function faultOnlyEquipmentState(
  enabled: boolean | undefined,
  fault: boolean | undefined,
): HealthState {
  if (fault) return "critical"
  if (enabled === false) return "disabled"
  if (enabled === true && fault === false) return "nominal"
  return "unknown"
}

function equipmentBadge(
  enabled: boolean | undefined,
  valid: boolean | undefined,
  fault: boolean | undefined,
) {
  if (fault) return "Fault"
  if (enabled === false) return "Disabled"
  if (enabled === undefined || valid === undefined) return "Unknown"
  return valid ? "Enabled · Valid" : "Enabled · Invalid"
}

function faultOnlyEquipmentBadge(
  enabled: boolean | undefined,
  fault: boolean | undefined,
) {
  if (fault) return "Fault"
  if (enabled === false) return "Disabled"
  if (enabled === true && fault === false) return "Enabled · Healthy"
  return "Unknown"
}

function countSetBits(value: number) {
  let count = 0
  let remaining = value
  while (remaining !== 0) {
    count += remaining & 1
    remaining >>>= 1
  }
  return count
}

function gnssFixName(value: number | null | undefined) {
  if (value === null || value === undefined) return "—"
  return {
    0: "No fix",
    1: "Dead reckoning",
    2: "2D fix",
    3: "3D fix",
    4: "GNSS + DR",
    5: "Time only",
  }[value] ?? `Unknown (${value})`
}

function signalCondition(
  latest: TelemetryRow | null,
  thresholds: AlertThresholds,
): { label: string; state: HealthState } {
  const rssi = latest?.lora_downlink_rssi_dbm
  const snr = latest?.lora_downlink_snr_db
  if (typeof rssi !== "number" && typeof snr !== "number") {
    return { label: "Unknown", state: "unknown" }
  }
  if (
    (typeof rssi === "number" && rssi < thresholds.rssiCriticalDbm) ||
    (typeof snr === "number" && snr < thresholds.snrCriticalDb)
  ) {
    return { label: "Critical", state: "critical" }
  }
  if (
    (typeof rssi === "number" && rssi < thresholds.rssiWarningDbm) ||
    (typeof snr === "number" && snr < thresholds.snrWarningDb)
  ) {
    return { label: "Weak", state: "warning" }
  }
  return { label: "Nominal", state: "nominal" }
}

function worseState(...states: HealthState[]): HealthState {
  const priority: Record<HealthState, number> = {
    unknown: 0,
    nominal: 1,
    disabled: 2,
    warning: 3,
    critical: 4,
  }
  return states.reduce((worst, state) =>
    priority[state] > priority[worst] ? state : worst,
  "unknown")
}

export function deriveArchitectureHealth({
  latest,
  backendStatus,
  connected,
  frontendError,
  thresholds,
  nowMs,
}: {
  latest: TelemetryRow | null
  backendStatus?: BackendStatus | null
  connected: boolean
  frontendError?: string | null
  thresholds: AlertThresholds
  nowMs: number
}): ArchitectureHealth {
  const receiver = backendStatus?.receiver

  const gnssEnabled = isEquipmentEnabled(latest, EQUIPMENT_GPS)
  const imuEnabled = isEquipmentEnabled(latest, EQUIPMENT_IMU)
  const barometerEnabled = isEquipmentEnabled(latest, EQUIPMENT_BARO)
  const batteryEnabled = isEquipmentEnabled(latest, EQUIPMENT_EPS_ADC)
  const coralEnabled = isEquipmentEnabled(latest, EQUIPMENT_CORAL)
  const sdEnabled = isEquipmentEnabled(latest, EQUIPMENT_SD)
  const loraEnabled = isEquipmentEnabled(latest, EQUIPMENT_LORA)

  const gnssFault = hasEquipmentFault(latest, EQUIPMENT_GPS)
  const imuFault = hasEquipmentFault(latest, EQUIPMENT_IMU)
  const barometerFault = hasEquipmentFault(latest, EQUIPMENT_BARO)
  const batteryFault = hasEquipmentFault(latest, EQUIPMENT_EPS_ADC)
  const coralFault = hasEquipmentFault(latest, EQUIPMENT_CORAL)
  const sdFault = hasEquipmentFault(latest, EQUIPMENT_SD)
  const loraFault = hasEquipmentFault(latest, EQUIPMENT_LORA)
  const cdhFault = hasEquipmentFault(latest, EQUIPMENT_CDH)

  const gnssValid = latest ? gnssFixIsValid(latest) : undefined
  const imuValid = flagValue(latest?.imu_valid_raw)
  const barometerValid = flagValue(latest?.baro_valid_raw)
  const batteryValid = flagValue(latest?.batt_valid_raw)
  const coralValid = flagValue(latest?.coral_valid_raw)
  const rxModeActive = flagValue(latest?.lora_rx_mode_active)

  const faultMask = parseEquipmentMask(latest?.scv_equipment_faults)
  const faultCount = faultMask === null ? undefined : countSetBits(faultMask)
  const watchdogResets = latest?.scv_watchdog_reset_count
  const i2cState = latest?.i2c_bus_state_raw
  const packetInvalid = latest !== null && (
    latest.crc_ok === false ||
    latest.packet_type_ok === false ||
    latest.protocol_version_ok === false ||
    latest.length_ok === false
  )

  let obcState: HealthState = latest ? "nominal" : "unknown"
  if (packetInvalid || cdhFault) obcState = "critical"
  else if (
    i2cState === 1 ||
    i2cState === 2 ||
    (typeof watchdogResets === "number" && watchdogResets > 0) ||
    (typeof faultCount === "number" && faultCount > 0)
  ) obcState = "warning"

  let fdirStatus = "—"
  if (latest) {
    if (i2cState === 1 || i2cState === 2) fdirStatus = "Recovery active"
    else if (typeof faultCount === "number" && faultCount > 0) fdirStatus = `${faultCount} fault${faultCount === 1 ? "" : "s"} tracked`
    else fdirStatus = "Monitoring"
  }

  let loraState = faultOnlyEquipmentState(loraEnabled, loraFault)
  if (loraState === "nominal" && (
    isLoraFailureEvent(latest?.lora_last_event) ||
    rxModeActive === false ||
    (typeof latest?.lora_consecutive_failures === "number" && latest.lora_consecutive_failures > 0)
  )) loraState = "warning"

  const age = packetAgeSeconds(latest?.pc_receive_time_unix, nowMs)
  let receptionLabel = "No telemetry"
  let receptionState: HealthState = "unknown"
  if (age !== null) {
    if (age > thresholds.staleTelemetryCriticalS) {
      receptionLabel = "Stale"
      receptionState = "critical"
    } else if (age > thresholds.staleTelemetryWarningS) {
      receptionLabel = "Aging"
      receptionState = "warning"
    } else {
      receptionLabel = "Current"
      receptionState = "nominal"
    }
  }

  const integrityKnown = latest !== null && (
    latest.crc_ok !== undefined ||
    latest.packet_type_ok !== undefined ||
    latest.protocol_version_ok !== undefined
  )
  const integrityValid = integrityKnown && !packetInvalid
  const integrityState: HealthState = !integrityKnown
    ? "unknown"
    : integrityValid ? "nominal" : "critical"
  const signal = signalCondition(latest, thresholds)
  const lossDetected = typeof latest?.lost_packets_since_previous === "number"
    ? latest.lost_packets_since_previous >= thresholds.packetLossWarningCount
    : undefined
  const lossState: HealthState = lossDetected === undefined
    ? "unknown"
    : lossDetected ? "warning" : "nominal"
  const downlinkState = worseState(
    receptionState,
    integrityState,
    signal.state,
    lossState,
  )

  const rxHardwareFailure = latest?.lora_last_rx_status === 5 || latest?.lora_last_rx_status === 6
  const rxPacketWarning = latest?.lora_last_rx_status === 3 || latest?.lora_last_rx_status === 4
  const commandOutcome = receiver?.last_command_outcome
  const commandWarning = commandOutcome === "retrying" ||
    commandOutcome === "unacknowledged" ||
    commandOutcome === "rejected"
  const ackWarning = receiver?.last_telemetry_ack_ok === false ||
    latest?.uplink_last_ack_status_name === "UNEXPECTED_ACK" ||
    (typeof latest?.lora_ack_timeout_count === "number" && latest.lora_ack_timeout_count > 0)

  let uplinkState: HealthState = latest ? "nominal" : "unknown"
  if (rxModeActive === false || rxHardwareFailure) uplinkState = "critical"
  else if (rxPacketWarning || commandWarning || ackWarning || loraFault) uplinkState = "warning"
  else if (rxModeActive === undefined) uplinkState = "unknown"

  let groundRadioState: HealthState = backendStatus ? "nominal" : "unknown"
  if (receiver?.last_error) groundRadioState = "critical"
  else if (receiver?.radio_enabled === false) groundRadioState = "disabled"
  else if (receiver && (!receiver.running || !receiver.radio_initialized)) groundRadioState = "warning"

  let groundStationState: HealthState = connected ? "nominal" : "critical"
  if (frontendError || receiver?.last_error) groundStationState = "critical"
  else if (!backendStatus && connected) groundStationState = "unknown"
  else if (receiver && !receiver.running) groundStationState = "warning"

  let i2cInterface: ArchitectureInterfaceHealth
  if (i2cState === 0) {
    i2cInterface = { label: "I²C · IDLE", state: "nominal", evidence: "Direct telemetry: the shared I²C recovery state is idle." }
  } else if (i2cState === 1) {
    i2cInterface = { label: "I²C · RESTART", state: "warning", evidence: "Direct telemetry: an I²C bus restart is in progress." }
  } else if (i2cState === 2) {
    i2cInterface = { label: "I²C · HOLD", state: "warning", evidence: "Direct telemetry: the I²C restart hold interval is active." }
  } else {
    i2cInterface = { label: "I²C · UNKNOWN", state: "unknown", evidence: "Direct I²C state is not available in the latest telemetry." }
  }

  const coralStatus = latest?.coral_status
  let uartInterface: ArchitectureInterfaceHealth
  if (!latest || (coralStatus === undefined && coralValid === undefined)) {
    uartInterface = { label: "UART · UNKNOWN", state: "unknown", evidence: "Inferred from Coral validity and status; no evidence is available." }
  } else if (typeof coralStatus === "number" && (coralStatus & 0x80) !== 0) {
    uartInterface = { label: "UART · NOT READY", state: "critical", evidence: "Inferred: Coral reports that its UART interface was not initialized." }
  } else if (typeof coralStatus === "number" && (coralStatus & 0x03) !== 0) {
    uartInterface = { label: "UART · DEGRADED", state: "warning", evidence: "Inferred: Coral reports a UART timeout or frame CRC error." }
  } else if (coralValid === false) {
    uartInterface = { label: "UART · DEGRADED", state: "warning", evidence: "Inferred: the latest Coral result is not valid." }
  } else {
    uartInterface = { label: "UART · OK", state: "nominal", evidence: "Inferred from a valid Coral result with no UART-related status error." }
  }

  let adcInterface: ArchitectureInterfaceHealth
  if (batteryFault) {
    adcInterface = { label: "ADC · FAULT", state: "critical", evidence: "Inferred from the EPS ADC equipment-fault bit." }
  } else if (batteryEnabled === false) {
    adcInterface = { label: "ADC · DISABLED", state: "disabled", evidence: "Inferred from the EPS ADC equipment-enabled bit." }
  } else if (batteryValid === true) {
    adcInterface = { label: "ADC · VALID", state: "nominal", evidence: "Inferred from the current battery-valid flag." }
  } else if (batteryValid === false) {
    adcInterface = { label: "ADC · INVALID", state: "warning", evidence: "Inferred from the cleared battery-valid flag." }
  } else {
    adcInterface = { label: "ADC · UNKNOWN", state: "unknown", evidence: "No battery ADC health evidence is available." }
  }

  const sdSpiInterface: ArchitectureInterfaceHealth = sdFault
    ? { label: "SPI · FAULT", state: "critical", evidence: "Inferred from the SD equipment-fault bit; no direct SD SPI state is downlinked." }
    : sdEnabled === false
      ? { label: "SPI · DISABLED", state: "disabled", evidence: "Inferred from the SD equipment-enabled bit." }
      : sdEnabled === true
        ? { label: "SPI · OK", state: "nominal", evidence: "Inferred from SD being enabled with no FDIR fault; no direct SD SPI state is downlinked." }
        : { label: "SPI · UNKNOWN", state: "unknown", evidence: "No SD interface health evidence is available." }

  const loraSpiFailed = latest?.lora_last_event === 4 || latest?.lora_last_event === 11
  const loraSpiInterface: ArchitectureInterfaceHealth = loraFault || loraSpiFailed
    ? { label: "SPI · ERROR", state: loraFault ? "critical" : "warning", evidence: "Inferred from the LoRa fault bit or an onboard TX/RX SPI failure event." }
    : loraEnabled === false
      ? { label: "SPI · DISABLED", state: "disabled", evidence: "Inferred from the LoRa equipment-enabled bit." }
      : loraEnabled === true
        ? { label: "SPI · OK", state: "nominal", evidence: "Inferred from LoRa being enabled with no SPI failure evidence." }
        : { label: "SPI · UNKNOWN", state: "unknown", evidence: "No LoRa SPI health evidence is available." }

  const usbSpiInterface: ArchitectureInterfaceHealth = !backendStatus || !receiver
    ? { label: "USB–SPI · UNKNOWN", state: "unknown", evidence: "Ground receiver runtime status is not available." }
    : receiver.last_error
      ? { label: "USB–SPI · ERROR", state: "critical", evidence: "Inferred from the ground receiver error state." }
      : !receiver.radio_enabled
        ? { label: "USB–SPI · DISABLED", state: "disabled", evidence: "The ground radio is disabled by backend configuration." }
        : receiver.radio_initialized
          ? { label: "USB–SPI · READY", state: "nominal", evidence: "Inferred from successful ground-radio initialization through the CH347 adapter." }
          : { label: "USB–SPI · NOT READY", state: "warning", evidence: "The backend has not confirmed ground-radio initialization." }

  return {
    nodes: {
      obc: {
        state: obcState,
        items: [
          { label: "FDIR", value: fdirStatus },
          { label: "Last reset", value: latest?.reset_reason_name ?? "—" },
          { label: "Watchdog streak", value: watchdogResets === undefined ? "—" : String(watchdogResets) },
        ],
      },
      gnss: {
        state: equipmentState(gnssEnabled, gnssValid, gnssFault),
        badge: equipmentBadge(gnssEnabled, gnssValid, gnssFault),
        items: [
          { label: "Satellites", value: latest?.gnss_satellites_used?.toString() ?? "—" },
          { label: "Fix type", value: gnssFixName(latest?.gnss_fix_type) },
        ],
      },
      imu: {
        state: equipmentState(imuEnabled, imuValid, imuFault),
        badge: equipmentBadge(imuEnabled, imuValid, imuFault),
        items: [],
      },
      barometer: {
        state: equipmentState(barometerEnabled, barometerValid, barometerFault),
        badge: equipmentBadge(barometerEnabled, barometerValid, barometerFault),
        items: [],
      },
      battery: {
        state: equipmentState(batteryEnabled, batteryValid, batteryFault),
        badge: equipmentBadge(batteryEnabled, batteryValid, batteryFault),
        items: [],
      },
      coral: {
        state: equipmentState(coralEnabled, coralValid, coralFault),
        badge: equipmentBadge(coralEnabled, coralValid, coralFault),
        items: [],
      },
      sd: {
        state: faultOnlyEquipmentState(sdEnabled, sdFault),
        badge: faultOnlyEquipmentBadge(sdEnabled, sdFault),
        items: [],
      },
      lora: {
        state: loraState,
        items: [
          { label: "Enabled", value: yesNo(loraEnabled) },
          { label: "Fault", value: presentAbsent(loraFault) },
          { label: "RX mode", value: activeInactive(rxModeActive) },
          { label: "Last event", value: latest?.lora_last_event_name ?? "—" },
        ],
      },
      downlink: {
        state: downlinkState,
        items: [
          { label: "Reception", value: receptionLabel },
          { label: "Integrity", value: !integrityKnown ? "—" : integrityValid ? "Valid" : "Invalid" },
          { label: "Signal", value: signal.label },
          { label: "Packet loss", value: lossDetected === undefined ? "—" : lossDetected ? "Detected" : "None" },
        ],
      },
      uplink: {
        state: uplinkState,
        items: [
          { label: "Flight RX", value: activeInactive(rxModeActive) },
          { label: "Command", value: commandOutcome?.replaceAll("_", " ") ?? "None" },
          { label: "Flight status", value: latest?.uplink_last_status_name ?? "—" },
          { label: "Telemetry ACK", value: latest?.uplink_last_ack_status_name ?? "—" },
        ],
      },
      groundRadio: {
        state: groundRadioState,
        items: [
          { label: "Enabled", value: yesNo(receiver?.radio_enabled) },
          { label: "Initialized", value: yesNo(receiver?.radio_initialized) },
        ],
      },
      groundStation: {
        state: groundStationState,
        items: [
          { label: "Backend", value: connected ? "Online" : "Offline" },
          { label: "Receiver", value: receiver ? (receiver.running ? "Running" : "Stopped") : "—" },
          { label: "Logging", value: backendStatus?.config ? (backendStatus.config.csv_enabled ? "Enabled" : "Disabled") : "—" },
          { label: "Errors", value: frontendError || receiver?.last_error ? "Present" : "None" },
        ],
      },
    },
    interfaces: {
      i2c: i2cInterface,
      uart: uartInterface,
      adc: adcInterface,
      sdSpi: sdSpiInterface,
      loraSpi: loraSpiInterface,
      usbSpi: usbSpiInterface,
    },
  }
}
