import { useMemo } from "react"

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

function formatTime(value: unknown) {
  if (typeof value !== "string") return ""
  const date = new Date(value)
  return Number.isNaN(date.getTime()) ? "" : date.toLocaleTimeString()
}

function packetRatePerMinute(history: DashboardPageProps["history"]) {
  if (history.length < 2) return null

  const first = history[0].pc_receive_time_unix
  const last = history[history.length - 1].pc_receive_time_unix

  if (!first || !last || last <= first) return null

  const minutes = (last - first) / 60
  return history.length / minutes
}

export function LinkAnalyticsPage({
  latest,
  history,
  backendStatus,
}: DashboardPageProps) {
  const stats = backendStatus?.stats
  const packets = stats?.total_packets_received ?? history.length
  const lost = stats?.total_lost_packets ?? latest?.total_lost_packets ?? 0
  const totalExpected = packets + lost
  const lossPercent = totalExpected > 0 ? (lost / totalExpected) * 100 : null
  const rate = packetRatePerMinute(history)
  const chartHistory = useMemo(() => latestValues(history), [history])

  return (
    <div className="flex h-full w-full flex-col overflow-hidden p-3">
      <header className="mb-2 shrink-0">
        <h1 className="text-xl font-bold tracking-tight">Packet-Quality Analytics</h1>
      </header>

      <section className="mb-4 grid shrink-0 gap-4 md:grid-cols-2 xl:grid-cols-6">
        <MetricCard
          title="Packets RX"
          value={fmt(packets)}
          subtitle="Decoded telemetry"
        />

        <MetricCard
          title="Estimated Lost"
          value={fmt(lost)}
          subtitle="Sequence gap estimate"
          variant={lost > 0 ? "warning" : "good"}
        />

        <MetricCard
          title="Packet Loss"
          value={lossPercent === null ? "—" : `${lossPercent.toFixed(2)}%`}
          subtitle="Lost / expected"
          variant={lossPercent && lossPercent > 5 ? "bad" : lossPercent && lossPercent > 1 ? "warning" : "good"}
        />

        <MetricCard
          title="RX Rate"
          value={rate === null ? "—" : `${rate.toFixed(1)}/min`}
          subtitle="Ground receive rate"
        />

        <MetricCard
          title="RSSI"
          value={fmt(latest?.lora_downlink_rssi_dbm, " dBm")}
          subtitle="Latest downlink"
        />

        <MetricCard
          title="SNR"
          value={fmt(latest?.lora_downlink_snr_db, " dB", 1)}
          subtitle="Latest downlink"
        />
      </section>

      <div className="grid min-h-0 flex-1 gap-4 xl:grid-cols-2">
        <Card className="min-h-0 overflow-hidden">
          <CardHeader className="px-4 py-3">
            <CardTitle className="text-base">RSSI [dBm] / SNR [dB] Trend</CardTitle>
          </CardHeader>
          <CardContent className="h-[calc(100%-56px)] px-4 pb-4">
            <ResponsiveContainer width="100%" height="100%">
              <LineChart data={chartHistory}>
                <CartesianGrid strokeDasharray="3 3" />
                <XAxis dataKey="pc_receive_time_iso" tickFormatter={formatTime} />
                <YAxis
                  yAxisId="rssi"
                  orientation="left"
                  domain={adaptiveAxisDomain(2)}
                  width={62}
                  tickFormatter={(value) => `${Number(value).toFixed(0)} dBm`}
                  tick={{ fill: "#9333ea" }}
                  axisLine={{ stroke: "#9333ea" }}
                  tickLine={{ stroke: "#9333ea" }}
                />
                <YAxis
                  yAxisId="snr"
                  orientation="right"
                  domain={adaptiveAxisDomain(1)}
                  width={54}
                  tickFormatter={(value) => `${Number(value).toFixed(1)} dB`}
                  tick={{ fill: "#0f766e" }}
                  axisLine={{ stroke: "#0f766e" }}
                  tickLine={{ stroke: "#0f766e" }}
                />
                <Tooltip labelFormatter={(label) => formatTime(label)} />
                <Line
                  type="monotone"
                  yAxisId="rssi"
                  dataKey="lora_downlink_rssi_dbm"
                  name="Downlink RSSI [dBm]"
                  dot={false}
                  isAnimationActive={false}
                  stroke="#9333ea"
                />
                <Line
                  type="monotone"
                  yAxisId="snr"
                  dataKey="lora_downlink_snr_db"
                  name="Downlink SNR [dB]"
                  dot={false}
                  isAnimationActive={false}
                  stroke="#0f766e"
                />
              </LineChart>
            </ResponsiveContainer>
          </CardContent>
        </Card>

        <Card className="min-h-0 overflow-hidden">
          <CardHeader className="px-4 py-3">
            <CardTitle className="text-base">Packet Loss [packets]</CardTitle>
          </CardHeader>
          <CardContent className="h-[calc(100%-56px)] px-4 pb-4">
            <ResponsiveContainer width="100%" height="100%">
              <LineChart data={chartHistory}>
                <CartesianGrid strokeDasharray="3 3" />
                <XAxis dataKey="pc_receive_time_iso" tickFormatter={formatTime} />
                <YAxis domain={adaptiveAxisDomain(1, 0)} />
                <Tooltip labelFormatter={(label) => formatTime(label)} />
                <Line
                  type="stepAfter"
                  dataKey="total_lost_packets"
                  name="Total lost packets [count]"
                  dot={false}
                  isAnimationActive={false}
                  stroke="#dc2626"
                />
              </LineChart>
            </ResponsiveContainer>
          </CardContent>
        </Card>
      </div>
    </div>
  )
}
