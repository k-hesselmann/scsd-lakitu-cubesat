import { useEffect, useState } from "react"

import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { Textarea } from "@/components/ui/textarea"
import { API_BASE_URL } from "@/lib/config"
import type { CommandSafety } from "@/types/telemetry"

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
  latestBootCount?: number
  latestSequence?: number
  latestUptimeMs?: number
}

export function CommandPanel({
  latestBootCount,
  latestSequence,
  latestUptimeMs,
}: CommandPanelProps) {
  const [payload, setPayload] = useState("REQ_TELEMETRY")
  const [timeout, setTimeoutValue] = useState("5")
  const [token, setToken] = useState("")
  const [confirmation, setConfirmation] = useState("")
  const [safety, setSafety] = useState<CommandSafety | null>(null)
  const [sending, setSending] = useState(false)
  const [result, setResult] = useState<string | null>(null)
  const [error, setError] = useState<string | null>(null)
  const [logs, setLogs] = useState<UplinkLog[]>([])

  function headers(json = false) {
    const result: Record<string, string> = {}
    if (json) result["Content-Type"] = "application/json"
    if (token.trim()) result["X-Ground-Station-Token"] = token.trim()
    return result
  }

  async function loadLogs() {
    try {
      const response = await fetch(API_BASE_URL + "/api/uplink-log?limit=100")
      if (!response.ok) return
      const body = (await response.json()) as { data?: UplinkLog[] }
      setLogs(body.data ?? [])
    } catch {
      // Controls remain usable while the history endpoint is unavailable.
    }
  }

  async function loadSafety() {
    try {
      const response = await fetch(API_BASE_URL + "/api/command-safety")
      if (!response.ok) return
      setSafety((await response.json()) as CommandSafety)
    } catch {
      // The send path will still return the authoritative backend error.
    }
  }

  useEffect(() => {
    void loadLogs()
    void loadSafety()
  }, [])

  async function armCommandPath() {
    setResult(null)
    setError(null)
    try {
      const response = await fetch(API_BASE_URL + "/api/command-arm", {
        method: "POST",
        headers: headers(true),
        body: JSON.stringify({ confirmation }),
      })
      const body = await response.json()
      if (!response.ok) throw new Error(body.detail ?? "Could not arm commands")
      setSafety(body as CommandSafety)
      setConfirmation("")
      setResult("TTC command path armed for one command.")
    } catch (err) {
      setError(String(err))
    }
  }

  async function disarmCommandPath() {
    setResult(null)
    setError(null)
    try {
      const response = await fetch(API_BASE_URL + "/api/command-arm", {
        method: "DELETE",
        headers: headers(),
      })
      const body = await response.json()
      if (!response.ok) throw new Error(body.detail ?? "Could not disarm commands")
      setSafety(body as CommandSafety)
      setResult("TTC command path disarmed.")
    } catch (err) {
      setError(String(err))
    }
  }

  async function sendCommand(commandPayload: string) {
    const timeoutSeconds = Number(timeout)
    const isAcknowledgement = commandPayload.startsWith("ACK,")

    if ([...commandPayload].some((character) => character.charCodeAt(0) > 0x7f)) {
      setError("Commands must contain ASCII characters only.")
      return
    }
    if (!Number.isFinite(timeoutSeconds) || timeoutSeconds < 0.5 || timeoutSeconds > 30) {
      setError("TX timeout must be between 0.5 and 30 seconds.")
      return
    }
    if (!isAcknowledgement && safety?.armed !== true) {
      setError('Arm the one-shot TTC command path by entering "ARM TTC" first.')
      return
    }

    setSending(true)
    setResult(null)
    setError(null)

    try {
      const response = await fetch(API_BASE_URL + "/api/send-ascii", {
        method: "POST",
        headers: headers(true),
        body: JSON.stringify({
          payload: commandPayload,
          timeout_s: timeoutSeconds,
          ack_timeout_s: 25,
          max_attempts: 3,
        }),
      })
      const body = await response.json()

      if (!response.ok) throw new Error(body.detail ?? "Command failed")

      setResult(
        body.acknowledged
          ? "Flight acknowledged command " +
              body.command_id +
              " in telemetry " +
              body.telemetry_sequence
          : "Uplink transmitted: " + body.payload,
      )
      setPayload(commandPayload)
    } catch (err) {
      setError(String(err))
    } finally {
      setSending(false)
      void loadLogs()
      void loadSafety()
    }
  }

  const acknowledgementPayload =
    latestBootCount === undefined ||
    latestSequence === undefined ||
    latestUptimeMs === undefined
      ? null
      : `ACK,${latestBootCount},${latestSequence},${Math.floor(latestUptimeMs / 1000)}`
  const armed = safety?.armed === true
  const rfAuthReady = safety?.rf_auth_configured === true
  const commandIdsAvailable = (safety?.command_ids_remaining ?? 0) > 0

  return (
    <div className="grid gap-4 2xl:grid-cols-[minmax(0,1fr)_minmax(360px,0.8fr)]">
      <div className="space-y-4">
        <Alert>
          <AlertTitle>Protected flight commands</AlertTitle>
          <AlertDescription>
            Commands require operator authentication when configured and a
            one-shot arm action. RF uplinks are cryptographically authenticated.
            Valid telemetry is acknowledged automatically; manual ACK
            retransmission does not consume the command arm.
          </AlertDescription>
        </Alert>

        <section className="space-y-3 rounded-lg border p-3">
          <div className="flex items-center justify-between gap-3">
            <div>
              <div className="text-sm font-medium">
                Command path: {armed ? "ARMED" : "DISARMED"}
              </div>
              <div className="text-xs text-muted-foreground">
                {armed
                  ? "One command permitted for approximately " +
                    Math.ceil(safety?.armed_remaining_s ?? 0) +
                    " s."
                  : "Enter the exact confirmation phrase to arm one command."}
              </div>
              <div className="text-xs text-muted-foreground">
                RF authentication: {rfAuthReady ? "CONFIGURED" : "NOT CONFIGURED"}
              </div>
              <div className="text-xs text-muted-foreground">
                Command IDs remaining: {safety?.command_ids_remaining ?? "—"}
              </div>
            </div>
            <Button
              variant="secondary"
              onClick={() => void disarmCommandPath()}
              disabled={!armed || sending}
            >
              Disarm
            </Button>
          </div>

          <div className="grid gap-3 md:grid-cols-2">
            <div>
              <label className="mb-1 block text-sm font-medium">
                Operator token
              </label>
              <Input
                type="password"
                autoComplete="off"
                value={token}
                onChange={(event) => setToken(event.target.value)}
                placeholder={
                  safety?.auth_required ? "Required by backend" : "Not configured"
                }
              />
            </div>
            <div>
              <label className="mb-1 block text-sm font-medium">
                Arm confirmation
              </label>
              <div className="flex gap-2">
                <Input
                  value={confirmation}
                  onChange={(event) => setConfirmation(event.target.value)}
                  placeholder="ARM TTC"
                />
                <Button
                  onClick={() => void armCommandPath()}
                  disabled={
                    sending ||
                    !rfAuthReady ||
                    !commandIdsAvailable ||
                    confirmation !== "ARM TTC"
                  }
                >
                  Arm
                </Button>
              </div>
            </div>
          </div>
        </section>

        <div className="flex flex-wrap gap-2">
          <Button
            onClick={() => void sendCommand("REQ_TELEMETRY")}
            disabled={sending || !armed}
          >
            Request telemetry now
          </Button>
          <Button
            variant="secondary"
            onClick={() =>
              acknowledgementPayload &&
              void sendCommand(acknowledgementPayload)
            }
            disabled={sending || acknowledgementPayload === null}
            title={
              acknowledgementPayload ??
              "Wait for telemetry before acknowledging it"
            }
          >
            {acknowledgementPayload
              ? "Resend bound ACK for seq " + latestSequence
              : "No telemetry to acknowledge"}
          </Button>
        </div>

        <div>
          <label className="mb-2 block text-sm font-medium">
            ASCII uplink payload
          </label>
          <Textarea
            value={payload}
            onChange={(event) => setPayload(event.target.value)}
            rows={4}
            maxLength={36}
          />
          <p className="mt-1 text-xs text-muted-foreground">
            Maximum 36 ASCII characters. Reliable commands receive a persisted
            ID and retry until fresh flight telemetry confirms the result.
          </p>
        </div>

        <div className="flex flex-wrap items-end gap-3">
          <div>
            <label className="mb-2 block text-sm font-medium">
              TX timeout [s]
            </label>
            <Input
              value={timeout}
              onChange={(event) => setTimeoutValue(event.target.value)}
              type="number"
              min="0.5"
              max="30"
              step="0.5"
              className="w-32"
            />
          </div>
          <Button
            onClick={() => void sendCommand(payload)}
            disabled={sending || !armed || payload.trim().length === 0}
          >
            {sending ? "Sending..." : "Send uplink"}
          </Button>
        </div>

        {result ? (
          <Alert>
            <AlertTitle>Command status</AlertTitle>
            <AlertDescription>{result}</AlertDescription>
          </Alert>
        ) : null}
        {error ? (
          <Alert variant="destructive">
            <AlertTitle>Uplink failed</AlertTitle>
            <AlertDescription>{error}</AlertDescription>
          </Alert>
        ) : null}
      </div>

      <section className="min-w-0 rounded-lg border">
        <div className="flex items-center justify-between border-b px-3 py-2">
          <h2 className="text-sm font-semibold">Uplink command log</h2>
          <Button variant="ghost" size="sm" onClick={() => void loadLogs()}>
            Refresh
          </Button>
        </div>
        <div className="max-h-[560px] overflow-auto">
          {logs.length === 0 ? (
            <p className="p-3 text-sm text-muted-foreground">
              No uplink attempts recorded in this backend session.
            </p>
          ) : (
            <table className="w-full text-left text-xs">
              <thead className="sticky top-0 bg-card text-muted-foreground">
                <tr>
                  <th className="px-3 py-2">Time</th>
                  <th className="px-3 py-2">Payload</th>
                  <th className="px-3 py-2">Result</th>
                </tr>
              </thead>
              <tbody>
                {logs.map((log, index) => (
                  <tr
                    key={log.pc_time_iso + "-" + index}
                    className="border-t align-top"
                  >
                    <td className="whitespace-nowrap px-3 py-2">
                      {new Date(log.pc_time_iso).toLocaleTimeString()}
                    </td>
                    <td className="break-all px-3 py-2 font-mono">
                      {log.payload}
                    </td>
                    <td className="px-3 py-2">
                      <span
                        className={
                          log.outcome === "acknowledged" ||
                          log.outcome === "sent"
                            ? "text-emerald-600"
                            : log.outcome === "radio_sent"
                              ? "text-blue-600"
                              : "text-destructive"
                        }
                      >
                        {log.outcome}
                      </span>
                      <div className="mt-0.5 text-muted-foreground">
                        {log.message}
                      </div>
                      {log.command_id ? (
                        <div className="mt-0.5 text-muted-foreground">
                          cmd {log.command_id} · attempt {log.attempt ?? "—"}
                        </div>
                      ) : null}
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
