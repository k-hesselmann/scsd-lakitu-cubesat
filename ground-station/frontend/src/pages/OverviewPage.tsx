import { MetricCard } from "@/components/MetricCard"
import { TelemetryCharts } from "@/components/TelemetryCharts"
import { fmt, formatGnssUtc } from "@/lib/format"
import { rawFlagIsValid } from "@/lib/telemetryHealth"
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
  const gpsValid = rawFlagIsValid(latest?.gps_valid_raw)
  const coralValid = rawFlagIsValid(latest?.coral_valid_raw)
  const fixType = gpsValid ? latest?.gnss_fix_type : null
  const fixName = typeof fixType === "number"
    ? (GNSS_FIX_NAMES[fixType] ?? `TYPE ${fixType}`)
    : "\u2014"

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
          subtitle={
            gpsValid
              ? `${fmt(latest?.gnss_satellites_used)} satellites`
              : "GNSS sample invalid"
          }
          variant={!latest ? "default" : fixType === 2 || fixType === 3 || fixType === 4 ? "good" : "warning"}
        />
        <MetricCard
          title="GNSS UTC"
          value={formatGnssUtc(gpsValid ? latest?.gnss_utc_sod : null)}
          subtitle="Seconds-of-day field"
        />
        <MetricCard
          title="GNSS Course"
          value={fmt(gpsValid ? latest?.course_deg : null, "\u00B0", 2)}
          subtitle="Course over ground"
        />
        <MetricCard
          title="Coral Cloud Fraction"
          value={fmt(coralValid ? latest?.coral_fraction_percent : null, "%", 2)}
          subtitle={`Sequence ${fmt(latest?.coral_sequence_low)}`}
        />
        <MetricCard
          title="Coral Result Age"
          value={fmt(coralValid ? latest?.coral_result_age_s : null, " s")}
          subtitle={`Status ${fmt(latest?.coral_status)}`}
        />
      </section>

      <div className="min-h-0 flex-1 overflow-hidden">
        <TelemetryCharts history={history} compact />
      </div>
    </div>
  )
}
