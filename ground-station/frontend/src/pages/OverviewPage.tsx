import { MetricCard } from "@/components/MetricCard"
import { TelemetryCharts } from "@/components/TelemetryCharts"
import { fmt, formatGnssUtc } from "@/lib/format"
import { gnssFixIsValid } from "@/lib/telemetryHealth"
import type { DashboardPageProps } from "@/pages/pageTypes"

const GNSS_FIX_NAMES: Record<number, string> = {
  0: "NO FIX",
  1: "DEAD RECKONING",
  2: "2D FIX",
  3: "3D FIX",
  4: "GNSS + DR",
  5: "TIME ONLY",
}

export function OverviewPage({ latest, history }: DashboardPageProps) {
  const fixType = latest?.gnss_fix_type
  const fixName = typeof fixType === "number"
    ? (GNSS_FIX_NAMES[fixType] ?? `TYPE ${fixType}`)
    : "\u2014"
  const gnssFixValid = gnssFixIsValid(latest)

  return (
    <div className="flex h-full w-full flex-col overflow-hidden p-3">
      <header className="mb-2 shrink-0">
        <h1 className="text-xl font-bold tracking-tight">
          Science Data Overview
        </h1>
      </header>

      <section className="mb-3 grid shrink-0 gap-3 sm:grid-cols-2 xl:grid-cols-6">
        <MetricCard
          title="Sensor Sample Age"
          value={fmt(latest?.sample_age_ms, " ms")}
          subtitle="Age when packet was built"
        />
        <MetricCard
          title="GNSS Fix"
          value={fixName}
          subtitle={`${fmt(latest?.gnss_satellites_used)} satellites \u00B7 ${gnssFixValid ? "3D solution usable" : "no usable 3D solution"}`}
          variant={!latest ? "default" : gnssFixValid ? "good" : "warning"}
        />
        <MetricCard
          title="GNSS UTC"
          value={gnssFixValid ? formatGnssUtc(latest?.gnss_utc_sod) : "\u2014"}
          subtitle="Shown only for a current 3D fix"
        />
        <MetricCard
          title="GNSS Course"
          value={gnssFixValid ? fmt(latest?.course_deg, "\u00B0", 2) : "\u2014"}
          subtitle="Current 3D-fix course over ground"
        />
        <MetricCard
          title="Coral Cloud Fraction"
          value={fmt(latest?.coral_fraction_percent, "%", 2)}
          subtitle={`Sequence ${fmt(latest?.coral_sequence_low)}`}
        />
        <MetricCard
          title="Coral Result Age"
          value={fmt(latest?.coral_result_age_s, " s")}
          subtitle={`Status ${fmt(latest?.coral_status)}`}
        />
      </section>

      <div className="min-h-0 flex-1 overflow-hidden">
        <TelemetryCharts history={history} compact />
      </div>
    </div>
  )
}
