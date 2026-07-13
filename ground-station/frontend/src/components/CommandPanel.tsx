import { useState } from "react"
import { API_BASE_URL } from "@/lib/config"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { Textarea } from "@/components/ui/textarea"
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert"

export function CommandPanel() {
  const [payload, setPayload] = useState("PING_GROUND_0001")
  const [timeout, setTimeoutValue] = useState("5")
  const [sending, setSending] = useState(false)
  const [result, setResult] = useState<string | null>(null)
  const [error, setError] = useState<string | null>(null)

  async function sendCommand(commandPayload: string) {
    setSending(true)
    setResult(null)
    setError(null)

    try {
      const response = await fetch(`${API_BASE_URL}/api/send-ascii`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify({
          payload: commandPayload,
          timeout_s: Number(timeout),
        }),
      })

      const body = await response.json()

      if (!response.ok) {
        throw new Error(body.detail ?? "Command failed")
      }

      setResult(`Command sent: ${body.payload}`)
    } catch (err) {
      setError(String(err))
    } finally {
      setSending(false)
    }
  }

  return (
    <div className="grid gap-4 xl:grid-cols-[2fr_1fr]">
      <div className="space-y-4">
        <div>
          <label className="mb-2 block text-sm font-medium">
            ASCII command payload
          </label>
          <Textarea
            value={payload}
            onChange={(event) => setPayload(event.target.value)}
            rows={5}
            maxLength={255}
          />
          <p className="mt-1 text-xs text-muted-foreground">
            Maximum 255 bytes. The backend sends this through the ground RFM95W
            and then returns the radio to RX mode.
          </p>
        </div>

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
            className="max-w-xs"
          />
        </div>

        <div className="flex flex-wrap gap-2">
          <Button
            onClick={() => sendCommand(payload)}
            disabled={sending || payload.trim().length === 0}
          >
            {sending ? "Sending..." : "Send command"}
          </Button>

          <Button
            variant="secondary"
            onClick={() => {
              setPayload("PING_GROUND_0001")
              sendCommand("PING_GROUND_0001")
            }}
            disabled={sending}
          >
            Send PING
          </Button>

          <Button
            variant="outline"
            onClick={() => setPayload("PING_GROUND_0001")}
            disabled={sending}
          >
            Load PING
          </Button>
        </div>

        {result ? (
          <Alert>
            <AlertTitle>Command sent</AlertTitle>
            <AlertDescription>{result}</AlertDescription>
          </Alert>
        ) : null}

        {error ? (
          <Alert variant="destructive">
            <AlertTitle>Command failed</AlertTitle>
            <AlertDescription>{error}</AlertDescription>
          </Alert>
        ) : null}
      </div>

      <Alert>
        <AlertTitle>RF safety reminder</AlertTitle>
        <AlertDescription>
          Use the command panel only when both RFM95W antennas are connected.
          This page transmits from the ground-side radio.
        </AlertDescription>
      </Alert>
    </div>
  )
}