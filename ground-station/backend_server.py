# backend_server.py

import argparse
import asyncio
import json
import os
import secrets
import threading
import time
import traceback
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Any
from datetime import datetime, timezone

import uvicorn
from fastapi import Depends, FastAPI, Header, WebSocket, WebSocketDisconnect, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field

from ground_event_store import GroundEventStore
from lora_radio import RFM95Radio
from telemetry_decoder import (
    TELEMETRY_PACKET_SIZE,
    decode_telemetry_packet,
)
from telemetry_store import TelemetryStore
from uplink_auth import parse_uplink_key, sign_uplink


# ============================================================
# Backend configuration
# ============================================================

class BackendConfig:
    host = "127.0.0.1"
    port = 8000

    enable_radio = True
    enable_csv = True
    history = 1000
    log_dir = "logs"

    frequency_hz = 869525000
    spreading_factor = 8
    sync_word = 0x12
    tx_power_dbm = 17

    # Half-duplex turnaround guard. The flight RFM95 must finish TxDone
    # processing and enter continuous RX before the ground ACK starts.
    telemetry_ack_turnaround_s = 3.0

    # Zero keeps supervised recovery running indefinitely. A positive value is
    # available for bounded bench tests.
    radio_reconnect_max_attempts = 0
    radio_reconnect_initial_backoff_s = 0.5
    radio_reconnect_max_backoff_s = 8.0

    command_state_filename = "uplink_command_state.json"
    operator_token = os.getenv("TTC_GROUND_API_TOKEN")
    rf_auth_key_hex = os.getenv("TTC_RF_AUTH_KEY_HEX")
    command_arm_duration_s = 60.0
    command_min_interval_s = 20.0
    websocket_send_timeout_s = 2.0

    cors_origins = [
        "http://127.0.0.1:5173",
        "http://localhost:5173",
    ]


CONFIG = BackendConfig()


# ============================================================
# Shared backend state
# ============================================================

class ReceiverState:
    def __init__(self):
        self.lock = threading.Lock()

        self.running = False
        self.radio_enabled = False
        self.radio_initialized = False

        self.last_error = None
        self.last_message = "Backend not started"
        self.last_packet_time_unix = None

        self.non_telemetry_packets = 0
        self.decode_errors = 0
        self.lora_crc_errors = 0
        self.command_tx_count = 0
        self.command_tx_failures = 0

        self.radio_io_failures = 0
        self.radio_reconnect_attempts = 0
        self.radio_reconnect_successes = 0
        self.consecutive_radio_failures = 0

        # Ground-observed evidence for the flight uplink/RX path.
        self.telemetry_ack_tx_count = 0
        self.telemetry_ack_tx_failures = 0
        self.last_telemetry_ack_sequence = None
        self.last_telemetry_ack_ok = None
        self.last_telemetry_ack_time_unix = None
        self.last_command_id = None
        self.last_command_outcome = None
        self.last_command_attempt = None
        self.last_command_time_unix = None

    def update(self, **kwargs):
        with self.lock:
            for key, value in kwargs.items():
                setattr(self, key, value)

    def increment(self, name, amount=1):
        with self.lock:
            current = getattr(self, name)
            setattr(self, name, current + amount)

    def snapshot(self):
        with self.lock:
            return {
                "running": self.running,
                "radio_enabled": self.radio_enabled,
                "radio_initialized": self.radio_initialized,
                "last_error": self.last_error,
                "last_message": self.last_message,
                "last_packet_time_unix": self.last_packet_time_unix,
                "non_telemetry_packets": self.non_telemetry_packets,
                "decode_errors": self.decode_errors,
                "lora_crc_errors": self.lora_crc_errors,
                "command_tx_count": self.command_tx_count,
                "command_tx_failures": self.command_tx_failures,
                "radio_io_failures": self.radio_io_failures,
                "radio_reconnect_attempts": self.radio_reconnect_attempts,
                "radio_reconnect_successes": self.radio_reconnect_successes,
                "consecutive_radio_failures": self.consecutive_radio_failures,
                "telemetry_ack_tx_count": self.telemetry_ack_tx_count,
                "telemetry_ack_tx_failures": self.telemetry_ack_tx_failures,
                "last_telemetry_ack_sequence": self.last_telemetry_ack_sequence,
                "last_telemetry_ack_ok": self.last_telemetry_ack_ok,
                "last_telemetry_ack_time_unix": self.last_telemetry_ack_time_unix,
                "last_command_id": self.last_command_id,
                "last_command_outcome": self.last_command_outcome,
                "last_command_attempt": self.last_command_attempt,
                "last_command_time_unix": self.last_command_time_unix,
            }


class ConnectionManager:
    def __init__(self):
        self.active_connections: list[WebSocket] = []
        self.lock = asyncio.Lock()

    async def connect(self, websocket: WebSocket):
        await websocket.accept()

        async with self.lock:
            self.active_connections.append(websocket)

    async def disconnect(self, websocket: WebSocket):
        async with self.lock:
            if websocket in self.active_connections:
                self.active_connections.remove(websocket)

    async def broadcast_json(self, message: dict[str, Any]):
        async with self.lock:
            connections = tuple(self.active_connections)

        async def send(websocket: WebSocket):
            try:
                await asyncio.wait_for(
                    websocket.send_json(message),
                    timeout=CONFIG.websocket_send_timeout_s,
                )
                return None
            except Exception:
                return websocket

        dead_connections = [
            connection
            for connection in await asyncio.gather(*(send(ws) for ws in connections))
            if connection is not None
        ]
        if dead_connections:
            async with self.lock:
                self.active_connections = [
                    ws for ws in self.active_connections if ws not in dead_connections
                ]


state = ReceiverState()
manager = ConnectionManager()

store: TelemetryStore | None = None
ground_events: GroundEventStore | None = None
radio: RFM95Radio | None = None
radio_lock = threading.Lock()
downlink_tx_lock = threading.Lock()
command_transaction_lock = asyncio.Lock()
uplink_log_lock = threading.Lock()
uplink_logs: list[dict[str, Any]] = []
UPLINK_LOG_LIMIT = 200
UPLINK_STATUS_ACCEPTED = 1
UPLINK_STATUS_DUPLICATE = 4
UPLINK_STATUS_STALE = 6
UPLINK_ACCEPTED_STATUSES = {UPLINK_STATUS_ACCEPTED, UPLINK_STATUS_DUPLICATE}
pending_command_lock = threading.Lock()
pending_command_acks: dict[int, "PendingCommandAck"] = {}
next_command_id = 1
command_safety_lock = threading.Lock()
command_armed_until_monotonic = 0.0
last_manual_command_monotonic = 0.0

receiver_thread: threading.Thread | None = None
stop_event = threading.Event()
main_loop: asyncio.AbstractEventLoop | None = None


# ============================================================
# Helper functions
# ============================================================

def get_store() -> TelemetryStore:
    if store is None:
        raise HTTPException(status_code=503, detail="Telemetry store is not initialized")

    return store


def get_radio() -> RFM95Radio:
    if radio is None:
        raise HTTPException(status_code=503, detail="Radio is not initialized")

    return radio


def get_rf_auth_key() -> bytes:
    try:
        key = parse_uplink_key(CONFIG.rf_auth_key_hex)
    except ValueError as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc
    if key is None:
        raise HTTPException(
            status_code=503,
            detail="TTC_RF_AUTH_KEY_HEX is required for authenticated uplink",
        )
    return key


def rf_auth_is_configured() -> bool:
    try:
        return parse_uplink_key(CONFIG.rf_auth_key_hex) is not None
    except ValueError:
        return False


def log_ground_event(
    event_type: str,
    severity: str = "info",
    message: str = "",
    *,
    sequence_number: int | None = None,
    lora_rssi_dbm: float | int | None = None,
    lora_snr_db: float | int | None = None,
    **details: Any,
):
    current_logger = ground_events
    if current_logger is None:
        return

    current_logger.add_event(
        event_type=event_type,
        severity=severity,
        message=message,
        sequence_number=sequence_number,
        lora_rssi_dbm=lora_rssi_dbm,
        lora_snr_db=lora_snr_db,
        receiver_state=state.snapshot(),
        details=details,
    )


def record_uplink_log(
    payload: str,
    outcome: str,
    message: str,
    *,
    timeout_s: float,
    command_id: int | None = None,
    attempt: int | None = None,
    telemetry_sequence: int | None = None,
) -> dict[str, Any]:
    now = datetime.now(timezone.utc)
    row = {
        "pc_time_iso": now.isoformat(),
        "pc_time_unix": now.timestamp(),
        "payload": payload,
        "outcome": outcome,
        "message": message,
        "timeout_s": timeout_s,
        "command_id": command_id,
        "attempt": attempt,
        "telemetry_sequence": telemetry_sequence,
    }
    with uplink_log_lock:
        uplink_logs.append(row)
        del uplink_logs[:-UPLINK_LOG_LIMIT]
    return row


def get_uplink_logs(limit: int) -> list[dict[str, Any]]:
    with uplink_log_lock:
        return list(reversed(uplink_logs[-limit:]))


class PendingCommandAck:
    def __init__(self, command_id: int, baseline_sequence: int | None):
        self.command_id = command_id
        self.baseline_sequence = baseline_sequence
        self.first_transmit_time_unix: float | None = None
        self.event = threading.Event()
        self.status: int | None = None
        self.telemetry_sequence: int | None = None


def command_state_path() -> Path:
    return Path(CONFIG.log_dir) / CONFIG.command_state_filename


def load_command_id_state() -> None:
    global next_command_id

    path = command_state_path()
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
        value = raw.get("next_command_id")
        if not isinstance(value, int) or not 1 <= value <= 0x10000:
            raise ValueError(
                "next_command_id must be 1..65536 (65536 means exhausted)"
            )
        next_command_id = value
    except FileNotFoundError:
        next_command_id = 1
    except Exception as exc:
        log_ground_event(
            "command_state_invalid",
            severity="critical",
            message=f"Could not load persistent command ID state: {exc}",
            state_path=str(path),
        )
        raise RuntimeError(
            f"Refusing to reuse command IDs because {path} is invalid: {exc}"
        ) from exc


def persist_command_id_state() -> None:
    path = command_state_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="\n") as handle:
        json.dump({"next_command_id": next_command_id}, handle)
        handle.write("\n")
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(temporary, path)


def command_safety_snapshot() -> dict[str, Any]:
    with command_safety_lock:
        remaining = max(0.0, command_armed_until_monotonic - time.monotonic())
    return {
        "auth_required": bool(CONFIG.operator_token),
        "rf_auth_configured": rf_auth_is_configured(),
        "command_ids_remaining": max(0, 0x10000 - next_command_id),
        "armed": remaining > 0.0,
        "armed_remaining_s": remaining,
        "arm_duration_s": CONFIG.command_arm_duration_s,
        "minimum_interval_s": CONFIG.command_min_interval_s,
    }


def arm_commands() -> dict[str, Any]:
    global command_armed_until_monotonic
    if next_command_id > 0xFFFF:
        raise HTTPException(
            status_code=503,
            detail="Command-ID space exhausted; coordinated RF rekey is required",
        )
    with command_safety_lock:
        command_armed_until_monotonic = (
            time.monotonic() + CONFIG.command_arm_duration_s
        )
    return command_safety_snapshot()


def disarm_commands() -> dict[str, Any]:
    global command_armed_until_monotonic
    with command_safety_lock:
        command_armed_until_monotonic = 0.0
    return command_safety_snapshot()


def consume_command_arm() -> None:
    global command_armed_until_monotonic
    global last_manual_command_monotonic

    now = time.monotonic()
    with command_safety_lock:
        if now >= command_armed_until_monotonic:
            raise HTTPException(
                status_code=423,
                detail="Uplink commands are disarmed; arm the TTC command path first",
            )
        elapsed = now - last_manual_command_monotonic
        if last_manual_command_monotonic and elapsed < CONFIG.command_min_interval_s:
            retry_after = CONFIG.command_min_interval_s - elapsed
            raise HTTPException(
                status_code=429,
                detail=f"Command rate limit active; retry in {retry_after:.1f} s",
            )
        command_armed_until_monotonic = 0.0
        last_manual_command_monotonic = now


def require_operator_token(
    x_ground_station_token: str | None = Header(
        default=None,
        alias="X-Ground-Station-Token",
    ),
) -> None:
    expected = CONFIG.operator_token
    if expected is None:
        return
    if (
        x_ground_station_token is None
        or not secrets.compare_digest(x_ground_station_token, expected)
    ):
        raise HTTPException(status_code=401, detail="Invalid ground-station operator token")


def register_pending_command() -> PendingCommandAck:
    global next_command_id

    with pending_command_lock:
        if next_command_id > 0xFFFF:
            raise HTTPException(
                status_code=503,
                detail=(
                    "The 16-bit command-ID space is exhausted for this RF key; "
                    "rekey flight and ground before sending more commands"
                ),
            )
        current_store = store
        latest = current_store.get_latest() if current_store is not None else None
        baseline = latest.get("sequence_number") if latest is not None else None
        if not isinstance(baseline, int):
            baseline = None
        command_id = next_command_id
        if command_id in pending_command_acks:
            raise HTTPException(status_code=503, detail="Command ID is already pending")
        next_command_id += 1
        pending = PendingCommandAck(command_id, baseline)
        pending_command_acks[command_id] = pending
        try:
            persist_command_id_state()
        except Exception as exc:
            pending_command_acks.pop(command_id, None)
            raise HTTPException(
                status_code=503,
                detail=f"Could not persist uplink command ID: {exc}",
            ) from exc
        return pending


def observe_command_ack(row: dict[str, Any]) -> None:
    if row.get("telemetry_valid") is not True:
        return

    command_id = row.get("uplink_last_command_id")
    status = row.get("uplink_last_status")

    if (
        not isinstance(command_id, int) or command_id == 0
        or not isinstance(status, int)
    ):
        return

    with pending_command_lock:
        pending = pending_command_acks.get(command_id)
        if pending is None:
            return
        receive_time = row.get("pc_receive_time_unix")
        sequence = row.get("sequence_number")
        if pending.first_transmit_time_unix is None:
            return
        if not isinstance(receive_time, (int, float)):
            return
        if receive_time < pending.first_transmit_time_unix:
            return
        if pending.baseline_sequence is not None:
            if not isinstance(sequence, int):
                return
            forward = (sequence - pending.baseline_sequence) & 0xFFFF
            if forward == 0 or forward >= 0x8000:
                return
        pending.status = status
        pending.telemetry_sequence = sequence if isinstance(sequence, int) else None
        pending.event.set()


def send_automatic_downlink_ack(
    boot_count: int,
    sequence_number: int,
    tx_uptime_s: int,
) -> None:
    global radio

    current_radio = None

    try:
        payload = sign_uplink(
            f"ACK,{boot_count},{sequence_number},{tx_uptime_s}",
            get_rf_auth_key(),
        )
    except HTTPException as exc:
        state.increment("telemetry_ack_tx_failures")
        state.update(
            last_telemetry_ack_sequence=sequence_number,
            last_telemetry_ack_ok=False,
            last_telemetry_ack_time_unix=time.time(),
        )
        log_ground_event(
            "telemetry_ack_auth_unavailable",
            severity="critical",
            message=str(exc.detail),
            sequence_number=sequence_number,
        )
        return
    try:
        with downlink_tx_lock:
            if stop_event.wait(CONFIG.telemetry_ack_turnaround_s):
                return
            with radio_lock:
                current_radio = radio
                if current_radio is None:
                    return
                ok = current_radio.send_packet(
                    payload,
                    timeout_s=5.0,
                    cancel_event=stop_event,
                )
                if not stop_event.is_set():
                    current_radio.start_rx_continuous()
    except Exception as exc:
        if current_radio is not None:
            with radio_lock:
                if radio is current_radio:
                    radio = None
                close_radio_safely(current_radio)
        state.increment("radio_io_failures")
        state.increment("telemetry_ack_tx_failures")
        state.update(
            radio_initialized=False,
            last_telemetry_ack_sequence=sequence_number,
            last_telemetry_ack_ok=False,
            last_telemetry_ack_time_unix=time.time(),
        )
        record_uplink_log(payload, "error", str(exc), timeout_s=5.0)
        log_ground_event(
            "telemetry_ack_error",
            severity="warning",
            message=str(exc),
            sequence_number=sequence_number,
            payload=payload,
        )
        return

    outcome = "sent" if ok else "timeout"
    state.increment("telemetry_ack_tx_count" if ok else "telemetry_ack_tx_failures")
    state.update(
        last_telemetry_ack_sequence=sequence_number,
        last_telemetry_ack_ok=ok,
        last_telemetry_ack_time_unix=time.time(),
    )
    record_uplink_log(
        payload,
        outcome,
        "Automatic telemetry acknowledgement" if ok else "Telemetry ACK TxDone timeout",
        timeout_s=5.0,
        telemetry_sequence=sequence_number,
    )
    log_ground_event(
        "telemetry_ack_sent" if ok else "telemetry_ack_timeout",
        severity="info" if ok else "warning",
        message="Ground acknowledged received telemetry" if ok else "Ground telemetry ACK timed out",
        sequence_number=sequence_number,
        payload=payload,
    )


def schedule_broadcast(message: dict[str, Any]):
    """
    Called from the receiver thread.

    It safely schedules an async WebSocket broadcast on the FastAPI event loop.
    """

    global main_loop

    if main_loop is None:
        return

    try:
        future = asyncio.run_coroutine_threadsafe(
            manager.broadcast_json(message),
            main_loop,
        )
        def report_failure(completed):
            try:
                error = completed.exception()
            except Exception:
                return
            if error is not None:
                log_ground_event(
                    "websocket_broadcast_error",
                    severity="warning",
                    message=str(error),
                    message_type=message.get("type"),
                )
        future.add_done_callback(report_failure)
    except Exception as exc:
        log_ground_event(
            "websocket_schedule_error",
            severity="warning",
            message=str(exc),
            message_type=message.get("type"),
        )


def backend_status_payload():
    current_store = store
    current_ground_events = ground_events

    stats = None
    latest = None
    ground_event_stats = None

    if current_store is not None:
        stats = current_store.get_stats()
        latest = current_store.get_latest()

    if current_ground_events is not None:
        ground_event_stats = current_ground_events.get_stats()

    return {
        "receiver": state.snapshot(),
        "stats": stats,
        "ground_event_stats": ground_event_stats,
        "latest": latest,
        "command_safety": command_safety_snapshot(),
        "config": {
            "frequency_hz": CONFIG.frequency_hz,
            "spreading_factor": CONFIG.spreading_factor,
            "sync_word": CONFIG.sync_word,
            "tx_power_dbm": CONFIG.tx_power_dbm,
            "telemetry_ack_turnaround_s": CONFIG.telemetry_ack_turnaround_s,
            "radio_reconnect_max_attempts": CONFIG.radio_reconnect_max_attempts,
            "radio_reconnect_initial_backoff_s": (
                CONFIG.radio_reconnect_initial_backoff_s
            ),
            "radio_reconnect_max_backoff_s": CONFIG.radio_reconnect_max_backoff_s,
            "command_auth_required": bool(CONFIG.operator_token),
            "rf_uplink_auth_configured": rf_auth_is_configured(),
            "command_arm_duration_s": CONFIG.command_arm_duration_s,
            "command_min_interval_s": CONFIG.command_min_interval_s,
            "telemetry_packet_size": TELEMETRY_PACKET_SIZE,
            "csv_enabled": CONFIG.enable_csv,
            "history": CONFIG.history,
            "log_dir": CONFIG.log_dir,
        },
    }


# ============================================================
# Background telemetry receiver
# ============================================================


def close_radio_safely(current_radio: RFM95Radio | None) -> None:
    if current_radio is None:
        return

    try:
        current_radio.close()
    except Exception as exc:
        log_ground_event(
            "radio_close_error",
            severity="warning",
            message=str(exc),
        )


def open_configured_radio() -> tuple[RFM95Radio, int]:
    candidate = RFM95Radio(
        frequency_hz=CONFIG.frequency_hz,
        spreading_factor=CONFIG.spreading_factor,
        sync_word=CONFIG.sync_word,
        tx_power_dbm=CONFIG.tx_power_dbm,
    )

    try:
        candidate.open()
        version = candidate.read_version()

        if version != 0x12:
            raise RuntimeError(
                f"RFM95W version register is 0x{version:02X}, expected 0x12"
            )

        candidate.init_rx()
        return candidate, version
    except Exception:
        close_radio_safely(candidate)
        raise


def wait_for_radio_reconnect(
    operation: str,
    error: Exception,
    consecutive_failures: int,
) -> bool:
    terminal_limit = max(0, int(CONFIG.radio_reconnect_max_attempts))
    terminal = terminal_limit > 0 and consecutive_failures >= terminal_limit

    state.increment("radio_io_failures")
    state.update(
        radio_initialized=False,
        last_error=str(error),
        last_message=f"Radio {operation} failed",
        consecutive_radio_failures=consecutive_failures,
    )
    log_ground_event(
        "radio_io_error",
        severity="critical" if terminal else "warning",
        message=str(error),
        operation=operation,
        consecutive_failures=consecutive_failures,
        terminal_limit=terminal_limit or None,
    )
    schedule_broadcast({
        "type": "status",
        "status": backend_status_payload(),
    })

    if terminal:
        raise RuntimeError(
            "Radio recovery exhausted after "
            f"{consecutive_failures} consecutive I/O failures "
            f"({operation}: {error})"
        ) from error

    initial_delay = max(0.0, float(CONFIG.radio_reconnect_initial_backoff_s))
    maximum_delay = max(
        initial_delay,
        float(CONFIG.radio_reconnect_max_backoff_s),
    )
    exponent = min(max(consecutive_failures - 1, 0), 30)
    retry_delay = min(initial_delay * (2 ** exponent), maximum_delay)

    state.increment("radio_reconnect_attempts")
    state.update(
        last_message=(
            f"Radio unavailable; reopening in {retry_delay:.2f} seconds "
            f"(failure {consecutive_failures}/"
            f"{terminal_limit if terminal_limit else 'unbounded'})"
        ),
    )
    log_ground_event(
        "radio_reconnect_scheduled",
        severity="warning",
        message="Scheduling CH347/RFM95W reopen",
        operation=operation,
        retry_delay_s=retry_delay,
        consecutive_failures=consecutive_failures,
        terminal_limit=terminal_limit or None,
    )

    return not stop_event.wait(retry_delay)


def telemetry_receiver_worker():
    global radio

    consecutive_failures = 0
    recovering = False

    try:
        state.update(
            running=True,
            radio_enabled=True,
            radio_initialized=False,
            last_error=None,
            last_message="Opening CH347/RFM95W radio...",
            consecutive_radio_failures=0,
        )
        log_ground_event(
            "receiver_starting",
            message="Opening CH347/RFM95W ground radio",
        )

        with radio_lock:
            stale_radio = radio
            radio = None
            close_radio_safely(stale_radio)

        while not stop_event.is_set():
            if radio is None:
                state.update(last_message="Opening CH347/RFM95W radio...")

                try:
                    with radio_lock:
                        radio, version = open_configured_radio()
                except Exception as exc:
                    consecutive_failures += 1
                    recovering = True
                    if not wait_for_radio_reconnect(
                        "open/configuration",
                        exc,
                        consecutive_failures,
                    ):
                        break
                    continue

                state.update(
                    radio_initialized=True,
                    last_message="LoRa RX mode active. Waiting for telemetry...",
                )
                log_ground_event(
                    "radio_reinitialized" if recovering else "radio_initialized",
                    message="Ground RFM95W initialized in continuous RX mode",
                    version_register=f"0x{version:02X}",
                )
                schedule_broadcast({
                    "type": "status",
                    "status": backend_status_payload(),
                })

            current_radio = radio
            if current_radio is None:
                continue
            try:
                with radio_lock:
                    packet = current_radio.read_packet_if_available()
            except Exception as exc:
                with radio_lock:
                    if radio is current_radio:
                        radio = None
                    close_radio_safely(current_radio)

                consecutive_failures += 1
                recovering = True
                if not wait_for_radio_reconnect(
                    "packet read",
                    exc,
                    consecutive_failures,
                ):
                    break
                continue

            if recovering:
                state.increment("radio_reconnect_successes")
                log_ground_event(
                    "radio_recovered",
                    message="CH347/RFM95W recovered and RX polling resumed",
                    previous_consecutive_failures=consecutive_failures,
                )
                recovering = False

            if consecutive_failures:
                consecutive_failures = 0
                state.update(
                    consecutive_radio_failures=0,
                    last_error=None,
                    last_message="LoRa RX mode active. Waiting for telemetry...",
                )
                schedule_broadcast({
                    "type": "status",
                    "status": backend_status_payload(),
                })

            if packet is None:
                stop_event.wait(0.02)
                continue

            current_store = get_store()

            if packet["crc_error"]:
                state.increment("lora_crc_errors")
                state.update(last_message="LoRa packet CRC error")
                log_ground_event(
                    "lora_crc_error",
                    severity="warning",
                    message="Ground radio reported a LoRa packet CRC error",
                    lora_rssi_dbm=packet.get("rssi_dbm"),
                    lora_snr_db=packet.get("snr_db"),
                )

                current_store.add_lora_crc_error(
                    lora_rssi_dbm=packet.get("rssi_dbm"),
                    lora_snr_db=packet.get("snr_db"),
                )

                schedule_broadcast({
                    "type": "lora_crc_error",
                    "data": {
                        "rssi_dbm": packet.get("rssi_dbm"),
                        "snr_db": packet.get("snr_db"),
                    },
                    "status": backend_status_payload(),
                })

                continue

            payload = packet["payload"]

            if len(payload) != TELEMETRY_PACKET_SIZE:
                state.increment("non_telemetry_packets")
                state.update(
                    last_message=f"Ignored non-telemetry packet: {len(payload)} bytes"
                )
                log_ground_event(
                    "non_telemetry_packet",
                    severity="warning",
                    message="Ground radio received a packet with an unexpected length",
                    lora_rssi_dbm=packet.get("rssi_dbm"),
                    lora_snr_db=packet.get("snr_db"),
                    packet_length=len(payload),
                    expected_length=TELEMETRY_PACKET_SIZE,
                    payload_hex=payload.hex(" "),
                )

                row = current_store.add_rejected_frame(
                    payload,
                    lora_rssi_dbm=packet.get("rssi_dbm"),
                    lora_snr_db=packet.get("snr_db"),
                    reason="invalid_telemetry_length",
                )

                schedule_broadcast({
                    "type": "invalid_telemetry",
                    "data": row,
                    "status": backend_status_payload(),
                })

                continue

            try:
                telemetry = decode_telemetry_packet(payload)
            except Exception as e:
                state.increment("decode_errors")
                state.update(
                    last_error=f"Telemetry decode error: {e}",
                    last_message="Telemetry decode error",
                )
                log_ground_event(
                    "telemetry_decode_error",
                    severity="critical",
                    message=str(e),
                    lora_rssi_dbm=packet.get("rssi_dbm"),
                    lora_snr_db=packet.get("snr_db"),
                    packet_length=len(payload),
                    payload_hex=payload.hex(" "),
                )

                schedule_broadcast({
                    "type": "decode_error",
                    "error": str(e),
                    "status": backend_status_payload(),
                })

                continue

            row = current_store.add_packet(
                telemetry_packet=telemetry,
                lora_rssi_dbm=packet["rssi_dbm"],
                lora_snr_db=packet["snr_db"],
                raw_payload=payload,
            )

            if not telemetry.validation_ok:
                state.update(
                    last_message=(
                        f"Rejected invalid telemetry sequence {telemetry.sequence_number}"
                    )
                )
                log_ground_event(
                    "invalid_telemetry",
                    severity="critical",
                    message="Decoded frame failed application-level validation",
                    sequence_number=telemetry.sequence_number,
                    lora_rssi_dbm=packet.get("rssi_dbm"),
                    lora_snr_db=packet.get("snr_db"),
                    packet_type_ok=telemetry.packet_type_ok,
                    protocol_version_ok=telemetry.protocol_version_ok,
                    crc_ok=telemetry.crc_ok,
                )
                schedule_broadcast({
                    "type": "invalid_telemetry",
                    "data": row,
                    "status": backend_status_payload(),
                })
                continue

            observe_command_ack(row)
            if row.get("is_duplicate_packet"):
                log_ground_event(
                    "telemetry_duplicate",
                    severity="warning",
                    message=(
                        f"Duplicate telemetry sequence {telemetry.sequence_number}; "
                        "flight likely did not receive the previous ground ACK in time"
                    ),
                    sequence_number=telemetry.sequence_number,
                    consecutive_duplicates=row.get("consecutive_duplicate_packets"),
                    total_duplicates=row.get("total_duplicate_packets"),
                )
            send_automatic_downlink_ack(
                telemetry.boot_count,
                telemetry.sequence_number,
                telemetry.obc_uptime_ms // 1000,
            )

            state.update(
                last_packet_time_unix=time.time(),
                last_error=None,
                last_message=f"Received telemetry sequence {telemetry.sequence_number}",
            )
            log_ground_event(
                "telemetry_received",
                message=f"Received v{telemetry.protocol_version} telemetry sequence {telemetry.sequence_number}",
                sequence_number=telemetry.sequence_number,
                lora_rssi_dbm=packet.get("rssi_dbm"),
                lora_snr_db=packet.get("snr_db"),
                packet_length=len(payload),
                protocol_version=telemetry.protocol_version,
                packet_type=telemetry.packet_type,
                crc_ok=telemetry.crc_ok,
                flight_state=telemetry.flight_state_name,
                lost_packets_since_previous=row.get("lost_packets_since_previous"),
            )

            schedule_broadcast({
                "type": "telemetry",
                "data": row,
                "status": backend_status_payload(),
            })

    except Exception as e:
        state.update(
            running=False,
            radio_initialized=False,
            last_error=str(e),
            last_message="Receiver stopped due to error",
        )
        log_ground_event(
            "receiver_error",
            severity="critical",
            message=str(e),
            traceback=traceback.format_exc(),
        )

        print("Receiver thread error:")
        traceback.print_exc()

        schedule_broadcast({
            "type": "receiver_error",
            "error": str(e),
            "status": backend_status_payload(),
        })

    finally:
        with radio_lock:
            current_radio = radio
            radio = None
            close_radio_safely(current_radio)

        state.update(
            running=False,
            radio_initialized=False,
        )
        log_ground_event(
            "receiver_stopped",
            message="Ground radio receiver thread stopped",
        )


# ============================================================
# FastAPI app lifecycle
# ============================================================

@asynccontextmanager
async def lifespan(app: FastAPI):
    global store
    global ground_events
    global receiver_thread
    global main_loop

    main_loop = asyncio.get_running_loop()

    # Validate all fail-closed startup state before opening log files or
    # starting worker threads, so a rejected startup leaves no live resources.
    store = None
    ground_events = None
    if CONFIG.enable_radio:
        get_rf_auth_key()
    load_command_id_state()
    disarm_commands()

    store = TelemetryStore(
        maxlen=CONFIG.history,
        log_dir=CONFIG.log_dir,
        enable_csv=CONFIG.enable_csv,
    )
    ground_events = GroundEventStore(
        log_dir=CONFIG.log_dir,
        enable_csv=CONFIG.enable_csv,
    )
    log_ground_event(
        "backend_started",
        message="Ground-station backend started",
        host=CONFIG.host,
        port=CONFIG.port,
        radio_enabled=CONFIG.enable_radio,
        telemetry_csv_path=store.csv_path,
        ground_event_csv_path=ground_events.csv_path,
        frequency_hz=CONFIG.frequency_hz,
        spreading_factor=CONFIG.spreading_factor,
        sync_word=CONFIG.sync_word,
        tx_power_dbm=CONFIG.tx_power_dbm,
    )

    stop_event.clear()

    if CONFIG.enable_radio:
        receiver_thread = threading.Thread(
            target=telemetry_receiver_worker,
            daemon=True,
        )
        receiver_thread.start()
    else:
        state.update(
            running=False,
            radio_enabled=False,
            radio_initialized=False,
            last_message="Backend started without radio",
        )
        log_ground_event(
            "radio_disabled",
            message="Backend started without a ground radio",
        )

    yield

    log_ground_event("backend_stopping", message="Ground-station backend stopping")
    stop_event.set()

    if receiver_thread is not None:
        receiver_thread.join(
            timeout=max(3.0, CONFIG.telemetry_ack_turnaround_s + 6.0)
        )

    receiver_stopped = receiver_thread is None or not receiver_thread.is_alive()
    if not receiver_stopped:
        log_ground_event(
            "receiver_shutdown_timeout",
            severity="critical",
            message=(
                "Receiver thread did not stop before the shutdown deadline; "
                "stores remain open to prevent a use-after-close"
            ),
        )
    else:
        if store is not None:
            store.close()

        if ground_events is not None:
            ground_events.close()


app = FastAPI(
    title="CubeSat Ground Station Backend",
    version="1.0.0",
    lifespan=lifespan,
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=CONFIG.cors_origins,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


# ============================================================
# Request models
# ============================================================

class SendAsciiCommandRequest(BaseModel):
    payload: str = Field(..., min_length=1, max_length=36)
    timeout_s: float = Field(default=5.0, ge=0.5, le=30.0)
    ack_timeout_s: float = Field(default=25.0, ge=1.0, le=30.0)
    max_attempts: int = Field(default=3, ge=1, le=5)


class GroundEventRequest(BaseModel):
    event_type: str = Field(..., min_length=1, max_length=80, pattern=r"^[a-z0-9_.-]+$")
    severity: str = Field(default="info", pattern=r"^(info|warning|critical)$")
    message: str = Field(default="", max_length=1000)
    details: dict[str, Any] = Field(default_factory=dict)


class CommandArmRequest(BaseModel):
    confirmation: str = Field(..., max_length=32)


# ============================================================
# HTTP routes
# ============================================================

@app.get("/")
def root():
    return {
        "name": "CubeSat Ground Station Backend",
        "status": "ok",
        "docs": "/docs",
        "websocket": "/ws/telemetry",
    }


@app.get("/api/status")
def api_status():
    return backend_status_payload()


@app.get("/api/command-safety")
def api_command_safety():
    return command_safety_snapshot()


@app.post(
    "/api/command-arm",
    dependencies=[Depends(require_operator_token)],
)
def api_command_arm(request: CommandArmRequest):
    if request.confirmation != "ARM TTC":
        raise HTTPException(
            status_code=422,
            detail='Confirmation must exactly match "ARM TTC"',
        )
    snapshot = arm_commands()
    log_ground_event(
        "command_path_armed",
        severity="warning",
        message="Operator armed the one-shot TTC command path",
        arm_duration_s=CONFIG.command_arm_duration_s,
    )
    return snapshot


@app.delete(
    "/api/command-arm",
    dependencies=[Depends(require_operator_token)],
)
def api_command_disarm():
    snapshot = disarm_commands()
    log_ground_event(
        "command_path_disarmed",
        message="Operator disarmed the TTC command path",
    )
    return snapshot


@app.get("/api/latest")
def api_latest():
    current_store = get_store()
    latest = current_store.get_latest()

    if latest is None:
        return {
            "available": False,
            "data": None,
        }

    return {
        "available": True,
        "data": latest,
    }


@app.get("/api/history")
def api_history(limit: int = 500):
    current_store = get_store()

    if limit < 1:
        limit = 1

    if limit > 5000:
        limit = 5000

    history = current_store.get_history(valid_only=True, limit=limit)

    return {
        "count": min(len(history), limit),
        "data": history[-limit:],
    }


@app.get("/api/stats")
def api_stats():
    current_store = get_store()

    return {
        "receiver": state.snapshot(),
        "stats": current_store.get_stats(),
        "ground_event_stats": ground_events.get_stats() if ground_events is not None else None,
    }


@app.get("/api/uplink-log")
def api_uplink_log(limit: int = 100):
    return {"data": get_uplink_logs(max(1, min(limit, UPLINK_LOG_LIMIT)))}


@app.post("/api/ground-event")
def api_ground_event(request: GroundEventRequest):
    log_ground_event(
        f"dashboard_{request.event_type}",
        severity=request.severity,
        message=request.message,
        source="dashboard",
        dashboard_details=request.details,
    )
    return {"ok": True}


async def command_transaction_guard():
    """Serialize complete command/response transactions, including ACK waits."""
    async with command_transaction_lock:
        yield


@app.post(
    "/api/send-ascii",
    dependencies=[
        Depends(require_operator_token),
        Depends(command_transaction_guard),
    ],
)
async def api_send_ascii(request: SendAsciiCommandRequest):
    """Transmit an ACK or a reliable command envelope over LoRa.

    Commands are assigned a stable 16-bit ID and retried until flight telemetry
    echoes that ID with ACCEPTED/DUPLICATE status. Telemetry ACKs bind the boot,
    sequence, and transmit uptime and intentionally do not require an ACK-of-ACK.
    """

    global radio

    try:
        request.payload.encode("ascii")
    except UnicodeEncodeError as exc:
        raise HTTPException(
            status_code=422,
            detail="Command payload must contain ASCII characters only",
        ) from exc

    if not CONFIG.enable_radio:
        raise HTTPException(status_code=503, detail="Radio is disabled")

    get_radio()
    rf_auth_key = get_rf_auth_key()
    if not state.snapshot()["radio_initialized"]:
        raise HTTPException(status_code=503, detail="Radio is not initialized")

    is_downlink_ack = request.payload.startswith("ACK,")
    if is_downlink_ack:
        parts = request.payload.split(",")
        valid_ack = (
            len(parts) == 4
            and all(part.isdigit() for part in parts[1:])
            and 0 <= int(parts[1]) <= 0xFFFF
            and 0 <= int(parts[2]) <= 0xFFFF
            and 0 <= int(parts[3]) <= 0xFFFFFFFF
        )
        if not valid_ack:
            raise HTTPException(
                status_code=422,
                detail=(
                    "ACK must be ACK,<uint16 boot>,<uint16 sequence>,"
                    "<uint32 tx_uptime_s>"
                ),
            )
        command_id = None
        wire_payload = sign_uplink(request.payload, rf_auth_key)
        pending = None
    else:
        consume_command_arm()
        pending = register_pending_command()
        command_id = pending.command_id
        wire_payload = sign_uplink(
            f"CMD,{command_id},{request.payload}",
            rf_auth_key,
        )
        if len(wire_payload.encode("ascii")) > 64:
            with pending_command_lock:
                pending_command_acks.pop(command_id, None)
            raise HTTPException(status_code=422, detail="Encoded command exceeds flight RX limit")

    def blocking_send(payload: str):
        global radio

        with downlink_tx_lock, radio_lock:
            active_radio = radio
            if active_radio is None:
                raise RuntimeError("Radio is not initialized")
            try:
                ok = active_radio.send_packet(
                    payload,
                    timeout_s=request.timeout_s,
                    cancel_event=stop_event,
                )
                if ok and pending is not None:
                    with pending_command_lock:
                        if pending.first_transmit_time_unix is None:
                            pending.first_transmit_time_unix = time.time()
                if not stop_event.is_set():
                    active_radio.start_rx_continuous()
                return ok
            except Exception:
                if radio is active_radio:
                    radio = None
                close_radio_safely(active_radio)
                raise

    attempts_used = 0
    last_failure = "No flight acknowledgement received"

    if command_id is not None:
        state.update(
            last_command_id=command_id,
            last_command_outcome="pending",
            last_command_attempt=0,
            last_command_time_unix=time.time(),
        )
        await manager.broadcast_json({
            "type": "status",
            "status": backend_status_payload(),
        })

    try:
        for attempt in range(1, request.max_attempts + 1):
            attempts_used = attempt
            try:
                ok = await asyncio.to_thread(blocking_send, wire_payload)
            except Exception as exc:
                state.increment("command_tx_failures")
                state.increment("radio_io_failures")
                state.update(radio_initialized=False)
                last_failure = str(exc)
                record_uplink_log(
                    wire_payload,
                    "error",
                    str(exc),
                    timeout_s=request.timeout_s,
                    command_id=command_id,
                    attempt=attempt,
                )
                log_ground_event(
                    "command_tx_error",
                    severity="warning",
                    message=str(exc),
                    payload=wire_payload,
                    command_id=command_id,
                    attempt=attempt,
                )
                if attempt < request.max_attempts and not stop_event.is_set():
                    recovery_deadline = (
                        asyncio.get_running_loop().time()
                        + max(1.0, CONFIG.radio_reconnect_max_backoff_s + 2.0)
                    )
                    while (
                        asyncio.get_running_loop().time() < recovery_deadline
                        and not stop_event.is_set()
                    ):
                        if radio is not None and state.snapshot()["radio_initialized"]:
                            break
                        await asyncio.sleep(0.1)
                continue

            if not ok:
                state.increment("command_tx_failures")
                if command_id is not None:
                    state.update(
                        last_command_outcome="retrying",
                        last_command_attempt=attempt,
                        last_command_time_unix=time.time(),
                    )
                last_failure = "Ground radio did not report TxDone"
                record_uplink_log(
                    wire_payload,
                    "timeout",
                    last_failure,
                    timeout_s=request.timeout_s,
                    command_id=command_id,
                    attempt=attempt,
                )
                continue

            state.increment("command_tx_count")
            state.update(last_message=f"Transmitted uplink: {wire_payload}")
            if command_id is not None:
                state.update(
                    last_command_outcome="pending",
                    last_command_attempt=attempt,
                    last_command_time_unix=time.time(),
                )
            record_uplink_log(
                wire_payload,
                "radio_sent",
                "Ground radio reported TxDone; waiting for flight acknowledgement"
                if pending is not None else "Ground radio reported TxDone",
                timeout_s=request.timeout_s,
                command_id=command_id,
                attempt=attempt,
            )

            if pending is None:
                now = datetime.now(timezone.utc)
                result = {
                    "ok": True,
                    "acknowledged": False,
                    "outcome": "sent",
                    "payload": request.payload,
                    "wire_payload": wire_payload,
                    "command_id": None,
                    "attempts": attempt,
                    "pc_time_iso": now.isoformat(),
                    "pc_time_unix": now.timestamp(),
                }
                await manager.broadcast_json({
                    "type": "command_tx",
                    "data": result,
                    "status": backend_status_payload(),
                })
                return result

            received = await asyncio.to_thread(pending.event.wait, request.ack_timeout_s)
            if received:
                status = pending.status
                accepted = status in UPLINK_ACCEPTED_STATUSES
                outcome = "acknowledged" if accepted else "rejected"
                message = (
                    f"Flight acknowledged command in telemetry sequence {pending.telemetry_sequence}"
                    if accepted
                    else f"Flight rejected command with status {status}"
                )
                record_uplink_log(
                    wire_payload,
                    outcome,
                    message,
                    timeout_s=request.ack_timeout_s,
                    command_id=command_id,
                    attempt=attempt,
                    telemetry_sequence=pending.telemetry_sequence,
                )
                now = datetime.now(timezone.utc)
                result = {
                    "ok": accepted,
                    "acknowledged": accepted,
                    "outcome": outcome,
                    "payload": request.payload,
                    "wire_payload": wire_payload,
                    "command_id": command_id,
                    "flight_status": status,
                    "telemetry_sequence": pending.telemetry_sequence,
                    "attempts": attempt,
                    "pc_time_iso": now.isoformat(),
                    "pc_time_unix": now.timestamp(),
                }
                state.update(
                    last_command_outcome=outcome,
                    last_command_attempt=attempt,
                    last_command_time_unix=time.time(),
                )
                if accepted:
                    state.update(last_message=f"Flight acknowledged command {command_id}")
                await manager.broadcast_json({
                    "type": "command_tx",
                    "data": result,
                    "status": backend_status_payload(),
                })
                if accepted:
                    log_ground_event(
                        "command_acknowledged",
                        message=message,
                        sequence_number=pending.telemetry_sequence,
                        command_id=command_id,
                        attempt=attempt,
                    )
                    return result
                log_ground_event(
                    "command_rejected",
                    severity="warning",
                    message=message,
                    sequence_number=pending.telemetry_sequence,
                    command_id=command_id,
                    attempt=attempt,
                )
                raise HTTPException(status_code=409, detail=message)

            last_failure = f"No flight acknowledgement within {request.ack_timeout_s:g} s"
            state.update(
                last_command_outcome="retrying",
                last_command_attempt=attempt,
                last_command_time_unix=time.time(),
            )
            await manager.broadcast_json({
                "type": "status",
                "status": backend_status_payload(),
            })
            record_uplink_log(
                wire_payload,
                "retry",
                last_failure,
                timeout_s=request.ack_timeout_s,
                command_id=command_id,
                attempt=attempt,
            )

        state.increment("command_tx_failures")
        state.update(
            last_message=f"Uplink command {command_id} was not acknowledged",
            last_command_outcome="unacknowledged",
            last_command_attempt=attempts_used,
            last_command_time_unix=time.time(),
        )
        record_uplink_log(
            wire_payload,
            "unacknowledged",
            last_failure,
            timeout_s=request.ack_timeout_s,
            command_id=command_id,
            attempt=attempts_used,
        )
        await manager.broadcast_json({
            "type": "status",
            "status": backend_status_payload(),
        })
        log_ground_event(
            "command_unacknowledged",
            severity="critical",
            message=last_failure,
            command_id=command_id,
            attempts=attempts_used,
        )
        raise HTTPException(
            status_code=504,
            detail=f"Command not acknowledged after {attempts_used} attempt(s): {last_failure}",
        )
    finally:
        if command_id is not None:
            with pending_command_lock:
                pending_command_acks.pop(command_id, None)

# ============================================================
# WebSocket route
# ============================================================

@app.websocket("/ws/telemetry")
async def websocket_telemetry(websocket: WebSocket):
    await manager.connect(websocket)
    client = websocket.client
    log_ground_event(
        "dashboard_connected",
        message="Dashboard WebSocket connected",
        client_host=client.host if client else None,
        client_port=client.port if client else None,
        active_connections=len(manager.active_connections),
    )

    try:
        await websocket.send_json({
            "type": "hello",
            "status": backend_status_payload(),
        })

        while True:
            # The frontend sends only keepalive text; receiving it also lets us
            # detect disconnects cleanly.
            await websocket.receive_text()

    except WebSocketDisconnect:
        pass

    except Exception as exc:
        log_ground_event(
            "dashboard_websocket_error",
            severity="warning",
            message=str(exc),
            client_host=client.host if client else None,
        )

    finally:
        await manager.disconnect(websocket)
        log_ground_event(
            "dashboard_disconnected",
            message="Dashboard WebSocket disconnected",
            client_host=client.host if client else None,
            client_port=client.port if client else None,
            active_connections=len(manager.active_connections),
        )


# ============================================================
# CLI
# ============================================================

def run_test_version():
    test_radio = RFM95Radio(
        frequency_hz=CONFIG.frequency_hz,
        spreading_factor=CONFIG.spreading_factor,
        sync_word=CONFIG.sync_word,
        tx_power_dbm=CONFIG.tx_power_dbm,
    )

    try:
        test_radio.open()
        version = test_radio.read_version()

        print(f"RFM95 version register = 0x{version:02X}")

        if version == 0x12:
            print("SPI OK")
        else:
            print("SPI ERROR: expected 0x12")
    finally:
        test_radio.close()


def host_is_loopback(host: str) -> bool:
    normalized = host.strip().lower()
    return normalized in {"127.0.0.1", "::1", "localhost"}


def parse_args():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--test-version",
        action="store_true",
        help="Only read RFM95W version register and exit",
    )

    parser.add_argument(
        "--no-radio",
        action="store_true",
        help="Start backend API without opening the CH347/RFM95W radio",
    )

    parser.add_argument(
        "--no-csv",
        action="store_true",
        help="Disable CSV logging",
    )

    parser.add_argument(
        "--history",
        type=int,
        default=1000,
        help="Number of telemetry rows to keep in memory",
    )

    parser.add_argument(
        "--log-dir",
        type=str,
        default="logs",
        help="Directory for telemetry and ground-event CSV logs",
    )

    parser.add_argument(
        "--host",
        type=str,
        default="127.0.0.1",
        help="Backend host",
    )

    parser.add_argument(
        "--port",
        type=int,
        default=8000,
        help="Backend port",
    )

    parser.add_argument(
        "--radio-reconnect-max-attempts",
        type=int,
        default=CONFIG.radio_reconnect_max_attempts,
        help="Stop after this many consecutive radio failures; 0 retries forever",
    )

    parser.add_argument(
        "--radio-reconnect-initial-backoff-s",
        type=float,
        default=CONFIG.radio_reconnect_initial_backoff_s,
        help="Initial delay before reopening the radio",
    )

    parser.add_argument(
        "--radio-reconnect-max-backoff-s",
        type=float,
        default=CONFIG.radio_reconnect_max_backoff_s,
        help="Maximum delay between radio reopen attempts",
    )

    args = parser.parse_args()

    if args.radio_reconnect_max_attempts < 0:
        parser.error("--radio-reconnect-max-attempts cannot be negative")
    if args.radio_reconnect_initial_backoff_s < 0:
        parser.error("--radio-reconnect-initial-backoff-s cannot be negative")
    if (
        args.radio_reconnect_max_backoff_s
        < args.radio_reconnect_initial_backoff_s
    ):
        parser.error(
            "--radio-reconnect-max-backoff-s must be greater than or equal to "
            "--radio-reconnect-initial-backoff-s"
        )

    return args


def main():
    args = parse_args()

    CONFIG.host = args.host
    CONFIG.port = args.port
    CONFIG.enable_radio = not args.no_radio
    CONFIG.enable_csv = not args.no_csv
    CONFIG.history = args.history
    CONFIG.log_dir = args.log_dir
    CONFIG.radio_reconnect_max_attempts = args.radio_reconnect_max_attempts
    CONFIG.radio_reconnect_initial_backoff_s = (
        args.radio_reconnect_initial_backoff_s
    )
    CONFIG.radio_reconnect_max_backoff_s = args.radio_reconnect_max_backoff_s

    if not host_is_loopback(CONFIG.host) and not CONFIG.operator_token:
        raise SystemExit(
            "Refusing a non-loopback bind without TTC_GROUND_API_TOKEN. "
            "Set a strong operator token in the environment first."
        )
    if CONFIG.enable_radio:
        try:
            parse_uplink_key(CONFIG.rf_auth_key_hex)
            if not rf_auth_is_configured():
                raise ValueError("key is not set")
        except ValueError as exc:
            raise SystemExit(
                "Authenticated radio operation requires TTC_RF_AUTH_KEY_HEX "
                f"(32 hex digits): {exc}"
            ) from exc

    if args.test_version:
        run_test_version()
        return

    print()
    print("Starting CubeSat Ground Station Backend")
    print("--------------------------------------")
    print(f"Backend:     http://{CONFIG.host}:{CONFIG.port}")
    print(f"API docs:    http://{CONFIG.host}:{CONFIG.port}/docs")
    print(f"WebSocket:   ws://{CONFIG.host}:{CONFIG.port}/ws/telemetry")
    print(f"Radio:       {'enabled' if CONFIG.enable_radio else 'disabled'}")
    print(f"RF auth:     {'configured' if rf_auth_is_configured() else 'not configured'}")
    print(f"CSV logging: {'enabled' if CONFIG.enable_csv else 'disabled'}")
    print(f"Log dir:     {CONFIG.log_dir}")
    print()
    print("Press Ctrl+C to stop.")
    print()

    uvicorn.run(
        app,
        host=CONFIG.host,
        port=CONFIG.port,
        log_level="info",
    )


if __name__ == "__main__":
    main()
