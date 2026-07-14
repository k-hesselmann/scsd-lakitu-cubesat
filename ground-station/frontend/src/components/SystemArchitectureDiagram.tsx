import { Badge } from "@/components/ui/badge"
import type { TelemetryRow } from "@/types/telemetry"
import { fmt } from "@/lib/format"

type NodeVariant = "nominal" | "warning" | "critical" | "unknown"

type FlagItem = {
  label: string
  active?: boolean
  badWhenActive?: boolean
}

function variantClass(variant: NodeVariant) {
  switch (variant) {
    case "nominal":
      return "border-green-500 bg-green-50 text-green-950"
    case "warning":
      return "border-amber-500 bg-amber-50 text-amber-950"
    case "critical":
      return "border-red-500 bg-red-50 text-red-950 animate-pulse"
    default:
      return "border-slate-300 bg-white text-slate-950"
  }
}

function nodeStatusLabel(variant: NodeVariant) {
  switch (variant) {
    case "nominal":
      return "NOMINAL"
    case "warning":
      return "WARNING"
    case "critical":
      return "FAULT"
    default:
      return "NO DATA"
  }
}

function badgeVariant(active?: boolean, badWhenActive = false) {
  if (active === undefined || active === null) return "outline"
  if (badWhenActive && active) return "destructive"
  if (!badWhenActive && active) return "default"
  return "secondary"
}

function ArchitectureNode({
  title,
  subtitle,
  flags,
  variant,
  className,
}: {
  title: string
  subtitle?: string
  flags: FlagItem[]
  variant: NodeVariant
  className: string
}) {
  return (
    <div
      className={[
        "absolute z-10 w-[200px] rounded-xl border-2 p-3 shadow-sm",
        variantClass(variant),
        className,
      ].join(" ")}
    >
      <div className="mb-1 flex items-start justify-between gap-2">
        <div className="text-sm font-bold leading-tight">{title}</div>
        <Badge variant={variant === "critical" ? "destructive" : "outline"} className="text-[9px]">
          {nodeStatusLabel(variant)}
        </Badge>
      </div>

      {subtitle ? (
        <div className="mb-2 text-xs text-muted-foreground">{subtitle}</div>
      ) : null}

      <div className="flex flex-wrap gap-1">
        {flags.map((flag) => (
          <Badge
            key={flag.label}
            variant={badgeVariant(flag.active, flag.badWhenActive)}
            className="text-[10px]"
          >
            {flag.label}
          </Badge>
        ))}
      </div>
    </div>
  )
}

function statusFromFlags(valid?: boolean, error?: boolean): NodeVariant {
  if (error) return "critical"
  if (valid === false) return "warning"
  if (valid === true) return "nominal"
  return "unknown"
}

function statusFromBattery(latest: TelemetryRow | null): NodeVariant {
  if (!latest) return "unknown"

  if (latest.BATTERY_VALID === false) return "warning"

  if (typeof latest.battery_v === "number") {
    if (latest.battery_v < 3.3) return "critical"
    if (latest.battery_v < 3.5) return "warning"
  }

  if (latest.BATTERY_VALID === true) return "nominal"

  return "unknown"
}

function statusFromRadio(latest: TelemetryRow | null): NodeVariant {
  if (!latest) return "unknown"

  if (latest.LAST_LORA_TX_OK === false) return "warning"

  if (typeof latest.lora_downlink_rssi_dbm === "number") {
    if (latest.lora_downlink_rssi_dbm < -120) return "critical"
    if (latest.lora_downlink_rssi_dbm < -110) return "warning"
  }

  if (typeof latest.lora_downlink_snr_db === "number") {
    if (latest.lora_downlink_snr_db < -10) return "critical"
    if (latest.lora_downlink_snr_db < 0) return "warning"
  }

  return "nominal"
}

function statusFromObc(latest: TelemetryRow | null): NodeVariant {
  if (!latest) return "unknown"

  if (
    latest.GPS_ERROR ||
    latest.IMU_ERROR ||
    latest.BARO_ERROR ||
    latest.SD_ERROR ||
    latest.crc_ok === false ||
    latest.packet_type_ok === false ||
    latest.protocol_version_ok === false
  ) {
    return "critical"
  }

  if (
    latest.GNSS_FIX_VALID === false ||
    latest.IMU_VALID === false ||
    latest.BARO_VALID === false ||
    latest.SD_LOGGING_OK === false
  ) {
    return "warning"
  }

  return "nominal"
}

export function SystemArchitectureDiagram({
  latest,
}: {
  latest: TelemetryRow | null
}) {
  const gnssStatus = statusFromFlags(
    Boolean(latest?.GNSS_FIX_VALID || latest?.GNSS_TIME_VALID),
    latest?.GPS_ERROR,
  )

  const imuStatus = statusFromFlags(latest?.IMU_VALID, latest?.IMU_ERROR)
  const baroStatus = statusFromFlags(latest?.BARO_VALID, latest?.BARO_ERROR)
  const sdStatus = statusFromFlags(latest?.SD_LOGGING_OK, latest?.SD_ERROR)
  const batteryStatus = statusFromBattery(latest)
  const coralStatus = statusFromFlags(latest?.CORAL_VALID, false)
  const radioStatus = statusFromRadio(latest)
  const obcStatus = statusFromObc(latest)

  return (
    <div className="relative h-full min-h-[560px] overflow-hidden rounded-xl border bg-slate-50">
      <svg className="absolute inset-0 h-full w-full" aria-hidden="true">
        <line x1="50%" y1="45%" x2="20%" y2="18%" stroke="#94a3b8" strokeWidth="2" />
        <line x1="50%" y1="45%" x2="20%" y2="45%" stroke="#94a3b8" strokeWidth="2" />
        <line x1="50%" y1="45%" x2="20%" y2="72%" stroke="#94a3b8" strokeWidth="2" />

        <line x1="50%" y1="45%" x2="80%" y2="18%" stroke="#94a3b8" strokeWidth="2" />
        <line x1="50%" y1="45%" x2="80%" y2="45%" stroke="#94a3b8" strokeWidth="2" />
        <line x1="50%" y1="45%" x2="80%" y2="72%" stroke="#94a3b8" strokeWidth="2" />

        <line x1="50%" y1="45%" x2="50%" y2="82%" stroke="#94a3b8" strokeWidth="2" />

        <text x="31%" y="14%" fontSize="11" fill="#64748b">USART / I2C / SPI</text>
        <text x="61%" y="14%" fontSize="11" fill="#64748b">SPI LoRa link</text>
        <text x="46%" y="77%" fontSize="11" fill="#64748b">SDIO / logging</text>
      </svg>

      <ArchitectureNode
        title="OBC / FSW"
        subtitle={`State ${fmt(latest?.flight_state_name)} · Uptime ${fmt(latest?.obc_uptime_ms, " ms")}`}
        variant={obcStatus}
        className="left-[50%] top-[45%] -translate-x-1/2 -translate-y-1/2"
        flags={[
          { label: "CRC OK", active: latest?.crc_ok },
          { label: "PKT OK", active: latest?.packet_type_ok },
          { label: "PROTO OK", active: latest?.protocol_version_ok },
          { label: "TIME FALLBACK", active: latest?.OBC_TIME_FALLBACK },
        ]}
      />

      <ArchitectureNode
        title="GNSS Receiver"
        subtitle={`${fmt(latest?.gnss_satellites_used)} sats · HDOP ${fmt(latest?.gnss_hdop, "", 2)} · VDOP ${fmt(latest?.gnss_vdop, "", 2)}`}
        variant={gnssStatus}
        className="left-[5%] top-[9%]"
        flags={[
          { label: "FIX", active: latest?.GNSS_FIX_VALID },
          { label: "TIME", active: latest?.GNSS_TIME_VALID },
          { label: "ERR", active: latest?.GPS_ERROR, badWhenActive: true },
        ]}
      />

      <ArchitectureNode
        title="IMU"
        subtitle={`aZ ${fmt(latest?.accel_z_ms2, " m/s²", 2)} · T ${fmt(latest?.imu_temperature_c, " °C", 1)}`}
        variant={imuStatus}
        className="left-[5%] top-[37%]"
        flags={[
          { label: "VALID", active: latest?.IMU_VALID },
          { label: "ERR", active: latest?.IMU_ERROR, badWhenActive: true },
        ]}
      />

      <ArchitectureNode
        title="Barometer"
        subtitle={`${fmt(latest?.baro_pressure_pa, " Pa")} · ${fmt(latest?.baro_altitude_m, " m", 1)}`}
        variant={baroStatus}
        className="left-[5%] top-[65%]"
        flags={[
          { label: "VALID", active: latest?.BARO_VALID },
          { label: "RANGE", active: latest?.BARO_RANGE_VALID },
          { label: "ERR", active: latest?.BARO_ERROR, badWhenActive: true },
        ]}
      />

      <ArchitectureNode
        title="LoRa Radio"
        subtitle={`RSSI ${fmt(latest?.lora_downlink_rssi_dbm, " dBm")} · SNR ${fmt(latest?.lora_downlink_snr_db, " dB", 1)}`}
        variant={radioStatus}
        className="right-[5%] top-[9%]"
        flags={[
          { label: "TX OK", active: latest?.LAST_LORA_TX_OK },
          { label: "CMD RX", active: latest?.COMMAND_RX_SINCE_LAST },
        ]}
      />

      <ArchitectureNode
        title="Battery / Power"
        subtitle={`${fmt(latest?.battery_v, " V", 2)} · ${fmt(latest?.battery_mv, " mV")}`}
        variant={batteryStatus}
        className="right-[5%] top-[37%]"
        flags={[
          { label: "VALID", active: latest?.BATTERY_VALID },
        ]}
      />

      <ArchitectureNode
        title="Coral Payload"
        subtitle={`Status ${fmt(latest?.coral_status)} · Age ${fmt(latest?.coral_result_age_s, " s")}`}
        variant={coralStatus}
        className="right-[5%] top-[65%]"
        flags={[
          { label: "VALID", active: latest?.CORAL_VALID },
          { label: "NEW", active: latest?.CORAL_NEW },
        ]}
      />

      <ArchitectureNode
        title="SD Logger"
        subtitle={`${fmt(latest?.sd_log_record_counter)} records · ${fmt(latest?.sd_error_counter)} errors`}
        variant={sdStatus}
        className="left-[50%] top-[78%] -translate-x-1/2"
        flags={[
          { label: "LOG OK", active: latest?.SD_LOGGING_OK },
          { label: "ERR", active: latest?.SD_ERROR, badWhenActive: true },
        ]}
      />

      <div className="absolute bottom-3 left-3 rounded-md bg-white/85 px-3 py-2 text-xs text-muted-foreground shadow-sm">
        Green = nominal · Amber = warning · Red = critical · Gray = no data
      </div>

      <div className="absolute bottom-3 right-3 rounded-md bg-white/85 px-3 py-2 text-xs text-muted-foreground shadow-sm">
        Seq {fmt(latest?.sequence_number)} · Boot {fmt(latest?.boot_count)} · Commands{" "}
        {fmt(latest?.command_counter)}
      </div>
    </div>
  )
}