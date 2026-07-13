import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "@/components/ui/table"
import type { TelemetryRow } from "@/types/telemetry"
import {
  telemetryColumns,
  formatTelemetryCell,
} from "@/lib/telemetryColumns"

export function HistoryTable({
  history,
  onRowClick,
}: {
  history: TelemetryRow[]
  onRowClick?: (row: TelemetryRow) => void
}) {
  const rows = [...history].reverse()

  if (rows.length === 0) {
    return <p className="text-sm text-muted-foreground">No history yet.</p>
  }

  return (
    <div className="h-full min-h-0 w-full overflow-auto rounded-md border">
      <Table className="min-w-max text-xs">
        <TableHeader className="sticky top-0 z-10 bg-background">
          <TableRow>
            {telemetryColumns.map((column) => (
              <TableHead
                key={column.key}
                className="whitespace-nowrap border-r px-2 py-2"
              >
                {column.label}
              </TableHead>
            ))}
          </TableRow>
        </TableHeader>

        <TableBody>
          {rows.map((row, index) => (
            <TableRow
              key={`${row.sequence_number}-${index}`}
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
  )
}