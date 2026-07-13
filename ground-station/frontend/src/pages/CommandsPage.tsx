import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card"
import { CommandPanel } from "@/components/CommandPanel"

export function CommandsPage() {
  return (
    <div className="flex h-full w-full flex-col overflow-hidden p-3">
      <header className="mb-2 shrink-0">
        <h1 className="text-xl font-bold tracking-tight">Commands</h1>
      </header>

      <Card className="min-h-0 flex-1 overflow-hidden">
        <CardHeader className="px-4 py-3">
          <CardTitle className="text-base">Ground-to-Flight Commands</CardTitle>
        </CardHeader>
        <CardContent className="h-[calc(100%-56px)] overflow-auto px-4 pb-4">
          <CommandPanel />
        </CardContent>
      </Card>
    </div>
  )
}