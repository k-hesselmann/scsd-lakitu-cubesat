import { useState } from "react"

import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card"
import { HistoryTable } from "@/components/HistoryTable"
import { TelemetryPacketModal } from "@/components/TelemetryPacketModal"
import type { DashboardPageProps } from "@/pages/pageTypes"
import type { TelemetryRow } from "@/types/telemetry"

export function RawTelemetryPage({ history }: DashboardPageProps) {
  const [selectedRow, setSelectedRow] = useState<TelemetryRow | null>(null)

  return (
    <div className="flex h-full w-full flex-col overflow-hidden p-3">
      <header className="mb-2 shrink-0">
        <h1 className="text-xl font-bold tracking-tight">Raw Telemetry</h1>
      </header>

      <Card className="min-h-0 flex-1 overflow-hidden">
        <CardHeader className="px-4 py-3">
          <CardTitle className="text-base">
            Recent Telemetry History — All Fields
          </CardTitle>
        </CardHeader>

        <CardContent className="h-[calc(100%-56px)] min-h-0 px-4 pb-4">
          <HistoryTable history={history} onRowClick={setSelectedRow} />
        </CardContent>
      </Card>

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