import { useEffect, useRef } from "react"

import { API_BASE_URL } from "@/lib/config"
import type { MissionAlert } from "@/lib/alerts"

function postGroundEvent(body: {
  event_type: string
  severity: "info" | "warning" | "critical"
  message: string
  details: Record<string, unknown>
}) {
  void fetch(`${API_BASE_URL}/api/ground-event`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  }).catch(() => {
    // If the backend is unreachable, there is nowhere to persist this event.
  })
}

export function useGroundEventLogger(
  alerts: MissionAlert[],
  enabled: boolean,
) {
  const previousAlerts = useRef(new Map<string, MissionAlert>())

  useEffect(() => {
    if (!enabled) return

    const currentAlerts = new Map(alerts.map((alert) => [alert.id, alert]))

    for (const [id, alert] of currentAlerts) {
      if (!previousAlerts.current.has(id)) {
        postGroundEvent({
          event_type: "alert_activated",
          severity: alert.level,
          message: alert.message,
          details: {
            alert_id: alert.id,
            title: alert.title,
          },
        })
      }
    }

    for (const [id, alert] of previousAlerts.current) {
      if (!currentAlerts.has(id)) {
        postGroundEvent({
          event_type: "alert_cleared",
          severity: "info",
          message: `Cleared: ${alert.title}`,
          details: {
            alert_id: alert.id,
            previous_level: alert.level,
            previous_message: alert.message,
          },
        })
      }
    }

    previousAlerts.current = currentAlerts
  }, [alerts, enabled])
}
