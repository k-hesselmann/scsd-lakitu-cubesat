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

export function ExpandablePanelCard({
  title,
  children,
  actions,
  size = "default",
  className,
  headerClassName,
  contentClassName,
  expandedContentClassName,
}: {
  title: string
  children: ReactNode
  actions?: ReactNode
  size?: "default" | "sm"
  className?: string
  headerClassName?: string
  contentClassName?: string
  expandedContentClassName?: string
}) {
  const [expanded, setExpanded] = useState(false)

  const controls = actions ? <div className="flex items-center gap-2">{actions}</div> : null

  return (
    <>
      <Card size={size} className={cn("min-h-0 overflow-hidden", className)}>
        <CardHeader className={cn("flex shrink-0 flex-row items-center justify-between gap-2", headerClassName)}>
          <CardTitle className="min-w-0 truncate text-xs">{title}</CardTitle>
          <div className="flex shrink-0 items-center gap-2">
            {controls}
            <Button
              type="button"
              variant="ghost"
              size="icon-sm"
              className="size-7"
              aria-label={`Enlarge ${title}`}
              title={`Enlarge ${title}`}
              onClick={() => setExpanded(true)}
            >
              <Maximize2Icon aria-hidden="true" className="size-3.5" />
            </Button>
          </div>
        </CardHeader>
        <CardContent className={cn("min-h-0", contentClassName)}>
          {children}
        </CardContent>
      </Card>

      <Dialog open={expanded} onOpenChange={setExpanded}>
        <DialogContent className="grid h-[min(90vh,900px)] w-[94vw] max-w-[1500px] grid-rows-[auto_minmax(0,1fr)] gap-2 p-3 sm:max-w-[1500px]">
          <DialogHeader className="flex min-h-8 flex-row items-center justify-between gap-3 pr-10">
            <DialogTitle className="truncate">{title}</DialogTitle>
            {controls}
          </DialogHeader>
          <div className={cn("min-h-0 overflow-hidden", expandedContentClassName)}>
            {children}
          </div>
        </DialogContent>
      </Dialog>
    </>
  )
}
