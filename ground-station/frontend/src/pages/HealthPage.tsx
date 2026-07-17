import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card"
import { AlertsPanel } from "@/components/AlertsPanel"
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

      <div className="grid min-h-0 flex-1 grid-cols-1 gap-4 xl:grid-cols-[minmax(0,1.65fr)_minmax(390px,1fr)]">
        <Card className="min-h-0 overflow-hidden">
          <CardHeader className="px-4 py-3">
            <CardTitle className="text-base">
              TT&amp;C / FSW System Architecture Status
            </CardTitle>
          </CardHeader>
          <CardContent className="h-[calc(100%-56px)] px-4 pb-4">
            <SystemArchitectureDiagram latest={latest} backendStatus={backendStatus} />
          </CardContent>
        </Card>

        <div className="grid min-h-0 grid-rows-[minmax(0,1fr)_minmax(0,1fr)] gap-4">
          <Card className="min-h-0 overflow-hidden">
            <CardHeader className="px-4 py-3">
              <CardTitle className="text-base">Packet / Command Timeline</CardTitle>
            </CardHeader>
            <CardContent className="h-[calc(100%-56px)] px-4 pb-4">
              <TimelinePanel events={events} />
            </CardContent>
          </Card>

          <Card className="min-h-0 overflow-hidden">
            <CardHeader className="px-4 py-3">
              <CardTitle className="text-base">Mission Health Alerts</CardTitle>
            </CardHeader>
            <CardContent className="h-[calc(100%-56px)] overflow-auto px-4 pb-4">
              <AlertsPanel
                latest={latest}
                backendStatus={backendStatus}
                connected={connected}
                error={error}
              />
            </CardContent>
          </Card>
        </div>
      </div>
    </div>
  )
}