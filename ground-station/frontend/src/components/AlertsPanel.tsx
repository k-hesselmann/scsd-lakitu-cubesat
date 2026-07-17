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
    return <Alert><AlertTitle>Mission health nominal</AlertTitle><AlertDescription>No active dashboard, telemetry, radio, or mission-health alerts.</AlertDescription></Alert>
  }

  return <div className="space-y-3">
    {alerts.map((alert) => <Alert key={alert.id} variant={alert.level === "critical" ? "destructive" : "default"} className={alert.level === "critical" ? "animate-pulse" : ""}>
      <div className="mb-1 flex items-center gap-2"><Badge variant={alert.level === "critical" ? "destructive" : "secondary"}>{alert.level.toUpperCase()}</Badge></div>
      <AlertTitle>{alert.title}</AlertTitle><AlertDescription>{alert.message}</AlertDescription>
    </Alert>)}
  </div>
}
