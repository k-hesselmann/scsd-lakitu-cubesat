import { Bell, BellOff, Radio, Satellite, Zap } from "lucide-react"
import { useEffect, useMemo, useState } from "react"

import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { useAlertThresholds } from "@/hooks/useAlertThresholds"
import { useCurrentTime } from "@/hooks/useCurrentTime"
import { useGroundEventLogger } from "@/hooks/useGroundEventLogger"
import { buildMissionAlerts } from "@/lib/alerts"
import { fmt, packetAgeSeconds } from "@/lib/format"
import { rawFlagIsValid } from "@/lib/telemetryHealth"
import type { BackendStatus, TelemetryRow } from "@/types/telemetry"

type SpokenAlert = { level: "warning" | "critical"; title: string }

function delay(ms: number) {
  return new Promise<void>((resolve) => window.setTimeout(resolve, ms))
}

function beep() {
  const AudioContextClass = window.AudioContext ||
    (window as typeof window & { webkitAudioContext?: typeof AudioContext }).webkitAudioContext
  if (!AudioContextClass) return

  const context = new AudioContextClass()
  const oscillator = context.createOscillator()
  const gain = context.createGain()
  oscillator.frequency.value = 880
  gain.gain.value = 0.08
  oscillator.connect(gain)
  gain.connect(context.destination)
  oscillator.start()
  oscillator.stop(context.currentTime + 0.2)
  window.setTimeout(() => context.close().catch(() => {}), 500)
}

function speakAlertQueue(alerts: SpokenAlert[], shouldStop: () => boolean) {
  if (!("speechSynthesis" in window)) return Promise.resolve()

  return alerts.reduce(
    (queue, alert) => queue.then(() => {
      if (shouldStop()) return
      return new Promise<void>((resolve) => {
        const utterance = new SpeechSynthesisUtterance(
          `${alert.level} alert: ${alert.title}`,
        )
        utterance.lang = "en-US"
        utterance.onend = () => resolve()
        utterance.onerror = () => resolve()
        window.speechSynthesis.speak(utterance)
      })
    }),
    Promise.resolve(),
  )
}

export function MissionStrip({
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
  const nowMs = useCurrentTime()
  const [alarmEnabled, setAlarmEnabled] = useState(false)

  const alerts = useMemo(
    () => buildMissionAlerts({ latest, backendStatus, connected, frontendError: error, thresholds, nowMs }),
    [latest, backendStatus, connected, error, thresholds, nowMs],
  )
  useGroundEventLogger(alerts, backendStatus !== null)

  const criticalAlerts = alerts.filter((alert) => alert.level === "critical")
  const warningAlerts = alerts.filter((alert) => alert.level === "warning")
  const audibleAlertKey = JSON.stringify(
    alerts
      .filter((alert) => alert.level === "critical" || alert.level === "warning")
      .map((alert) => ({ level: alert.level, title: alert.title })),
  )
  const audibleAlerts = useMemo(
    () => JSON.parse(audibleAlertKey) as SpokenAlert[],
    [audibleAlertKey],
  )

  useEffect(() => {
    let stopped = false
    if (!alarmEnabled || audibleAlerts.length === 0) return

    async function alarmLoop() {
      while (!stopped) {
        beep()
        await speakAlertQueue(audibleAlerts, () => stopped)
        if (!stopped) await delay(Math.max(1, thresholds.alarmRepeatS) * 1000)
      }
    }

    void alarmLoop()
    return () => {
      stopped = true
      window.speechSynthesis?.cancel()
    }
  }, [alarmEnabled, audibleAlerts, thresholds.alarmRepeatS])

  const age = packetAgeSeconds(latest?.pc_receive_time_unix, nowMs)
  const linkText = !connected ? "DISCONNECTED" : latest && age !== null && age < thresholds.staleTelemetryWarningS ? "LIVE" : "STALE"
  const linkVariant = linkText === "LIVE" ? "default" : linkText === "STALE" ? "secondary" : "destructive"
  const criticalClass = criticalAlerts.length > 0 ? "border-red-500 bg-red-50 animate-pulse" : "bg-card"
  const gpsValid = rawFlagIsValid(latest?.gps_valid_raw)

  return (
    <div className={`flex h-auto min-h-14 shrink-0 items-center gap-3 border-b px-3 py-2 ${criticalClass}`}>
      <div className="flex shrink-0 items-center gap-2 text-sm font-semibold">
        <Radio className="h-4 w-4" />
        Mission Strip
      </div>
      <Badge variant={linkVariant}>{linkText}</Badge>

      <div className="grid flex-1 grid-cols-2 gap-x-4 gap-y-1 text-[11px] sm:grid-cols-4 xl:grid-cols-8">
        <div><div className="text-muted-foreground">Seq</div><div className="font-semibold">{fmt(latest?.sequence_number)}</div></div>
        <div><div className="text-muted-foreground">Phase</div><div className="font-semibold">{fmt(latest?.flight_state_name)}</div></div>
        <div><div className="text-muted-foreground">Battery</div><div className="font-semibold">{fmt(latest?.battery_v, " V", 2)}</div></div>
        <div><div className="text-muted-foreground">GPS</div><div className="font-semibold"><Satellite className="mr-1 inline h-3 w-3" />{latest ? (gpsValid ? "VALID" : "INVALID") : "—"}</div></div>
        <div><div className="text-muted-foreground">Sensor sample age</div><div className="font-semibold">{fmt(latest?.sample_age_ms, " ms")}</div></div>
        <div><div className="text-muted-foreground">Ground RX RSSI</div><div className="font-semibold">{fmt(latest?.lora_downlink_rssi_dbm, " dBm")}</div></div>
        <div><div className="text-muted-foreground">Ground RX SNR</div><div className="font-semibold">{fmt(latest?.lora_downlink_snr_db, " dB", 1)}</div></div>
        <div><div className="text-muted-foreground">Packet age</div><div className="font-semibold">{age === null ? "—" : `${age.toFixed(1)} s`}</div></div>
      </div>

      <div className="flex shrink-0 items-center gap-2">
        {criticalAlerts.length > 0 ? <Badge variant="destructive">{criticalAlerts.length} critical</Badge> : null}
        {warningAlerts.length > 0 ? <Badge variant="secondary">{warningAlerts.length} warnings</Badge> : null}
        <Button variant="outline" size="sm" title="Enable or mute sound alarms" onClick={() => {
          setAlarmEnabled((current) => {
            if (current) window.speechSynthesis?.cancel()
            else beep()
            return !current
          })
        }}>
          {alarmEnabled ? <Bell className="h-4 w-4" /> : <BellOff className="h-4 w-4" />}
        </Button>
        {criticalAlerts.length > 0 ? <Zap className="h-5 w-5 text-red-600" /> : null}
      </div>
    </div>
  )
}
