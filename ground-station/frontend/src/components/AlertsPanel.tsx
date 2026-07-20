import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert"
import { Badge } from "@/components/ui/badge"
import { useAlertThresholds } from "@/hooks/useAlertThresholds"
import { useCurrentTime } from "@/hooks/useCurrentTime"
import { buildMissionAlerts } from "@/lib/alerts"
import type { BackendStatus, TelemetryRow } from "@/types/telemetry"

export function AlertsPanel({ latest, backendStatus, connected, error }: {
  latest: TelemetryRow | null
  backendStatus: BackendStatus | null
  connected: boolean
  error: string | null
}) {
  const { thresholds } = useAlertThresholds()
  const nowMs = useCurrentTime()
  const alerts = buildMissionAlerts({ latest, backendStatus, connected, frontendError: error, thresholds, nowMs })

  if (alerts.length === 0) {
    return <Alert className="py-1.5"><AlertTitle className="text-xs">Mission health nominal</AlertTitle><AlertDescription className="text-xs">No active dashboard, telemetry, radio, or mission-health alerts.</AlertDescription></Alert>
  }

  return <div className="space-y-1.5">
    {alerts.map((alert) => <Alert key={alert.id} variant={alert.level === "critical" ? "destructive" : "default"} className={alert.level === "critical" ? "animate-pulse py-1.5" : "py-1.5"}>
      <div className="mb-0.5 flex items-center gap-1"><Badge className="h-4 px-1 text-[9px]" variant={alert.level === "critical" ? "destructive" : "secondary"}>{alert.level.toUpperCase()}</Badge></div>
      <AlertTitle className="text-xs">{alert.title}</AlertTitle><AlertDescription className="text-xs leading-4">{alert.message}</AlertDescription>
    </Alert>)}
  </div>
}
