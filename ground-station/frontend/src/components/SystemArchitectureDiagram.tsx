import { Badge } from "@/components/ui/badge"
import { fmt } from "@/lib/format"
import {
  EQUIPMENT_BARO,
  EQUIPMENT_GPS,
  EQUIPMENT_IMU,
  EQUIPMENT_LORA,
  EQUIPMENT_SD,
  LORA_EVENT_CONFIG_FAIL,
  LORA_EVENT_INIT_OK,
  hasEquipmentFault,
  rawFlagIsValid,
} from "@/lib/v4Telemetry"
import type { BackendStatus, TelemetryRow } from "@/types/telemetry"

type Variant = "nominal" | "warning" | "critical" | "unknown"

type StatusCardProps = { title: string; detail: string; variant: Variant; tags: string[] }

function variantClass(variant: Variant) {
  return {
    nominal: "border-green-500 bg-green-50",
    warning: "border-amber-500 bg-amber-50",
    critical: "border-red-500 bg-red-50",
    unknown: "border-slate-300 bg-white",
  }[variant]
}

function badgeVariant(variant: Variant) {
  return variant === "critical" ? "destructive" : variant === "nominal" ? "default" : "secondary"
}

function statusFromValidity(valid: boolean, fault: boolean | undefined): Variant {
  if (fault) return "critical"
  return valid ? "nominal" : "warning"
}

function StatusCard({ title, detail, variant, tags }: StatusCardProps) {
  return (
    <section className={`rounded-xl border-2 p-3 ${variantClass(variant)}`}>
      <div className="flex items-start justify-between gap-2">
        <h3 className="text-sm font-bold">{title}</h3>
        <Badge variant={badgeVariant(variant)}>{variant.toUpperCase()}</Badge>
      </div>
      <p className="mt-1 text-xs text-muted-foreground">{detail}</p>
      <div className="mt-2 flex flex-wrap gap-1">
        {tags.map((tag) => <Badge key={tag} variant="outline" className="text-[10px]">{tag}</Badge>)}
      </div>
    </section>
  )
}

export function SystemArchitectureDiagram({
  latest,
  backendStatus,
}: {
  latest: TelemetryRow | null
  backendStatus?: BackendStatus | null
}) {
  if (!latest) {
    return <div className="flex h-full items-center justify-center rounded-xl border bg-slate-50 text-sm text-muted-foreground">Waiting for a raw-v7 telemetry packet.</div>
  }

  const receiver = backendStatus?.receiver
  const stats = backendStatus?.stats
  const gpsFault = hasEquipmentFault(latest, EQUIPMENT_GPS)
  const imuFault = hasEquipmentFault(latest, EQUIPMENT_IMU)
  const baroFault = hasEquipmentFault(latest, EQUIPMENT_BARO)
  const sdFault = hasEquipmentFault(latest, EQUIPMENT_SD)
  const loraFault = hasEquipmentFault(latest, EQUIPMENT_LORA)
  const gps = statusFromValidity(rawFlagIsValid(latest.gps_valid_raw), gpsFault)
  const imu = statusFromValidity(rawFlagIsValid(latest.imu_valid_raw), imuFault)
  const baro = statusFromValidity(rawFlagIsValid(latest.baro_valid_raw), baroFault)
  const battery = statusFromValidity(rawFlagIsValid(latest.batt_valid_raw), false)
  const coral = statusFromValidity(rawFlagIsValid(latest.coral_valid_raw), false)

  // This packet arriving proves a successful flight-to-ground transmission.
  // The embedded values are the onboard snapshot taken before that transmission.
  const loraConfigFailed = latest.lora_last_event === LORA_EVENT_CONFIG_FAIL
  const loraConfigPassed = latest.lora_last_event === LORA_EVENT_INIT_OK
  const priorRadioFailures = typeof latest.lora_consecutive_failures === "number" && latest.lora_consecutive_failures > 0
  const flightTx: Variant = loraConfigFailed || loraFault || priorRadioFailures ? "warning" : "nominal"
  const registerCheck = loraConfigFailed ? "FAILED" : loraConfigPassed ? "PASSED" : "NOT REPORTED"
  const txDetail = loraConfigFailed
    ? "Current downlink arrived, but the pre-TX register verification snapshot reports a failure."
    : priorRadioFailures
      ? "Current downlink arrived after one or more preceding onboard radio failures."
      : "Current telemetry reception confirms the flight downlink transmitter completed this packet."

  // Flight RX health combines onboard readback with end-to-end protocol evidence
  // observed on the ground. A missing command response cannot identify which
  // direction lost the packet, so it is reported as an unconfirmed uplink.
  const rxModeActive = rawFlagIsValid(latest.lora_rx_mode_active)
  const rxStatus = latest.lora_last_rx_status
  const rxHardwareFailure = rxStatus === 5 || rxStatus === 6
  const rxPacketWarning = rxStatus === 3 || rxStatus === 4
  const duplicateTelemetry = latest.is_duplicate_packet === true
  const commandOutcome = receiver?.last_command_outcome
  const commandUnconfirmed = commandOutcome === "retrying" || commandOutcome === "unacknowledged"
  const ackTxFailed = receiver?.last_telemetry_ack_ok === false
  const unexpectedAck = latest.uplink_last_status_name === "UNEXPECTED_ACK"

  let flightRx: Variant = "nominal"
  if (!rxModeActive || rxHardwareFailure) flightRx = "critical"
  else if (duplicateTelemetry || commandUnconfirmed || ackTxFailed || rxPacketWarning || unexpectedAck || loraFault) flightRx = "warning"

  let rxDetail = "Flight RX is active; no current end-to-end uplink warning is present."
  if (!rxModeActive) rxDetail = "The flight radio reported that continuous RX mode was inactive before this downlink."
  else if (rxHardwareFailure) rxDetail = `The flight receiver reported ${latest.lora_last_rx_status_name ?? "a hardware/configuration error"}.`
  else if (commandOutcome === "unacknowledged") rxDetail = `Command ${receiver?.last_command_id ?? "—"} was not confirmed after ${receiver?.last_command_attempt ?? "—"} attempt(s); the uplink or its telemetry response may have been lost.`
  else if (duplicateTelemetry) rxDetail = `Telemetry sequence ${latest.sequence_number ?? "—"} was duplicated: flight did not confirm the previous ground ACK before its retry deadline. The ground ACK was sent again.`
  else if (commandOutcome === "retrying") rxDetail = `Command ${receiver?.last_command_id ?? "—"} has not yet been confirmed; the ground station is retrying the same command ID.`
  else if (ackTxFailed) rxDetail = `The ground radio did not complete ACK transmission for telemetry sequence ${receiver?.last_telemetry_ack_sequence ?? "—"}.`
  else if (unexpectedAck) rxDetail = "Flight received an ACK that did not match its outstanding telemetry sequence."
  else if (rxPacketWarning) rxDetail = `The latest onboard RX result was ${latest.lora_last_rx_status_name ?? "invalid"}.`

  const sd: Variant = sdFault ? "critical" : "nominal"
  const obc: Variant = latest.crc_ok === false || latest.protocol_version_ok === false || latest.packet_type_ok === false ? "critical" : "nominal"

  return (
    <div className="h-full overflow-auto rounded-xl border bg-slate-50 p-3">
      <div className="mb-3 flex flex-wrap items-center justify-between gap-2">
        <div>
          <h2 className="text-sm font-bold">Raw-v7 subsystem status</h2>
          <p className="text-xs text-muted-foreground">Flight radio TX and RX are evaluated separately using onboard telemetry and live protocol evidence.</p>
        </div>
        <Badge variant={badgeVariant(obc)}>OBC {obc.toUpperCase()}</Badge>
      </div>
      <div className="grid gap-3 md:grid-cols-2 xl:grid-cols-3">
        <StatusCard title="GPS" variant={gps} detail={`Altitude ${fmt(latest.gnss_altitude_m, " m", 1)} · Vertical speed ${fmt(latest.vertical_speed_ms, " m/s", 2)}`} tags={[`valid ${fmt(latest.gps_valid_raw)}`, `timeouts ${fmt(latest.scv_gps_timeout_count)}`]} />
        <StatusCard title="IMU" variant={imu} detail={`Acceleration magnitude ${fmt(latest.imu_accel_mag_g, " g", 3)}`} tags={[`valid ${fmt(latest.imu_valid_raw)}`, `timeouts ${fmt(latest.scv_imu_timeout_count)}`]} />
        <StatusCard title="Barometer" variant={baro} detail={`${fmt(latest.baro_pressure_pa, " Pa")} · ${fmt(latest.baro_altitude_m, " m", 1)}`} tags={[`valid ${fmt(latest.baro_valid_raw)}`, `timeouts ${fmt(latest.scv_baro_timeout_count)}`]} />
        <StatusCard title="Battery" variant={battery} detail={`${fmt(latest.battery_v, " V", 2)} · SCV last ${fmt(latest.scv_last_batt_mv, " mV")}`} tags={[`valid ${fmt(latest.batt_valid_raw)}`]} />
        <StatusCard title="Coral" variant={coral} detail={fmt(latest.coral_payload_text)} tags={[`valid ${fmt(latest.coral_valid_raw)}`, `timeouts ${fmt(latest.scv_coral_timeout_count)}`]} />
        <StatusCard
          title="Flight TX Health (Downlink)"
          variant={flightTx}
          detail={txDetail}
          tags={[
            `current packet RECEIVED`,
            `register check ${registerCheck}`,
            `event ${fmt(latest.lora_last_event_name)} (${fmt(latest.lora_last_event)})`,
            `preceding failures ${fmt(latest.lora_consecutive_failures)}`,
            `last TX success ${fmt(latest.lora_last_success_ms, " ms")}`,
            `recoveries ${fmt(latest.lora_recovery_count)}`,
          ]}
        />
        <StatusCard
          title="Flight RX Health (Uplink)"
          variant={flightRx}
          detail={rxDetail}
          tags={[
            `RX mode ${rxModeActive ? "ACTIVE" : "INACTIVE"}`,
            `RX status ${fmt(latest.lora_last_rx_status_name)}`,
            `uplink packets ${fmt(latest.lora_rx_packet_count)}`,
            `uplink CRC errors ${fmt(latest.lora_rx_crc_error_count)}`,
            `telemetry ACK timeouts ${fmt(latest.lora_ack_timeout_count)}`,
            `last onboard RX ${fmt(latest.lora_last_rx_ms, " ms")}`,
            `last command ${receiver?.last_command_id ?? "—"}: ${commandOutcome ?? "NONE"}`,
            `command attempt ${receiver?.last_command_attempt ?? "—"}`,
            `flight command status ${fmt(latest.uplink_last_status_name)}`,
            `flight confirmed ACK seq ${fmt(latest.uplink_last_ack_sequence)}`,
            `ground ACK seq ${receiver?.last_telemetry_ack_sequence ?? "—"}: ${receiver?.last_telemetry_ack_ok === true ? "TX DONE" : receiver?.last_telemetry_ack_ok === false ? "FAILED" : "NONE"}`,
            `ground ACK TxDone ${receiver?.telemetry_ack_tx_count ?? 0}`,
            `ground ACK failures ${receiver?.telemetry_ack_tx_failures ?? 0}`,
            `flight accepted commands ${fmt(latest.uplink_command_count)}`,
            `duplicate streak ${fmt(latest.consecutive_duplicate_packets)}`,
            `duplicates total ${stats?.total_duplicate_packets ?? latest.total_duplicate_packets ?? 0}`,
          ]}
        />
        <StatusCard title="SD / SCV" variant={sd} detail={`SD faults ${fmt(latest.scv_sd_fault_count)} · watchdog resets ${fmt(latest.scv_watchdog_reset_count)}`} tags={[`fault ${sdFault === undefined ? "—" : sdFault ? "YES" : "NO"}`, `state ${fmt(latest.flight_state_name)}`]} />
        <StatusCard title="Packet validation" variant={obc} detail={`Sequence ${fmt(latest.sequence_number)} · CRC ${fmt(latest.crc_ok)}`} tags={[`type ${fmt(latest.packet_type)}`, `version ${fmt(latest.protocol_version)}`, `fault mask ${fmt(latest.scv_equipment_faults)}`]} />
      </div>
    </div>
  )
}