import type { TelemetryRow } from "@/types/telemetry"

export const EQUIPMENT_GPS = 1 << 0
export const EQUIPMENT_IMU = 1 << 1
export const EQUIPMENT_BARO = 1 << 2
export const EQUIPMENT_CORAL = 1 << 3
export const EQUIPMENT_SD = 1 << 4
export const EQUIPMENT_LORA = 1 << 5
export const EQUIPMENT_EPS_ADC = 1 << 6
export const EQUIPMENT_CDH = 1 << 15

const EQUIPMENT_BITS = [
  [EQUIPMENT_GPS, "GPS"],
  [EQUIPMENT_IMU, "IMU"],
  [EQUIPMENT_BARO, "BARO"],
  [EQUIPMENT_CORAL, "CORAL"],
  [EQUIPMENT_SD, "SD"],
  [EQUIPMENT_LORA, "LORA"],
  [EQUIPMENT_EPS_ADC, "EPS ADC"],
  [EQUIPMENT_CDH, "CDH"],
] as const

export function rawFlagIsValid(value: unknown) {
  return value === 1 || value === true
}

export function parseEquipmentMask(value: unknown) {
  if (typeof value === "number" && Number.isInteger(value) && value >= 0 && value <= 0xffff) return value
  if (typeof value === "string") {
    const trimmed = value.trim()
    if (!/^(?:0[xX][0-9a-fA-F]+|\d+)$/.test(trimmed)) return null
    const parsed = Number.parseInt(trimmed, 0)
    return parsed >= 0 && parsed <= 0xffff ? parsed : null
  }
  return null
}

export function decodeEquipmentMask(value: unknown) {
  const mask = parseEquipmentMask(value)
  if (mask === null) return null

  const names: string[] = []
  let knownMask = 0
  for (const [bitMask, name] of EQUIPMENT_BITS) {
    knownMask |= bitMask
    if ((mask & bitMask) !== 0) names.push(name)
  }
  for (let bit = 0; bit < 16; bit += 1) {
    const bitMask = 1 << bit
    if ((mask & bitMask) !== 0 && (knownMask & bitMask) === 0) {
      names.push(`UNKNOWN_BIT_${bit}`)
    }
  }
  return names
}

export function formatEquipmentMask(value: unknown) {
  const mask = parseEquipmentMask(value)
  if (mask === null) return "\u2014"

  const names = decodeEquipmentMask(mask) ?? []
  const hex = `0x${mask.toString(16).toUpperCase().padStart(4, "0")}`
  return `${hex} (${names.length > 0 ? names.join(", ") : "none"})`
}

export function equipmentEnabledMask(row: TelemetryRow | null) {
  return parseEquipmentMask(row?.scv_equipment_enabled)
}

export function isEquipmentEnabled(row: TelemetryRow | null, equipment: number) {
  const mask = equipmentEnabledMask(row)
  return mask === null ? undefined : (mask & equipment) !== 0
}

export function equipmentFaultMask(row: TelemetryRow | null) {
  return parseEquipmentMask(row?.scv_equipment_faults)
}

export function hasEquipmentFault(row: TelemetryRow | null, equipment: number) {
  const mask = equipmentFaultMask(row)
  return mask === null ? undefined : (mask & equipment) !== 0
}

export const LORA_EVENT_INIT_OK = 1
export const LORA_EVENT_INIT_FAIL = 2
export const LORA_EVENT_TX_OK = 3
export const LORA_EVENT_TX_SPI_FAIL = 4
export const LORA_EVENT_TX_TIMEOUT = 5
export const LORA_EVENT_TX_BAD_LENGTH = 6
export const LORA_EVENT_NOT_READY = 7
export const LORA_EVENT_CONFIG_FAIL = 8
export const LORA_EVENT_RX_OK = 9
export const LORA_EVENT_RX_CRC_ERROR = 10
export const LORA_EVENT_RX_SPI_FAIL = 11
export const LORA_EVENT_RX_MODE_FAIL = 12
export const LORA_EVENT_ACK_TIMEOUT = 13
export const LORA_EVENT_ACK_RX_UNAVAILABLE = 14

export function isLoraFailureEvent(event: unknown) {
  return event === LORA_EVENT_INIT_FAIL ||
    event === LORA_EVENT_TX_SPI_FAIL ||
    event === LORA_EVENT_TX_TIMEOUT ||
    event === LORA_EVENT_TX_BAD_LENGTH ||
    event === LORA_EVENT_CONFIG_FAIL ||
    event === LORA_EVENT_RX_SPI_FAIL ||
    event === LORA_EVENT_RX_MODE_FAIL ||
    event === LORA_EVENT_ACK_TIMEOUT ||
    event === LORA_EVENT_ACK_RX_UNAVAILABLE ||
    event === LORA_EVENT_NOT_READY
}

export function loraTxIsHealthy(row: TelemetryRow | null) {
  if (typeof row?.lora_last_event !== "number") return undefined
  if (row.lora_last_event === LORA_EVENT_INIT_OK || row.lora_last_event === LORA_EVENT_TX_OK || row.lora_last_event === LORA_EVENT_RX_OK) return true
  if (isLoraFailureEvent(row.lora_last_event)) return false
  return undefined
}
