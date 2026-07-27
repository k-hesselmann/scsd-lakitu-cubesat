import {
  CartesianGrid,
  Line,
  LineChart,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from "recharts"

import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card"
import { MetricCard } from "@/components/MetricCard"
import { adaptiveAxisDomain } from "@/lib/chartAxis"
import { fmt } from "@/lib/format"
import { latestValues } from "@/lib/telemetrySeries"
import type { DashboardPageProps } from "@/pages/pageTypes"
import type { TelemetryRow } from "@/types/telemetry"

function formatTime(value: unknown) {
  if (typeof value !== "string") return ""
  const date = new Date(value)
  return Number.isNaN(date.getTime()) ? "" : date.toLocaleTimeString()
}

function maxAltitude(history: TelemetryRow[]) {
  const valid = history.filter((row) => typeof row.gnss_altitude_m === "number")
  if (valid.length === 0) return null

  return valid.reduce((best, row) =>
    Number(row.gnss_altitude_m) > Number(best.gnss_altitude_m) ? row : best,
  )
}

function timeInCurrentPhase(history: TelemetryRow[], latest: TelemetryRow | null) {
  if (!latest?.flight_state_name || !latest.pc_receive_time_unix) return null

  const reversed = [...history].reverse()
  let phaseStart = latest

  for (const row of reversed) {
    if (row.flight_state_name === latest.flight_state_name) {
      phaseStart = row
    } else {
      break
    }
  }

  if (!phaseStart.pc_receive_time_unix) return null

  return latest.pc_receive_time_unix - phaseStart.pc_receive_time_unix
}

export function FlightPhasePage({ latest, history }: DashboardPageProps) {
  const peak = maxAltitude(history)
  const phaseTime = timeInCurrentPhase(history, latest)

  return (
    <div className="flex h-full w-full flex-col overflow-hidden p-4">
      <header className="mb-3 shrink-0">
        <h1 className="text-2xl font-bold tracking-tight">Flight Phase</h1>
        <p className="text-sm text-muted-foreground">
          Mission progress, vertical motion, altitude trend, and phase timing.
        </p>
      </header>

      <section className="mb-4 grid shrink-0 gap-4 md:grid-cols-2 xl:grid-cols-5">
        <MetricCard
          title="Current Phase"
          value={fmt(latest?.flight_state_name)}
          subtitle={`State ID: ${fmt(latest?.flight_state)}`}
          variant="default"
        />

        <MetricCard
          title="Time in Phase"
          value={phaseTime === null ? "—" : `${phaseTime.toFixed(1)} s`}
          subtitle="Estimated from received packets"
        />

        <MetricCard
          title="GNSS Altitude MSL"
          value={fmt(latest?.gnss_altitude_m, " m", 1)}
          subtitle="Current 3D fix"
        />

        <MetricCard
          title="Vertical Speed"
          value={fmt(latest?.vertical_speed_ms, " m/s", 2)}
          subtitle="Positive = ascent"
        />

        <MetricCard
          title="Maximum GNSS Altitude MSL"
          value={fmt(peak?.gnss_altitude_m, " m", 1)}
          subtitle={`Seq ${fmt(peak?.sequence_number)}`}
        />
      </section>

      <div className="grid min-h-0 flex-1 gap-4 xl:grid-cols-[2fr_1fr]">
        <Card className="min-h-0 overflow-hidden">
          <CardHeader className="px-4 py-3">
            <CardTitle className="text-base">GNSS Altitude MSL [m] / Baro Relative Altitude [m]</CardTitle>
          </CardHeader>
          <CardContent className="h-[calc(100%-56px)] px-4 pb-4">
            <ResponsiveContainer width="100%" height="100%">
              <LineChart data={latestValues(history)}>
                <CartesianGrid strokeDasharray="3 3" />
                <XAxis dataKey="pc_receive_time_iso" tickFormatter={formatTime} />
                <YAxis domain={adaptiveAxisDomain(5)} />
                <Tooltip labelFormatter={(label) => formatTime(label)} />
                <Line
                  type="monotone"
                  dataKey="gnss_altitude_m"
                  name="GNSS MSL [m]"
                  dot={false}
                  stroke="#2563eb"
                />
                <Line
                  type="monotone"
                  dataKey="baro_altitude_m"
                  name="Baro relative [m]"
                  dot={false}
                  stroke="#ea580c"
                />
                <Line
                  type="monotone"
                  dataKey="vertical_speed_ms"
                  name="Vertical speed [m/s]"
                  dot={false}
                  stroke="#16a34a"
                />
              </LineChart>
            </ResponsiveContainer>
          </CardContent>
        </Card>

        <Card className="min-h-0 overflow-hidden">
          <CardHeader className="px-4 py-3">
            <CardTitle className="text-base">Mission Phase Checklist</CardTitle>
          </CardHeader>
          <CardContent className="space-y-3 overflow-auto px-4 pb-4 text-sm">
            <div>Pre-launch: verify telemetry, battery, GNSS, SD logging.</div>
            <div>Ascent: monitor vertical speed, altitude, RSSI/SNR.</div>
            <div>Float/mission: monitor payload status and science data.</div>
            <div>Descent: monitor trajectory, recovery estimate, link margin.</div>
            <div>Recovery: use last known position and trajectory map.</div>
          </CardContent>
        </Card>
      </div>
    </div>
  )
}
