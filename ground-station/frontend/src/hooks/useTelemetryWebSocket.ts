import { useEffect, useState } from "react"
import { API_BASE_URL, WS_URL } from "@/lib/config"
import type {
  BackendStatus,
  TelemetryRow,
  TelemetryWebSocketMessage,
  TimelineEvent,
} from "@/types/telemetry"

function validUnixTime(value: unknown) {
  return typeof value === "number" && Number.isFinite(value) && value > 0
}

function eventId(prefix: string) {
  return `${prefix}-${Date.now()}-${Math.random().toString(16).slice(2)}`
}

function telemetryEventsFromRow(row: TelemetryRow): TimelineEvent[] {
  const events: TimelineEvent[] = []

  if (validUnixTime(row.utc_timestamp)) {
    const txUnix = Number(row.utc_timestamp)

    events.push({
      id: eventId("telemetry-tx"),
      type: "telemetry_tx",
      time_iso: new Date(txUnix * 1000).toISOString(),
      time_unix: txUnix,
      title: "Telemetry TX",
      description: `Flight packet transmitted, seq ${row.sequence_number ?? "—"}`,
      sequence_number: row.sequence_number,
      severity: "info",
    })
  }

  if (validUnixTime(row.pc_receive_time_unix)) {
    events.push({
      id: eventId("telemetry-rx"),
      type: "telemetry_rx",
      time_iso: row.pc_receive_time_iso ?? new Date(Number(row.pc_receive_time_unix) * 1000).toISOString(),
      time_unix: Number(row.pc_receive_time_unix),
      title: "Telemetry RX",
      description: `Ground station received packet, seq ${row.sequence_number ?? "—"}`,
      sequence_number: row.sequence_number,
      severity: row.crc_ok === false ? "critical" : "info",
    })
  }

  return events
}

function sortAndLimitEvents(events: TimelineEvent[], limit: number) {
  return [...events]
    .sort((a, b) => b.time_unix - a.time_unix)
    .slice(0, limit)
}

export function useTelemetryWebSocket(historyLimit = 500, eventLimit = 300) {
  const [latest, setLatest] = useState<TelemetryRow | null>(null)
  const [history, setHistory] = useState<TelemetryRow[]>([])
  const [events, setEvents] = useState<TimelineEvent[]>([])
  const [backendStatus, setBackendStatus] = useState<BackendStatus | null>(null)
  const [connected, setConnected] = useState(false)
  const [lastMessageType, setLastMessageType] = useState<string>("none")
  const [error, setError] = useState<string | null>(null)

  useEffect(() => {
    let cancelled = false
    let ws: WebSocket | null = null
    let reconnectTimer: number | undefined
    let pingTimer: number | undefined

    async function loadInitialData() {
      try {
        const [historyResponse, statusResponse] = await Promise.all([
          fetch(`${API_BASE_URL}/api/history?limit=${historyLimit}`),
          fetch(`${API_BASE_URL}/api/status`),
        ])

        const historyJson = await historyResponse.json()
        const statusJson = await statusResponse.json()

        if (cancelled) return

        const rows: TelemetryRow[] = historyJson.data ?? []

        setHistory(rows)
        setLatest(rows.length > 0 ? rows[rows.length - 1] : statusJson.latest ?? null)
        setBackendStatus(statusJson)
        setEvents(sortAndLimitEvents(rows.flatMap(telemetryEventsFromRow), eventLimit))
        setError(null)
      } catch (err) {
        if (!cancelled) {
          setError(`Initial API load failed: ${String(err)}`)
        }
      }
    }

    function appendEvents(newEvents: TimelineEvent[]) {
      setEvents((previous) => sortAndLimitEvents([...previous, ...newEvents], eventLimit))
    }

    function connectWebSocket() {
      if (cancelled) return

      ws = new WebSocket(WS_URL)

      ws.onopen = () => {
        if (cancelled) return

        setConnected(true)
        setError(null)

        pingTimer = window.setInterval(() => {
          if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send("ping")
          }
        }, 15000)
      }

      ws.onmessage = (event) => {
        try {
          const message = JSON.parse(event.data) as TelemetryWebSocketMessage

          setLastMessageType(message.type)

          if (message.type === "hello") {
            setBackendStatus(message.data as BackendStatus)
            return
          }

          if (message.status) {
            setBackendStatus(message.status)
          }

          if (message.type === "telemetry") {
            const row = message.data as TelemetryRow

            setLatest(row)

            setHistory((previous) => {
              const next = [...previous, row]
              return next.slice(-historyLimit)
            })

            appendEvents(telemetryEventsFromRow(row))
          }

          if (message.type === "command_tx") {
            const data = message.data as {
              payload?: string
              pc_time_iso?: string
              pc_time_unix?: number
            }

            const timeUnix = data.pc_time_unix ?? Date.now() / 1000
            const timeIso = data.pc_time_iso ?? new Date(timeUnix * 1000).toISOString()

            appendEvents([
              {
                id: eventId("command-tx"),
                type: "command_tx",
                time_iso: timeIso,
                time_unix: timeUnix,
                title: "Command TX",
                description: `Ground command transmitted: ${data.payload ?? "—"}`,
                payload: data.payload,
                severity: "info",
              },
            ])
          }

          if (message.type === "lora_crc_error") {
            const now = Date.now() / 1000

            appendEvents([
              {
                id: eventId("lora-crc"),
                type: "lora_crc_error",
                time_iso: new Date(now * 1000).toISOString(),
                time_unix: now,
                title: "LoRa CRC error",
                description: "Ground radio received a packet with LoRa CRC error.",
                severity: "warning",
              },
            ])
          }

          if (message.type === "non_telemetry_packet") {
            const now = Date.now() / 1000

            appendEvents([
              {
                id: eventId("non-telemetry"),
                type: "non_telemetry_packet",
                time_iso: new Date(now * 1000).toISOString(),
                time_unix: now,
                title: "Non-telemetry packet",
                description: "Ground radio received a packet that does not match the current raw-v3 telemetry length.",
                severity: "warning",
              },
            ])
          }

          if (message.type === "receiver_error" || message.type === "decode_error") {
            const now = Date.now() / 1000

            setError(message.error ?? "Backend error")

            appendEvents([
              {
                id: eventId(message.type),
                type: message.type,
                time_iso: new Date(now * 1000).toISOString(),
                time_unix: now,
                title: message.type === "receiver_error" ? "Receiver error" : "Decode error",
                description: message.error ?? "Backend error",
                severity: "critical",
              },
            ])
          }
        } catch (err) {
          setError(`WebSocket message parse failed: ${String(err)}`)
        }
      }

      ws.onerror = () => {
        setError("WebSocket error")
      }

      ws.onclose = () => {
        if (pingTimer !== undefined) {
          window.clearInterval(pingTimer)
        }

        if (cancelled) return

        setConnected(false)

        reconnectTimer = window.setTimeout(() => {
          connectWebSocket()
        }, 2000)
      }
    }

    loadInitialData()
    connectWebSocket()

    return () => {
      cancelled = true

      if (reconnectTimer !== undefined) {
        window.clearTimeout(reconnectTimer)
      }

      if (pingTimer !== undefined) {
        window.clearInterval(pingTimer)
      }

      if (ws !== null) {
        ws.close()
      }
    }
  }, [historyLimit, eventLimit])

  return {
    latest,
    history,
    events,
    backendStatus,
    connected,
    lastMessageType,
    error,
  }
}