import { Table, TableBody, TableCell, TableRow } from "@/components/ui/table"
import { fmt } from "@/lib/format"
import { gnssFixIsValid } from "@/lib/telemetryHealth"
import type { TelemetryRow } from "@/types/telemetry"

export function LatestPacketTable({ latest }: { latest: TelemetryRow | null }) {
  if (!latest) return <p className="text-sm text-muted-foreground">No protocol-v8 telemetry received yet.</p>
  const gnssFixValid = gnssFixIsValid(latest)

  const rows: [string, string][] = [
    ["Ground receive time", fmt(latest.pc_receive_time_iso)], ["Sequence", fmt(latest.sequence_number)],
    ["Flight phase", fmt(latest.flight_state_name)], ["TX uptime", fmt(latest.obc_uptime_ms, " ms")],
    ["Battery voltage", fmt(latest.battery_v, " V", 2)], ["GNSS 3D fix valid", fmt(gnssFixValid)],
    ["GNSS fix type", fmt(latest.gnss_fix_type)], ["GNSS satellites", fmt(latest.gnss_satellites_used)],
    ["Latitude", fmt(gnssFixValid ? latest.latitude_deg : undefined, "", 7)], ["Longitude", fmt(gnssFixValid ? latest.longitude_deg : undefined, "", 7)],
    ["GNSS altitude MSL", fmt(gnssFixValid ? latest.gnss_altitude_m : undefined, " m", 1)], ["Barometer altitude (relative)", fmt(latest.baro_altitude_m, " m", 1)],
    ["Pressure", fmt(latest.baro_pressure_pa, " Pa")], ["Barometer temperature", fmt(latest.baro_temperature_c, " °C", 2)],
    ["Ground RX RSSI", fmt(latest.lora_downlink_rssi_dbm, " dBm")], ["Ground RX SNR", fmt(latest.lora_downlink_snr_db, " dB", 1)],
    ["Telemetry CRC valid", fmt(latest.crc_ok)], ["SCV equipment faults", fmt(latest.scv_equipment_faults)],
    ["Spacecraft LoRa event", fmt(latest.lora_last_event_name)], ["Coral cloud fraction", fmt(latest.coral_fraction_percent, "%", 2)],
    ["Coral status", fmt(latest.coral_status)], ["Coral result age", fmt(latest.coral_result_age_s, " s")],
  ]

  return <Table><TableBody>{rows.map(([name, value]) => <TableRow key={name}><TableCell className="font-medium">{name}</TableCell><TableCell>{value}</TableCell></TableRow>)}</TableBody></Table>
}
