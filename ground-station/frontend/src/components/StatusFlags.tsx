import { Badge } from "@/components/ui/badge"
import type { TelemetryRow } from "@/types/telemetry"

const flagNames = [
  "GNSS_FIX_VALID",
  "GNSS_TIME_VALID",
  "IMU_VALID",
  "BARO_VALID",
  "BATTERY_VALID",
  "CORAL_VALID",
  "CORAL_NEW",
  "SD_LOGGING_OK",
  "LAST_LORA_TX_OK",
  "COMMAND_RX_SINCE_LAST",
  "GPS_ERROR",
  "IMU_ERROR",
  "BARO_ERROR",
  "SD_ERROR",
]

const badWhenActive = new Set([
  "GPS_ERROR",
  "IMU_ERROR",
  "BARO_ERROR",
  "SD_ERROR",
])

export function StatusFlags({ latest }: { latest: TelemetryRow | null }) {
  if (!latest) {
    return <p className="text-sm text-muted-foreground">No status flags yet.</p>
  }

  return (
    <div className="flex flex-wrap gap-2">
      {flagNames.map((name) => {
        const value = Boolean(latest[name])
        const isBadFlag = badWhenActive.has(name)

        let variant: "default" | "secondary" | "destructive" | "outline" =
          "secondary"

        if (isBadFlag && value) {
          variant = "destructive"
        } else if (!isBadFlag && value) {
          variant = "default"
        } else {
          variant = "outline"
        }

        return (
          <Badge key={name} variant={variant}>
            {name}: {value ? "ON" : "OFF"}
          </Badge>
        )
      })}
    </div>
  )
}