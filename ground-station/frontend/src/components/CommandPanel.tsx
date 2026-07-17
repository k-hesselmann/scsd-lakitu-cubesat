import { useEffect, useState } from "react"
import { API_BASE_URL } from "@/lib/config"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { Textarea } from "@/components/ui/textarea"
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert"

type UplinkLog = {
  pc_time_iso: string
  payload: string
  outcome: string
  message: string
  timeout_s: number
  command_id?: number | null
  attempt?: number | null
  telemetry_sequence?: number | null
}

type CommandPanelProps = {
  latestSequence?: number
}

export function CommandPanel({ latestSequence }: CommandPanelProps) {
  const [payload, setPayload] = useState("REQ_TELEMETRY")
  const [timeout, setTimeoutValue] = useState("5")
  const [sending, setSending] = useState(false)
  const [result, setResult] = useState<string | null>(null)
  const [error, setError] = useState<string | null>(null)
  const [logs, setLogs] = useState<UplinkLog[]>([])

  async function loadLogs() {
    try {
      const response = await fetch(`${API_BASE_URL}/api/uplink-log?limit=100`)
      if (!response.ok) return
      const body = (await response.json()) as { data?: UplinkLog[] }
      setLogs(body.data ?? [])
    } catch {
      // The command controls remain usable while the history endpoint is down.
    }
  }

  useEffect(() => {
    void loadLogs()
  }, [])

  async function sendCommand(commandPayload: string) {
    const timeoutSeconds = Number(timeout)

    if ([...commandPayload].some((character) => character.charCodeAt(0) > 0x7f)) {
      setError("Commands must contain ASCII characters only.")
      return
    }

    if (!Number.isFinite(timeoutSeconds) || timeoutSeconds < 0.5 || timeoutSeconds > 30) {
      setError("TX timeout must be between 0.5 and 30 seconds.")
      return
    }

    setSending(true)
    setResult(null)
    setError(null)

    try {
      const response = await fetch(`${API_BASE_URL}/api/send-ascii`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ payload: commandPayload, timeout_s: timeoutSeconds, ack_timeout_s: timeoutSeconds, max_attempts: 3 }),
      })
      const body = await response.json()

      if (!response.ok) throw new Error(body.detail ?? "Command failed")

      setResult(body.acknowledged ? `Flight acknowledged command ${body.command_id} in telemetry ${body.telemetry_sequence}` : `Uplink transmitted: ${body.payload}`)
      setPayload(commandPayload)
    } catch (err) {
      setError(String(err))
    } finally {
      setSending(false)
      void loadLogs()
    }
  }

  const acknowledgementPayload = latestSequence === undefined ? null : `ACK,${latestSequence}`

  return (
    <div className="grid gap-4 2xl:grid-cols-[minmax(0,1fr)_minmax(360px,0.8fr)]">
      <div className="space-y-4">
        <Alert>
          <AlertTitle>Supported flight commands</AlertTitle>
          <AlertDescription>
            <code>REQ_TELEMETRY</code> requests an immediate current packet. <code>ACK,&lt;sequence&gt;</code>
            acknowledges a received downlink. Valid telemetry is acknowledged automatically; the button below can resend an ACK manually.
          </AlertDescription>
        </Alert>

        <div className="flex flex-wrap gap-2">
          <Button onClick={() => void sendCommand("REQ_TELEMETRY")} disabled={sending}>
            Request telemetry now
          </Button>
          <Button
            variant="secondary"
            onClick={() => acknowledgementPayload && void sendCommand(acknowledgementPayload)}
            disabled={sending || acknowledgementPayload === null}
            title={acknowledgementPayload ?? "Wait for a telemetry packet before acknowledging it"}
          >
            {acknowledgementPayload ? `Resend ACK for seq ${latestSequence}` : "No telemetry to acknowledge"}
          </Button>
        </div>

        <div>
          <label className="mb-2 block text-sm font-medium">ASCII uplink payload</label>
          <Textarea value={payload} onChange={(event) => setPayload(event.target.value)} rows={4} maxLength={48} />
          <p className="mt-1 text-xs text-muted-foreground">
            Maximum 48 ASCII characters. Commands receive a unique ID and are retried up to three times until flight acknowledges them.
          </p>
        </div>

        <div className="flex flex-wrap items-end gap-3">
          <div>
            <label className="mb-2 block text-sm font-medium">TX timeout [s]</label>
            <Input value={timeout} onChange={(event) => setTimeoutValue(event.target.value)} type="number" min="0.5" max="30" step="0.5" className="w-32" />
          </div>
          <Button onClick={() => void sendCommand(payload)} disabled={sending || payload.trim().length === 0}>
            {sending ? "Sending..." : "Send uplink"}
          </Button>
        </div>

        {result ? <Alert><AlertTitle>Uplink sent</AlertTitle><AlertDescription>{result}</AlertDescription></Alert> : null}
        {error ? <Alert variant="destructive"><AlertTitle>Uplink failed</AlertTitle><AlertDescription>{error}</AlertDescription></Alert> : null}
      </div>

      <section className="min-w-0 rounded-lg border">
        <div className="flex items-center justify-between border-b px-3 py-2">
          <h2 className="text-sm font-semibold">Uplink command log</h2>
          <Button variant="ghost" size="sm" onClick={() => void loadLogs()}>Refresh</Button>
        </div>
        <div className="max-h-[560px] overflow-auto">
          {logs.length === 0 ? (
            <p className="p-3 text-sm text-muted-foreground">No uplink attempts recorded in this backend session.</p>
          ) : (
            <table className="w-full text-left text-xs">
              <thead className="sticky top-0 bg-card text-muted-foreground">
                <tr><th className="px-3 py-2">Time</th><th className="px-3 py-2">Payload</th><th className="px-3 py-2">Result</th></tr>
              </thead>
              <tbody>
                {logs.map((log, index) => (
                  <tr key={`${log.pc_time_iso}-${index}`} className="border-t align-top">
                    <td className="whitespace-nowrap px-3 py-2">{new Date(log.pc_time_iso).toLocaleTimeString()}</td>
                    <td className="break-all px-3 py-2 font-mono">{log.payload}</td>
                    <td className="px-3 py-2">
                      <span className={log.outcome === "acknowledged" || log.outcome === "sent" ? "text-emerald-600" : log.outcome === "radio_sent" ? "text-blue-600" : "text-destructive"}>{log.outcome}</span>
                      <div className="mt-0.5 text-muted-foreground">{log.message}</div>
                      {log.command_id ? <div className="mt-0.5 text-muted-foreground">cmd {log.command_id} · attempt {log.attempt ?? "—"}</div> : null}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </div>
      </section>
    </div>
  )
}