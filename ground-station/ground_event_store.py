"""Thread-safe CSV logger for ground-station and dashboard events."""

import csv
import json
import os
import threading
from datetime import datetime, timezone
from typing import Any


class GroundEventStore:
    FIELDNAMES = [
        "pc_time_iso",
        "pc_time_unix",
        "event_type",
        "severity",
        "message",
        "sequence_number",
        "lora_rssi_dbm",
        "lora_snr_db",
        "receiver_running",
        "radio_enabled",
        "radio_initialized",
        "non_telemetry_packets",
        "decode_errors",
        "lora_crc_errors",
        "command_tx_count",
        "command_tx_failures",
        "details_json",
    ]

    def __init__(self, log_dir: str = "logs", enable_csv: bool = True):
        self.lock = threading.Lock()
        self.enable_csv = enable_csv
        self.csv_file = None
        self.csv_writer = None
        self.csv_path = None
        self.total_events_logged = 0

        if enable_csv:
            os.makedirs(log_dir, exist_ok=True)
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            self.csv_path = os.path.join(
                log_dir,
                f"ground_station_events_{timestamp}.csv",
            )
            self.csv_file = open(
                self.csv_path,
                mode="w",
                newline="",
                encoding="utf-8",
            )
            self.csv_writer = csv.DictWriter(
                self.csv_file,
                fieldnames=self.FIELDNAMES,
            )
            self.csv_writer.writeheader()
            self.csv_file.flush()

    def add_event(
        self,
        event_type: str,
        severity: str = "info",
        message: str = "",
        *,
        sequence_number: int | None = None,
        lora_rssi_dbm: float | int | None = None,
        lora_snr_db: float | int | None = None,
        receiver_state: dict[str, Any] | None = None,
        details: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        now = datetime.now(timezone.utc)
        receiver = receiver_state or {}
        row = {
            "pc_time_iso": now.isoformat(),
            "pc_time_unix": now.timestamp(),
            "event_type": event_type,
            "severity": severity,
            "message": message,
            "sequence_number": sequence_number,
            "lora_rssi_dbm": lora_rssi_dbm,
            "lora_snr_db": lora_snr_db,
            "receiver_running": receiver.get("running"),
            "radio_enabled": receiver.get("radio_enabled"),
            "radio_initialized": receiver.get("radio_initialized"),
            "non_telemetry_packets": receiver.get("non_telemetry_packets"),
            "decode_errors": receiver.get("decode_errors"),
            "lora_crc_errors": receiver.get("lora_crc_errors"),
            "command_tx_count": receiver.get("command_tx_count"),
            "command_tx_failures": receiver.get("command_tx_failures"),
            "details_json": json.dumps(
                details or {},
                ensure_ascii=False,
                sort_keys=True,
                default=str,
                separators=(",", ":"),
            ),
        }

        with self.lock:
            if self.csv_writer is not None:
                self.csv_writer.writerow(row)
                self.csv_file.flush()
                self.total_events_logged += 1

        return row

    def get_stats(self) -> dict[str, Any]:
        with self.lock:
            return {
                "total_events_logged": self.total_events_logged,
                "csv_path": self.csv_path,
            }

    def close(self) -> None:
        with self.lock:
            if self.csv_file is not None:
                self.csv_file.flush()
                self.csv_file.close()
            self.csv_file = None
            self.csv_writer = None
