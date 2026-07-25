import math
import unittest
from datetime import datetime, timezone

from telemetry_decoder import (
    TELEMETRY_PACKET_SIZE,
    TELEMETRY_STRUCT,
    crc16_ccitt,
    decode_equipment_mask,
    decode_telemetry_packet,
    format_equipment_mask,
)
from telemetry_store import TelemetryStore


def build_v8_packet(**overrides):
    fields = {
        "packet_type": 1,
        "protocol_version": 8,
        "sequence_number": 0x1234,
        "tx_uptime_s": 100,
        "mission_elapsed_s": 80,
        "boot_count": 2,
        "flight_phase": 2,
        "reset_reason": 1,
        "validity_flags": 0x1F,
        "i2c_bus_state": 0,
        "equipment_enabled": 0x7F,
        "equipment_faults": 0,
        "sample_age_ms": 25,
        "watchdog_resets": 1,
        "sd_faults": 2,
        "latitude_e7": 413876543,
        "longitude_e7": 21876543,
        "gnss_altitude_dm": 12345,
        "vertical_speed_cms": 678,
        "ground_speed_cms": 123,
        "course_cdeg": 27050,
        "gnss_utc_sod": 45296,
        "satellites": 11,
        "fix_type": 2,
        "accel_x_mg": 100,
        "accel_y_mg": -200,
        "accel_z_mg": 980,
        "gyro_x_ddeg_s": 15,
        "gyro_y_ddeg_s": -25,
        "gyro_z_ddeg_s": 35,
        "baro_pressure_pa": 81234,
        "baro_altitude_dm": 4321,
        "baro_temperature_cdeg": -1250,
        "battery_mv": 3712,
        "coral_sequence": 321,
        "coral_fraction": 32768,
        "coral_status": 0,
        "coral_age_s": 3,
        "lora_event": 3,
        "lora_consecutive_failures": 0,
        "lora_recoveries": 1,
        "lora_rx_state": 0x0A,
        "lora_tx_faults": 4,
        "lora_ack_timeouts": 5,
        "last_command_id": 99,
        "uplink_state": 1 | (5 << 3),
    }
    fields.update(overrides)
    order = [
        "packet_type", "protocol_version", "sequence_number", "tx_uptime_s",
        "mission_elapsed_s", "boot_count", "flight_phase", "reset_reason",
        "validity_flags", "i2c_bus_state", "equipment_enabled", "equipment_faults",
        "sample_age_ms", "watchdog_resets", "sd_faults", "latitude_e7",
        "longitude_e7", "gnss_altitude_dm", "vertical_speed_cms",
        "ground_speed_cms", "course_cdeg", "gnss_utc_sod", "satellites",
        "fix_type", "accel_x_mg", "accel_y_mg", "accel_z_mg",
        "gyro_x_ddeg_s", "gyro_y_ddeg_s", "gyro_z_ddeg_s",
        "baro_pressure_pa", "baro_altitude_dm", "baro_temperature_cdeg",
        "battery_mv", "coral_sequence", "coral_fraction", "coral_status",
        "coral_age_s", "lora_event", "lora_consecutive_failures",
        "lora_recoveries", "lora_rx_state", "lora_tx_faults",
        "lora_ack_timeouts", "last_command_id", "uplink_state",
    ]
    raw = TELEMETRY_STRUCT.pack(*(fields[name] for name in order), 0)
    crc = crc16_ccitt(raw[:-2])
    return raw[:-2] + crc.to_bytes(2, "little")


class TelemetryV8DecoderTests(unittest.TestCase):
    def test_equipment_masks_decode_known_and_reserved_bits(self):
        self.assertEqual(
            decode_equipment_mask(0x8091),
            ["GPS", "SD", "CDH", "UNKNOWN_BIT_7"],
        )

    def test_empty_equipment_mask_is_explicit(self):
        self.assertEqual(decode_equipment_mask(0), [])
        self.assertEqual(format_equipment_mask(0), "NONE")

    def test_csv_row_includes_decoded_equipment_columns(self):
        packet = decode_telemetry_packet(
            build_v8_packet(
                equipment_enabled=0x007F,
                equipment_faults=0x8010,
            )
        )
        store = TelemetryStore(enable_csv=False)

        row = store._packet_to_row(
            telemetry_packet=packet,
            pc_receive_time=datetime.now(timezone.utc),
        )

        self.assertEqual(row["scv_equipment_enabled"], "0x007F")
        self.assertEqual(
            row["scv_equipment_enabled_decoded"],
            "GPS, IMU, BARO, CORAL, SD, LORA, EPS_ADC",
        )
        self.assertEqual(row["scv_equipment_faults"], "0x8010")
        self.assertEqual(row["scv_equipment_faults_decoded"], "SD, CDH")

    def test_golden_v8_vector(self):
        raw = build_v8_packet()
        self.assertEqual(len(raw), TELEMETRY_PACKET_SIZE)

        packet = decode_telemetry_packet(raw)
        self.assertTrue(packet.validation_ok)
        self.assertEqual(packet.sequence_number, 0x1234)
        self.assertAlmostEqual(packet.latitude_deg, 41.3876543)
        self.assertAlmostEqual(packet.longitude_deg, 2.1876543)
        self.assertAlmostEqual(packet.gnss_altitude_m, 1234.5)
        self.assertAlmostEqual(packet.vertical_speed_ms, 6.78)
        self.assertAlmostEqual(packet.course_deg, 270.5)
        self.assertEqual(packet.gnss_satellites_used, 11)
        self.assertEqual(packet.gnss_fix_type, 2)
        self.assertAlmostEqual(packet.imu_accel_mag_g, math.sqrt(1.0104), places=6)
        self.assertAlmostEqual(packet.baro_temperature_c, -12.5)
        self.assertAlmostEqual(packet.battery_v, 3.712)
        self.assertAlmostEqual(packet.coral_fraction_percent, 50.00076295)
        self.assertEqual(packet.lora_last_rx_status, 2)
        self.assertEqual(packet.lora_rx_mode_active, 1)
        self.assertEqual(packet.uplink_last_status_name, "ACCEPTED")
        self.assertEqual(packet.uplink_last_ack_status_name, "UNEXPECTED_ACK")

    def test_non_v8_length_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "expected 92"):
            decode_telemetry_packet(bytes(155))

    def test_previous_protocol_version_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "protocol version: 7"):
            decode_telemetry_packet(build_v8_packet(protocol_version=7))

    def test_v8_invalid_sentinels_decode_to_none(self):
        raw = build_v8_packet(
            validity_flags=0,
            latitude_e7=-(1 << 31),
            longitude_e7=-(1 << 31),
            gnss_altitude_dm=-(1 << 31),
            vertical_speed_cms=-(1 << 15),
            ground_speed_cms=0xFFFF,
            course_cdeg=0xFFFF,
            gnss_utc_sod=0xFFFFFFFF,
            satellites=0xFF,
            fix_type=0xFF,
            accel_x_mg=-(1 << 15),
            accel_y_mg=-(1 << 15),
            accel_z_mg=-(1 << 15),
            battery_mv=0xFFFF,
            coral_sequence=0xFFFF,
            coral_fraction=0xFFFF,
            coral_age_s=0xFFFF,
        )
        packet = decode_telemetry_packet(raw)
        self.assertTrue(packet.validation_ok)
        self.assertIsNone(packet.latitude_deg)
        self.assertIsNone(packet.imu_accel_mag_g)
        self.assertIsNone(packet.battery_v)
        self.assertIsNone(packet.coral_result_age_s)
        self.assertFalse(packet.status_flags["GNSS_FIX_VALID"])


if __name__ == "__main__":
    unittest.main()
