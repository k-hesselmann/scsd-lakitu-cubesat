export type TelemetryRow = {
  // Ground-station reception metadata.
  pc_receive_time_iso?: string
  pc_receive_time_unix?: number
  lora_downlink_rssi_dbm?: number | null
  lora_downlink_snr_db?: number | null
  lora_crc_error?: boolean

  // Raw-v7 packet envelope and ground-side validation.
  packet_type?: number
  protocol_version?: number
  packet_type_ok?: boolean
  protocol_version_ok?: boolean
  length_ok?: boolean
  crc_ok?: boolean
  received_crc16?: string
  calculated_crc16?: string
  sequence_number?: number
  lost_packets_since_previous?: number
  total_lost_packets?: number
  is_duplicate_packet?: boolean
  consecutive_duplicate_packets?: number
  total_duplicate_packets?: number

  // Engineering values derived directly from raw-v7 source units.
  obc_uptime_ms?: number
  flight_state?: number
  flight_state_name?: string
  battery_mv?: number
  battery_v?: number
  latitude_deg?: number
  longitude_deg?: number
  gnss_altitude_m?: number
  ground_speed_ms?: number
  vertical_speed_ms?: number
  baro_pressure_pa?: number
  baro_temperature_c?: number
  baro_altitude_m?: number
  accel_x_ms2?: number
  accel_y_ms2?: number
  accel_z_ms2?: number
  gyro_x_rads?: number
  gyro_y_rads?: number
  gyro_z_rads?: number

  // Raw-v7 datapool snapshot.
  datapool_timestamp_ms?: number
  gps_valid_raw?: number
  imu_accel_x_g?: number
  imu_accel_y_g?: number
  imu_accel_z_g?: number
  imu_accel_mag_g?: number
  imu_gyro_x_dps?: number
  imu_gyro_y_dps?: number
  imu_gyro_z_dps?: number
  imu_valid_raw?: number
  baro_valid_raw?: number
  i2c_bus_state_raw?: number
  batt_valid_raw?: number
  coral_payload_text?: string
  coral_payload_hex?: string
  coral_valid_raw?: number

  // Raw-v7 SCV snapshot.
  scv_magic?: string
  boot_count?: number
  scv_mission_elapsed_ms?: number
  reset_cause_raw?: number
  reset_reason_name?: string
  scv_equipment_enabled?: string
  scv_equipment_faults?: string
  scv_gps_timeout_count?: number
  scv_imu_timeout_count?: number
  scv_baro_timeout_count?: number
  scv_coral_timeout_count?: number
  scv_sd_fault_count?: number
  scv_watchdog_reset_count?: number
  scv_last_batt_mv?: number
  scv_baro_ground_alt_cm?: number
  scv_crc16?: string

  // Raw-v7 spacecraft reliable uplink/acknowledgement snapshot.
  uplink_last_command?: number
  uplink_last_command_name?: string
  uplink_last_status?: number
  uplink_last_status_name?: string
  uplink_last_command_id?: number
  uplink_last_ack_sequence?: number
  uplink_command_count?: number

  // Raw-v7 spacecraft LoRa TX/RX-health snapshot.
  lora_last_event?: number
  lora_last_event_name?: string
  lora_consecutive_failures?: number
  lora_recovery_count?: number
  lora_last_success_ms?: number
  lora_rx_mode_active?: number
  lora_last_rx_status?: number
  lora_last_rx_status_name?: string
  lora_rx_packet_count?: number
  lora_rx_crc_error_count?: number
  lora_ack_timeout_count?: number
  lora_last_rx_ms?: number

  [key: string]: unknown
}

export type ReceiverSnapshot = {
  running: boolean
  radio_enabled: boolean
  radio_initialized: boolean
  last_error: string | null
  last_message: string
  last_packet_time_unix: number | null
  non_telemetry_packets: number
  decode_errors: number
  lora_crc_errors: number
  command_tx_count: number
  command_tx_failures: number
  telemetry_ack_tx_count?: number
  telemetry_ack_tx_failures?: number
  last_telemetry_ack_sequence?: number | null
  last_telemetry_ack_ok?: boolean | null
  last_telemetry_ack_time_unix?: number | null
  last_command_id?: number | null
  last_command_outcome?: "pending" | "retrying" | "acknowledged" | "rejected" | "unacknowledged" | null
  last_command_attempt?: number | null
  last_command_time_unix?: number | null
}

export type StoreStats = {
  total_packets_received: number
  total_packets_logged: number
  total_crc_errors: number
  total_packet_type_errors: number
  total_protocol_errors: number
  total_lost_packets: number
  total_duplicate_packets?: number
  consecutive_duplicate_packets?: number
  history_length: number
  csv_path: string | null
}

export type GroundEventStats = {
  total_events_logged: number
  csv_path: string | null
}

export type BackendStatus = {
  receiver: ReceiverSnapshot
  stats: StoreStats | null
  ground_event_stats?: GroundEventStats | null
  latest: TelemetryRow | null
  config?: {
    frequency_hz: number
    spreading_factor: number
    sync_word: number
    tx_power_dbm: number
    telemetry_packet_size: number
    csv_enabled: boolean
    history: number
    log_dir: string
  }
}

export type TelemetryWebSocketMessage = {
  type:
    | "hello"
    | "status"
    | "telemetry"
    | "lora_crc_error"
    | "non_telemetry_packet"
    | "decode_error"
    | "receiver_error"
    | "command_tx"
  data?: TelemetryRow | BackendStatus | unknown
  status?: BackendStatus
  error?: string
}

export type TimelineEventType =
  | "telemetry_rx"
  | "command_tx"
  | "lora_crc_error"
  | "decode_error"
  | "receiver_error"
  | "non_telemetry_packet"

export type TimelineEvent = {
  id: string
  type: TimelineEventType
  time_iso: string
  time_unix: number
  title: string
  description?: string
  sequence_number?: number
  payload?: string
  severity?: "info" | "warning" | "critical"
}
