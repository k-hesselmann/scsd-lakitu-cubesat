import { useMemo, type ReactNode } from "react"

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

import { ExpandableChartCard } from "@/components/ExpandableChartCard"
import { adaptiveAxisDomain } from "@/lib/chartAxis"
import { gnssFixIsValid } from "@/lib/telemetryHealth"
import type { TelemetryRow } from "@/types/telemetry"
import { latestValues } from "@/lib/telemetrySeries"

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
    <ExpandableChartCard title={title} compact={compact}>
      {children}
    </ExpandableChartCard>
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
    margin: { top: 2, right: 4, bottom: 0, left: 0 },
  }
}

function unitTick(unit: string, decimals = 0) {
  return (value: number) => `${Number(value).toFixed(decimals)} ${unit}`
}

function pressureTick(value: number) {
  return `${(Number(value) / 1000).toFixed(0)} kPa`
}

function truncateToDecimals(
  value: number | undefined,
  decimals: number,
): number | undefined
function truncateToDecimals(
  value: number | null | undefined,
  decimals: number,
): number | null | undefined
function truncateToDecimals(
  value: number | null | undefined,
  decimals: number,
) {
  if (typeof value !== "number" || !Number.isFinite(value)) return value

  const factor = 10 ** decimals
  return Math.trunc(value * factor) / factor
}

function decimalTooltip(unit: string, decimals: number) {
  return (value: unknown) => {
    const numericValue = Number(value)

    return Number.isFinite(numericValue)
      ? `${numericValue.toFixed(decimals)} ${unit}`
      : "—"
  }
}

export function TelemetryCharts({
  history,
  compact = false,
}: {
  history: TelemetryRow[]
  compact?: boolean
}) {
  const chartHistory = useMemo(
    () => latestValues(history).map((row) => {
      const chartRow = gnssFixIsValid(row) ? row : {
        ...row,
        gnss_altitude_m: undefined,
        ground_speed_ms: undefined,
        vertical_speed_ms: undefined,
      }

      return {
        ...chartRow,
        accel_x_ms2: truncateToDecimals(chartRow.accel_x_ms2, 3),
        accel_y_ms2: truncateToDecimals(chartRow.accel_y_ms2, 3),
        accel_z_ms2: truncateToDecimals(chartRow.accel_z_ms2, 3),
        coral_fraction_percent: truncateToDecimals(
          chartRow.coral_fraction_percent,
          3,
        ),
      }
    }),
    [history],
  )
  const gridClass = compact
    ? "grid h-full min-h-0 grid-cols-1 grid-rows-6 gap-2 p-px lg:grid-cols-2 lg:grid-rows-3 2xl:grid-cols-3 2xl:grid-rows-2"
    : "grid gap-4 xl:grid-cols-2"

  if (history.length === 0) {
    return (
      <div className={gridClass}>
        <EmptyChart title="Battery Voltage" compact={compact} />
        <EmptyChart title="Altitude" compact={compact} />
        <EmptyChart title="Barometer" compact={compact} />
        <EmptyChart title="Acceleration" compact={compact} />
        <EmptyChart title="Coral Cloud Fraction" compact={compact} />
        <EmptyChart title="Speed" compact={compact} />
      </div>
    )
  }

  return (
    <div className={gridClass}>
      <ChartCard title="Battery Voltage [V]" compact={compact}>
        <ResponsiveContainer width="100%" height="100%">
          <LineChart {...commonChartProps(chartHistory)}>
            <CartesianGrid strokeDasharray="3 3" />
            <XAxis dataKey="pc_receive_time_iso" tickFormatter={formatTime} />
            <YAxis
              width={62}
              domain={adaptiveAxisDomain(0.05)}
              tickFormatter={unitTick("V", 2)}
              tick={{ fontSize: 10 }}
            />
            <Tooltip labelFormatter={(label) => formatTime(label)} />
            <Legend />
            <Line
              type="monotone"
              dataKey="battery_v"
              name="Battery [V]"
              dot={false}
              isAnimationActive={false}
              stroke="#2563eb"
            />
          </LineChart>
        </ResponsiveContainer>
      </ChartCard>

      <ChartCard title="Altitude [m]" compact={compact}>
        <ResponsiveContainer width="100%" height="100%">
          <LineChart {...commonChartProps(chartHistory)}>
            <CartesianGrid strokeDasharray="3 3" />
            <XAxis dataKey="pc_receive_time_iso" tickFormatter={formatTime} />
            <YAxis domain={adaptiveAxisDomain(5)} width={56} tickFormatter={unitTick("m")} tick={{ fontSize: 10 }} />
            <Tooltip labelFormatter={(label) => formatTime(label)} />
            <Legend />
            <Line
              type="monotone"
              dataKey="gnss_altitude_m"
              name="GNSS MSL [m]"
              dot={false}
              isAnimationActive={false}
              stroke="#16a34a"
            />
            <Line
              type="monotone"
              dataKey="baro_altitude_m"
              name="Baro relative [m]"
              dot={false}
              isAnimationActive={false}
              stroke="#ea580c"
            />
          </LineChart>
        </ResponsiveContainer>
      </ChartCard>

      <ChartCard title="Barometer: pressure [Pa] / temperature [deg C]" compact={compact}>
        <ResponsiveContainer width="100%" height="100%">
          <LineChart {...commonChartProps(chartHistory)}>
            <CartesianGrid strokeDasharray="3 3" />
            <XAxis dataKey="pc_receive_time_iso" tickFormatter={formatTime} />
            <YAxis yAxisId="pressure" orientation="left" domain={adaptiveAxisDomain(1000)} width={62} tickFormatter={pressureTick} tick={{ fontSize: 10, fill: "#7c3aed" }} axisLine={{ stroke: "#7c3aed" }} tickLine={{ stroke: "#7c3aed" }} />
            <YAxis yAxisId="temperature" orientation="right" domain={adaptiveAxisDomain(1)} width={58} tickFormatter={unitTick("deg C")} tick={{ fontSize: 10, fill: "#dc2626" }} axisLine={{ stroke: "#dc2626" }} tickLine={{ stroke: "#dc2626" }} />
            <Tooltip labelFormatter={(label) => formatTime(label)} />
            <Legend />
            <Line
              type="monotone"
              yAxisId="pressure"
              dataKey="baro_pressure_pa"
              name="Pressure [Pa]"
              dot={false}
              isAnimationActive={false}
              stroke="#7c3aed"
            />
            <Line
              type="monotone"
              yAxisId="temperature"
              dataKey="baro_temperature_c"
              name="Baro temp [°C]"
              dot={false}
              isAnimationActive={false}
              stroke="#dc2626"
            />
          </LineChart>
        </ResponsiveContainer>
      </ChartCard>

      <ChartCard title="Acceleration [m/s^2]" compact={compact}>
        <ResponsiveContainer width="100%" height="100%">
          <LineChart {...commonChartProps(chartHistory)}>
            <CartesianGrid strokeDasharray="3 3" />
            <XAxis dataKey="pc_receive_time_iso" tickFormatter={formatTime} />
            <YAxis domain={adaptiveAxisDomain(0.1)} width={82} tickFormatter={unitTick("m/s^2", 3)} tick={{ fontSize: 10 }} />
            <Tooltip
              labelFormatter={(label) => formatTime(label)}
              formatter={decimalTooltip("m/s^2", 3)}
            />
            <Legend />
            <Line
              type="monotone"
              dataKey="accel_x_ms2"
              name="X [m/s^2]"
              dot={false}
              isAnimationActive={false}
              stroke="#2563eb"
            />
            <Line
              type="monotone"
              dataKey="accel_y_ms2"
              name="Y [m/s^2]"
              dot={false}
              isAnimationActive={false}
              stroke="#16a34a"
            />
            <Line
              type="monotone"
              dataKey="accel_z_ms2"
              name="Z [m/s^2]"
              dot={false}
              isAnimationActive={false}
              stroke="#dc2626"
            />
          </LineChart>
        </ResponsiveContainer>
      </ChartCard>

      <ChartCard title="Coral Cloud Fraction [%]" compact={compact}>
        <ResponsiveContainer width="100%" height="100%">
          <LineChart {...commonChartProps(chartHistory)}>
            <CartesianGrid strokeDasharray="3 3" />
            <XAxis dataKey="pc_receive_time_iso" tickFormatter={formatTime} />
            <YAxis
              width={64}
              domain={adaptiveAxisDomain(1, 0, 100)}
              tickFormatter={unitTick("%", 3)}
              tick={{ fontSize: 10 }}
            />
            <Tooltip
              labelFormatter={(label) => formatTime(label)}
              formatter={decimalTooltip("%", 3)}
            />
            <Legend />
            <Line
              type="monotone"
              dataKey="coral_fraction_percent"
              name="Cloud fraction [%]"
              dot={false}
              isAnimationActive={false}
              stroke="#9333ea"
            />
          </LineChart>
        </ResponsiveContainer>
      </ChartCard>

      <ChartCard title="Speed [m/s]" compact={compact}>
        <ResponsiveContainer width="100%" height="100%">
          <LineChart {...commonChartProps(chartHistory)}>
            <CartesianGrid strokeDasharray="3 3" />
            <XAxis dataKey="pc_receive_time_iso" tickFormatter={formatTime} />
            <YAxis domain={adaptiveAxisDomain(0.5)} width={60} tickFormatter={unitTick("m/s", 1)} tick={{ fontSize: 10 }} />
            <Tooltip labelFormatter={(label) => formatTime(label)} />
            <Legend />
            <Line
              type="monotone"
              dataKey="ground_speed_ms"
              name="Ground [m/s]"
              dot={false}
              isAnimationActive={false}
              stroke="#2563eb"
            />
            <Line
              type="monotone"
              dataKey="vertical_speed_ms"
              name="Vertical [m/s]"
              dot={false}
              isAnimationActive={false}
              stroke="#dc2626"
            />
          </LineChart>
        </ResponsiveContainer>
      </ChartCard>
    </div>
  )
}
