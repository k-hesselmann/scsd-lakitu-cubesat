import { AlertsPanel } from "@/components/AlertsPanel"
import { ExpandablePanelCard } from "@/components/ExpandablePanelCard"
import { SystemArchitectureDiagram } from "@/components/SystemArchitectureDiagram"
import { TimelinePanel } from "@/components/TimelinePanel"
import type { DashboardPageProps } from "@/pages/pageTypes"

export function HealthPage({
  latest,
  events,
  backendStatus,
  connected,
  error,
}: DashboardPageProps) {
  return (
    <div className="flex h-full w-full flex-col overflow-hidden p-3">
      <header className="mb-2 shrink-0">
        <h1 className="text-xl font-bold tracking-tight">Mission Health</h1>
      </header>

      <div className="grid min-h-0 flex-1 grid-cols-1 gap-2 xl:grid-cols-[minmax(0,1.75fr)_minmax(340px,0.85fr)]">
        <ExpandablePanelCard
          title="System Architecture"
          size="sm"
          className="gap-2 py-2"
          headerClassName="px-3"
          contentClassName="flex-1 px-0"
          expandedContentClassName="h-full"
        >
          <SystemArchitectureDiagram
            latest={latest}
            backendStatus={backendStatus}
            connected={connected}
            frontendError={error}
          />
        </ExpandablePanelCard>

        <div className="grid min-h-0 grid-rows-[minmax(0,1.1fr)_minmax(0,0.9fr)] gap-2">
          <ExpandablePanelCard
            title="Packet / Command Timeline"
            size="sm"
            className="gap-2 py-2"
            headerClassName="px-3"
            contentClassName="flex-1 px-3"
            expandedContentClassName="h-full p-1"
          >
            <TimelinePanel events={events} />
          </ExpandablePanelCard>

          <ExpandablePanelCard
            title="Mission Health Alerts"
            size="sm"
            className="gap-2 py-2"
            headerClassName="px-3"
            contentClassName="flex-1 overflow-auto px-3"
            expandedContentClassName="overflow-auto p-1"
          >
            <AlertsPanel
              latest={latest}
              backendStatus={backendStatus}
              connected={connected}
              error={error}
            />
          </ExpandablePanelCard>
        </div>
      </div>
    </div>
  )
}
