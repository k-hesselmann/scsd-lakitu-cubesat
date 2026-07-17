export type TelemetryRow = {
  pc_receive_time_iso?: string
  pc_receive_time_unix?: number

  lora_downlink_rssi_dbm?: number | null
  lora_downlink_snr_db?: number | null
  lora_crc_error?: boolean

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

  utc_timestamp?: number
  obc_uptime_ms?: number
  flight_state?: number
  flight_state_name?: string

  status_flags_raw?: number

  GNSS_FIX_VALID?: boolean
  GNSS_TIME_VALID?: boolean
  IMU_VALID?: boolean
  BARO_VALID?: boolean
  BARO_RANGE_VALID?: boolean
  BATTERY_VALID?: boolean
  CORAL_VALID?: boolean
  CORAL_NEW?: boolean
  SD_LOGGING_OK?: boolean
  LAST_LORA_TX_OK?: boolean
  COMMAND_RX_SINCE_LAST?: boolean
  OBC_TIME_FALLBACK?: boolean
  GPS_ERROR?: boolean
  IMU_ERROR?: boolean
  BARO_ERROR?: boolean
  SD_ERROR?: boolean

  battery_mv?: number
  battery_v?: number

  gnss_fix_type?: number
  gnss_satellites_used?: number
  latitude_deg?: number
  longitude_deg?: number
  gnss_altitude_m?: number
  gnss_hdop?: number
  gnss_vdop?: number
  ground_speed_ms?: number
  vertical_speed_ms?: number
  course_deg?: number

  baro_pressure_pa?: number
  baro_temperature_c?: number
  baro_altitude_m?: number

  accel_x_ms2?: number
  accel_y_ms2?: number
  accel_z_ms2?: number

  gyro_x_rads?: number
  gyro_y_rads?: number
  gyro_z_rads?: number

  imu_temperature_c?: number
  mcu_temperature_c?: number

  reset_cause_raw?: number
  PIN_RESET?: boolean
  BOR_RESET?: boolean
  SOFTWARE_RESET?: boolean
  IWDG_RESET?: boolean
  WWDG_RESET?: boolean
  LOW_POWER_RESET?: boolean
  OPTION_BYTE_RESET?: boolean
  FIREWALL_RESET?: boolean

  boot_count?: number
  sd_log_record_counter?: number
  sd_error_counter?: number
  sensor_error_counter?: number
  command_counter?: number

  last_uplink_rssi_dbm?: number
  last_uplink_snr_db?: number

  coral_status?: number
  coral_result_age_s?: number
  coral_payload_text?: string
  coral_payload_hex?: string

  // Raw v3 datapool / SCV fields received directly from the spacecraft.
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
  coral_valid_raw?: number
  scv_magic?: string
  scv_mission_elapsed_ms?: number
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
  reset_reason_name?: string

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
}

export type StoreStats = {
  total_packets_received: number
  total_packets_logged: number
  total_crc_errors: number
  total_packet_type_errors: number
  total_protocol_errors: number
  total_lost_packets: number
  history_length: number
  csv_path: string | null
}

export type BackendStatus = {
  receiver: ReceiverSnapshot
  stats: StoreStats | null
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
  | "telemetry_tx"
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