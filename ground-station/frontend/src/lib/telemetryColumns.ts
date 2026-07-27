import type { TelemetryRow } from "@/types/telemetry"
import { fmt, fmtBoolean } from "@/lib/format"
import {
  formatCoralStatusMask,
  formatEquipmentMask,
} from "@/lib/telemetryHealth"

export type TelemetryOutput = {
  value: string
  meaning: string
}

type TelemetryColumnBase = {
  key: keyof TelemetryRow | string
  label: string
  decimals?: number
  suffix?: string
}

export type TelemetryColumn = TelemetryColumnBase & {
  description: string
  outputFormat: string
  outputs?: readonly TelemetryOutput[]
  boolean?: boolean
  mask?: "equipment" | "coral-status"
  valueLabels?: Readonly<Record<string, string>>
}

// V8 wire fields plus ground-side reception and packet-validation metadata.
const baseTelemetryColumns = [
  { key: "pc_receive_time_iso", label: "Ground receive time" },
  { key: "sequence_number", label: "Sequence" },
  { key: "is_duplicate_packet", label: "Duplicate telemetry" },
  { key: "consecutive_duplicate_packets", label: "Consecutive duplicates" },
  { key: "total_duplicate_packets", label: "Total duplicates" },
  { key: "lora_downlink_rssi_dbm", label: "Ground RX RSSI", suffix: " dBm" },
  { key: "lora_downlink_snr_db", label: "Ground RX SNR", suffix: " dB", decimals: 1 },
  { key: "packet_type", label: "Packet type" },
  { key: "protocol_version", label: "Protocol version" },
  { key: "crc_ok", label: "CRC valid" },
  { key: "received_crc16", label: "Packet CRC" },
  { key: "sample_age_ms", label: "Sensor sample age", suffix: " ms" },
  { key: "obc_uptime_ms", label: "TX uptime", suffix: " ms" },
  { key: "scv_mission_elapsed_ms", label: "Mission elapsed", suffix: " ms" },
  { key: "i2c_bus_state_raw", label: "I2C bus state" },
  { key: "gnss_fix_valid_raw", label: "GNSS 3D fix valid" },
  { key: "gnss_utc_sod", label: "GNSS UTC second of day", suffix: " s" },
  { key: "gnss_fix_type", label: "GNSS fix type" },
  { key: "gnss_satellites_used", label: "GNSS satellites" },
  { key: "latitude_deg", label: "GNSS latitude", decimals: 7 },
  { key: "longitude_deg", label: "GNSS longitude", decimals: 7 },
  { key: "gnss_altitude_m", label: "GNSS altitude MSL", suffix: " m", decimals: 1 },
  { key: "vertical_speed_ms", label: "GNSS vertical speed", suffix: " m/s", decimals: 2 },
  { key: "ground_speed_ms", label: "GNSS ground speed", suffix: " m/s", decimals: 2 },
  { key: "course_deg", label: "GNSS course", suffix: " deg", decimals: 2 },
  { key: "imu_valid_raw", label: "IMU valid" },
  { key: "imu_accel_x_g", label: "IMU accel X", suffix: " g", decimals: 3 },
  { key: "imu_accel_y_g", label: "IMU accel Y", suffix: " g", decimals: 3 },
  { key: "imu_accel_z_g", label: "IMU accel Z", suffix: " g", decimals: 3 },
  { key: "imu_gyro_x_dps", label: "IMU gyro X", suffix: " deg/s", decimals: 1 },
  { key: "imu_gyro_y_dps", label: "IMU gyro Y", suffix: " deg/s", decimals: 1 },
  { key: "imu_gyro_z_dps", label: "IMU gyro Z", suffix: " deg/s", decimals: 1 },
  { key: "baro_valid_raw", label: "Barometer valid" },
  { key: "baro_pressure_pa", label: "Barometer pressure", suffix: " Pa" },
  { key: "baro_altitude_m", label: "Barometer altitude (relative)", suffix: " m", decimals: 1 },
  { key: "baro_temperature_c", label: "Barometer temperature", suffix: " °C", decimals: 2 },
  { key: "batt_valid_raw", label: "Battery valid" },
  { key: "battery_mv", label: "Battery voltage", suffix: " mV" },
  { key: "coral_valid_raw", label: "Coral valid" },
  { key: "coral_sequence_low", label: "Coral sequence (low 16 bits)" },
  { key: "coral_fraction_percent", label: "Coral cloud fraction", suffix: " %", decimals: 2 },
  { key: "coral_status", label: "Coral status" },
  { key: "coral_result_age_s", label: "Coral result age", suffix: " s" },
  { key: "boot_count", label: "Boot count (saturated)" },
  { key: "flight_state_name", label: "Flight phase" },
  { key: "reset_reason_name", label: "Reset reason" },
  { key: "scv_equipment_enabled", label: "Equipment enabled (decoded)" },
  { key: "scv_equipment_faults", label: "Equipment faults (decoded)" },
  { key: "scv_sd_fault_count", label: "SD faults" },
  { key: "scv_watchdog_reset_count", label: "Watchdog resets" },
  { key: "uplink_last_command_id", label: "Last flight command ID" },
  { key: "uplink_last_status_name", label: "Last flight command status" },
  { key: "uplink_last_ack_status_name", label: "Last telemetry ACK status" },
  { key: "lora_last_event_name", label: "Spacecraft LoRa last event" },
  { key: "lora_consecutive_failures", label: "Spacecraft LoRa consecutive failures" },
  { key: "lora_tx_fault_count", label: "Spacecraft LoRa lifetime TX failures" },
  { key: "lora_recovery_count", label: "Spacecraft LoRa recoveries" },
  { key: "lora_rx_mode_active", label: "Spacecraft RX mode active" },
  { key: "lora_last_rx_status_name", label: "Spacecraft RX status" },
  { key: "lora_ack_timeout_count", label: "Telemetry ACK timeouts" },
] as const satisfies readonly TelemetryColumnBase[]

type TelemetryColumnKey = (typeof baseTelemetryColumns)[number]["key"]
type TelemetryColumnHelp = Omit<
  TelemetryColumn,
  "key" | "label" | "decimals" | "suffix"
>

const BOOLEAN_OUTPUTS = [
  { value: "Yes", meaning: "The flag is set (wire value 1 or true)." },
  { value: "No", meaning: "The flag is clear (wire value 0 or false)." },
  { value: "Unknown (value)", meaning: "An unexpected non-boolean value was received." },
] as const

const booleanHelp = {
  outputFormat: "Yes or No",
  outputs: BOOLEAN_OUTPUTS,
  boolean: true,
} as const

const PACKET_TYPE_OUTPUTS = [
  { value: "1 (Telemetry)", meaning: "A protocol-v8 spacecraft telemetry frame." },
] as const

const I2C_BUS_OUTPUTS = [
  { value: "0 (Idle)", meaning: "The shared sensor I2C bus is operating normally." },
  { value: "1 (Restart in progress)", meaning: "CDH is deinitializing and clearing the bus." },
  { value: "2 (Restart hold)", meaning: "The bus is in its short hold period before reinitialization." },
] as const

const GNSS_FIX_OUTPUTS = [
  { value: "0 (No fix)", meaning: "No navigation solution is available." },
  { value: "1 (Dead reckoning)", meaning: "Dead-reckoning-only solution." },
  { value: "2 (2D fix)", meaning: "Horizontal position solution without a full 3D fix." },
  { value: "3 (3D fix)", meaning: "Full three-dimensional GNSS solution." },
  { value: "4 (GNSS + dead reckoning)", meaning: "Combined GNSS and dead-reckoning solution." },
  { value: "5 (Time only)", meaning: "Time solution without a navigation position." },
] as const

const FLIGHT_PHASE_OUTPUTS = [
  { value: "STANDBY (0)", meaning: "Waiting on the ground for launch detection." },
  { value: "LAUNCH (1)", meaning: "Launch has been detected." },
  { value: "ASCENT (2)", meaning: "The spacecraft is climbing." },
  { value: "CRUISE (3)", meaning: "The spacecraft is in its cruise or float phase." },
  { value: "DESCENT (4)", meaning: "The spacecraft is descending." },
  { value: "LANDING (5)", meaning: "Landing conditions have been detected." },
] as const

const RESET_REASON_OUTPUTS = [
  { value: "UNKNOWN (0)", meaning: "The reset source was not classified." },
  { value: "POWER_ON (1)", meaning: "Power-on, brownout, or external pin reset." },
  { value: "WATCHDOG (2)", meaning: "The independent watchdog reset the OBC." },
  { value: "SOFTWARE (3)", meaning: "Software requested the reset." },
  { value: "INVALID (255)", meaning: "Reserved invalid or unavailable reset code." },
] as const

const EQUIPMENT_MASK_OUTPUTS = [
  { value: "0x0000", meaning: "No equipment bits are set." },
  { value: "0x0001", meaning: "GPS / GNSS receiver bit." },
  { value: "0x0002", meaning: "IMU bit." },
  { value: "0x0004", meaning: "Barometer bit." },
  { value: "0x0008", meaning: "Coral payload bit." },
  { value: "0x0010", meaning: "SD card bit." },
  { value: "0x0020", meaning: "LoRa radio bit." },
  { value: "0x0040", meaning: "EPS ADC / battery monitor bit." },
  { value: "0x8000", meaning: "CDH datapool-freshness pseudo-equipment bit." },
  { value: "Combined mask", meaning: "Multiple set bits are decoded and listed together." },
  { value: "UNKNOWN_BIT_n", meaning: "A reserved or unknown set bit was received." },
] as const

const CORAL_STATUS_OUTPUTS = [
  { value: "0x00 (OK)", meaning: "No Coral status error bits are set." },
  { value: "0x01 (TIMEOUT)", meaning: "The Coral UART frame timed out or became stale." },
  { value: "0x02 (CRC_ERROR)", meaning: "The Coral frame failed its CRC check." },
  { value: "0x04 (SD_ERROR)", meaning: "Writing the Coral image to the SD card failed." },
  { value: "0x80 (UART_NOT_INITIALIZED)", meaning: "The Coral UART interface has not been initialized." },
  { value: "Combined mask", meaning: "Multiple set error bits are decoded and listed together." },
  { value: "UNKNOWN_BIT_n", meaning: "A reserved or unknown set bit was received." },
] as const

const UPLINK_STATUS_OUTPUTS = [
  { value: "NONE (0)", meaning: "No corresponding uplink has been observed." },
  { value: "ACCEPTED (1)", meaning: "The command or acknowledgement was valid and accepted." },
  { value: "INVALID_FORMAT (2)", meaning: "The uplink syntax or fields were invalid." },
  { value: "UNSUPPORTED (3)", meaning: "The command was well formed but is not supported." },
  { value: "DUPLICATE (4)", meaning: "The command or acknowledgement had already been processed." },
  { value: "UNEXPECTED_ACK (5)", meaning: "The acknowledgement did not match an expected telemetry sequence." },
] as const

const LORA_EVENT_OUTPUTS = [
  { value: "NONE (0)", meaning: "No spacecraft radio event has been recorded." },
  { value: "INIT_OK (1)", meaning: "LoRa radio initialization succeeded." },
  { value: "INIT_FAIL (2)", meaning: "LoRa radio initialization failed." },
  { value: "TX_OK (3)", meaning: "The most recent telemetry transmission completed." },
  { value: "TX_SPI_FAIL (4)", meaning: "A transmit operation failed on the SPI interface." },
  { value: "TX_TIMEOUT (5)", meaning: "The radio did not report transmit completion in time." },
  { value: "TX_BAD_LENGTH (6)", meaning: "The requested transmit payload length was invalid." },
  { value: "NOT_READY (7)", meaning: "A radio operation was requested before the driver was ready." },
  { value: "CONFIG_FAIL (8)", meaning: "Radio configuration or readback verification failed." },
  { value: "RX_OK (9)", meaning: "A valid uplink packet was received." },
  { value: "RX_CRC_ERROR (10)", meaning: "An uplink packet failed the modem CRC check." },
  { value: "RX_SPI_FAIL (11)", meaning: "A receive operation failed on the SPI interface." },
  { value: "RX_MODE_FAIL (12)", meaning: "The driver could not enter or restore continuous receive mode." },
  { value: "ACK_TIMEOUT (13)", meaning: "The five-second telemetry acknowledgement window expired." },
  { value: "ACK_RX_UNAVAILABLE (14)", meaning: "Receive mode could not be restored in time to observe an acknowledgement." },
] as const

const LORA_RX_STATUS_OUTPUTS = [
  { value: "NONE (0)", meaning: "No receive-health state has been recorded." },
  { value: "ACTIVE (1)", meaning: "Continuous receive mode is active." },
  { value: "PACKET_OK (2)", meaning: "The latest received packet passed radio checks." },
  { value: "CRC_ERROR (3)", meaning: "The latest received packet failed its CRC check." },
  { value: "BAD_LENGTH (4)", meaning: "The latest received packet had an invalid length." },
  { value: "SPI_ERROR (5)", meaning: "The receive path encountered an SPI error." },
  { value: "MODE_ERROR (6)", meaning: "The radio failed to enter or maintain receive mode." },
] as const

const telemetryColumnHelp: Record<TelemetryColumnKey, TelemetryColumnHelp> = {
  pc_receive_time_iso: {
    description: "UTC timestamp assigned by the ground-station computer when the radio packet was received.",
    outputFormat: "ISO 8601 timestamp with timezone",
  },
  sequence_number: {
    description: "Fresh telemetry sequence number generated by the spacecraft; it wraps after 65535.",
    outputFormat: "Integer from 0 to 65535",
  },
  is_duplicate_packet: {
    description: "Whether this packet repeats the previous trusted spacecraft sequence in the same boot session.",
    ...booleanHelp,
  },
  consecutive_duplicate_packets: {
    description: "Number of duplicate telemetry packets received consecutively up to this row.",
    outputFormat: "Non-negative packet count",
  },
  total_duplicate_packets: {
    description: "Cumulative duplicate telemetry packets observed by this ground-station session.",
    outputFormat: "Non-negative packet count",
  },
  lora_downlink_rssi_dbm: {
    description: "Received signal strength measured by the ground LoRa radio for this downlink.",
    outputFormat: "Signal level in dBm; values closer to 0 are stronger",
  },
  lora_downlink_snr_db: {
    description: "Signal-to-noise ratio measured by the ground LoRa radio for this downlink.",
    outputFormat: "Signal-to-noise ratio in dB",
  },
  packet_type: {
    description: "Wire-level packet class declared in byte 0 of the frame.",
    outputFormat: "Numeric type with decoded name",
    outputs: PACKET_TYPE_OUTPUTS,
    valueLabels: { "1": "Telemetry" },
  },
  protocol_version: {
    description: "Telemetry wire-format revision declared by the spacecraft.",
    outputFormat: "8 (Protocol v8)",
    outputs: [{ value: "8", meaning: "The implemented 92-byte protocol-v8 layout." }],
    valueLabels: { "8": "Protocol v8" },
  },
  crc_ok: {
    description: "Whether the received CRC-16 matches the CRC calculated over the packet payload.",
    ...booleanHelp,
  },
  received_crc16: {
    description: "CRC-16/CCITT-FALSE value carried in the final two bytes of the spacecraft packet.",
    outputFormat: "Four-digit hexadecimal value from 0x0000 to 0xFFFF",
  },
  sample_age_ms: {
    description: "Age of the sensor datapool snapshot when the spacecraft built this telemetry frame.",
    outputFormat: "Milliseconds; unavailable when the wire sentinel is received",
  },
  obc_uptime_ms: {
    description: "Time since the OBC most recently booted, measured when this packet was built.",
    outputFormat: "Milliseconds since boot",
  },
  scv_mission_elapsed_ms: {
    description: "Spacecraft mission elapsed time carried in the configuration vector.",
    outputFormat: "Mission elapsed milliseconds",
  },
  i2c_bus_state_raw: {
    description: "Current state of the CDH/FDIR recovery state machine for the shared sensor I2C bus.",
    outputFormat: "Numeric state with decoded name",
    outputs: I2C_BUS_OUTPUTS,
    valueLabels: { "0": "Idle", "1": "Restart in progress", "2": "Restart hold" },
  },
  gnss_fix_valid_raw: {
    description: "Whether the navigation values come from a fresh, usable 3D GNSS solution. Retained navigation values may be stale when this is No.",
    ...booleanHelp,
  },
  gnss_utc_sod: {
    description: "UTC time reported by GNSS, represented as seconds elapsed since 00:00:00 UTC.",
    outputFormat: "Integer from 0 to 86399 seconds",
  },
  gnss_fix_type: {
    description: "u-blox NAV-PVT solution type. Only a fresh type-3 solution sets GNSS 3D fix valid to Yes.",
    outputFormat: "Numeric fix type with decoded name",
    outputs: GNSS_FIX_OUTPUTS,
    valueLabels: {
      "0": "No fix",
      "1": "Dead reckoning",
      "2": "2D fix",
      "3": "3D fix",
      "4": "GNSS + dead reckoning",
      "5": "Time only",
    },
  },
  gnss_satellites_used: {
    description: "Number of satellites used by the receiver in its current or retained navigation solution.",
    outputFormat: "Satellite count from 0 to 254",
  },
  latitude_deg: {
    description: "WGS-84 latitude from GNSS; positive is north and negative is south.",
    outputFormat: "Decimal degrees from -90 to +90",
  },
  longitude_deg: {
    description: "WGS-84 longitude from GNSS; positive is east and negative is west.",
    outputFormat: "Decimal degrees from -180 to +180",
  },
  gnss_altitude_m: {
    description: "GNSS altitude above mean sea level.",
    outputFormat: "Metres, displayed to 0.1 m",
  },
  vertical_speed_ms: {
    description: "GNSS vertical velocity; positive values indicate upward motion.",
    outputFormat: "Metres per second, displayed to 0.01 m/s",
  },
  ground_speed_ms: {
    description: "Horizontal speed over the ground reported by GNSS.",
    outputFormat: "Metres per second, displayed to 0.01 m/s",
  },
  course_deg: {
    description: "GNSS heading of motion measured clockwise from true north.",
    outputFormat: "Degrees from 0.00 to 359.99",
  },
  imu_valid_raw: {
    description: "Whether the transmitted IMU acceleration and gyro axes are from a current valid sample.",
    ...booleanHelp,
  },
  imu_accel_x_g: {
    description: "Acceleration along the spacecraft body-frame X axis.",
    outputFormat: "Acceleration in g, displayed to 0.001 g",
  },
  imu_accel_y_g: {
    description: "Acceleration along the spacecraft body-frame Y axis.",
    outputFormat: "Acceleration in g, displayed to 0.001 g",
  },
  imu_accel_z_g: {
    description: "Acceleration along the spacecraft body-frame Z axis; approximately +1 g on the ground in the nominal orientation.",
    outputFormat: "Acceleration in g, displayed to 0.001 g",
  },
  imu_gyro_x_dps: {
    description: "Angular rate about the spacecraft body-frame X axis.",
    outputFormat: "Degrees per second, displayed to 0.1 deg/s",
  },
  imu_gyro_y_dps: {
    description: "Angular rate about the spacecraft body-frame Y axis.",
    outputFormat: "Degrees per second, displayed to 0.1 deg/s",
  },
  imu_gyro_z_dps: {
    description: "Angular rate about the spacecraft body-frame Z axis.",
    outputFormat: "Degrees per second, displayed to 0.1 deg/s",
  },
  baro_valid_raw: {
    description: "Whether the transmitted barometer pressure, altitude, and temperature are from a current valid sample.",
    ...booleanHelp,
  },
  baro_pressure_pa: {
    description: "Atmospheric pressure measured by the MS5607 barometer.",
    outputFormat: "Pressure in pascals",
  },
  baro_altitude_m: {
    description: "Barometric altitude relative to the ground baseline captured during Standby.",
    outputFormat: "Metres relative to baseline, displayed to 0.1 m",
  },
  baro_temperature_c: {
    description: "Temperature measured by the barometer sensor.",
    outputFormat: "Degrees Celsius, displayed to 0.01 °C",
  },
  batt_valid_raw: {
    description: "Whether the battery voltage is from a current valid EPS ADC sample.",
    ...booleanHelp,
  },
  battery_mv: {
    description: "Battery-pack voltage after applying the EPS resistor-divider scaling.",
    outputFormat: "Millivolts",
  },
  coral_valid_raw: {
    description: "Whether the Coral cloud-fraction result is current and valid.",
    ...booleanHelp,
  },
  coral_sequence_low: {
    description: "Low 16 bits of the image/inference sequence assigned by the Coral payload computer.",
    outputFormat: "Integer from 0 to 65534; wraps with the low 16 bits",
  },
  coral_fraction_percent: {
    description: "Cloud-covered fraction inferred by the Coral payload model.",
    outputFormat: "Percentage from 0.00% to 100.00%",
  },
  coral_status: {
    description: "Decoded Coral payload status bit mask. Multiple error conditions may be present at the same time.",
    outputFormat: "Hexadecimal byte followed by every set status name",
    outputs: CORAL_STATUS_OUTPUTS,
    mask: "coral-status",
  },
  coral_result_age_s: {
    description: "Age of the most recent Coral result when the telemetry packet was built.",
    outputFormat: "Seconds; unavailable when the saturated wire sentinel is received",
  },
  boot_count: {
    description: "Number of OBC boots persisted by FDIR, saturated by telemetry at 65535.",
    outputFormat: "Integer from 0 to 65535",
  },
  flight_state_name: {
    description: "Current state of the spacecraft flight-state machine.",
    outputFormat: "Decoded flight-phase name",
    outputs: FLIGHT_PHASE_OUTPUTS,
  },
  reset_reason_name: {
    description: "FDIR classification of the cause of the most recent OBC reset.",
    outputFormat: "Decoded reset-reason name",
    outputs: RESET_REASON_OUTPUTS,
  },
  scv_equipment_enabled: {
    description: "Policy-plane bit mask of equipment currently allowed to operate or be recovered. A set bit means enabled.",
    outputFormat: "Hexadecimal mask followed by every enabled equipment name",
    outputs: EQUIPMENT_MASK_OUTPUTS,
    mask: "equipment",
  },
  scv_equipment_faults: {
    description: "FDIR health bit mask of equipment whose data is currently not trusted. A set bit means faulted.",
    outputFormat: "Hexadecimal mask followed by every faulted equipment name",
    outputs: EQUIPMENT_MASK_OUTPUTS,
    mask: "equipment",
  },
  scv_sd_fault_count: {
    description: "Persisted SD-card fault count, saturated at the 8-bit maximum.",
    outputFormat: "Integer from 0 to 255",
  },
  scv_watchdog_reset_count: {
    description: "Consecutive watchdog-reset count used by FDIR reduced-mode policy.",
    outputFormat: "Integer from 0 to 255",
  },
  uplink_last_command_id: {
    description: "Identifier from the most recent CMD packet observed by the spacecraft; telemetry ACKs do not overwrite it.",
    outputFormat: "Integer command ID from 0 to 65535",
  },
  uplink_last_status_name: {
    description: "Spacecraft processing result for the most recent flight command.",
    outputFormat: "Decoded uplink-status name",
    outputs: UPLINK_STATUS_OUTPUTS,
  },
  uplink_last_ack_status_name: {
    description: "Spacecraft processing result for the most recent ground acknowledgement of telemetry.",
    outputFormat: "Decoded uplink-status name",
    outputs: UPLINK_STATUS_OUTPUTS,
  },
  lora_last_event_name: {
    description: "Most recent spacecraft-side LoRa driver event across transmit, receive, and recovery operations.",
    outputFormat: "Decoded LoRa event name",
    outputs: LORA_EVENT_OUTPUTS,
  },
  lora_consecutive_failures: {
    description: "Current run of consecutive spacecraft LoRa transmit failures; a successful transmission clears it.",
    outputFormat: "Integer from 0 to 255",
  },
  lora_tx_fault_count: {
    description: "Lifetime spacecraft LoRa transmit-failure count, saturated in telemetry at 255.",
    outputFormat: "Integer from 0 to 255",
  },
  lora_recovery_count: {
    description: "Number of spacecraft LoRa recovery attempts since the current boot.",
    outputFormat: "Integer from 0 to 255",
  },
  lora_rx_mode_active: {
    description: "Whether the spacecraft radio reports continuous receive mode as active.",
    ...booleanHelp,
  },
  lora_last_rx_status_name: {
    description: "Most recent spacecraft-side health result for the LoRa receive path.",
    outputFormat: "Decoded LoRa receive-health name",
    outputs: LORA_RX_STATUS_OUTPUTS,
  },
  lora_ack_timeout_count: {
    description: "Number of five-second telemetry acknowledgement windows that expired on the spacecraft, saturated at 255.",
    outputFormat: "Integer from 0 to 255",
  },
}

export const telemetryColumns: TelemetryColumn[] = baseTelemetryColumns.map(
  (column) => ({ ...column, ...telemetryColumnHelp[column.key] }),
)

export function formatTelemetryCell(row: TelemetryRow, column: TelemetryColumn) {
  const value = row[column.key]
  if (column.boolean) return fmtBoolean(value)
  if (column.mask === "equipment") {
    return formatEquipmentMask(row[column.key])
  }
  if (column.mask === "coral-status") return formatCoralStatusMask(value)
  if (column.valueLabels && value !== null && value !== undefined) {
    const label = column.valueLabels[String(value)]
    return `${String(value)} (${label ?? "Unknown"})`
  }
  return fmt(value, column.suffix ?? "", column.decimals)
}
