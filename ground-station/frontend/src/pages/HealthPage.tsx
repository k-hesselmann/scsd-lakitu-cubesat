import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card"
import { Badge } from "@/components/ui/badge"
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
  const receiver = backendStatus?.receiver
  const flightRxActive = latest?.lora_rx_mode_active === 1
  const ackState = receiver?.last_telemetry_ack_ok
  return (
    <div className="flex h-full w-full flex-col overflow-hidden p-3">
      <header className="mb-2 flex shrink-0 flex-wrap items-center justify-between gap-2">
        <h1 className="text-xl font-bold tracking-tight">Mission Health</h1>
        <div className="flex flex-wrap items-center gap-1.5 text-[10px]">
          <Badge variant={connected ? "default" : "destructive"}>Backend {connected ? "ONLINE" : "OFFLINE"}</Badge>
          <Badge variant={receiver?.radio_initialized ? "default" : "destructive"}>
            Ground radio {receiver?.radio_initialized ? "READY" : "NOT READY"}
          </Badge>
          <Badge variant={flightRxActive ? "default" : latest ? "destructive" : "outline"}>
            Flight RX {flightRxActive ? "ACTIVE" : latest ? "INACTIVE" : "UNKNOWN"}
          </Badge>
          <Badge variant={ackState === true ? "default" : ackState === false ? "destructive" : "outline"}>
            ACK {ackState === true ? `TX DONE #${receiver?.last_telemetry_ack_sequence}` : ackState === false ? "FAILED" : "WAITING"}
          </Badge>
        </div>
      </header>

      <div className="grid min-h-0 flex-1 grid-cols-1 gap-2 xl:grid-cols-[minmax(0,1.75fr)_minmax(340px,0.85fr)]">
        <Card size="sm" className="min-h-0 gap-2 overflow-hidden py-2">
          <CardHeader className="px-3">
            <CardTitle className="text-sm">
              TT&amp;C / FSW System Architecture Status
            </CardTitle>
          </CardHeader>
          <CardContent className="min-h-0 flex-1 px-3">
            <SystemArchitectureDiagram latest={latest} backendStatus={backendStatus} />
          </CardContent>
        </Card>

        <div className="grid min-h-0 grid-rows-[minmax(0,1.1fr)_minmax(0,0.9fr)] gap-2">
          <Card size="sm" className="min-h-0 gap-2 overflow-hidden py-2">
            <CardHeader className="px-3">
              <CardTitle className="text-sm">Packet / Command Timeline</CardTitle>
            </CardHeader>
            <CardContent className="min-h-0 flex-1 px-3">
              <TimelinePanel events={events} />
            </CardContent>
          </Card>

          <Card size="sm" className="min-h-0 gap-2 overflow-hidden py-2">
            <CardHeader className="px-3">
              <CardTitle className="text-sm">Mission Health Alerts</CardTitle>
            </CardHeader>
            <CardContent className="min-h-0 flex-1 overflow-auto px-3">
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