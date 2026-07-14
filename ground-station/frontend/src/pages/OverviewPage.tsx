import { TelemetryCharts } from "@/components/TelemetryCharts"
import type { DashboardPageProps } from "@/pages/pageTypes"

export function OverviewPage({ history }: DashboardPageProps) {
  return (
    <div className="flex h-full w-full flex-col overflow-hidden p-3">
      <header className="mb-2 shrink-0">
        <h1 className="text-xl font-bold tracking-tight">
          Science Data Overview
        </h1>
      </header>

      <div className="min-h-0 flex-1 overflow-hidden">
        <TelemetryCharts history={history} compact />
      </div>
    </div>
  )
}