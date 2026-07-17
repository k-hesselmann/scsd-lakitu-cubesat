import type { ReactNode } from "react"

import {
  CartesianGrid,
  Legend,
  Line,
  LineChart,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from "recharts"

import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card"
import type { TelemetryRow } from "@/types/telemetry"

function formatTime(value: unknown): string {
  if (typeof value !== "string") return ""

  const date = new Date(value)

  if (Number.isNaN(date.getTime())) return ""

  return date.toLocaleTimeString()
}

function ChartCard({
  title,
  children,
  compact = false,
}: {
  title: string
  children: ReactNode
  compact?: boolean
}) {
  return (
    <Card className="min-h-0 overflow-hidden">
      <CardHeader className={compact ? "px-3 py-2" : undefined}>
        <CardTitle className={compact ? "text-sm" : "text-base"}>
          {title}
        </CardTitle>
      </CardHeader>
      <CardContent className={compact ? "h-[calc(100%-40px)] px-2 pb-2" : "h-[280px]"}>
        {children}
      </CardContent>
    </Card>
  )
}

function EmptyChart({
  title,
  compact,
}: {
  title: string
  compact?: boolean
}) {
  return (
    <ChartCard title={title} compact={compact}>
      <div className="flex h-full items-center justify-center text-sm text-muted-foreground">
        Waiting for telemetry...
      </div>
    </ChartCard>
  )
}

function commonChartProps(history: TelemetryRow[]) {
  return {
    data: history,
    margin: { top: 5, right: 10, bottom: 5, left: 0 },
  }
}

export function TelemetryCharts({
  history,
  compact = false,
}: {
  history: TelemetryRow[]
  compact?: boolean
}) {
  const gridClass = compact
    ? "grid h-full min-h-0 grid-cols-1 grid-rows-6 gap-3 lg:grid-cols-2 lg:grid-rows-3 2xl:grid-cols-3 2xl:grid-rows-2"
    : "grid gap-4 xl:grid-cols-2"

  if (history.length === 0) {
    return (
      <div className={gridClass}>
        <EmptyChart title="Battery Voltage" compact={compact} />
        <EmptyChart title="Altitude" compact={compact} />
        <EmptyChart title="Barometer" compact={compact} />
        <EmptyChart title="Acceleration" compact={compact} />
        <EmptyChart title="Link Quality" compact={compact} />
        <EmptyChart title="Speed" compact={compact} />
      </div>
    )
  }

  return (
    <div className={gridClass}>
      <ChartCard title="Battery Voltage" compact={compact}>
        <ResponsiveContainer width="100%" height="100%">
          <LineChart {...commonChartProps(history)}>
            <CartesianGrid strokeDasharray="3 3" />
            <XAxis dataKey="pc_receive_time_iso" tickFormatter={formatTime} />
            <YAxis width={45} />
            <Tooltip labelFormatter={(label) => formatTime(label)} />
            <Legend />
            <Line
              type="monotone"
              dataKey="battery_v"
              name="Battery [V]"
              dot={false}
              stroke="#2563eb"
            />
          </LineChart>
        </ResponsiveContainer>
      </ChartCard>

      <ChartCard title="Altitude" compact={compact}>
        <ResponsiveContainer width="100%" height="100%">
          <LineChart {...commonChartProps(history)}>
            <CartesianGrid strokeDasharray="3 3" />
            <XAxis dataKey="pc_receive_time_iso" tickFormatter={formatTime} />
            <YAxis width={45} />
            <Tooltip labelFormatter={(label) => formatTime(label)} />
            <Legend />
            <Line
              type="monotone"
              dataKey="gnss_altitude_m"
              name="GNSS [m]"
              dot={false}
              stroke="#16a34a"
            />
            <Line
              type="monotone"
              dataKey="baro_altitude_m"
              name="Baro [m]"
              dot={false}
              stroke="#ea580c"
            />
          </LineChart>
        </ResponsiveContainer>
      </ChartCard>

      <ChartCard title="Barometer" compact={compact}>
        <ResponsiveContainer width="100%" height="100%">
          <LineChart {...commonChartProps(history)}>
            <CartesianGrid strokeDasharray="3 3" />
            <XAxis dataKey="pc_receive_time_iso" tickFormatter={formatTime} />
            <YAxis width={45} />
            <Tooltip labelFormatter={(label) => formatTime(label)} />
            <Legend />
            <Line
              type="monotone"
              dataKey="baro_pressure_pa"
              name="Pressure [Pa]"
              dot={false}
              stroke="#7c3aed"
            />
            <Line
              type="monotone"
              dataKey="baro_temperature_c"
              name="Baro temp [°C]"
              dot={false}
              stroke="#dc2626"
            />
          </LineChart>
        </ResponsiveContainer>
      </ChartCard>

      <ChartCard title="Acceleration" compact={compact}>
        <ResponsiveContainer width="100%" height="100%">
          <LineChart {...commonChartProps(history)}>
            <CartesianGrid strokeDasharray="3 3" />
            <XAxis dataKey="pc_receive_time_iso" tickFormatter={formatTime} />
            <YAxis width={45} />
            <Tooltip labelFormatter={(label) => formatTime(label)} />
            <Legend />
            <Line
              type="monotone"
              dataKey="accel_x_ms2"
              name="X"
              dot={false}
              stroke="#2563eb"
            />
            <Line
              type="monotone"
              dataKey="accel_y_ms2"
              name="Y"
              dot={false}
              stroke="#16a34a"
            />
            <Line
              type="monotone"
              dataKey="accel_z_ms2"
              name="Z"
              dot={false}
              stroke="#dc2626"
            />
          </LineChart>
        </ResponsiveContainer>
      </ChartCard>

      <ChartCard title="Link Quality" compact={compact}>
        <ResponsiveContainer width="100%" height="100%">
          <LineChart {...commonChartProps(history)}>
            <CartesianGrid strokeDasharray="3 3" />
            <XAxis dataKey="pc_receive_time_iso" tickFormatter={formatTime} />
            <YAxis width={45} />
            <Tooltip labelFormatter={(label) => formatTime(label)} />
            <Legend />
            <Line
              type="monotone"
              dataKey="lora_downlink_rssi_dbm"
              name="RX RSSI"
              dot={false}
              stroke="#9333ea"
            />
            <Line
              type="monotone"
              dataKey="lora_downlink_snr_db"
              name="RX SNR"
              dot={false}
              stroke="#0f766e"
            />
          </LineChart>
        </ResponsiveContainer>
      </ChartCard>

      <ChartCard title="Speed" compact={compact}>
        <ResponsiveContainer width="100%" height="100%">
          <LineChart {...commonChartProps(history)}>
            <CartesianGrid strokeDasharray="3 3" />
            <XAxis dataKey="pc_receive_time_iso" tickFormatter={formatTime} />
            <YAxis width={45} />
            <Tooltip labelFormatter={(label) => formatTime(label)} />
            <Legend />
            <Line
              type="monotone"
              dataKey="ground_speed_ms"
              name="Ground [m/s]"
              dot={false}
              stroke="#2563eb"
            />
            <Line
              type="monotone"
              dataKey="vertical_speed_ms"
              name="Vertical [m/s]"
              dot={false}
              stroke="#dc2626"
            />
          </LineChart>
        </ResponsiveContainer>
      </ChartCard>
    </div>
  )
}