import type { TelemetryRow } from "@/types/telemetry"

export const EQUIPMENT_GPS = 1 << 0
export const EQUIPMENT_IMU = 1 << 1
export const EQUIPMENT_BARO = 1 << 2
export const EQUIPMENT_CORAL = 1 << 3
export const EQUIPMENT_SD = 1 << 4
export const EQUIPMENT_LORA = 1 << 5
export const EQUIPMENT_EPS_ADC = 1 << 6

export function rawFlagIsValid(value: unknown) {
  return value === 1 || value === true
}

function parseEquipmentMask(value: unknown) {
  if (typeof value === "number" && Number.isInteger(value)) return value
  if (typeof value === "string") {
    const parsed = Number.parseInt(value, 0)
    return Number.isNaN(parsed) ? null : parsed
  }
  return null
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

export function isLoraFailureEvent(event: unknown) {
  return event === LORA_EVENT_INIT_FAIL ||
    event === LORA_EVENT_TX_SPI_FAIL ||
    event === LORA_EVENT_TX_TIMEOUT ||
    event === LORA_EVENT_TX_BAD_LENGTH ||
    event === LORA_EVENT_CONFIG_FAIL ||
    event === LORA_EVENT_RX_SPI_FAIL ||
    event === LORA_EVENT_RX_MODE_FAIL ||
    event === LORA_EVENT_ACK_TIMEOUT ||
    event === LORA_EVENT_NOT_READY
}

export function loraTxIsHealthy(row: TelemetryRow | null) {
  if (typeof row?.lora_last_event !== "number") return undefined
  if (row.lora_last_event === LORA_EVENT_INIT_OK || row.lora_last_event === LORA_EVENT_TX_OK || row.lora_last_event === LORA_EVENT_RX_OK) return true
  if (isLoraFailureEvent(row.lora_last_event)) return false
  return undefined
}
