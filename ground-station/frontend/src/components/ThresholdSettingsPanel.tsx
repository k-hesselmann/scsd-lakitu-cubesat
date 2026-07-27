import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { defaultAlertThresholds, type AlertThresholds } from "@/lib/thresholds"
import { useAlertThresholds } from "@/hooks/useAlertThresholds"

export function ThresholdSettingsPanel() {
  const { thresholds, setThresholds } = useAlertThresholds()

  function update(key: keyof AlertThresholds, value: string) {
    setThresholds({
      ...thresholds,
      [key]: Number(value),
    })
  }

  const rows: [keyof AlertThresholds, string, string][] = [
    ["staleTelemetryWarningS", "Stale telemetry warning", "s"],
    ["staleTelemetryCriticalS", "Stale telemetry critical", "s"],
    ["batteryCriticalV", "Battery low critical", "V"],
    ["batteryWarningV", "Battery low warning", "V"],
    ["batteryHighWarningV", "Battery high warning", "V"],
    ["batteryHighCriticalV", "Battery high critical", "V"],
    ["rssiWarningDbm", "RSSI warning", "dBm"],
    ["rssiCriticalDbm", "RSSI critical", "dBm"],
    ["snrWarningDb", "SNR warning", "dB"],
    ["snrCriticalDb", "SNR critical", "dB"],
    ["packetLossWarningCount", "Packet loss warning", "packets"],
    ["alarmRepeatS", "Alarm repeat interval", "s"],
  ]

  return (
    <div className="space-y-3">
      {rows.map(([key, label, unit]) => (
        <div
          key={key}
          className="grid grid-cols-[1fr_120px_70px] items-center gap-2"
        >
          <label className="text-sm">{label}</label>

          <Input
            type="number"
            value={thresholds[key]}
            onChange={(event) => update(key, event.target.value)}
          />

          <span className="text-xs text-muted-foreground">{unit}</span>
        </div>
      ))}

      <Button
        variant="outline"
        onClick={() => setThresholds(defaultAlertThresholds)}
      >
        Reset defaults
      </Button>
    </div>
  )
}
