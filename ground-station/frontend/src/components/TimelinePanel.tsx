import {
  ArrowDownToLine,

  CircleAlert,
  Radio,
  Send,
} from "lucide-react"

import { Badge } from "@/components/ui/badge"
import type { TimelineEvent } from "@/types/telemetry"

function eventIcon(type: TimelineEvent["type"]) {
  switch (type) {
    case "telemetry_rx":
      return <ArrowDownToLine className="h-4 w-4" />
    case "command_tx":
      return <Send className="h-4 w-4" />
    case "lora_crc_error":
    case "decode_error":
    case "receiver_error":
      return <CircleAlert className="h-4 w-4" />
    default:
      return <Radio className="h-4 w-4" />
  }
}

function badgeVariant(severity?: TimelineEvent["severity"]) {
  if (severity === "critical") return "destructive"
  if (severity === "warning") return "secondary"
  return "default"
}

function formatEventTime(timeIso: string) {
  const date = new Date(timeIso)

  if (Number.isNaN(date.getTime())) return "—"

  return date.toLocaleTimeString()
}

export function TimelinePanel({ events }: { events: TimelineEvent[] }) {
  if (events.length === 0) {
    return (
      <div className="flex h-full items-center justify-center rounded-md border text-sm text-muted-foreground">
        No timeline events yet.
      </div>
    )
  }

  return (
    <div className="h-full overflow-auto rounded-md border bg-background">
      <div className="relative p-3">
        <div className="absolute bottom-3 left-[25px] top-3 w-px bg-border" />

        <div className="space-y-3">
          {events.map((event) => (
            <div key={event.id} className="relative flex gap-3">
              <div className="z-10 flex h-7 w-7 shrink-0 items-center justify-center rounded-full border bg-background">
                {eventIcon(event.type)}
              </div>

              <div className="min-w-0 flex-1 rounded-md border bg-card p-2">
                <div className="flex items-center justify-between gap-2">
                  <div className="truncate text-sm font-semibold">
                    {event.title}
                  </div>

                  <Badge variant={badgeVariant(event.severity)} className="shrink-0">
                    {formatEventTime(event.time_iso)}
                  </Badge>
                </div>

                {event.description ? (
                  <div className="mt-1 text-xs text-muted-foreground">
                    {event.description}
                  </div>
                ) : null}

                {event.sequence_number !== undefined ? (
                  <div className="mt-1 text-xs">
                    Seq: {event.sequence_number}
                  </div>
                ) : null}

                {event.payload ? (
                  <div className="mt-1 text-xs">
                    Payload: {event.payload}
                  </div>
                ) : null}
              </div>
            </div>
          ))}
        </div>
      </div>
    </div>
  )
}