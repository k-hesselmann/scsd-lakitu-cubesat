import {
  Table,
  TableBody,
  TableCell,
  TableRow,
} from "@/components/ui/table"

import type { TelemetryRow } from "@/types/telemetry"

function fmt(value: unknown, suffix = "", decimals?: number) {
  if (value === null || value === undefined) return "—"

  if (typeof value === "number" && decimals !== undefined) {
    return `${value.toFixed(decimals)}${suffix}`
  }

  if (typeof value === "boolean") {
    return value ? "YES" : "NO"
  }

  return `${value}${suffix}`
}

export function LatestPacketTable({ latest }: { latest: TelemetryRow | null }) {
  if (!latest) {
    return <p className="text-sm text-muted-foreground">No telemetry received yet.</p>
  }

  const rows: [string, string][] = [
    ["PC receive time", fmt(latest.pc_receive_time_iso)],
    ["Sequence number", fmt(latest.sequence_number)],
    ["Flight state", fmt(latest.flight_state_name)],
    ["OBC uptime", fmt(latest.obc_uptime_ms, " ms")],
    ["Battery voltage", fmt(latest.battery_v, " V", 2)],
    ["GNSS fix type", fmt(latest.gnss_fix_type)],
    ["Satellites used", fmt(latest.gnss_satellites_used)],
    ["Latitude", fmt(latest.latitude_deg, "", 7)],
    ["Longitude", fmt(latest.longitude_deg, "", 7)],
    ["GNSS altitude", fmt(latest.gnss_altitude_m, " m", 1)],
    ["Baro altitude", fmt(latest.baro_altitude_m, " m", 1)],
    ["Pressure", fmt(latest.baro_pressure_pa, " Pa")],
    ["Baro temperature", fmt(latest.baro_temperature_c, " °C", 2)],
    ["MCU temperature", fmt(latest.mcu_temperature_c, " °C", 2)],
    ["Accel X", fmt(latest.accel_x_ms2, " m/s²", 2)],
    ["Accel Y", fmt(latest.accel_y_ms2, " m/s²", 2)],
    ["Accel Z", fmt(latest.accel_z_ms2, " m/s²", 2)],
    ["Ground RX RSSI", fmt(latest.lora_downlink_rssi_dbm, " dBm")],
    ["Ground RX SNR", fmt(latest.lora_downlink_snr_db, " dB", 1)],
    ["Telemetry CRC OK", fmt(latest.crc_ok)],
    ["Coral status", fmt(latest.coral_status)],
    ["Coral payload", fmt(latest.coral_payload_text)],
  ]

  return (
    <Table>
      <TableBody>
        {rows.map(([name, value]) => (
          <TableRow key={name}>
            <TableCell className="font-medium">{name}</TableCell>
            <TableCell>{value}</TableCell>
          </TableRow>
        ))}
      </TableBody>
    </Table>
  )
}