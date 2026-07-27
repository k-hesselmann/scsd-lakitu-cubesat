import { CartesianGrid, Line, LineChart, ResponsiveContainer, Tooltip, XAxis, YAxis } from "recharts"
import type { ReactNode } from "react"
import { ExpandableChartCard } from "@/components/ExpandableChartCard"
import { Card, CardContent } from "@/components/ui/card"
import { adaptiveAxisDomain } from "@/lib/chartAxis"
import { fmt } from "@/lib/format"
import { gnssFixIsValid } from "@/lib/telemetryHealth"
import { latestValues } from "@/lib/telemetrySeries"
import type { TelemetryRow } from "@/types/telemetry"

function formatTime(value: unknown) { if (typeof value !== "string") return ""; const date = new Date(value); return Number.isNaN(date.getTime()) ? "" : date.toLocaleTimeString() }
function maxAltitude(history: TelemetryRow[]) { return history.filter((row) => gnssFixIsValid(row) && typeof row.gnss_altitude_m === "number").reduce<TelemetryRow | null>((best, row) => !best || Number(row.gnss_altitude_m) > Number(best.gnss_altitude_m) ? row : best, null) }
function timeInCurrentPhase(history: TelemetryRow[], latest: TelemetryRow | null) { if (!latest?.flight_state_name || !latest.pc_receive_time_unix) return null; let phaseStart = latest; for (const row of [...history].reverse()) { if (row.flight_state_name === latest.flight_state_name) phaseStart = row; else break } return phaseStart.pc_receive_time_unix ? latest.pc_receive_time_unix - phaseStart.pc_receive_time_unix : null }
function MiniCard({ title, value, subtitle }: { title: string; value: string; subtitle?: string }) {
  return (
    <Card className="gap-0 overflow-hidden py-0">
      <CardContent className="p-1.5">
        <div className="text-[10px] leading-3 text-muted-foreground">{title}</div>
        <div className="mt-0.5 truncate text-lg leading-5 font-bold">{value}</div>
        {subtitle ? <div className="mt-0.5 truncate text-[9px] leading-3 text-muted-foreground">{subtitle}</div> : null}
      </CardContent>
    </Card>
  )
}

function ChartCard({ title, children }: { title: string; children: ReactNode }) {
  return <ExpandableChartCard title={title}>{children}</ExpandableChartCard>
}

export function FlightPhasePanel({ latest, history }: { latest: TelemetryRow | null; history: TelemetryRow[] }) {
  const peak = maxAltitude(history)
  const phaseTime = timeInCurrentPhase(history, latest)
  const gnssFixValid = gnssFixIsValid(latest)
  const chartHistory = latestValues(history).map((row) => gnssFixIsValid(row) ? row : {
    ...row,
    gnss_altitude_m: undefined,
    vertical_speed_ms: undefined,
    ground_speed_ms: undefined,
  })

  return <div className="grid h-full min-h-0 gap-3 xl:grid-cols-[360px_minmax(0,1fr)]"><section className="grid min-h-0 grid-cols-2 gap-2"><MiniCard title="Current Phase" value={fmt(latest?.flight_state_name)} subtitle={`State ID: ${fmt(latest?.flight_state)}`} /><MiniCard title="Time in Phase" value={phaseTime === null ? "—" : `${phaseTime.toFixed(1)} s`} subtitle="Estimated from ground reception" /><MiniCard title="GNSS Altitude MSL" value={fmt(gnssFixValid ? latest?.gnss_altitude_m : undefined, " m", 1)} subtitle={latest && gnssFixValid ? "Current 3D fix" : "No usable 3D fix"} /><MiniCard title="Vertical Speed" value={fmt(gnssFixValid ? latest?.vertical_speed_ms : undefined, " m/s", 2)} subtitle="Positive = ascent" /><MiniCard title="Maximum GNSS Altitude MSL" value={fmt(peak?.gnss_altitude_m, " m", 1)} subtitle={`Seq ${fmt(peak?.sequence_number)}`} /><MiniCard title="Ground Speed" value={fmt(gnssFixValid ? latest?.ground_speed_ms : undefined, " m/s", 2)} subtitle="Current 3D-fix speed" /></section><div className="grid min-h-0 gap-3 xl:grid-cols-2"><ChartCard title="Altitude"><ResponsiveContainer width="100%" height="100%"><LineChart data={chartHistory} margin={{ top: 3, right: 8, bottom: 0, left: 0 }}><CartesianGrid strokeDasharray="3 3" /><XAxis dataKey="pc_receive_time_iso" tickFormatter={formatTime} tick={{ fontSize: 10 }} /><YAxis domain={adaptiveAxisDomain(5)} width={38} tick={{ fontSize: 10 }} /><Tooltip labelFormatter={(label) => formatTime(label)} /><Line type="monotone" dataKey="gnss_altitude_m" name="GNSS MSL [m]" dot={false} stroke="#2563eb" /><Line type="monotone" dataKey="baro_altitude_m" name="Baro relative [m]" dot={false} stroke="#ea580c" /></LineChart></ResponsiveContainer></ChartCard><ChartCard title="Speed"><ResponsiveContainer width="100%" height="100%"><LineChart data={chartHistory} margin={{ top: 3, right: 8, bottom: 0, left: 0 }}><CartesianGrid strokeDasharray="3 3" /><XAxis dataKey="pc_receive_time_iso" tickFormatter={formatTime} tick={{ fontSize: 10 }} /><YAxis domain={adaptiveAxisDomain(0.5)} width={38} tick={{ fontSize: 10 }} /><Tooltip labelFormatter={(label) => formatTime(label)} /><Line type="monotone" dataKey="vertical_speed_ms" name="Vertical speed [m/s]" dot={false} stroke="#16a34a" /><Line type="monotone" dataKey="ground_speed_ms" name="Ground speed [m/s]" dot={false} stroke="#dc2626" /></LineChart></ResponsiveContainer></ChartCard></div></div>
}
