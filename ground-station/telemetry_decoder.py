# telemetry_decoder.py
"""Decoder for the 92-byte protocol-v8 telemetry packet."""

import math
import struct
from dataclasses import asdict, dataclass, field
from typing import Optional

TELEMETRY_PACKET_TYPE = 0x01
TELEMETRY_PROTOCOL_VERSION = 0x08
TELEMETRY_PACKET_SIZE = 92
STANDARD_GRAVITY_MS2 = 9.80665

EQUIPMENT_GPS = 1 << 0
EQUIPMENT_IMU = 1 << 1
EQUIPMENT_BARO = 1 << 2
EQUIPMENT_CORAL = 1 << 3
EQUIPMENT_SD = 1 << 4
EQUIPMENT_LORA = 1 << 5
EQUIPMENT_EPS_ADC = 1 << 6
EQUIPMENT_CDH = 1 << 15

EQUIPMENT_BITS = (
    (EQUIPMENT_GPS, "GPS"),
    (EQUIPMENT_IMU, "IMU"),
    (EQUIPMENT_BARO, "BARO"),
    (EQUIPMENT_CORAL, "CORAL"),
    (EQUIPMENT_SD, "SD"),
    (EQUIPMENT_LORA, "LORA"),
    (EQUIPMENT_EPS_ADC, "EPS_ADC"),
    (EQUIPMENT_CDH, "CDH"),
)

VALID_GPS = 1 << 0
VALID_IMU = 1 << 1
VALID_BARO = 1 << 2
VALID_BATTERY = 1 << 3
VALID_CORAL = 1 << 4
LORA_RX_STATUS_MASK = 0x07
LORA_RX_ACTIVE = 1 << 3
UPLINK_STATUS_MASK = 0x07
UPLINK_ACK_STATUS_SHIFT = 3

FLIGHT_STATES = {
    0: "STANDBY", 1: "LAUNCH", 2: "ASCENT", 3: "CRUISE",
    4: "DESCENT", 5: "LANDING",
}
LORA_EVENTS = {
    0: "NONE", 1: "INIT_OK", 2: "INIT_FAIL", 3: "TX_OK",
    4: "TX_SPI_FAIL", 5: "TX_TIMEOUT", 6: "TX_BAD_LENGTH",
    7: "NOT_READY", 8: "CONFIG_FAIL", 9: "RX_OK",
    10: "RX_CRC_ERROR", 11: "RX_SPI_FAIL", 12: "RX_MODE_FAIL",
    13: "ACK_TIMEOUT",
}
UPLINK_STATUSES = {
    0: "NONE", 1: "ACCEPTED", 2: "INVALID_FORMAT",
    3: "UNSUPPORTED", 4: "DUPLICATE", 5: "UNEXPECTED_ACK",
}
LORA_RX_HEALTH = {
    0: "NONE", 1: "ACTIVE", 2: "PACKET_OK", 3: "CRC_ERROR",
    4: "BAD_LENGTH", 5: "SPI_ERROR", 6: "MODE_ERROR",
}
RESET_REASONS = {
    0: "UNKNOWN", 1: "POWER_ON", 2: "WATCHDOG", 3: "SOFTWARE", 255: "INVALID",
}

# Little-endian packed layout matching the protocol-v8 TelemetryPacket_t.
TELEMETRY_STRUCT = struct.Struct("<BBHIIHBBBBHHHBBiiihHHIBBhhhhhhIihHHHBHBBBBBBHBH")
assert TELEMETRY_STRUCT.size == TELEMETRY_PACKET_SIZE


@dataclass
class TelemetryPacket:
    packet_type: int = 0
    protocol_version: int = 0
    sequence_number: int = 0
    utc_timestamp: Optional[int] = None
    obc_uptime_ms: int = 0
    flight_state: int = 0
    flight_state_name: str = "UNKNOWN"
    status_flags_raw: int = 0
    status_flags: dict = field(default_factory=dict)
    battery_mv: Optional[int] = None
    battery_v: Optional[float] = None
    gnss_fix_type: Optional[int] = None
    gnss_satellites_used: Optional[int] = None
    latitude_deg: Optional[float] = None
    longitude_deg: Optional[float] = None
    gnss_altitude_m: Optional[float] = None
    ground_speed_ms: Optional[float] = None
    vertical_speed_ms: Optional[float] = None
    course_deg: Optional[float] = None
    baro_pressure_pa: Optional[float] = None
    baro_temperature_c: Optional[float] = None
    baro_altitude_m: Optional[float] = None
    accel_x_ms2: Optional[float] = None
    accel_y_ms2: Optional[float] = None
    accel_z_ms2: Optional[float] = None
    gyro_x_rads: Optional[float] = None
    gyro_y_rads: Optional[float] = None
    gyro_z_rads: Optional[float] = None
    reset_cause_raw: int = 0
    reset_cause: dict = field(default_factory=dict)
    boot_count: int = 0
    sd_error_counter: Optional[int] = None
    coral_status: int = 0
    coral_result_age_s: Optional[int] = None
    coral_payload_raw: bytes = b""
    coral_payload_text: str = ""
    received_crc16: int = 0
    calculated_crc16: int = 0
    crc_ok: bool = False
    packet_type_ok: bool = False
    protocol_version_ok: bool = False
    length_ok: bool = False

    gps_valid_raw: int = 0
    imu_accel_x_g: Optional[float] = None
    imu_accel_y_g: Optional[float] = None
    imu_accel_z_g: Optional[float] = None
    imu_accel_mag_g: Optional[float] = None
    imu_gyro_x_dps: Optional[float] = None
    imu_gyro_y_dps: Optional[float] = None
    imu_gyro_z_dps: Optional[float] = None
    imu_valid_raw: int = 0
    baro_valid_raw: int = 0
    i2c_bus_state_raw: int = 0
    batt_valid_raw: int = 0
    coral_valid_raw: int = 0

    scv_mission_elapsed_ms: int = 0
    scv_equipment_enabled: int = 0
    scv_equipment_faults: int = 0
    scv_sd_fault_count: int = 0
    scv_watchdog_reset_count: int = 0
    reset_reason_name: str = "UNKNOWN"

    uplink_last_status: int = 0
    uplink_last_status_name: str = "NONE"
    uplink_last_command_id: int = 0
    uplink_last_ack_status: int = 0
    uplink_last_ack_status_name: str = "NONE"

    lora_last_event: int = 0
    lora_last_event_name: str = "NONE"
    lora_consecutive_failures: int = 0
    lora_recovery_count: int = 0
    lora_rx_mode_active: int = 0
    lora_last_rx_status: int = 0
    lora_last_rx_status_name: str = "NONE"
    lora_ack_timeout_count: int = 0
    lora_tx_fault_count: Optional[int] = None

    sample_age_ms: Optional[int] = None
    gnss_utc_sod: Optional[int] = None
    coral_sequence_low: Optional[int] = None
    coral_fraction_q16: Optional[int] = None
    coral_fraction_percent: Optional[float] = None
    latitude_e7: Optional[int] = None
    longitude_e7: Optional[int] = None
    gnss_altitude_dm: Optional[int] = None
    vertical_speed_cms: Optional[int] = None
    ground_speed_cms: Optional[int] = None
    course_cdeg: Optional[int] = None
    accel_x_mg: Optional[int] = None
    accel_y_mg: Optional[int] = None
    accel_z_mg: Optional[int] = None
    gyro_x_ddeg_s: Optional[int] = None
    gyro_y_ddeg_s: Optional[int] = None
    gyro_z_ddeg_s: Optional[int] = None
    baro_altitude_dm: Optional[int] = None
    baro_temperature_cdeg: Optional[int] = None

    @property
    def validation_ok(self) -> bool:
        return self.length_ok and self.crc_ok and self.packet_type_ok and self.protocol_version_ok

    def to_dict(self):
        return asdict(self)


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def decode_reset_cause(reset_reason: int) -> dict:
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


def decode_equipment_mask(mask: int) -> list[str]:
    """Return all set equipment names, preserving any reserved set bits."""
    names = [name for bit_mask, name in EQUIPMENT_BITS if mask & bit_mask]
    known_mask = sum(bit_mask for bit_mask, _ in EQUIPMENT_BITS)
    names.extend(
        f"UNKNOWN_BIT_{bit}"
        for bit in range(16)
        if mask & (1 << bit) and not known_mask & (1 << bit)
    )
    return names


def format_equipment_mask(mask: int) -> str:
    names = decode_equipment_mask(mask)
    return ", ".join(names) if names else "NONE"


def decode_status_flags(gps_valid: int, imu_valid: int, baro_valid: int,
                        batt_valid: int, coral_valid: int, faults: int,
                        gnss_time_valid: bool = False) -> dict:
    return {
        "GNSS_FIX_VALID": bool(gps_valid),
        "GNSS_TIME_VALID": bool(gnss_time_valid),
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


def _signed(value: int, sentinel: int, scale: float) -> Optional[float]:
    return None if value == sentinel else value / scale


def _unsigned(value: int, sentinel: int, scale: float = 1.0) -> Optional[float]:
    return None if value == sentinel else value / scale


def _decode_v8(raw: bytes) -> TelemetryPacket:
    values = TELEMETRY_STRUCT.unpack(raw)
    (
        packet_type, protocol_version, sequence_number, tx_uptime_s,
        mission_elapsed_s, boot_count, flight_phase, reset_reason,
        validity_flags, i2c_bus_state, equipment_enabled, equipment_faults,
        sample_age_raw, watchdog_reset_count, sd_fault_count,
        latitude_e7, longitude_e7, gnss_altitude_dm, vertical_speed_cms,
        ground_speed_cms, course_cdeg, gnss_utc_sod_raw, gnss_satellites_raw,
        gnss_fix_type_raw, accel_x_mg, accel_y_mg, accel_z_mg,
        gyro_x_ddeg_s, gyro_y_ddeg_s, gyro_z_ddeg_s, baro_pressure_raw,
        baro_altitude_dm, baro_temperature_cdeg, battery_mv_raw,
        coral_sequence_raw, coral_fraction_raw, coral_status, coral_age_raw,
        lora_last_event, lora_consecutive_failures, lora_recovery_count,
        lora_rx_state, lora_tx_fault_count, lora_ack_timeout_count,
        last_command_id, uplink_state, received_crc16,
    ) = values

    gps_valid = int(bool(validity_flags & VALID_GPS))
    imu_valid = int(bool(validity_flags & VALID_IMU))
    baro_valid = int(bool(validity_flags & VALID_BARO))
    batt_valid = int(bool(validity_flags & VALID_BATTERY))
    coral_valid = int(bool(validity_flags & VALID_CORAL))

    latitude = _signed(latitude_e7, -(1 << 31), 10_000_000.0)
    longitude = _signed(longitude_e7, -(1 << 31), 10_000_000.0)
    gnss_altitude = _signed(gnss_altitude_dm, -(1 << 31), 10.0)
    vertical_speed = _signed(vertical_speed_cms, -(1 << 15), 100.0)
    ground_speed = _unsigned(ground_speed_cms, 0xFFFF, 100.0)
    course = _unsigned(course_cdeg, 0xFFFF, 100.0)
    gnss_utc_sod = None if gnss_utc_sod_raw == 0xFFFFFFFF else gnss_utc_sod_raw
    gnss_satellites = None if gnss_satellites_raw == 0xFF else gnss_satellites_raw
    gnss_fix_type = None if gnss_fix_type_raw == 0xFF else gnss_fix_type_raw

    accel_x_g = _signed(accel_x_mg, -(1 << 15), 1000.0)
    accel_y_g = _signed(accel_y_mg, -(1 << 15), 1000.0)
    accel_z_g = _signed(accel_z_mg, -(1 << 15), 1000.0)
    gyro_x_dps = _signed(gyro_x_ddeg_s, -(1 << 15), 10.0)
    gyro_y_dps = _signed(gyro_y_ddeg_s, -(1 << 15), 10.0)
    gyro_z_dps = _signed(gyro_z_ddeg_s, -(1 << 15), 10.0)
    accel_magnitude = None
    if accel_x_g is not None and accel_y_g is not None and accel_z_g is not None:
        accel_magnitude = math.sqrt(accel_x_g ** 2 + accel_y_g ** 2 + accel_z_g ** 2)

    baro_pressure = _unsigned(baro_pressure_raw, 0xFFFFFFFF)
    baro_altitude = _signed(baro_altitude_dm, -(1 << 31), 10.0)
    baro_temperature = _signed(baro_temperature_cdeg, -(1 << 15), 100.0)
    battery_mv = None if battery_mv_raw == 0xFFFF else battery_mv_raw
    coral_sequence = None if coral_sequence_raw == 0xFFFF else coral_sequence_raw
    coral_fraction = None if coral_fraction_raw == 0xFFFF else coral_fraction_raw
    coral_age = None if coral_age_raw == 0xFFFF else coral_age_raw
    coral_percent = None if coral_fraction is None else coral_fraction * 100.0 / 65535.0

    lora_rx_status = lora_rx_state & LORA_RX_STATUS_MASK
    lora_rx_active = int(bool(lora_rx_state & LORA_RX_ACTIVE))
    command_status = uplink_state & UPLINK_STATUS_MASK
    ack_status = (uplink_state >> UPLINK_ACK_STATUS_SHIFT) & UPLINK_STATUS_MASK
    calculated_crc16 = crc16_ccitt(raw[:-2])
    status_flags = decode_status_flags(
        gps_valid, imu_valid, baro_valid, batt_valid, coral_valid,
        equipment_faults, gnss_utc_sod is not None,
    )
    coral_raw = struct.pack("<HHBH", coral_sequence_raw, coral_fraction_raw,
                            coral_status, coral_age_raw)
    coral_text = (
        f"seq={coral_sequence if coral_sequence is not None else 'unknown'} "
        f"cloud={coral_percent:.2f}% status=0x{coral_status:02X}"
        if coral_percent is not None else f"seq=unknown cloud=unknown status=0x{coral_status:02X}"
    )

    return TelemetryPacket(
        packet_type=packet_type,
        protocol_version=protocol_version,
        sequence_number=sequence_number,
        utc_timestamp=gnss_utc_sod,
        obc_uptime_ms=tx_uptime_s * 1000,
        flight_state=flight_phase,
        flight_state_name=FLIGHT_STATES.get(flight_phase, f"UNKNOWN_{flight_phase}"),
        status_flags_raw=validity_flags,
        status_flags=status_flags,
        battery_mv=battery_mv,
        battery_v=None if battery_mv is None else battery_mv / 1000.0,
        gnss_fix_type=gnss_fix_type,
        gnss_satellites_used=gnss_satellites,
        latitude_deg=latitude,
        longitude_deg=longitude,
        gnss_altitude_m=gnss_altitude,
        ground_speed_ms=ground_speed,
        vertical_speed_ms=vertical_speed,
        course_deg=course,
        baro_pressure_pa=baro_pressure,
        baro_temperature_c=baro_temperature,
        baro_altitude_m=baro_altitude,
        accel_x_ms2=None if accel_x_g is None else accel_x_g * STANDARD_GRAVITY_MS2,
        accel_y_ms2=None if accel_y_g is None else accel_y_g * STANDARD_GRAVITY_MS2,
        accel_z_ms2=None if accel_z_g is None else accel_z_g * STANDARD_GRAVITY_MS2,
        gyro_x_rads=None if gyro_x_dps is None else math.radians(gyro_x_dps),
        gyro_y_rads=None if gyro_y_dps is None else math.radians(gyro_y_dps),
        gyro_z_rads=None if gyro_z_dps is None else math.radians(gyro_z_dps),
        reset_cause_raw=reset_reason,
        reset_cause=decode_reset_cause(reset_reason),
        boot_count=boot_count,
        sd_error_counter=sd_fault_count,
        coral_status=coral_status,
        coral_result_age_s=coral_age,
        coral_payload_raw=coral_raw,
        coral_payload_text=coral_text,
        received_crc16=received_crc16,
        calculated_crc16=calculated_crc16,
        crc_ok=received_crc16 == calculated_crc16,
        packet_type_ok=packet_type == TELEMETRY_PACKET_TYPE,
        protocol_version_ok=protocol_version == TELEMETRY_PROTOCOL_VERSION,
        length_ok=True,
        gps_valid_raw=gps_valid,
        imu_accel_x_g=accel_x_g,
        imu_accel_y_g=accel_y_g,
        imu_accel_z_g=accel_z_g,
        imu_accel_mag_g=accel_magnitude,
        imu_gyro_x_dps=gyro_x_dps,
        imu_gyro_y_dps=gyro_y_dps,
        imu_gyro_z_dps=gyro_z_dps,
        imu_valid_raw=imu_valid,
        baro_valid_raw=baro_valid,
        i2c_bus_state_raw=i2c_bus_state,
        batt_valid_raw=batt_valid,
        coral_valid_raw=coral_valid,
        scv_mission_elapsed_ms=mission_elapsed_s * 1000,
        scv_equipment_enabled=equipment_enabled,
        scv_equipment_faults=equipment_faults,
        scv_sd_fault_count=sd_fault_count,
        scv_watchdog_reset_count=watchdog_reset_count,
        reset_reason_name=RESET_REASONS.get(reset_reason, f"UNKNOWN_{reset_reason}"),
        uplink_last_status=command_status,
        uplink_last_status_name=UPLINK_STATUSES.get(command_status, f"UNKNOWN_{command_status}"),
        uplink_last_command_id=last_command_id,
        uplink_last_ack_status=ack_status,
        uplink_last_ack_status_name=UPLINK_STATUSES.get(ack_status, f"UNKNOWN_{ack_status}"),
        lora_last_event=lora_last_event,
        lora_last_event_name=LORA_EVENTS.get(lora_last_event, f"UNKNOWN_{lora_last_event}"),
        lora_consecutive_failures=lora_consecutive_failures,
        lora_recovery_count=lora_recovery_count,
        lora_rx_mode_active=lora_rx_active,
        lora_last_rx_status=lora_rx_status,
        lora_last_rx_status_name=LORA_RX_HEALTH.get(lora_rx_status, f"UNKNOWN_{lora_rx_status}"),
        lora_ack_timeout_count=lora_ack_timeout_count,
        lora_tx_fault_count=lora_tx_fault_count,
        sample_age_ms=None if sample_age_raw == 0xFFFF else sample_age_raw,
        gnss_utc_sod=gnss_utc_sod,
        coral_sequence_low=coral_sequence,
        coral_fraction_q16=coral_fraction,
        coral_fraction_percent=coral_percent,
        latitude_e7=None if latitude_e7 == -(1 << 31) else latitude_e7,
        longitude_e7=None if longitude_e7 == -(1 << 31) else longitude_e7,
        gnss_altitude_dm=None if gnss_altitude_dm == -(1 << 31) else gnss_altitude_dm,
        vertical_speed_cms=None if vertical_speed_cms == -(1 << 15) else vertical_speed_cms,
        ground_speed_cms=None if ground_speed_cms == 0xFFFF else ground_speed_cms,
        course_cdeg=None if course_cdeg == 0xFFFF else course_cdeg,
        accel_x_mg=None if accel_x_mg == -(1 << 15) else accel_x_mg,
        accel_y_mg=None if accel_y_mg == -(1 << 15) else accel_y_mg,
        accel_z_mg=None if accel_z_mg == -(1 << 15) else accel_z_mg,
        gyro_x_ddeg_s=None if gyro_x_ddeg_s == -(1 << 15) else gyro_x_ddeg_s,
        gyro_y_ddeg_s=None if gyro_y_ddeg_s == -(1 << 15) else gyro_y_ddeg_s,
        gyro_z_ddeg_s=None if gyro_z_ddeg_s == -(1 << 15) else gyro_z_ddeg_s,
        baro_altitude_dm=None if baro_altitude_dm == -(1 << 31) else baro_altitude_dm,
        baro_temperature_cdeg=None if baro_temperature_cdeg == -(1 << 15) else baro_temperature_cdeg,
    )



def decode_telemetry_packet(raw: bytes) -> TelemetryPacket:
    if len(raw) != TELEMETRY_PACKET_SIZE:
        raise ValueError(
            f"Invalid telemetry packet length: {len(raw)} bytes; "
            f"expected {TELEMETRY_PACKET_SIZE}"
        )
    if raw[0] != TELEMETRY_PACKET_TYPE:
        raise ValueError(f"Unsupported telemetry packet type: {raw[0]}")
    if raw[1] != TELEMETRY_PROTOCOL_VERSION:
        raise ValueError(f"Unsupported telemetry protocol version: {raw[1]}; expected 8")
    return _decode_v8(raw)


def short_summary(packet: TelemetryPacket) -> str:
    crc_text = "OK" if packet.crc_ok else "BAD"
    battery = "unknown" if packet.battery_v is None else f"{packet.battery_v:.2f} V"
    latitude = "unknown" if packet.latitude_deg is None else f"{packet.latitude_deg:.7f}"
    longitude = "unknown" if packet.longitude_deg is None else f"{packet.longitude_deg:.7f}"
    return (
        f"v={packet.protocol_version} seq={packet.sequence_number} "
        f"state={packet.flight_state_name} battery={battery} "
        f"lat={latitude} lon={longitude} CRC={crc_text}"
    )
