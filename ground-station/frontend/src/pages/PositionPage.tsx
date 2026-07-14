import { FlightPhasePanel } from "@/components/FlightPhasePanel"
import { PositionPanel } from "@/components/PositionPanel"
import type { DashboardPageProps } from "@/pages/pageTypes"

export function PositionPage({ latest, history }: DashboardPageProps) {
  return (
    <div className="flex h-full w-full flex-col overflow-hidden p-3">
      <header className="mb-2 shrink-0">
        <h1 className="text-xl font-bold tracking-tight">
          Position / Flight Phase
        </h1>
      </header>

      <div className="grid min-h-0 flex-1 grid-rows-[220px_minmax(0,1fr)] gap-3">
        <FlightPhasePanel latest={latest} history={history} />
        <PositionPanel latest={latest} history={history} />
      </div>
    </div>
  )
}