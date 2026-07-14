import type { BackendStatus, TelemetryRow, TimelineEvent } from "@/types/telemetry"

export type DashboardPageProps = {
  latest: TelemetryRow | null
  history: TelemetryRow[]
  events: TimelineEvent[]
  backendStatus: BackendStatus | null
  connected: boolean
  lastMessageType: string
  error: string | null
}