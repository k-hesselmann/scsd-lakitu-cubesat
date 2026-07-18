import { Badge } from "@/components/ui/badge"
import { EQUIPMENT_BARO, EQUIPMENT_CORAL, EQUIPMENT_EPS_ADC, EQUIPMENT_GPS, EQUIPMENT_IMU, EQUIPMENT_LORA, EQUIPMENT_SD, hasEquipmentFault, rawFlagIsValid } from "@/lib/telemetryHealth"
import type { TelemetryRow } from "@/types/telemetry"

export function StatusFlags({ latest }: { latest: TelemetryRow | null }) {
  if (!latest) return <p className="text-sm text-muted-foreground">No protocol-v8 status data yet.</p>
  const flags = [
    ["GPS valid", rawFlagIsValid(latest.gps_valid_raw), false], ["IMU valid", rawFlagIsValid(latest.imu_valid_raw), false],
    ["Barometer valid", rawFlagIsValid(latest.baro_valid_raw), false], ["Battery valid", rawFlagIsValid(latest.batt_valid_raw), false],
    ["Coral valid", rawFlagIsValid(latest.coral_valid_raw), false], ["GPS fault", hasEquipmentFault(latest, EQUIPMENT_GPS), true],
    ["IMU fault", hasEquipmentFault(latest, EQUIPMENT_IMU), true], ["Barometer fault", hasEquipmentFault(latest, EQUIPMENT_BARO), true],
    ["Battery ADC fault", hasEquipmentFault(latest, EQUIPMENT_EPS_ADC), true], ["Coral fault", hasEquipmentFault(latest, EQUIPMENT_CORAL), true],
    ["SD fault", hasEquipmentFault(latest, EQUIPMENT_SD), true], ["LoRa fault", hasEquipmentFault(latest, EQUIPMENT_LORA), true],
  ] as const
  return <div className="flex flex-wrap gap-2">{flags.map(([name, value, badWhenActive]) => <Badge key={name} variant={value === undefined ? "outline" : badWhenActive && value ? "destructive" : value ? "default" : "secondary"}>{name}: {value === undefined ? "—" : value ? "ON" : "OFF"}</Badge>)}</div>
}
