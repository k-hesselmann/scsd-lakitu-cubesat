import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert"
import { Badge } from "@/components/ui/badge"
import { buildMissionAlerts } from "@/lib/alerts"
import { useAlertThresholds } from "@/hooks/useAlertThresholds"
import type { BackendStatus, TelemetryRow } from "@/types/telemetry"

export function AlertsPanel({
  latest,
  backendStatus,
  connected,
  error,
}: {
  latest: TelemetryRow | null
  backendStatus: BackendStatus | null
  connected: boolean
  error: string | null
}) {
  const { thresholds } = useAlertThresholds()

  const alerts = buildMissionAlerts({
    latest,
    backendStatus,
    connected,
    frontendError: error,
    thresholds,
  })

  if (alerts.length === 0) {
    return (
      <Alert>
        <AlertTitle>Mission health nominal</AlertTitle>
        <AlertDescription>
          No active dashboard, telemetry, radio, or mission-health alerts.
        </AlertDescription>
      </Alert>
    )
  }

  return (
    <div className="space-y-3">
      {alerts.map((alert) => (
        <Alert
          key={alert.id}
          variant={alert.level === "critical" ? "destructive" : "default"}
          className={alert.level === "critical" ? "animate-pulse" : ""}
        >
          <div className="mb-1 flex items-center gap-2">
            <Badge
              variant={
                alert.level === "critical"
                  ? "destructive"
                  : alert.level === "warning"
                    ? "secondary"
                    : "default"
              }
            >
              {alert.level.toUpperCase()}
            </Badge>
          </div>

          <AlertTitle>{alert.title}</AlertTitle>
          <AlertDescription>{alert.message}</AlertDescription>
        </Alert>
      ))}
    </div>
  )
}