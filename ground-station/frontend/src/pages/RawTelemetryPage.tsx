import { useState } from "react"

import { HistoryTable } from "@/components/HistoryTable"
import { TelemetryPacketModal } from "@/components/TelemetryPacketModal"
import type { DashboardPageProps } from "@/pages/pageTypes"
import type { TelemetryRow } from "@/types/telemetry"

export function RawTelemetryPage({ history }: DashboardPageProps) {
  const [selectedRow, setSelectedRow] = useState<TelemetryRow | null>(null)

  return (
    <div className="flex h-full w-full flex-col overflow-hidden p-2">
      <header className="mb-1.5 shrink-0">
        <h1 className="text-xl font-bold tracking-tight">Raw Telemetry</h1>
      </header>

      <div className="min-h-0 flex-1">
        <HistoryTable history={history} onRowClick={setSelectedRow} />
      </div>

      <TelemetryPacketModal
        row={selectedRow}
        open={selectedRow !== null}
        onOpenChange={(open) => {
          if (!open) setSelectedRow(null)
        }}
      />
    </div>
  )
}
