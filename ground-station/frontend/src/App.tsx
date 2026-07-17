import { useState } from "react"
import {
  Activity,
  AlertTriangle,
  Home,
  MapPinned,
  RadioTower,
  Send,
  Settings,
  Table2,
} from "lucide-react"
import {
  BrowserRouter,
  Navigate,
  NavLink,
  Route,
  Routes,
} from "react-router-dom"

import { MissionStrip } from "@/components/MissionStrip"
import { SettingsDialog } from "@/components/SettingsDialog"
import { useTelemetryWebSocket } from "@/hooks/useTelemetryWebSocket"

import { OverviewPage } from "@/pages/OverviewPage"
import { HealthPage } from "@/pages/HealthPage"
import { PositionPage } from "@/pages/PositionPage"
import { RawTelemetryPage } from "@/pages/RawTelemetryPage"
import { LinkAnalyticsPage } from "@/pages/LinkAnalyticsPage"
import { CommandsPage } from "@/pages/CommandsPage"

function navClass({ isActive }: { isActive: boolean }) {
  return [
    "flex h-12 w-12 items-center justify-center rounded-xl transition",
    isActive
      ? "bg-primary text-primary-foreground"
      : "text-muted-foreground hover:bg-muted hover:text-foreground",
  ].join(" ")
}

function iconButtonClass() {
  return "flex h-12 w-12 items-center justify-center rounded-xl text-muted-foreground transition hover:bg-muted hover:text-foreground"
}

export default function App() {
  const dashboardData = useTelemetryWebSocket(500)
  const [settingsOpen, setSettingsOpen] = useState(false)

  return (
    <BrowserRouter>
      <main className="h-screen w-screen overflow-hidden bg-background">
        <div className="grid h-full w-full grid-cols-[72px_minmax(0,1fr)]">
          <aside className="flex h-full flex-col items-center border-r bg-card py-4">
            <div className="mb-6 flex h-12 w-12 items-center justify-center rounded-xl bg-primary text-primary-foreground">
              <Activity className="h-6 w-6" />
            </div>

            <nav className="flex flex-1 flex-col items-center gap-2">
              <NavLink to="/overview" className={navClass} title="Overview">
                <Home className="h-5 w-5" />
              </NavLink>

              <NavLink to="/health" className={navClass} title="Health">
                <AlertTriangle className="h-5 w-5" />
              </NavLink>

              <NavLink to="/position" className={navClass} title="Position / Flight phase">
                <MapPinned className="h-5 w-5" />
              </NavLink>

              <NavLink to="/link" className={navClass} title="Packet-quality analytics">
                <RadioTower className="h-5 w-5" />
              </NavLink>

              <NavLink to="/commands" className={navClass} title="Commands">
                <Send className="h-5 w-5" />
              </NavLink>

              <NavLink to="/raw" className={navClass} title="Raw telemetry">
                <Table2 className="h-5 w-5" />
              </NavLink>
            </nav>

            <button
              className={iconButtonClass()}
              title="Settings"
              onClick={() => setSettingsOpen(true)}
            >
              <Settings className="h-5 w-5" />
            </button>
          </aside>

          <section className="flex h-full min-w-0 flex-col overflow-hidden">
            <MissionStrip
              latest={dashboardData.latest}
              backendStatus={dashboardData.backendStatus}
              connected={dashboardData.connected}
              error={dashboardData.error}
            />

            <div className="min-h-0 flex-1 overflow-hidden">
              <Routes>
                <Route path="/" element={<Navigate to="/overview" replace />} />
                <Route path="/overview" element={<OverviewPage {...dashboardData} />} />
                <Route path="/health" element={<HealthPage {...dashboardData} />} />
                <Route path="/position" element={<PositionPage {...dashboardData} />} />
                <Route path="/phase" element={<Navigate to="/position" replace />} />
                <Route path="/commands" element={<CommandsPage latest={dashboardData.latest} />} />
                <Route path="/link" element={<LinkAnalyticsPage {...dashboardData} />} />
                <Route path="/raw" element={<RawTelemetryPage {...dashboardData} />} />
              </Routes>
            </div>
          </section>
        </div>

        <SettingsDialog open={settingsOpen} onOpenChange={setSettingsOpen} />
      </main>
    </BrowserRouter>
  )
}