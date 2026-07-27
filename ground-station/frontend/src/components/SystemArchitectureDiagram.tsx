import { Tooltip } from "@base-ui/react/tooltip"
import {
  ActivityIcon,
  BatteryIcon,
  BrainCircuitIcon,
  CpuIcon,
  GaugeIcon,
  HardDriveIcon,
  RadioIcon,
  SatelliteIcon,
  ServerIcon,
  type LucideIcon,
} from "lucide-react"

import { useAlertThresholds } from "@/hooks/useAlertThresholds"
import { useCurrentTime } from "@/hooks/useCurrentTime"
import {
  deriveArchitectureHealth,
  type ArchitectureInterfaceHealth,
  type ArchitectureNodeHealth,
  type HealthState,
} from "@/lib/architectureHealth"
import { cn } from "@/lib/utils"
import type { BackendStatus, TelemetryRow } from "@/types/telemetry"

const STATE_STYLES: Record<HealthState, {
  node: string
  badge: string
  dot: string
  line: string
}> = {
  nominal: {
    node: "border-emerald-500/70 bg-emerald-50/90 dark:bg-emerald-950/25",
    badge: "bg-emerald-600 text-white dark:bg-emerald-500 dark:text-emerald-950",
    dot: "bg-emerald-500",
    line: "stroke-emerald-500",
  },
  warning: {
    node: "border-amber-500/70 bg-amber-50/90 dark:bg-amber-950/25",
    badge: "bg-amber-500 text-amber-950",
    dot: "bg-amber-500",
    line: "stroke-amber-500",
  },
  critical: {
    node: "border-red-500/80 bg-red-50/90 dark:bg-red-950/25",
    badge: "bg-red-600 text-white",
    dot: "bg-red-500",
    line: "stroke-red-500",
  },
  disabled: {
    node: "border-slate-400/70 bg-slate-100/90 dark:bg-slate-900/40",
    badge: "bg-slate-500 text-white",
    dot: "bg-slate-400",
    line: "stroke-slate-400",
  },
  unknown: {
    node: "border-border bg-background/95",
    badge: "border bg-background text-muted-foreground",
    dot: "bg-slate-300 dark:bg-slate-600",
    line: "stroke-slate-300 dark:stroke-slate-600",
  },
}

const STATE_LABELS: Record<HealthState, string> = {
  nominal: "Nominal",
  warning: "Warning",
  critical: "Critical",
  disabled: "Disabled",
  unknown: "Unknown",
}

const MARKERS: Record<HealthState, string> = {
  nominal: "url(#architecture-arrow-nominal)",
  warning: "url(#architecture-arrow-warning)",
  critical: "url(#architecture-arrow-critical)",
  disabled: "url(#architecture-arrow-disabled)",
  unknown: "url(#architecture-arrow-unknown)",
}

function ArchitectureNode({
  title,
  icon: Icon,
  health,
  className,
}: {
  title: string
  icon: LucideIcon
  health: ArchitectureNodeHealth
  className?: string
}) {
  const style = STATE_STYLES[health.state]
  return (
    <section
      className={cn(
        "rounded-xl border border-l-4 p-2 shadow-sm backdrop-blur-sm",
        style.node,
        className,
      )}
      aria-label={`${title} health: ${health.badge ?? STATE_LABELS[health.state]}`}
    >
      <header className="flex items-center gap-1.5">
        <span className="flex size-6 shrink-0 items-center justify-center rounded-lg border bg-background/80">
          <Icon aria-hidden="true" className="size-3.5" />
        </span>
        <h3 className="min-w-0 flex-1 truncate text-xs font-bold">{title}</h3>
        <span className={cn("rounded-full px-1.5 py-0.5 text-[8px] font-bold uppercase tracking-wide", style.badge)}>
          {health.badge ?? STATE_LABELS[health.state]}
        </span>
      </header>
      {health.items.length > 0 && (
        <dl className="mt-2 space-y-0.5 text-[10px] leading-4">
          {health.items.map((item) => (
            <div key={item.label} className="flex min-w-0 items-baseline justify-between gap-2">
              <dt className="shrink-0 text-muted-foreground">{item.label}</dt>
              <dd className="truncate text-right font-medium" title={item.value}>{item.value}</dd>
            </div>
          ))}
        </dl>
      )}
    </section>
  )
}

function InterfaceTag({
  health,
  className,
}: {
  health: ArchitectureInterfaceHealth
  className?: string
}) {
  const style = STATE_STYLES[health.state]
  return (
    <Tooltip.Root>
      <Tooltip.Trigger
        delay={250}
        className={cn(
          "z-20 flex cursor-help items-center gap-1 rounded-full border bg-background/95 px-2 py-1 text-[9px] font-bold shadow-sm outline-none focus-visible:ring-2 focus-visible:ring-ring",
          className,
        )}
        aria-label={`${health.label}. ${health.evidence}`}
      >
        <span aria-hidden="true" className={cn("size-1.5 rounded-full", style.dot)} />
        {health.label}
      </Tooltip.Trigger>
      <Tooltip.Portal>
        <Tooltip.Positioner side="top" sideOffset={7} className="z-50">
          <Tooltip.Popup className="w-72 max-w-[calc(100vw-2rem)] rounded-lg border bg-popover p-2.5 text-xs leading-relaxed text-popover-foreground shadow-lg">
            <p className="font-semibold">{health.label}</p>
            <p className="mt-1 text-muted-foreground">{health.evidence}</p>
          </Tooltip.Popup>
        </Tooltip.Positioner>
      </Tooltip.Portal>
    </Tooltip.Root>
  )
}

function ArchitecturePath({
  d,
  state,
  startArrow = false,
  endArrow = true,
}: {
  d: string
  state: HealthState
  startArrow?: boolean
  endArrow?: boolean
}) {
  return (
    <path
      d={d}
      fill="none"
      strokeWidth="2.25"
      strokeLinecap="round"
      strokeLinejoin="round"
      vectorEffect="non-scaling-stroke"
      className={cn("transition-colors", STATE_STYLES[state].line)}
      markerStart={startArrow ? MARKERS[state] : undefined}
      markerEnd={endArrow ? MARKERS[state] : undefined}
    />
  )
}

function ArrowDefinitions() {
  return (
    <defs>
      {[
        ["nominal", "#10b981"],
        ["warning", "#f59e0b"],
        ["critical", "#ef4444"],
        ["disabled", "#94a3b8"],
        ["unknown", "#cbd5e1"],
      ].map(([name, color]) => (
        <marker
          key={name}
          id={`architecture-arrow-${name}`}
          markerWidth="7"
          markerHeight="7"
          refX="6"
          refY="3.5"
          orient="auto-start-reverse"
          markerUnits="strokeWidth"
        >
          <path d="M0,0 L7,3.5 L0,7 Z" fill={color} />
        </marker>
      ))}
    </defs>
  )
}

function HealthLegend() {
  return (
    <div className="flex flex-wrap items-center gap-x-3 gap-y-1 text-[9px] text-muted-foreground">
      {(Object.keys(STATE_LABELS) as HealthState[]).map((state) => (
        <span key={state} className="flex items-center gap-1">
          <span className={cn("size-1.5 rounded-full", STATE_STYLES[state].dot)} />
          {STATE_LABELS[state]}
        </span>
      ))}
      <span className="ml-auto">I²C is direct telemetry; other interface tags are inferred.</span>
    </div>
  )
}

function MobileArrow() {
  return (
    <div aria-hidden="true" className="mx-auto h-5 w-px bg-border after:block after:size-1.5 after:-translate-x-[3px] after:translate-y-3 after:rotate-45 after:border-r after:border-b" />
  )
}

export function SystemArchitectureDiagram({
  latest,
  backendStatus,
  connected,
  frontendError,
}: {
  latest: TelemetryRow | null
  backendStatus?: BackendStatus | null
  connected: boolean
  frontendError?: string | null
}) {
  const { thresholds } = useAlertThresholds()
  const nowMs = useCurrentTime()
  const health = deriveArchitectureHealth({
    latest,
    backendStatus,
    connected,
    frontendError,
    thresholds,
    nowMs,
  })
  const { nodes, interfaces } = health

  return (
    <div className="flex h-full min-h-0 flex-col gap-1.5 overflow-hidden p-2">
      <div className="min-h-0 flex-1 overflow-auto">
        <div className="hidden min-h-[600px] min-w-[940px] lg:block">
          <div className="relative h-[620px] w-full">
            <svg
              aria-hidden="true"
              viewBox="0 0 1000 620"
              preserveAspectRatio="none"
              className="absolute inset-0 z-0 h-full w-full"
            >
              <ArrowDefinitions />

              <ArchitecturePath d="M200 55 H290" state={interfaces.i2c.state} endArrow={false} />
              <ArchitecturePath d="M200 155 H290" state={interfaces.i2c.state} endArrow={false} />
              <ArchitecturePath d="M200 205 H290" state={interfaces.i2c.state} endArrow={false} />
              <ArchitecturePath d="M290 55 V205" state={interfaces.i2c.state} endArrow={false} />
              <ArchitecturePath d="M290 130 C335 130 345 65 380 65" state={interfaces.i2c.state} />

              <ArchitecturePath d="M200 255 H315 C355 255 350 90 380 90" state={interfaces.adc.state} />
              <ArchitecturePath d="M200 305 H340 C375 305 360 115 380 115" state={interfaces.uart.state} />
              <ArchitecturePath d="M490 125 V260" state={interfaces.sdSpi.state} />
              <ArchitecturePath d="M600 70 H755" state={interfaces.loraSpi.state} startArrow />

              <ArchitecturePath d="M830 140 C830 170 710 165 710 190" state={nodes.downlink.state} />
              <ArchitecturePath d="M710 305 C710 335 830 325 830 355" state={nodes.downlink.state} />
              <ArchitecturePath d="M920 355 C920 325 910 335 910 305" state={nodes.uplink.state} />
              <ArchitecturePath d="M910 190 C910 165 920 170 920 140" state={nodes.uplink.state} />

              <ArchitecturePath d="M870 437 V490" state={interfaces.usbSpi.state} startArrow />
            </svg>

            <ArchitectureNode title="GNSS" icon={SatelliteIcon} health={nodes.gnss} className="absolute left-0 top-[3%] z-10 w-[20%]" />
            <ArchitectureNode title="IMU" icon={ActivityIcon} health={nodes.imu} className="absolute left-0 top-[22%] z-10 w-[20%]" />
            <ArchitectureNode title="Barometer" icon={GaugeIcon} health={nodes.barometer} className="absolute left-0 top-[30%] z-10 w-[20%]" />
            <ArchitectureNode title="Battery" icon={BatteryIcon} health={nodes.battery} className="absolute left-0 top-[38%] z-10 w-[20%]" />
            <ArchitectureNode title="Coral" icon={BrainCircuitIcon} health={nodes.coral} className="absolute left-0 top-[46%] z-10 w-[20%]" />

            <ArchitectureNode title="OBC" icon={CpuIcon} health={nodes.obc} className="absolute left-[38%] top-[3%] z-10 w-[22%]" />
            <ArchitectureNode title="SD card" icon={HardDriveIcon} health={nodes.sd} className="absolute left-[40%] top-[42%] z-10 w-[18%]" />
            <ArchitectureNode title="Flight LoRa" icon={RadioIcon} health={nodes.lora} className="absolute left-[75.5%] top-[4%] z-10 w-[23%]" />
            <ArchitectureNode title="Downlink" icon={RadioIcon} health={nodes.downlink} className="absolute left-[63%] top-[31%] z-10 w-[16%]" />
            <ArchitectureNode title="Uplink" icon={RadioIcon} health={nodes.uplink} className="absolute left-[83%] top-[31%] z-10 w-[16%]" />
            <ArchitectureNode title="Ground radio" icon={RadioIcon} health={nodes.groundRadio} className="absolute left-[75.5%] top-[57%] z-10 w-[23%]" />
            <ArchitectureNode title="Ground station" icon={ServerIcon} health={nodes.groundStation} className="absolute left-[75.5%] top-[79%] z-10 w-[23%]" />

            <InterfaceTag health={interfaces.i2c} className="absolute left-[27%] top-[18%]" />
            <InterfaceTag health={interfaces.adc} className="absolute left-[27%] top-[39%]" />
            <InterfaceTag health={interfaces.uart} className="absolute left-[27%] top-[47%]" />
            <InterfaceTag health={interfaces.sdSpi} className="absolute left-[49%] top-[29%] -translate-x-1/2 -translate-y-1/2" />
            <InterfaceTag health={interfaces.loraSpi} className="absolute left-[67.75%] top-[11.3%] -translate-x-1/2 -translate-y-1/2" />
            <InterfaceTag health={interfaces.usbSpi} className="absolute left-[87%] top-[73%] -translate-x-1/2" />
          </div>
        </div>

        <div className="space-y-2 p-2 lg:hidden">
          <ArchitectureNode title="OBC" icon={CpuIcon} health={nodes.obc} />
          <MobileArrow />
          <p className="text-[9px] font-bold uppercase tracking-wider text-muted-foreground">Spacecraft inputs</p>
          <div className="grid grid-cols-1 gap-2 sm:grid-cols-2">
            <ArchitectureNode title="GNSS" icon={SatelliteIcon} health={nodes.gnss} />
            <ArchitectureNode title="IMU" icon={ActivityIcon} health={nodes.imu} />
            <ArchitectureNode title="Barometer" icon={GaugeIcon} health={nodes.barometer} />
            <ArchitectureNode title="Battery" icon={BatteryIcon} health={nodes.battery} />
            <ArchitectureNode title="Coral" icon={BrainCircuitIcon} health={nodes.coral} />
          </div>
          <div className="flex flex-wrap justify-center gap-1.5">
            <InterfaceTag health={interfaces.i2c} />
            <InterfaceTag health={interfaces.adc} />
            <InterfaceTag health={interfaces.uart} />
          </div>
          <MobileArrow />
          <div className="grid grid-cols-1 gap-2 sm:grid-cols-2">
            <div className="space-y-1.5">
              <div className="flex justify-center"><InterfaceTag health={interfaces.sdSpi} /></div>
              <ArchitectureNode title="SD card" icon={HardDriveIcon} health={nodes.sd} />
            </div>
            <div className="space-y-1.5">
              <div className="flex justify-center"><InterfaceTag health={interfaces.loraSpi} /></div>
              <ArchitectureNode title="Flight LoRa" icon={RadioIcon} health={nodes.lora} />
            </div>
          </div>
          <MobileArrow />
          <div className="grid grid-cols-1 gap-2 sm:grid-cols-2">
            <ArchitectureNode title="Downlink" icon={RadioIcon} health={nodes.downlink} />
            <ArchitectureNode title="Uplink" icon={RadioIcon} health={nodes.uplink} />
          </div>
          <MobileArrow />
          <ArchitectureNode title="Ground radio" icon={RadioIcon} health={nodes.groundRadio} />
          <div className="flex justify-center"><InterfaceTag health={interfaces.usbSpi} /></div>
          <MobileArrow />
          <ArchitectureNode title="Ground station" icon={ServerIcon} health={nodes.groundStation} />
        </div>
      </div>

      <div className="px-2">
        <HealthLegend />
      </div>
    </div>
  )
}
