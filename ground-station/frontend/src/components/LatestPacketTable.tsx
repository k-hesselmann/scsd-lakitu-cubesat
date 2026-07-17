import { Table, TableBody, TableCell, TableRow } from "@/components/ui/table"
import { fmt } from "@/lib/format"
import type { TelemetryRow } from "@/types/telemetry"

export function LatestPacketTable({ latest }: { latest: TelemetryRow | null }) {
  if (!latest) return <p className="text-sm text-muted-foreground">No raw-v7 telemetry received yet.</p>

  const rows: [string, string][] = [
    ["Ground receive time", fmt(latest.pc_receive_time_iso)], ["Sequence", fmt(latest.sequence_number)],
    ["Flight phase", fmt(latest.flight_state_name)], ["TX uptime", fmt(latest.obc_uptime_ms, " ms")],
    ["Battery voltage", fmt(latest.battery_v, " V", 2)], ["GPS valid", fmt(latest.gps_valid_raw)],
    ["Latitude", fmt(latest.latitude_deg, "", 7)], ["Longitude", fmt(latest.longitude_deg, "", 7)],
    ["GPS altitude", fmt(latest.gnss_altitude_m, " m", 1)], ["Barometer altitude", fmt(latest.baro_altitude_m, " m", 1)],
    ["Pressure", fmt(latest.baro_pressure_pa, " Pa")], ["Barometer temperature", fmt(latest.baro_temperature_c, " °C", 2)],
    ["Ground RX RSSI", fmt(latest.lora_downlink_rssi_dbm, " dBm")], ["Ground RX SNR", fmt(latest.lora_downlink_snr_db, " dB", 1)],
    ["Telemetry CRC valid", fmt(latest.crc_ok)], ["SCV equipment faults", fmt(latest.scv_equipment_faults)],
    ["Spacecraft LoRa event", fmt(latest.lora_last_event_name)], ["Coral payload", fmt(latest.coral_payload_text)],
  ]

  return <Table><TableBody>{rows.map(([name, value]) => <TableRow key={name}><TableCell className="font-medium">{name}</TableCell><TableCell>{value}</TableCell></TableRow>)}</TableBody></Table>
}
