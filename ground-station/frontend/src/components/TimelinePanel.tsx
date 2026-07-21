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
  const visibleEvents = events.slice(0, 80)

  if (events.length === 0) {
    return (
      <div className="flex h-full items-center justify-center rounded-md border text-sm text-muted-foreground">
        No timeline events yet.
      </div>
    )
  }

  return (
    <div className="h-full overflow-auto rounded-md border bg-background">
      {events.length > visibleEvents.length ? (
        <div className="sticky top-0 z-20 border-b bg-background/95 px-2 py-1 text-[10px] text-muted-foreground backdrop-blur">
          Showing newest {visibleEvents.length} of {events.length} events
        </div>
      ) : null}
      <div className="relative p-2">
        <div className="absolute bottom-2 left-[19px] top-2 w-px bg-border" />

        <div className="space-y-1.5">
          {visibleEvents.map((event) => (
            <div key={event.id} className="relative flex gap-2">
              <div className="z-10 flex h-6 w-6 shrink-0 items-center justify-center rounded-full border bg-background [&_svg]:h-3 [&_svg]:w-3">
                {eventIcon(event.type)}
              </div>

              <div className="min-w-0 flex-1 rounded-md border bg-card px-2 py-1.5">
                <div className="flex items-center justify-between gap-2">
                  <div className="truncate text-xs font-semibold">
                    {event.title}
                  </div>

                  <Badge variant={badgeVariant(event.severity)} className="h-4 shrink-0 px-1 text-[9px]">
                    {formatEventTime(event.time_iso)}
                  </Badge>
                </div>

                {event.description ? (
                  <div className="mt-0.5 line-clamp-2 text-[10px] leading-4 text-muted-foreground">
                    {event.description}
                  </div>
                ) : null}

                {event.sequence_number !== undefined ? (
                  <div className="mt-0.5 text-[10px]">
                    Seq: {event.sequence_number}
                  </div>
                ) : null}

                {event.payload ? (
                  <div className="mt-0.5 truncate text-[10px]" title={event.payload}>
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