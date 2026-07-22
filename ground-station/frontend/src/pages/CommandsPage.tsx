import { CommandPanel } from "@/components/CommandPanel"
import { MetricCard } from "@/components/MetricCard"
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card"
import { fmt } from "@/lib/format"
import type { TelemetryRow } from "@/types/telemetry"

type CommandsPageProps = {
  latest: TelemetryRow | null
}

export function CommandsPage({ latest }: CommandsPageProps) {
  return (
    <div className="flex h-full w-full flex-col overflow-hidden p-3">
      <header className="mb-2 shrink-0">
        <h1 className="text-xl font-bold tracking-tight">Commands</h1>
      </header>

      <section className="mb-3 grid shrink-0 gap-3 sm:grid-cols-2 xl:grid-cols-4">
        <MetricCard
          title="Last Flight Command ID"
          value={fmt(latest?.uplink_last_command_id)}
          subtitle="Latched onboard"
        />
        <MetricCard
          title="Command Status"
          value={fmt(latest?.uplink_last_status_name)}
          subtitle="Independent command latch"
          variant={latest?.uplink_last_status_name === "ACCEPTED" ? "good" : latest?.uplink_last_status_name === "NONE" ? "default" : "warning"}
        />
        <MetricCard
          title="Telemetry ACK Status"
          value={fmt(latest?.uplink_last_ack_status_name)}
          subtitle="Independent ACK latch"
          variant={latest?.uplink_last_ack_status_name === "ACCEPTED" ? "good" : latest?.uplink_last_ack_status_name === "NONE" ? "default" : "warning"}
        />
        <MetricCard
          title="ACK Retry Exhaustions"
          value={fmt(latest?.lora_ack_timeout_count)}
          subtitle="Saturated count since boot"
          variant={!latest ? "default" : (latest.lora_ack_timeout_count ?? 0) > 0 ? "warning" : "good"}
        />
      </section>

      <Card className="min-h-0 flex-1 overflow-hidden">
        <CardHeader className="px-4 py-3">
          <CardTitle className="text-base">Ground-to-Flight Uplink</CardTitle>
        </CardHeader>
        <CardContent className="h-[calc(100%-56px)] overflow-auto px-4 pb-4">
          <CommandPanel
            latestBootCount={latest?.boot_count}
            latestSequence={latest?.sequence_number}
            latestUptimeMs={latest?.obc_uptime_ms}
          />
        </CardContent>
      </Card>
    </div>
  )
}
