# telemetry_decoder.py
"""Decoder for the reliable raw datapool/SCV/TTC-state v7 downlink packet.

The STM32 copies source values into the frame without rescaling.  Any display
conversion (for example g to m/s? and deg/s to rad/s) happens here on ground.
"""

import math
import struct
from dataclasses import asdict, dataclass
from typing import Optional

TELEMETRY_PACKET_TYPE = 0x01
TELEMETRY_PROTOCOL_VERSION = 0x07
TELEMETRY_PACKET_SIZE = 155
STANDARD_GRAVITY_MS2 = 9.80665

EQUIPMENT_GPS = 1 << 0
EQUIPMENT_IMU = 1 << 1
EQUIPMENT_BARO = 1 << 2
EQUIPMENT_CORAL = 1 << 3
EQUIPMENT_SD = 1 << 4
EQUIPMENT_LORA = 1 << 5
EQUIPMENT_EPS_ADC = 1 << 6

FLIGHT_STATES = {
    0: "STANDBY",
    1: "LAUNCH",
    2: "ASCENT",
    3: "CRUISE",
    4: "DESCENT",
    5: "LANDING",
}

LORA_EVENTS = {
    0: "NONE",
    1: "INIT_OK",
    2: "INIT_FAIL",
    3: "TX_OK",
    4: "TX_SPI_FAIL",
    5: "TX_TIMEOUT",
    6: "TX_BAD_LENGTH",
    7: "NOT_READY",
    8: "CONFIG_FAIL",
    9: "RX_OK",
    10: "RX_CRC_ERROR",
    11: "RX_SPI_FAIL",
    12: "RX_MODE_FAIL",
    13: "ACK_TIMEOUT",
}

UPLINK_COMMANDS = {0: "NONE", 1: "REQUEST_TELEMETRY", 2: "ACKNOWLEDGE"}
UPLINK_STATUSES = {
    0: "NONE", 1: "ACCEPTED", 2: "INVALID_FORMAT",
    3: "UNSUPPORTED", 4: "DUPLICATE", 5: "UNEXPECTED_ACK",
}

LORA_RX_HEALTH = {
    0: "NONE",
    1: "ACTIVE",
    2: "PACKET_OK",
    3: "CRC_ERROR",
    4: "BAD_LENGTH",
    5: "SPI_ERROR",
    6: "MODE_ERROR",
}

RESET_REASONS = {
    0: "UNKNOWN",
    1: "POWER_ON",
    2: "WATCHDOG",
    3: "SOFTWARE",
    255: "INVALID",
}

# Little-endian packed layout matching TelemetryPacket_t in datapool.h.
TELEMETRY_STRUCT = struct.Struct("<BBHIIfffffBfffffffBfffBBHB16sBHIIBBHH6BHiHBBHHHBBBIBBHHHIH")


@dataclass
class TelemetryPacket:
    packet_type: int
    protocol_version: int
    sequence_number: int
    utc_timestamp: Optional[int]
    obc_uptime_ms: int
    flight_state: int
    flight_state_name: str
    status_flags_raw: int
    status_flags: dict
    battery_mv: int
    battery_v: float
    gnss_fix_type: Optional[int]
    gnss_satellites_used: Optional[int]
    latitude_deg: float
    longitude_deg: float
    gnss_altitude_m: float
    gnss_hdop: Optional[float]
    gnss_vdop: Optional[float]
    ground_speed_ms: float
    vertical_speed_ms: float
    course_deg: Optional[float]
    baro_pressure_pa: float
    baro_temperature_c: float
    baro_altitude_m: float
    accel_x_ms2: float
    accel_y_ms2: float
    accel_z_ms2: float
    gyro_x_rads: float
    gyro_y_rads: float
    gyro_z_rads: float
    imu_temperature_c: Optional[float]
    mcu_temperature_c: Optional[float]
    reset_cause_raw: int
    reset_cause: dict
    boot_count: int
    sd_log_record_counter: Optional[int]
    sd_error_counter: int
    sensor_error_counter: Optional[int]
    command_counter: Optional[int]
    last_uplink_rssi_dbm: Optional[int]
    last_uplink_snr_db: Optional[int]
    coral_status: int
    coral_result_age_s: Optional[int]
    coral_payload_raw: bytes
    coral_payload_text: str
    received_crc16: int
    calculated_crc16: int
    crc_ok: bool
    packet_type_ok: bool
    protocol_version_ok: bool
    length_ok: bool

    # Raw datapool values: these are exactly the values received over LoRa.
    datapool_timestamp_ms: int
    gps_valid_raw: int
    imu_accel_x_g: float
    imu_accel_y_g: float
    imu_accel_z_g: float
    imu_accel_mag_g: float
    imu_gyro_x_dps: float
    imu_gyro_y_dps: float
    imu_gyro_z_dps: float
    imu_valid_raw: int
    baro_valid_raw: int
    i2c_bus_state_raw: int
    batt_valid_raw: int
    coral_valid_raw: int

    # Raw SCV snapshot.
    scv_magic: int
    scv_mission_elapsed_ms: int
    scv_equipment_enabled: int
    scv_equipment_faults: int
    scv_gps_timeout_count: int
    scv_imu_timeout_count: int
    scv_baro_timeout_count: int
    scv_coral_timeout_count: int
    scv_sd_fault_count: int
    scv_watchdog_reset_count: int
    scv_last_batt_mv: int
    scv_baro_ground_alt_cm: int
    scv_crc16: int
    reset_reason_name: str

    # Ground-to-flight command/acknowledgement snapshot.
    uplink_last_command: int
    uplink_last_command_name: str
    uplink_last_status: int
    uplink_last_status_name: str
    uplink_last_command_id: int
    uplink_last_ack_sequence: int
    uplink_command_count: int

    # Minimal volatile LoRa FDIR health snapshot.
    lora_last_event: int
    lora_last_event_name: str
    lora_consecutive_failures: int
    lora_recovery_count: int
    lora_last_success_ms: int
    lora_rx_mode_active: int
    lora_last_rx_status: int
    lora_last_rx_status_name: str
    lora_rx_packet_count: int
    lora_rx_crc_error_count: int
    lora_ack_timeout_count: int
    lora_last_rx_ms: int

    @property
    def validation_ok(self) -> bool:
        """True when the complete v7 transport envelope is valid."""
        return (
            self.length_ok
            and self.crc_ok
            and self.packet_type_ok
            and self.protocol_version_ok
        )

    def to_dict(self):
        return asdict(self)

def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def decode_status_flags(gps_valid: int, imu_valid: int, baro_valid: int,
                        batt_valid: int, coral_valid: int, faults: int) -> dict:
    """Ground-derived display flags; no status word is synthesized on the MCU."""
    return {
        "GNSS_FIX_VALID": bool(gps_valid),
        "GNSS_TIME_VALID": False,
        "IMU_VALID": bool(imu_valid),
        "BARO_VALID": bool(baro_valid),
        "BARO_RANGE_VALID": bool(baro_valid),
        "BATTERY_VALID": bool(batt_valid),
        "CORAL_VALID": bool(coral_valid),
        "CORAL_NEW": False,
        "SD_LOGGING_OK": not bool(faults & EQUIPMENT_SD),
        "LAST_LORA_TX_OK": not bool(faults & EQUIPMENT_LORA),
        "COMMAND_RX_SINCE_LAST": False,
        "OBC_TIME_FALLBACK": False,
        "GPS_ERROR": bool(faults & EQUIPMENT_GPS),
        "IMU_ERROR": bool(faults & EQUIPMENT_IMU),
        "BARO_ERROR": bool(faults & EQUIPMENT_BARO),
        "SD_ERROR": bool(faults & EQUIPMENT_SD),
    }


def decode_reset_cause(reset_reason: int) -> dict:
    # Preserve the existing UI flag names while interpreting SCV's enum values.
    return {
        "PIN_RESET": False,
        "BOR_RESET": False,
        "SOFTWARE_RESET": reset_reason == 3,
        "IWDG_RESET": reset_reason == 2,
        "WWDG_RESET": False,
        "LOW_POWER_RESET": False,
        "OPTION_BYTE_RESET": False,
        "FIREWALL_RESET": False,
    }


def decode_coral_payload(payload: bytes) -> str:
    clean = payload.split(b"\x00", 1)[0]
    try:
        return clean.decode("ascii", errors="replace")
    except Exception:
        return payload.hex(" ")


def decode_telemetry_packet(raw: bytes) -> TelemetryPacket:
    if len(raw) != TELEMETRY_PACKET_SIZE:
        raise ValueError(f"Invalid telemetry packet length: {len(raw)} bytes, expected {TELEMETRY_PACKET_SIZE}")
    if TELEMETRY_STRUCT.size != TELEMETRY_PACKET_SIZE:
        raise RuntimeError(f"Python struct size mismatch: {TELEMETRY_STRUCT.size}, expected {TELEMETRY_PACKET_SIZE}")

    (
        packet_type, protocol_version, sequence_number, tx_uptime_ms,
        datapool_timestamp_ms,
        gps_lat_deg, gps_lon_deg, gps_alt_m, gps_vvel_mps, gps_speed_mps, gps_valid,
        imu_accel_x_g, imu_accel_y_g, imu_accel_z_g, imu_accel_mag_g,
        imu_gyro_x_dps, imu_gyro_y_dps, imu_gyro_z_dps, imu_valid,
        baro_pressure_pa, baro_alt_m, baro_temp_c, baro_valid, i2c_bus_state,
        batt_voltage_mv, batt_valid, coral_block, coral_valid,
        scv_magic, scv_boot_count, scv_mission_elapsed_ms, scv_flight_phase,
        scv_reset_reason, scv_equipment_enabled, scv_equipment_faults,
        scv_gps_timeout_count, scv_imu_timeout_count, scv_baro_timeout_count,
        scv_coral_timeout_count, scv_sd_fault_count, scv_watchdog_reset_count,
        scv_last_batt_mv, scv_baro_ground_alt_cm, scv_crc16,
        uplink_last_command, uplink_last_status, uplink_last_command_id,
        uplink_last_ack_sequence, uplink_command_count,
        lora_last_event, lora_consecutive_failures, lora_recovery_count,
        lora_last_success_ms, lora_rx_mode_active, lora_last_rx_status,
        lora_rx_packet_count, lora_rx_crc_error_count, lora_ack_timeout_count,
        lora_last_rx_ms,
        received_crc16,
    ) = TELEMETRY_STRUCT.unpack(raw)

    status_flags = decode_status_flags(gps_valid, imu_valid, baro_valid,
                                       batt_valid, coral_valid, scv_equipment_faults)
    reset_cause = decode_reset_cause(scv_reset_reason)

    return TelemetryPacket(
        packet_type=packet_type,
        protocol_version=protocol_version,
        sequence_number=sequence_number,
        utc_timestamp=None,  # No RTC/UTC source exists in SensorData_t yet.
        obc_uptime_ms=tx_uptime_ms,
        flight_state=scv_flight_phase,
        flight_state_name=FLIGHT_STATES.get(scv_flight_phase, f"UNKNOWN_{scv_flight_phase}"),
        status_flags_raw=0,  # Validity/fault values are sent as independent raw fields.
        status_flags=status_flags,
        battery_mv=batt_voltage_mv,
        battery_v=batt_voltage_mv / 1000.0,
        gnss_fix_type=None,
        gnss_satellites_used=None,
        latitude_deg=gps_lat_deg,
        longitude_deg=gps_lon_deg,
        gnss_altitude_m=gps_alt_m,
        gnss_hdop=None,
        gnss_vdop=None,
        ground_speed_ms=gps_speed_mps,
        vertical_speed_ms=gps_vvel_mps,
        course_deg=None,
        baro_pressure_pa=baro_pressure_pa,
        baro_temperature_c=baro_temp_c,
        baro_altitude_m=baro_alt_m,
        accel_x_ms2=imu_accel_x_g * STANDARD_GRAVITY_MS2,
        accel_y_ms2=imu_accel_y_g * STANDARD_GRAVITY_MS2,
        accel_z_ms2=imu_accel_z_g * STANDARD_GRAVITY_MS2,
        gyro_x_rads=math.radians(imu_gyro_x_dps),
        gyro_y_rads=math.radians(imu_gyro_y_dps),
        gyro_z_rads=math.radians(imu_gyro_z_dps),
        imu_temperature_c=None,
        mcu_temperature_c=None,
        reset_cause_raw=scv_reset_reason,
        reset_cause=reset_cause,
        boot_count=scv_boot_count,
        sd_log_record_counter=None,
        sd_error_counter=scv_sd_fault_count,
        sensor_error_counter=None,
        command_counter=None,
        last_uplink_rssi_dbm=None,
        last_uplink_snr_db=None,
        coral_status=int(bool(coral_valid)),
        coral_result_age_s=None,
        coral_payload_raw=coral_block,
        coral_payload_text=decode_coral_payload(coral_block),
        received_crc16=received_crc16,
        calculated_crc16=crc16_ccitt(raw[:-2]),
        crc_ok=(received_crc16 == crc16_ccitt(raw[:-2])),
        packet_type_ok=(packet_type == TELEMETRY_PACKET_TYPE),
        protocol_version_ok=(protocol_version == TELEMETRY_PROTOCOL_VERSION),
        length_ok=True,
        datapool_timestamp_ms=datapool_timestamp_ms,
        gps_valid_raw=gps_valid,
        imu_accel_x_g=imu_accel_x_g,
        imu_accel_y_g=imu_accel_y_g,
        imu_accel_z_g=imu_accel_z_g,
        imu_accel_mag_g=imu_accel_mag_g,
        imu_gyro_x_dps=imu_gyro_x_dps,
        imu_gyro_y_dps=imu_gyro_y_dps,
        imu_gyro_z_dps=imu_gyro_z_dps,
        imu_valid_raw=imu_valid,
        baro_valid_raw=baro_valid,
        i2c_bus_state_raw=i2c_bus_state,
        batt_valid_raw=batt_valid,
        coral_valid_raw=coral_valid,
        scv_magic=scv_magic,
        scv_mission_elapsed_ms=scv_mission_elapsed_ms,
        scv_equipment_enabled=scv_equipment_enabled,
        scv_equipment_faults=scv_equipment_faults,
        scv_gps_timeout_count=scv_gps_timeout_count,
        scv_imu_timeout_count=scv_imu_timeout_count,
        scv_baro_timeout_count=scv_baro_timeout_count,
        scv_coral_timeout_count=scv_coral_timeout_count,
        scv_sd_fault_count=scv_sd_fault_count,
        scv_watchdog_reset_count=scv_watchdog_reset_count,
        scv_last_batt_mv=scv_last_batt_mv,
        scv_baro_ground_alt_cm=scv_baro_ground_alt_cm,
        scv_crc16=scv_crc16,
        reset_reason_name=RESET_REASONS.get(scv_reset_reason, f"UNKNOWN_{scv_reset_reason}"),
        uplink_last_command=uplink_last_command,
        uplink_last_command_name=UPLINK_COMMANDS.get(
            uplink_last_command, f"UNKNOWN_{uplink_last_command}"
        ),
        uplink_last_status=uplink_last_status,
        uplink_last_status_name=UPLINK_STATUSES.get(
            uplink_last_status, f"UNKNOWN_{uplink_last_status}"
        ),
        uplink_last_command_id=uplink_last_command_id,
        uplink_last_ack_sequence=uplink_last_ack_sequence,
        uplink_command_count=uplink_command_count,
        lora_last_event=lora_last_event,
        lora_last_event_name=LORA_EVENTS.get(lora_last_event, f"UNKNOWN_{lora_last_event}"),
        lora_consecutive_failures=lora_consecutive_failures,
        lora_recovery_count=lora_recovery_count,
        lora_last_success_ms=lora_last_success_ms,
        lora_rx_mode_active=lora_rx_mode_active,
        lora_last_rx_status=lora_last_rx_status,
        lora_last_rx_status_name=LORA_RX_HEALTH.get(
            lora_last_rx_status, f"UNKNOWN_{lora_last_rx_status}"
        ),
        lora_rx_packet_count=lora_rx_packet_count,
        lora_rx_crc_error_count=lora_rx_crc_error_count,
        lora_ack_timeout_count=lora_ack_timeout_count,
        lora_last_rx_ms=lora_last_rx_ms,
    )


def short_summary(packet: TelemetryPacket) -> str:
    crc_text = "OK" if packet.crc_ok else "BAD"
    return (
        f"seq={packet.sequence_number} state={packet.flight_state_name} "
        f"battery={packet.battery_v:.2f} V lat={packet.latitude_deg:.7f} "
        f"lon={packet.longitude_deg:.7f} baro_alt={packet.baro_altitude_m:.1f} m "
        f"CRC={crc_text}"
    )
