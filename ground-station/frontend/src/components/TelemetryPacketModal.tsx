import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog"
import {
  Table,
  TableBody,
  TableCell,
  TableRow,
} from "@/components/ui/table"

import type { TelemetryRow } from "@/types/telemetry"
import {
  telemetryColumns,
  formatTelemetryCell,
} from "@/lib/telemetryColumns"

export function TelemetryPacketModal({
  row,
  open,
  onOpenChange,
}: {
  row: TelemetryRow | null
  open: boolean
  onOpenChange: (open: boolean) => void
}) {
  if (!row) return null

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="max-h-[90vh] w-[96vw] max-w-[1400px] overflow-hidden sm:max-w-[1400px]">
        <DialogHeader>
          <DialogTitle>
            Telemetry Packet — Seq {row.sequence_number ?? "—"}
          </DialogTitle>
        </DialogHeader>

        <div className="max-h-[72vh] overflow-auto rounded-md border">
          <Table className="text-sm">
            <TableBody>
              {telemetryColumns.map((column) => (
                <TableRow key={column.key}>
                  <TableCell className="w-[260px] whitespace-nowrap border-r font-medium">
                    {column.label}
                  </TableCell>
                  <TableCell className="whitespace-nowrap">
                    {formatTelemetryCell(row, column)}
                  </TableCell>
                </TableRow>
              ))}
            </TableBody>
          </Table>
        </div>
      </DialogContent>
    </Dialog>
  )
}
