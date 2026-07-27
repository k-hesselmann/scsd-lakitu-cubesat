import { Tooltip } from "@base-ui/react/tooltip"
import { InfoIcon } from "lucide-react"

import type { TelemetryColumn } from "@/lib/telemetryColumns"

export function TelemetryColumnHeader({
  column,
}: {
  column: TelemetryColumn
}) {
  return (
    <Tooltip.Root>
      <Tooltip.Trigger
        delay={300}
        closeDelay={100}
        className="group flex w-full cursor-help appearance-none items-center gap-1.5 border-0 bg-transparent p-0 text-left font-inherit text-inherit outline-none focus-visible:rounded-sm focus-visible:ring-2 focus-visible:ring-ring"
        aria-label={`${column.label}: show column description and possible outputs`}
      >
        <span>{column.label}</span>
        <InfoIcon
          aria-hidden="true"
          className="size-3.5 shrink-0 text-muted-foreground transition-colors group-hover:text-foreground group-data-popup-open:text-foreground"
        />
      </Tooltip.Trigger>

      <Tooltip.Portal>
        <Tooltip.Positioner
          side="bottom"
          align="start"
          sideOffset={8}
          className="z-50"
        >
          <Tooltip.Popup className="max-h-[min(70vh,32rem)] w-80 max-w-[calc(100vw-2rem)] origin-[var(--transform-origin)] overflow-y-auto rounded-lg border bg-popover p-3 text-xs text-popover-foreground shadow-lg transition-[transform,opacity] duration-100 data-ending-style:scale-[0.98] data-ending-style:opacity-0 data-starting-style:scale-[0.98] data-starting-style:opacity-0">
            <p className="font-semibold">{column.label}</p>
            <p className="mt-1 whitespace-normal leading-relaxed text-muted-foreground">
              {column.description}
            </p>

            <div className="mt-2 border-t pt-2">
              <p className="font-medium">Output format</p>
              <p className="mt-0.5 whitespace-normal leading-relaxed text-muted-foreground">
                {column.outputFormat}
              </p>
            </div>

            {column.outputs && column.outputs.length > 0 && (
              <div className="mt-2 border-t pt-2">
                <p className="mb-1.5 font-medium">Possible outputs</p>
                <dl className="space-y-1.5">
                  {column.outputs.map((output) => (
                    <div key={output.value} className="grid grid-cols-[minmax(0,0.9fr)_minmax(0,1.4fr)] gap-2">
                      <dt className="whitespace-normal font-mono font-medium">
                        {output.value}
                      </dt>
                      <dd className="whitespace-normal leading-relaxed text-muted-foreground">
                        {output.meaning}
                      </dd>
                    </div>
                  ))}
                </dl>
              </div>
            )}

            <p className="mt-2 border-t pt-2 whitespace-normal text-[11px] leading-relaxed text-muted-foreground">
              — means the value was not available in this packet.
            </p>
          </Tooltip.Popup>
        </Tooltip.Positioner>
      </Tooltip.Portal>
    </Tooltip.Root>
  )
}
