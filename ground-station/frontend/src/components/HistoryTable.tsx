import { useEffect, useMemo, useState } from "react"

import { Button } from "@/components/ui/button"
import { TelemetryColumnHeader } from "@/components/TelemetryColumnHeader"
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "@/components/ui/table"
import {
  telemetryColumns,
  formatTelemetryCell,
} from "@/lib/telemetryColumns"
import { telemetryRowId } from "@/lib/telemetrySeries"
import type { TelemetryRow } from "@/types/telemetry"

const PAGE_SIZE = 25

export function HistoryTable({
  history,
  onRowClick,
}: {
  history: TelemetryRow[]
  onRowClick?: (row: TelemetryRow) => void
}) {
  const [page, setPage] = useState(0)
  const rows = useMemo(() => [...history].reverse(), [history])
  const pageCount = Math.max(1, Math.ceil(rows.length / PAGE_SIZE))
  const activePage = Math.min(page, pageCount - 1)
  const visibleRows = rows.slice(
    activePage * PAGE_SIZE,
    (activePage + 1) * PAGE_SIZE,
  )

  useEffect(() => {
    if (page >= pageCount) setPage(pageCount - 1)
  }, [page, pageCount])

  if (rows.length === 0) {
    return <p className="text-sm text-muted-foreground">No history yet.</p>
  }

  const firstVisible = activePage * PAGE_SIZE + 1
  const lastVisible = Math.min((activePage + 1) * PAGE_SIZE, rows.length)

  return (
    <div className="flex h-full min-h-0 w-full flex-col gap-2">
      <div className="flex shrink-0 items-center justify-between gap-3 text-xs text-muted-foreground">
        <span>
          Showing {firstVisible}-{lastVisible} of {rows.length} packets
        </span>
        <div className="flex items-center gap-2">
          <Button
            variant="outline"
            size="sm"
            className="h-7 px-2 text-xs"
            disabled={activePage === 0}
            onClick={() => setPage((value) => Math.max(0, value - 1))}
          >
            Newer
          </Button>
          <span className="tabular-nums">
            Page {activePage + 1}/{pageCount}
          </span>
          <Button
            variant="outline"
            size="sm"
            className="h-7 px-2 text-xs"
            disabled={activePage >= pageCount - 1}
            onClick={() => setPage((value) => Math.min(pageCount - 1, value + 1))}
          >
            Older
          </Button>
        </div>
      </div>

      <div className="min-h-0 flex-1 overflow-auto rounded-md border">
        <Table className="min-w-max text-xs">
          <TableHeader className="sticky top-0 z-10 bg-background">
            <TableRow>
              {telemetryColumns.map((column) => (
                <TableHead
                  key={column.key}
                  className="whitespace-nowrap border-r px-2 py-2"
                >
                  <TelemetryColumnHeader column={column} />
                </TableHead>
              ))}
            </TableRow>
          </TableHeader>

          <TableBody>
            {visibleRows.map((row) => (
              <TableRow
                key={telemetryRowId(row)}
                onClick={() => onRowClick?.(row)}
                className="cursor-pointer hover:bg-muted"
              >
                {telemetryColumns.map((column) => (
                  <TableCell
                    key={column.key}
                    className="whitespace-nowrap border-r px-2 py-1"
                  >
                    {formatTelemetryCell(row, column)}
                  </TableCell>
                ))}
              </TableRow>
            ))}
          </TableBody>
        </Table>
      </div>
    </div>
  )
}
