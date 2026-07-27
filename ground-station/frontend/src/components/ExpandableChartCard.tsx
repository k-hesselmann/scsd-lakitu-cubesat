import { Maximize2Icon } from "lucide-react"
import { useState, type ReactNode } from "react"

import { Button } from "@/components/ui/button"
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card"
import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog"
import { cn } from "@/lib/utils"

export function ExpandableChartCard({
  title,
  children,
  compact = true,
  contentClassName,
}: {
  title: string
  children: ReactNode
  compact?: boolean
  contentClassName?: string
}) {
  const [expanded, setExpanded] = useState(false)

  return (
    <>
      <Card className="min-h-0 gap-0 overflow-hidden py-0">
        <CardHeader className="flex h-8 shrink-0 flex-row items-center justify-between gap-1 px-2 py-1">
          <CardTitle className={cn("min-w-0 truncate", compact ? "text-xs" : "text-sm")}>
            {title}
          </CardTitle>
          <Button
            type="button"
            variant="ghost"
            size="icon-sm"
            className="size-6 shrink-0"
            aria-label={`Enlarge ${title} graph`}
            title={`Enlarge ${title} graph`}
            onClick={() => setExpanded(true)}
          >
            <Maximize2Icon aria-hidden="true" className="size-3.5" />
          </Button>
        </CardHeader>
        <CardContent
          className={cn(
            compact ? "min-h-0 flex-1 p-1" : "h-[280px] p-1",
            contentClassName,
          )}
        >
          {children}
        </CardContent>
      </Card>

      <Dialog open={expanded} onOpenChange={setExpanded}>
        <DialogContent className="grid h-[min(86vh,760px)] w-[94vw] max-w-[1400px] grid-rows-[auto_minmax(0,1fr)] gap-2 p-3 sm:max-w-[1400px]">
          <DialogHeader className="pr-10">
            <DialogTitle>{title}</DialogTitle>
          </DialogHeader>
          <div className="min-h-0 overflow-hidden rounded-lg border p-1">
            {children}
          </div>
        </DialogContent>
      </Dialog>
    </>
  )
}
