import { Bell, BellOff, Radio, Satellite, Zap } from "lucide-react"
import { useEffect, useMemo, useState } from "react"

import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { buildMissionAlerts } from "@/lib/alerts"
import { fmt, packetAgeSeconds } from "@/lib/format"
import { useAlertThresholds } from "@/hooks/useAlertThresholds"
import type { BackendStatus, TelemetryRow } from "@/types/telemetry"

type SpokenAlert = {
  level: "warning" | "critical"
  title: string
}

function delay(ms: number) {
  return new Promise<void>((resolve) => {
    window.setTimeout(resolve, ms)
  })
}

function beep() {
  const AudioContextClass =
    window.AudioContext ||
    (window as typeof window & { webkitAudioContext?: typeof AudioContext })
      .webkitAudioContext

  if (!AudioContextClass) return

  const ctx = new AudioContextClass()
  const oscillator = ctx.createOscillator()
  const gain = ctx.createGain()

  oscillator.type = "sine"
  oscillator.frequency.value = 880
  gain.gain.value = 0.08

  oscillator.connect(gain)
  gain.connect(ctx.destination)

  oscillator.start()
  oscillator.stop(ctx.currentTime + 0.2)

  window.setTimeout(() => {
    ctx.close().catch(() => {})
  }, 500)
}

function loadVoices(): Promise<SpeechSynthesisVoice[]> {
  return new Promise((resolve) => {
    if (!("speechSynthesis" in window)) {
      resolve([])
      return
    }

    const existing = window.speechSynthesis.getVoices()

    if (existing.length > 0) {
      resolve(existing)
      return
    }

    const timeout = window.setTimeout(() => {
      resolve(window.speechSynthesis.getVoices())
    }, 1200)

    window.speechSynthesis.onvoiceschanged = () => {
      window.clearTimeout(timeout)
      resolve(window.speechSynthesis.getVoices())
    }
  })
}

function englishVoices(voices: SpeechSynthesisVoice[]) {
  return voices.filter((voice) => voice.lang.toLowerCase().startsWith("en"))
}

function findFemaleAmericanVoice(voices: SpeechSynthesisVoice[]) {
  const en = englishVoices(voices)

  return (
    en.find(
      (voice) =>
        voice.lang === "en-US" &&
        /zira|samantha|victoria|jenny|aria|female|woman/i.test(voice.name),
    ) ??
    en.find((voice) => voice.lang === "en-US") ??
    en.find((voice) =>
      /zira|samantha|victoria|jenny|aria|female|woman/i.test(voice.name),
    ) ??
    en[0] ??
    null
  )
}

function findMaleEnglishVoice(voices: SpeechSynthesisVoice[]) {
  const en = englishVoices(voices)

  return (
    en.find((voice) =>
      /david|mark|daniel|george|alex|fred|tom|male|man/i.test(voice.name),
    ) ??
    en.find((voice) => voice.lang === "en-US") ??
    en.find((voice) => voice.lang === "en-GB") ??
    en[0] ??
    null
  )
}

function speakOne(text: string, voice: SpeechSynthesisVoice | null) {
  return new Promise<void>((resolve) => {
    if (!("speechSynthesis" in window)) {
      resolve()
      return
    }

    const utterance = new SpeechSynthesisUtterance(text)

    utterance.lang = voice?.lang ?? "en-US"
    utterance.voice = voice
    utterance.rate = 0.95
    utterance.pitch = 1.0
    utterance.volume = 1

    utterance.onend = () => resolve()
    utterance.onerror = () => resolve()

    window.speechSynthesis.speak(utterance)
  })
}

async function speakAlertQueue(
  alerts: SpokenAlert[],
  shouldStop: () => boolean,
) {
  if (!("speechSynthesis" in window)) return

  const voices = await loadVoices()
  const femaleAmericanVoice = findFemaleAmericanVoice(voices)
  const maleEnglishVoice = findMaleEnglishVoice(voices)

  for (const alert of alerts) {
    if (shouldStop()) return

    const prefix =
      alert.level === "critical" ? "critical alert" : "warning alert"

    const voice =
      alert.level === "critical" ? femaleAmericanVoice : maleEnglishVoice

    await speakOne(`${prefix}: ${alert.title}`, voice)
  }
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
  const [alarmEnabled, setAlarmEnabled] = useState(false)

  const alerts = useMemo(
    () =>
      buildMissionAlerts({
        latest,
        backendStatus,
        connected,
        frontendError: error,
        thresholds,
      }),
    [latest, backendStatus, connected, error, thresholds],
  )

  const criticalAlerts = alerts.filter((alert) => alert.level === "critical")
  const warningAlerts = alerts.filter((alert) => alert.level === "warning")

  const audibleAlerts: SpokenAlert[] = useMemo(
    () => [
      ...criticalAlerts.map((alert) => ({
        level: "critical" as const,
        title: alert.title,
      })),
      ...warningAlerts.map((alert) => ({
        level: "warning" as const,
        title: alert.title,
      })),
    ],
    [criticalAlerts, warningAlerts],
  )

  const audibleAlertKey = audibleAlerts
    .map((alert) => `${alert.level}:${alert.title}`)
    .join("|")

  useEffect(() => {
    let stopped = false

    if (!alarmEnabled) return
    if (audibleAlerts.length === 0) return

    async function alarmLoop() {
      while (!stopped) {
        beep()

        await speakAlertQueue(audibleAlerts, () => stopped)

        if (stopped) return

        await delay(Math.max(1, thresholds.alarmRepeatS) * 1000)
      }
    }

    alarmLoop()

    return () => {
      stopped = true
      window.speechSynthesis?.cancel()
    }
  }, [alarmEnabled, audibleAlertKey, thresholds.alarmRepeatS])

  const age = packetAgeSeconds(latest?.pc_receive_time_unix)

  const linkText =
    !connected
      ? "DISCONNECTED"
      : latest && age !== null && age < thresholds.staleTelemetryWarningS
        ? "LIVE"
        : "STALE"

  const criticalClass =
    criticalAlerts.length > 0
      ? "border-red-500 bg-red-50 animate-pulse"
      : "bg-card"

  return (
    <div
      className={`flex h-14 shrink-0 items-center gap-3 border-b px-3 ${criticalClass}`}
    >
      <div className="flex items-center gap-2 text-sm font-semibold">
        <Radio className="h-4 w-4" />
        Mission Strip
      </div>

      <Badge variant={connected ? "default" : "destructive"}>{linkText}</Badge>

      <div className="grid flex-1 grid-cols-2 gap-2 text-[11px] md:grid-cols-4 xl:grid-cols-8">
        <div>
          <div className="text-muted-foreground">Seq</div>
          <div className="font-semibold">{fmt(latest?.sequence_number)}</div>
        </div>

        <div>
          <div className="text-muted-foreground">Phase</div>
          <div className="font-semibold">{fmt(latest?.flight_state_name)}</div>
        </div>

        <div>
          <div className="text-muted-foreground">Battery</div>
          <div className="font-semibold">{fmt(latest?.battery_v, " V", 2)}</div>
        </div>

        <div>
          <div className="text-muted-foreground">GNSS</div>
          <div className="font-semibold">
            <Satellite className="mr-1 inline h-3 w-3" />
            {fmt(latest?.gnss_satellites_used)} sats
          </div>
        </div>

        <div>
          <div className="text-muted-foreground">Altitude</div>
          <div className="font-semibold">
            {fmt(latest?.gnss_altitude_m, " m", 1)}
          </div>
        </div>

        <div>
          <div className="text-muted-foreground">RSSI</div>
          <div className="font-semibold">
            {fmt(latest?.lora_downlink_rssi_dbm, " dBm")}
          </div>
        </div>

        <div>
          <div className="text-muted-foreground">SNR</div>
          <div className="font-semibold">
            {fmt(latest?.lora_downlink_snr_db, " dB", 1)}
          </div>
        </div>

        <div>
          <div className="text-muted-foreground">Last packet</div>
          <div className="font-semibold">
            {age === null ? "—" : `${age.toFixed(1)} s`}
          </div>
        </div>
      </div>

      <div className="flex items-center gap-2">
        {criticalAlerts.length > 0 ? (
          <Badge variant="destructive">{criticalAlerts.length} critical</Badge>
        ) : null}

        {warningAlerts.length > 0 ? (
          <Badge variant="secondary">{warningAlerts.length} warnings</Badge>
        ) : null}

        <Button
          variant="outline"
          size="sm"
          title="Enable / mute sound and voice alarms"
          onClick={() => {
            setAlarmEnabled((current) => {
              const next = !current

              if (!current) {
                beep()
                speakAlertQueue(
                  [{ level: "warning", title: "Ground station alarm audio enabled" }],
                  () => false,
                )
              } else {
                window.speechSynthesis?.cancel()
              }

              return next
            })
          }}
        >
          {alarmEnabled ? (
            <Bell className="h-4 w-4" />
          ) : (
            <BellOff className="h-4 w-4" />
          )}
        </Button>

        {criticalAlerts.length > 0 ? (
          <Zap className="h-5 w-5 text-red-600" />
        ) : null}
      </div>
    </div>
  )
}