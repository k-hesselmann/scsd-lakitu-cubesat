export type TelemetryRow = {
  // Ground-station reception metadata.
  pc_receive_time_iso?: string
  pc_receive_time_unix?: number
  lora_downlink_rssi_dbm?: number | null
  lora_downlink_snr_db?: number | null
  lora_crc_error?: boolean
  telemetry_valid?: boolean
  rejection_reason?: string
  raw_payload_hex?: string

  // V8 packet envelope and ground-side validation.
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
  is_out_of_order_packet?: boolean
  total_out_of_order_packets?: number

  // Engineering values normalized from the active wire revision.
  obc_uptime_ms?: number
  utc_timestamp?: number | null
  sample_age_ms?: number | null
  gnss_utc_sod?: number | null
  flight_state?: number
  flight_state_name?: string
  battery_mv?: number | null
  battery_v?: number | null
  gnss_fix_type?: number | null
  gnss_satellites_used?: number | null
  latitude_deg?: number | null
  longitude_deg?: number | null
  gnss_altitude_m?: number | null
  ground_speed_ms?: number | null
  vertical_speed_ms?: number | null
  course_deg?: number | null
  baro_pressure_pa?: number | null
  baro_temperature_c?: number | null
  baro_altitude_m?: number | null
  accel_x_ms2?: number | null
  accel_y_ms2?: number | null
  accel_z_ms2?: number | null
  gyro_x_rads?: number | null
  gyro_y_rads?: number | null
  gyro_z_rads?: number | null

  // Raw v8 fixed-point and derived engineering values.
  latitude_e7?: number | null
  longitude_e7?: number | null
  gnss_altitude_dm?: number | null
  vertical_speed_cms?: number | null
  ground_speed_cms?: number | null
  course_cdeg?: number | null
  accel_x_mg?: number | null
  accel_y_mg?: number | null
  accel_z_mg?: number | null
  gyro_x_ddeg_s?: number | null
  gyro_y_ddeg_s?: number | null
  gyro_z_ddeg_s?: number | null
  baro_altitude_dm?: number | null
  baro_temperature_cdeg?: number | null
  gps_valid_raw?: number
  imu_accel_x_g?: number
  imu_accel_y_g?: number
  imu_accel_z_g?: number
  imu_gyro_x_dps?: number
  imu_gyro_y_dps?: number
  imu_gyro_z_dps?: number
  imu_valid_raw?: number
  baro_valid_raw?: number
  i2c_bus_state_raw?: number
  batt_valid_raw?: number
  coral_sequence_low?: number | null
  coral_fraction_q16?: number | null
  coral_fraction_percent?: number | null
  coral_result_age_s?: number | null
  coral_status?: number
  coral_valid_raw?: number

  // V8 spacecraft-health values.
  boot_count?: number
  scv_mission_elapsed_ms?: number
  reset_cause_raw?: number
  reset_reason_name?: string
  scv_equipment_enabled?: string
  scv_equipment_faults?: string
  scv_sd_fault_count?: number
  scv_watchdog_reset_count?: number

  // Spacecraft command/acknowledgement state.
  uplink_last_status?: number
  uplink_last_status_name?: string
  uplink_last_command_id?: number
  uplink_last_ack_status?: number
  uplink_last_ack_status_name?: string

  // Spacecraft LoRa TX/RX health.
  lora_last_event?: number
  lora_last_event_name?: string
  lora_consecutive_failures?: number
  lora_recovery_count?: number
  lora_rx_mode_active?: number
  lora_last_rx_status?: number
  lora_last_rx_status_name?: string
  lora_ack_timeout_count?: number
  lora_tx_fault_count?: number | null

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
  radio_io_failures?: number
  radio_reconnect_attempts?: number
  radio_reconnect_successes?: number
  consecutive_radio_failures?: number
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
  total_valid_packets: number
  total_invalid_packets: number
  total_unique_valid_packets: number
  total_packets_logged: number
  total_crc_errors: number
  total_packet_type_errors: number
  total_protocol_errors: number
  total_length_errors: number
  total_lost_packets: number
  total_duplicate_packets?: number
  total_out_of_order_packets?: number
  consecutive_duplicate_packets?: number
  sequence_session_resets?: number
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
  command_safety?: CommandSafety
  config?: {
    frequency_hz: number
    spreading_factor: number
    sync_word: number
    tx_power_dbm: number
    telemetry_ack_turnaround_s?: number
    radio_reconnect_max_attempts?: number
    radio_reconnect_initial_backoff_s?: number
    radio_reconnect_max_backoff_s?: number
    command_auth_required?: boolean
    rf_uplink_auth_configured?: boolean
    command_arm_duration_s?: number
    command_min_interval_s?: number
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
    | "invalid_telemetry"
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
  | "invalid_telemetry"

export type CommandSafety = {
  auth_required: boolean
  rf_auth_configured: boolean
  command_ids_remaining: number
  armed: boolean
  armed_remaining_s: number
  arm_duration_s: number
  minimum_interval_s: number
}

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
