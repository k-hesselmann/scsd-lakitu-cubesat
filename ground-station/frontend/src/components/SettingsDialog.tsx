import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog"
import { ThresholdSettingsPanel } from "@/components/ThresholdSettingsPanel"

export function SettingsDialog({
  open,
  onOpenChange,
}: {
  open: boolean
  onOpenChange: (open: boolean) => void
}) {
  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="max-w-xl">
        <DialogHeader>
          <DialogTitle>Ground Station Settings</DialogTitle>
        </DialogHeader>

        <div className="max-h-[70vh] overflow-auto pr-2">
          <ThresholdSettingsPanel />
        </div>
      </DialogContent>
    </Dialog>
  )
}