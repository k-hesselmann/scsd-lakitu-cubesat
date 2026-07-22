import { Table, TableBody, TableCell, TableRow } from "@/components/ui/table"
import { fmt } from "@/lib/format"
import { rawFlagIsValid } from "@/lib/telemetryHealth"
import type { TelemetryRow } from "@/types/telemetry"

export function LatestPacketTable({ latest }: { latest: TelemetryRow | null }) {
  if (!latest) return <p className="text-sm text-muted-foreground">No protocol-v8 telemetry received yet.</p>

  const gpsValid = rawFlagIsValid(latest.gps_valid_raw)
  const baroValid = rawFlagIsValid(latest.baro_valid_raw)
  const batteryValid = rawFlagIsValid(latest.batt_valid_raw)
  const coralValid = rawFlagIsValid(latest.coral_valid_raw)

  const rows: [string, string][] = [
    ["Ground receive time", fmt(latest.pc_receive_time_iso)], ["Sequence", fmt(latest.sequence_number)],
    ["Flight phase", fmt(latest.flight_state_name)], ["TX uptime", fmt(latest.obc_uptime_ms, " ms")],
    ["Battery voltage", fmt(batteryValid ? latest.battery_v : null, " V", 2)], ["GPS valid", fmt(gpsValid)],
    ["Latitude", fmt(gpsValid ? latest.latitude_deg : null, "", 7)], ["Longitude", fmt(gpsValid ? latest.longitude_deg : null, "", 7)],
    ["GPS altitude", fmt(gpsValid ? latest.gnss_altitude_m : null, " m", 1)], ["Barometer altitude", fmt(baroValid ? latest.baro_altitude_m : null, " m", 1)],
    ["Pressure", fmt(baroValid ? latest.baro_pressure_pa : null, " Pa")], ["Barometer temperature", fmt(baroValid ? latest.baro_temperature_c : null, " °C", 2)],
    ["Ground RX RSSI", fmt(latest.lora_downlink_rssi_dbm, " dBm")], ["Ground RX SNR", fmt(latest.lora_downlink_snr_db, " dB", 1)],
    ["Telemetry CRC valid", fmt(latest.crc_ok)], ["SCV equipment faults", fmt(latest.scv_equipment_faults)],
    ["Spacecraft LoRa event", fmt(latest.lora_last_event_name)], ["Coral cloud fraction", fmt(coralValid ? latest.coral_fraction_percent : null, "%", 2)],
    ["Coral status", fmt(latest.coral_status)], ["Coral result age", fmt(coralValid ? latest.coral_result_age_s : null, " s")],
  ]

  return <Table><TableBody>{rows.map(([name, value]) => <TableRow key={name}><TableCell className="font-medium">{name}</TableCell><TableCell>{value}</TableCell></TableRow>)}</TableBody></Table>
}
