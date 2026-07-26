# telemetry_store.py

import csv
import os
import threading
from collections import deque
from datetime import datetime, timezone


class TelemetryStore:
    """
    Stores decoded telemetry packets in memory and optionally logs them to CSV.

    This class is designed for two uses:
      1. Terminal receiver:
            receive LoRa packet -> decode telemetry -> store.add_packet(...)

      2. Future dashboard:
            dashboard reads store.get_latest()
            dashboard reads store.get_history()
    """

    def __init__(self, maxlen=1000, log_dir="logs", enable_csv=True):
        self.history = deque(maxlen=maxlen)
        self.lock = threading.Lock()

        self.enable_csv = enable_csv
        self.log_dir = log_dir

        self.csv_file = None
        self.csv_writer = None
        self.csv_path = None

        self.total_packets_received = 0
        self.total_valid_packets = 0
        self.total_invalid_packets = 0
        self.total_packets_logged = 0
        self.total_crc_errors = 0
        self.total_packet_type_errors = 0
        self.total_protocol_errors = 0
        self.total_lost_packets = 0
        self.total_duplicate_packets = 0
        self.consecutive_duplicate_packets = 0
        self.sequence_session_resets = 0

        self.previous_sequence_number = None
        self.previous_boot_count = None
        self.previous_uptime_ms = None

        if self.enable_csv:
            self._open_csv_log()

    def _open_csv_log(self):
        os.makedirs(self.log_dir, exist_ok=True)

        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        self.csv_path = os.path.join(
            self.log_dir,
            f"telemetry_{timestamp}.csv"
        )

        self.csv_file = open(
            self.csv_path,
            mode="w",
            newline="",
            encoding="utf-8"
        )

        self.csv_writer = csv.DictWriter(
            self.csv_file,
            fieldnames=self._csv_fieldnames()
        )

        self.csv_writer.writeheader()
        self.csv_file.flush()

    def _csv_fieldnames(self):
        """
        Fixed CSV column order.

        Keep this explicit so that logs are stable and easy to import into
        Excel, MATLAB, Python/Pandas, etc.
        """

        return [
            # PC-side metadata
            "pc_receive_time_iso",
            "pc_receive_time_unix",
            "lora_downlink_rssi_dbm",
            "lora_downlink_snr_db",
            "lora_crc_error",

            # Packet validation
            "telemetry_valid",
            "packet_type",
            "protocol_version",
            "packet_type_ok",
            "protocol_version_ok",
            "length_ok",
            "crc_ok",
            "received_crc16",
            "calculated_crc16",

            # Counters
            "sequence_number",
            "sequence_session_reset",
            "lost_packets_since_previous",
            "total_lost_packets",
            "is_duplicate_packet",
            "consecutive_duplicate_packets",
            "total_duplicate_packets",

            # Time and state
            "utc_timestamp",
            "obc_uptime_ms",
            "sample_age_ms",
            "gnss_utc_sod",
            "flight_state",
            "flight_state_name",

            # Status
            "status_flags_raw",
            "GNSS_FIX_VALID",
            "GNSS_TIME_VALID",
            "IMU_VALID",
            "BARO_VALID",
            "BARO_RANGE_VALID",
            "BATTERY_VALID",
            "CORAL_VALID",
            "CORAL_NEW",
            "SD_LOGGING_OK",
            "LAST_LORA_TX_OK",
            "COMMAND_RX_SINCE_LAST",
            "OBC_TIME_FALLBACK",
            "GPS_ERROR",
            "IMU_ERROR",
            "BARO_ERROR",
            "SD_ERROR",

            # Power
            "battery_mv",
            "battery_v",

            # GNSS
            "gnss_fix_type",
            "gnss_satellites_used",
            "latitude_deg",
            "longitude_deg",
            "gnss_altitude_m",
            "ground_speed_ms",
            "vertical_speed_ms",
            "course_deg",
            "latitude_e7",
            "longitude_e7",
            "gnss_altitude_dm",
            "vertical_speed_cms",
            "ground_speed_cms",
            "course_cdeg",

            # Barometer
            "baro_pressure_pa",
            "baro_temperature_c",
            "baro_altitude_m",

            # IMU
            "accel_x_ms2",
            "accel_y_ms2",
            "accel_z_ms2",
            "gyro_x_rads",
            "gyro_y_rads",
            "gyro_z_rads",
            "accel_x_mg",
            "accel_y_mg",
            "accel_z_mg",
            "gyro_x_ddeg_s",
            "gyro_y_ddeg_s",
            "gyro_z_ddeg_s",
            "baro_altitude_dm",
            "baro_temperature_cdeg",

            # Reset cause
            "reset_cause_raw",
            "PIN_RESET",
            "BOR_RESET",
            "SOFTWARE_RESET",
            "IWDG_RESET",
            "WWDG_RESET",
            "LOW_POWER_RESET",
            "OPTION_BYTE_RESET",
            "FIREWALL_RESET",

            # System counters
            "boot_count",
            "sd_error_counter",

            # Uplink quality reported by OBC

            # V8 validity, engineering, and spacecraft-health values
            "gps_valid_raw",
            "imu_accel_x_g",
            "imu_accel_y_g",
            "imu_accel_z_g",
            "imu_accel_mag_g",
            "imu_gyro_x_dps",
            "imu_gyro_y_dps",
            "imu_gyro_z_dps",
            "imu_valid_raw",
            "baro_valid_raw",
            "i2c_bus_state_raw",
            "batt_valid_raw",
            "coral_valid_raw",
            "scv_mission_elapsed_ms",
            "scv_equipment_enabled",
            "scv_equipment_faults",
            "scv_sd_fault_count",
            "scv_watchdog_reset_count",
            "reset_reason_name",

            # Ground-to-flight command/acknowledgement snapshot
            "uplink_last_status",
            "uplink_last_status_name",
            "uplink_last_command_id",
            "uplink_last_ack_status",
            "uplink_last_ack_status_name",

            # Minimal LoRa FDIR snapshot
            "lora_last_event",
            "lora_last_event_name",
            "lora_consecutive_failures",
            "lora_recovery_count",
            "lora_rx_mode_active",
            "lora_last_rx_status",
            "lora_last_rx_status_name",
            "lora_ack_timeout_count",
            "lora_tx_fault_count",

            # Coral payload
            "coral_status",
            "coral_result_age_s",
            "coral_sequence_low",
            "coral_fraction_q16",
            "coral_fraction_percent",
            "coral_payload_text",
            "coral_payload_hex",
        ]

    def add_packet(self, telemetry_packet, lora_rssi_dbm=None, lora_snr_db=None):
        """Store every decoded frame, but advance mission state only for trusted telemetry."""

        now = datetime.now(timezone.utc)
        telemetry_valid = bool(telemetry_packet.validation_ok)

        with self.lock:
            self.total_packets_received += 1
            if telemetry_valid:
                self.total_valid_packets += 1
            else:
                self.total_invalid_packets += 1

            if not telemetry_packet.crc_ok:
                self.total_crc_errors += 1

            if not telemetry_packet.packet_type_ok:
                self.total_packet_type_errors += 1

            if not telemetry_packet.protocol_version_ok:
                self.total_protocol_errors += 1

            row = self._packet_to_row(
                telemetry_packet=telemetry_packet,
                pc_receive_time=now,
                lora_rssi_dbm=lora_rssi_dbm,
                lora_snr_db=lora_snr_db,
                update_sequence=telemetry_valid,
            )
            self.history.append(row)

            if self.csv_writer is not None:
                self.csv_writer.writerow(row)
                self.csv_file.flush()
                self.total_packets_logged += 1

        return row

    def _calculate_sequence_health(self, sequence_number, boot_count, uptime_ms):
        """Estimate loss within one spacecraft boot/session, including uint16 wrap."""

        if self.previous_sequence_number is None:
            self.previous_sequence_number = sequence_number
            self.previous_boot_count = boot_count
            self.previous_uptime_ms = uptime_ms
            self.consecutive_duplicate_packets = 0
            return 0, False, False

        boot_changed = boot_count != self.previous_boot_count
        uptime_regressed = (
            isinstance(uptime_ms, int)
            and isinstance(self.previous_uptime_ms, int)
            and uptime_ms < self.previous_uptime_ms
        )
        if boot_changed or uptime_regressed:
            self.previous_sequence_number = sequence_number
            self.previous_boot_count = boot_count
            self.previous_uptime_ms = uptime_ms
            self.consecutive_duplicate_packets = 0
            self.sequence_session_resets += 1
            return 0, False, True

        previous = self.previous_sequence_number
        if sequence_number == previous:
            self.total_duplicate_packets += 1
            self.consecutive_duplicate_packets += 1
            return 0, True, False

        if sequence_number >= previous:
            difference = sequence_number - previous
        else:
            difference = (65536 - previous) + sequence_number

        self.previous_sequence_number = sequence_number
        self.previous_boot_count = boot_count
        self.previous_uptime_ms = uptime_ms
        self.consecutive_duplicate_packets = 0

        if difference <= 1:
            return 0, False, False

        lost = difference - 1
        self.total_lost_packets += lost
        return lost, False, False

    def _packet_to_row(
        self,
        telemetry_packet,
        pc_receive_time,
        lora_rssi_dbm=None,
        lora_snr_db=None,
        update_sequence=False,
    ):
        if update_sequence:
            lost_packets, is_duplicate, sequence_session_reset = (
                self._calculate_sequence_health(
                    telemetry_packet.sequence_number,
                    telemetry_packet.boot_count,
                    telemetry_packet.obc_uptime_ms,
                )
            )
        else:
            lost_packets, is_duplicate, sequence_session_reset = 0, False, False

        status = telemetry_packet.status_flags
        reset = telemetry_packet.reset_cause

        row = {
            # PC-side metadata
            "pc_receive_time_iso": pc_receive_time.isoformat(),
            "pc_receive_time_unix": pc_receive_time.timestamp(),
            "lora_downlink_rssi_dbm": lora_rssi_dbm,
            "lora_downlink_snr_db": lora_snr_db,
            "lora_crc_error": False,

            # Packet validation
            "telemetry_valid": bool(telemetry_packet.validation_ok),
            "packet_type": telemetry_packet.packet_type,
            "protocol_version": telemetry_packet.protocol_version,
            "packet_type_ok": telemetry_packet.packet_type_ok,
            "protocol_version_ok": telemetry_packet.protocol_version_ok,
            "length_ok": telemetry_packet.length_ok,
            "crc_ok": telemetry_packet.crc_ok,
            "received_crc16": f"0x{telemetry_packet.received_crc16:04X}",
            "calculated_crc16": f"0x{telemetry_packet.calculated_crc16:04X}",

            # Counters
            "sequence_number": telemetry_packet.sequence_number,
            "sequence_session_reset": sequence_session_reset,
            "lost_packets_since_previous": lost_packets,
            "total_lost_packets": self.total_lost_packets,
            "is_duplicate_packet": is_duplicate,
            "consecutive_duplicate_packets": self.consecutive_duplicate_packets,
            "total_duplicate_packets": self.total_duplicate_packets,

            # Time and state
            "utc_timestamp": telemetry_packet.utc_timestamp,
            "obc_uptime_ms": telemetry_packet.obc_uptime_ms,
            "sample_age_ms": telemetry_packet.sample_age_ms,
            "gnss_utc_sod": telemetry_packet.gnss_utc_sod,
            "flight_state": telemetry_packet.flight_state,
            "flight_state_name": telemetry_packet.flight_state_name,

            # Status flags
            "status_flags_raw": telemetry_packet.status_flags_raw,
            "GNSS_FIX_VALID": status.get("GNSS_FIX_VALID", False),
            "GNSS_TIME_VALID": status.get("GNSS_TIME_VALID", False),
            "IMU_VALID": status.get("IMU_VALID", False),
            "BARO_VALID": status.get("BARO_VALID", False),
            "BARO_RANGE_VALID": status.get("BARO_RANGE_VALID", False),
            "BATTERY_VALID": status.get("BATTERY_VALID", False),
            "CORAL_VALID": status.get("CORAL_VALID", False),
            "CORAL_NEW": status.get("CORAL_NEW", False),
            "SD_LOGGING_OK": status.get("SD_LOGGING_OK", False),
            "LAST_LORA_TX_OK": status.get("LAST_LORA_TX_OK", False),
            "COMMAND_RX_SINCE_LAST": status.get("COMMAND_RX_SINCE_LAST", False),
            "OBC_TIME_FALLBACK": status.get("OBC_TIME_FALLBACK", False),
            "GPS_ERROR": status.get("GPS_ERROR", False),
            "IMU_ERROR": status.get("IMU_ERROR", False),
            "BARO_ERROR": status.get("BARO_ERROR", False),
            "SD_ERROR": status.get("SD_ERROR", False),

            # Power
            "battery_mv": telemetry_packet.battery_mv,
            "battery_v": telemetry_packet.battery_v,

            # GNSS
            "gnss_fix_type": telemetry_packet.gnss_fix_type,
            "gnss_satellites_used": telemetry_packet.gnss_satellites_used,
            "latitude_deg": telemetry_packet.latitude_deg,
            "longitude_deg": telemetry_packet.longitude_deg,
            "gnss_altitude_m": telemetry_packet.gnss_altitude_m,
            "ground_speed_ms": telemetry_packet.ground_speed_ms,
            "vertical_speed_ms": telemetry_packet.vertical_speed_ms,
            "course_deg": telemetry_packet.course_deg,
            "latitude_e7": telemetry_packet.latitude_e7,
            "longitude_e7": telemetry_packet.longitude_e7,
            "gnss_altitude_dm": telemetry_packet.gnss_altitude_dm,
            "vertical_speed_cms": telemetry_packet.vertical_speed_cms,
            "ground_speed_cms": telemetry_packet.ground_speed_cms,
            "course_cdeg": telemetry_packet.course_cdeg,

            # Barometer
            "baro_pressure_pa": telemetry_packet.baro_pressure_pa,
            "baro_temperature_c": telemetry_packet.baro_temperature_c,
            "baro_altitude_m": telemetry_packet.baro_altitude_m,

            # IMU
            "accel_x_ms2": telemetry_packet.accel_x_ms2,
            "accel_y_ms2": telemetry_packet.accel_y_ms2,
            "accel_z_ms2": telemetry_packet.accel_z_ms2,
            "gyro_x_rads": telemetry_packet.gyro_x_rads,
            "gyro_y_rads": telemetry_packet.gyro_y_rads,
            "gyro_z_rads": telemetry_packet.gyro_z_rads,
            "accel_x_mg": telemetry_packet.accel_x_mg,
            "accel_y_mg": telemetry_packet.accel_y_mg,
            "accel_z_mg": telemetry_packet.accel_z_mg,
            "gyro_x_ddeg_s": telemetry_packet.gyro_x_ddeg_s,
            "gyro_y_ddeg_s": telemetry_packet.gyro_y_ddeg_s,
            "gyro_z_ddeg_s": telemetry_packet.gyro_z_ddeg_s,
            "baro_altitude_dm": telemetry_packet.baro_altitude_dm,
            "baro_temperature_cdeg": telemetry_packet.baro_temperature_cdeg,

            # Reset cause
            "reset_cause_raw": telemetry_packet.reset_cause_raw,
            "PIN_RESET": reset.get("PIN_RESET", False),
            "BOR_RESET": reset.get("BOR_RESET", False),
            "SOFTWARE_RESET": reset.get("SOFTWARE_RESET", False),
            "IWDG_RESET": reset.get("IWDG_RESET", False),
            "WWDG_RESET": reset.get("WWDG_RESET", False),
            "LOW_POWER_RESET": reset.get("LOW_POWER_RESET", False),
            "OPTION_BYTE_RESET": reset.get("OPTION_BYTE_RESET", False),
            "FIREWALL_RESET": reset.get("FIREWALL_RESET", False),

            # System counters
            "boot_count": telemetry_packet.boot_count,
            "sd_error_counter": telemetry_packet.sd_error_counter,


            # Coral
            "coral_status": telemetry_packet.coral_status,
            "coral_result_age_s": telemetry_packet.coral_result_age_s,
            "coral_sequence_low": telemetry_packet.coral_sequence_low,
            "coral_fraction_q16": telemetry_packet.coral_fraction_q16,
            "coral_fraction_percent": telemetry_packet.coral_fraction_percent,
            "coral_payload_text": telemetry_packet.coral_payload_text,
            "coral_payload_hex": telemetry_packet.coral_payload_raw.hex(" "),

            # V8 raw wire and spacecraft-health values.
            "gps_valid_raw": telemetry_packet.gps_valid_raw,
            "imu_accel_x_g": telemetry_packet.imu_accel_x_g,
            "imu_accel_y_g": telemetry_packet.imu_accel_y_g,
            "imu_accel_z_g": telemetry_packet.imu_accel_z_g,
            "imu_accel_mag_g": telemetry_packet.imu_accel_mag_g,
            "imu_gyro_x_dps": telemetry_packet.imu_gyro_x_dps,
            "imu_gyro_y_dps": telemetry_packet.imu_gyro_y_dps,
            "imu_gyro_z_dps": telemetry_packet.imu_gyro_z_dps,
            "imu_valid_raw": telemetry_packet.imu_valid_raw,
            "baro_valid_raw": telemetry_packet.baro_valid_raw,
            "i2c_bus_state_raw": telemetry_packet.i2c_bus_state_raw,
            "batt_valid_raw": telemetry_packet.batt_valid_raw,
            "coral_valid_raw": telemetry_packet.coral_valid_raw,
            "scv_mission_elapsed_ms": telemetry_packet.scv_mission_elapsed_ms,
            "scv_equipment_enabled": f"0x{telemetry_packet.scv_equipment_enabled:04X}",
            "scv_equipment_faults": f"0x{telemetry_packet.scv_equipment_faults:04X}",
            "scv_sd_fault_count": telemetry_packet.scv_sd_fault_count,
            "scv_watchdog_reset_count": telemetry_packet.scv_watchdog_reset_count,
            "reset_reason_name": telemetry_packet.reset_reason_name,
            "uplink_last_status": telemetry_packet.uplink_last_status,
            "uplink_last_status_name": telemetry_packet.uplink_last_status_name,
            "uplink_last_command_id": telemetry_packet.uplink_last_command_id,
            "uplink_last_ack_status": telemetry_packet.uplink_last_ack_status,
            "uplink_last_ack_status_name": telemetry_packet.uplink_last_ack_status_name,
            "lora_last_event": telemetry_packet.lora_last_event,
            "lora_last_event_name": telemetry_packet.lora_last_event_name,
            "lora_consecutive_failures": telemetry_packet.lora_consecutive_failures,
            "lora_recovery_count": telemetry_packet.lora_recovery_count,
            "lora_rx_mode_active": telemetry_packet.lora_rx_mode_active,
            "lora_last_rx_status": telemetry_packet.lora_last_rx_status,
            "lora_last_rx_status_name": telemetry_packet.lora_last_rx_status_name,
            "lora_ack_timeout_count": telemetry_packet.lora_ack_timeout_count,
            "lora_tx_fault_count": telemetry_packet.lora_tx_fault_count,
        }

        return row

    def add_lora_crc_error(self, lora_rssi_dbm=None, lora_snr_db=None):
        """
        Optional helper for storing radio-level CRC errors.
        This is useful for link diagnostics, but these rows are not full telemetry.
        """

        now = datetime.now(timezone.utc)

        row = {field: None for field in self._csv_fieldnames()}

        row["pc_receive_time_iso"] = now.isoformat()
        row["pc_receive_time_unix"] = now.timestamp()
        row["lora_downlink_rssi_dbm"] = lora_rssi_dbm
        row["lora_downlink_snr_db"] = lora_snr_db
        row["lora_crc_error"] = True
        row["telemetry_valid"] = False
        row["crc_ok"] = False

        with self.lock:
            self.total_crc_errors += 1
            self.history.append(row)

            if self.csv_writer is not None:
                self.csv_writer.writerow(row)
                self.csv_file.flush()

        return row

    def get_latest(self):
        """
        Return the newest decoded telemetry row, or None if none has been received.

        Radio-level CRC errors are retained in history for link diagnostics, but
        they do not contain spacecraft telemetry and must not replace the data
        used by the dashboard's current-state displays.
        """

        with self.lock:
            for row in reversed(self.history):
                if row.get("telemetry_valid") is True:
                    return dict(row)

            return None

    def get_history(self, valid_only=False, limit=None):
        """Return copied history, optionally filtered and limited to newest rows."""
        with self.lock:
            if limit is not None:
                if limit < 1:
                    return []

                newest = []
                for row in reversed(self.history):
                    if valid_only and row.get("telemetry_valid") is not True:
                        continue

                    newest.append(dict(row))
                    if len(newest) >= limit:
                        break

                newest.reverse()
                return newest

            if valid_only:
                return [dict(row) for row in self.history
                        if row.get("telemetry_valid") is True]
            return [dict(row) for row in self.history]

    def get_stats(self):
        """
        Return receiver/store statistics.
        """

        with self.lock:
            return {
                "total_packets_received": self.total_packets_received,
                "total_valid_packets": self.total_valid_packets,
                "total_invalid_packets": self.total_invalid_packets,
                "total_unique_valid_packets": self.total_valid_packets - self.total_duplicate_packets,
                "total_packets_logged": self.total_packets_logged,
                "total_crc_errors": self.total_crc_errors,
                "total_packet_type_errors": self.total_packet_type_errors,
                "total_protocol_errors": self.total_protocol_errors,
                "total_lost_packets": self.total_lost_packets,
                "total_duplicate_packets": self.total_duplicate_packets,
                "consecutive_duplicate_packets": self.consecutive_duplicate_packets,
                "sequence_session_resets": self.sequence_session_resets,
                "history_length": len(self.history),
                "csv_path": self.csv_path,
            }

    def close(self):
        """
        Close the CSV file cleanly.
        """

        with self.lock:
            if self.csv_file is not None:
                self.csv_file.flush()
                self.csv_file.close()

            self.csv_file = None
            self.csv_writer = None