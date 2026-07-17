import type { TelemetryRow } from "@/types/telemetry"
import { fmt } from "@/lib/format"

export type TelemetryColumn = {
  key: keyof TelemetryRow | string
  label: string
  decimals?: number
  suffix?: string
}

export const telemetryColumns: TelemetryColumn[] = [
  { key: "pc_receive_time_iso", label: "PC Time" },
  { key: "pc_receive_time_unix", label: "PC Unix", decimals: 3 },
  { key: "sequence_number", label: "Seq" },
  { key: "lost_packets_since_previous", label: "Lost Prev" },
  { key: "total_lost_packets", label: "Lost Total" },

  { key: "packet_type", label: "Type" },
  { key: "protocol_version", label: "Version" },
  { key: "packet_type_ok", label: "Type OK" },
  { key: "protocol_version_ok", label: "Proto OK" },
  { key: "length_ok", label: "Len OK" },
  { key: "crc_ok", label: "CRC OK" },
  { key: "received_crc16", label: "CRC RX" },
  { key: "calculated_crc16", label: "CRC Calc" },

  // Raw v3 source values. These are included so this view remains a complete
  // representation of the current downlink, alongside the display-converted
  // fields below.
  { key: "datapool_timestamp_ms", label: "DP Timestamp", suffix: " ms" },
  { key: "gps_valid_raw", label: "GPS Valid Raw" },
  { key: "imu_accel_x_g", label: "IMU Accel X Raw", suffix: " g", decimals: 4 },
  { key: "imu_accel_y_g", label: "IMU Accel Y Raw", suffix: " g", decimals: 4 },
  { key: "imu_accel_z_g", label: "IMU Accel Z Raw", suffix: " g", decimals: 4 },
  { key: "imu_accel_mag_g", label: "IMU Accel Mag Raw", suffix: " g", decimals: 4 },
  { key: "imu_gyro_x_dps", label: "IMU Gyro X Raw", suffix: " deg/s", decimals: 3 },
  { key: "imu_gyro_y_dps", label: "IMU Gyro Y Raw", suffix: " deg/s", decimals: 3 },
  { key: "imu_gyro_z_dps", label: "IMU Gyro Z Raw", suffix: " deg/s", decimals: 3 },
  { key: "imu_valid_raw", label: "IMU Valid Raw" },
  { key: "baro_valid_raw", label: "Baro Valid Raw" },
  { key: "i2c_bus_state_raw", label: "I2C Bus State Raw" },
  { key: "batt_valid_raw", label: "Battery Valid Raw" },
  { key: "coral_valid_raw", label: "Coral Valid Raw" },
  { key: "scv_magic", label: "SCV Magic" },
  { key: "scv_mission_elapsed_ms", label: "SCV Mission Elapsed", suffix: " ms" },
  { key: "scv_equipment_enabled", label: "SCV Equipment Enabled" },
  { key: "scv_equipment_faults", label: "SCV Equipment Faults" },
  { key: "scv_gps_timeout_count", label: "SCV GPS Timeouts" },
  { key: "scv_imu_timeout_count", label: "SCV IMU Timeouts" },
  { key: "scv_baro_timeout_count", label: "SCV Baro Timeouts" },
  { key: "scv_coral_timeout_count", label: "SCV Coral Timeouts" },
  { key: "scv_sd_fault_count", label: "SCV SD Faults" },
  { key: "scv_watchdog_reset_count", label: "SCV Watchdog Resets" },
  { key: "scv_last_batt_mv", label: "SCV Last Battery", suffix: " mV" },
  { key: "scv_baro_ground_alt_cm", label: "SCV Baro Ground Alt", suffix: " cm" },
  { key: "scv_crc16", label: "SCV CRC" },
  { key: "reset_reason_name", label: "SCV Reset Reason" },

  { key: "utc_timestamp", label: "UTC Timestamp" },
  { key: "obc_uptime_ms", label: "OBC Uptime", suffix: " ms" },
  { key: "flight_state", label: "State ID" },
  { key: "flight_state_name", label: "State" },

  { key: "status_flags_raw", label: "Status Raw" },
  { key: "GNSS_FIX_VALID", label: "GNSS Fix" },
  { key: "GNSS_TIME_VALID", label: "GNSS Time" },
  { key: "IMU_VALID", label: "IMU Valid" },
  { key: "BARO_VALID", label: "Baro Valid" },
  { key: "BARO_RANGE_VALID", label: "Baro Range" },
  { key: "BATTERY_VALID", label: "Batt Valid" },
  { key: "CORAL_VALID", label: "Coral Valid" },
  { key: "CORAL_NEW", label: "Coral New" },
  { key: "SD_LOGGING_OK", label: "SD OK" },
  { key: "LAST_LORA_TX_OK", label: "Last TX OK" },
  { key: "COMMAND_RX_SINCE_LAST", label: "Cmd RX" },
  { key: "OBC_TIME_FALLBACK", label: "Time Fallback" },
  { key: "GPS_ERROR", label: "GPS Err" },
  { key: "IMU_ERROR", label: "IMU Err" },
  { key: "BARO_ERROR", label: "Baro Err" },
  { key: "SD_ERROR", label: "SD Err" },

  { key: "battery_mv", label: "Batt", suffix: " mV" },
  { key: "battery_v", label: "Batt", suffix: " V", decimals: 2 },

  { key: "gnss_fix_type", label: "Fix Type" },
  { key: "gnss_satellites_used", label: "Sats" },
  { key: "latitude_deg", label: "Lat", decimals: 7 },
  { key: "longitude_deg", label: "Lon", decimals: 7 },
  { key: "gnss_altitude_m", label: "GNSS Alt", suffix: " m", decimals: 1 },
  { key: "gnss_hdop", label: "HDOP", decimals: 2 },
  { key: "gnss_vdop", label: "VDOP", decimals: 2 },
  { key: "ground_speed_ms", label: "Ground Speed", suffix: " m/s", decimals: 2 },
  { key: "vertical_speed_ms", label: "Vertical Speed", suffix: " m/s", decimals: 2 },
  { key: "course_deg", label: "Course", suffix: "°", decimals: 2 },

  { key: "baro_pressure_pa", label: "Pressure", suffix: " Pa" },
  { key: "baro_temperature_c", label: "Baro Temp", suffix: " °C", decimals: 2 },
  { key: "baro_altitude_m", label: "Baro Alt", suffix: " m", decimals: 1 },

  { key: "accel_x_ms2", label: "Accel X", suffix: " m/s²", decimals: 2 },
  { key: "accel_y_ms2", label: "Accel Y", suffix: " m/s²", decimals: 2 },
  { key: "accel_z_ms2", label: "Accel Z", suffix: " m/s²", decimals: 2 },

  { key: "gyro_x_rads", label: "Gyro X", suffix: " rad/s", decimals: 3 },
  { key: "gyro_y_rads", label: "Gyro Y", suffix: " rad/s", decimals: 3 },
  { key: "gyro_z_rads", label: "Gyro Z", suffix: " rad/s", decimals: 3 },

  { key: "imu_temperature_c", label: "IMU Temp", suffix: " °C", decimals: 2 },
  { key: "mcu_temperature_c", label: "MCU Temp", suffix: " °C", decimals: 2 },

  { key: "reset_cause_raw", label: "Reset Raw" },
  { key: "PIN_RESET", label: "PIN Reset" },
  { key: "BOR_RESET", label: "BOR Reset" },
  { key: "SOFTWARE_RESET", label: "SW Reset" },
  { key: "IWDG_RESET", label: "IWDG" },
  { key: "WWDG_RESET", label: "WWDG" },
  { key: "LOW_POWER_RESET", label: "Low Power" },
  { key: "OPTION_BYTE_RESET", label: "Opt Byte" },
  { key: "FIREWALL_RESET", label: "Firewall" },

  { key: "boot_count", label: "Boot Count" },
  { key: "sd_log_record_counter", label: "SD Records" },
  { key: "sd_error_counter", label: "SD Errors" },
  { key: "sensor_error_counter", label: "Sensor Errors" },
  { key: "command_counter", label: "Commands" },

  { key: "last_uplink_rssi_dbm", label: "Uplink RSSI", suffix: " dBm" },
  { key: "last_uplink_snr_db", label: "Uplink SNR", suffix: " dB" },
  { key: "lora_downlink_rssi_dbm", label: "Downlink RSSI", suffix: " dBm" },
  { key: "lora_downlink_snr_db", label: "Downlink SNR", suffix: " dB", decimals: 1 },

  { key: "coral_status", label: "Coral Status" },
  { key: "coral_result_age_s", label: "Coral Age", suffix: " s" },
  { key: "coral_payload_text", label: "Coral Payload" },
  { key: "coral_payload_hex", label: "Coral Payload Hex" },
]

export function formatTelemetryCell(row: TelemetryRow, column: TelemetryColumn) {
  return fmt(row[column.key], column.suffix ?? "", column.decimals)
}