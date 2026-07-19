# backend_server.py

import argparse
import asyncio
import threading
import time
import traceback
from contextlib import asynccontextmanager
from typing import Any
from datetime import datetime, timezone

import uvicorn
from fastapi import Depends, FastAPI, WebSocket, WebSocketDisconnect, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field

from ground_event_store import GroundEventStore
from lora_radio import RFM95Radio
from telemetry_decoder import (
    TELEMETRY_PACKET_SIZE,
    decode_telemetry_packet,
)
from telemetry_store import TelemetryStore


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

    frequency_hz = 868000000
    spreading_factor = 9
    sync_word = 0x12
    tx_power_dbm = 10

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
            dead_connections = []

            for websocket in self.active_connections:
                try:
                    await websocket.send_json(message)
                except Exception:
                    dead_connections.append(websocket)

            for websocket in dead_connections:
                if websocket in self.active_connections:
                    self.active_connections.remove(websocket)


state = ReceiverState()
manager = ConnectionManager()

store: TelemetryStore | None = None
ground_events: GroundEventStore | None = None
radio: RFM95Radio | None = None
radio_lock = threading.Lock()
command_transaction_lock = asyncio.Lock()
uplink_log_lock = threading.Lock()
uplink_logs: list[dict[str, Any]] = []
UPLINK_LOG_LIMIT = 200
UPLINK_STATUS_ACCEPTED = 1
UPLINK_STATUS_DUPLICATE = 4
UPLINK_ACCEPTED_STATUSES = {UPLINK_STATUS_ACCEPTED, UPLINK_STATUS_DUPLICATE}
pending_command_lock = threading.Lock()
pending_command_acks: dict[int, "PendingCommandAck"] = {}
next_command_id = 1

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
    def __init__(self, command_id: int):
        self.command_id = command_id
        self.event = threading.Event()
        self.status: int | None = None
        self.telemetry_sequence: int | None = None


def register_pending_command() -> PendingCommandAck:
    global next_command_id

    with pending_command_lock:
        for _ in range(0xFFFF):
            command_id = next_command_id
            next_command_id = 1 if next_command_id >= 0xFFFF else next_command_id + 1
            if command_id not in pending_command_acks:
                pending = PendingCommandAck(command_id)
                pending_command_acks[command_id] = pending
                return pending

    raise HTTPException(status_code=503, detail="No uplink command IDs available")


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
        pending.status = status
        sequence = row.get("sequence_number")
        pending.telemetry_sequence = sequence if isinstance(sequence, int) else None
        pending.event.set()


def send_automatic_downlink_ack(sequence_number: int) -> None:
    current_radio = radio
    if current_radio is None:
        return

    payload = f"ACK,{sequence_number}"
    try:
        with radio_lock:
            try:
                ok = current_radio.send_packet(payload, timeout_s=5.0)
            finally:
                current_radio.start_rx_continuous()
    except Exception as exc:
        state.increment("telemetry_ack_tx_failures")
        state.update(
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
        asyncio.run_coroutine_threadsafe(
            manager.broadcast_json(message),
            main_loop,
        )
    except Exception:
        pass


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
        "config": {
            "frequency_hz": CONFIG.frequency_hz,
            "spreading_factor": CONFIG.spreading_factor,
            "sync_word": CONFIG.sync_word,
            "tx_power_dbm": CONFIG.tx_power_dbm,
            "telemetry_packet_size": TELEMETRY_PACKET_SIZE,
            "csv_enabled": CONFIG.enable_csv,
            "history": CONFIG.history,
            "log_dir": CONFIG.log_dir,
        },
    }


# ============================================================
# Background telemetry receiver
# ============================================================

def telemetry_receiver_worker():
    global radio

    try:
        state.update(
            running=True,
            radio_enabled=True,
            radio_initialized=False,
            last_error=None,
            last_message="Opening CH347/RFM95W radio...",
        )
        log_ground_event(
            "receiver_starting",
            message="Opening CH347/RFM95W ground radio",
        )

        radio = RFM95Radio(
            frequency_hz=CONFIG.frequency_hz,
            spreading_factor=CONFIG.spreading_factor,
            sync_word=CONFIG.sync_word,
            tx_power_dbm=CONFIG.tx_power_dbm,
        )

        with radio_lock:
            radio.open()

            version = radio.read_version()

            if version != 0x12:
                raise RuntimeError(
                    f"RFM95W version register is 0x{version:02X}, expected 0x12"
                )

            state.update(last_message="Initializing LoRa RX mode...")

            radio.init_rx()

        state.update(
            radio_initialized=True,
            last_message="LoRa RX mode active. Waiting for telemetry...",
        )
        log_ground_event(
            "radio_initialized",
            message="Ground RFM95W initialized in continuous RX mode",
            version_register=f"0x{version:02X}",
        )

        schedule_broadcast({
            "type": "status",
            "data": backend_status_payload(),
        })

        while not stop_event.is_set():
            with radio_lock:
                packet = radio.read_packet_if_available()

            if packet is None:
                time.sleep(0.02)
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

                schedule_broadcast({
                    "type": "non_telemetry_packet",
                    "data": {
                        "length": len(payload),
                    },
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
            send_automatic_downlink_ack(telemetry.sequence_number)

            state.update(
                last_packet_time_unix=time.time(),
                last_error=None,
                last_message=f"Received telemetry sequence {telemetry.sequence_number}",
            )
            log_ground_event(
                "telemetry_received",
                message=f"Received raw-v7 telemetry sequence {telemetry.sequence_number}",
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
        state.update(running=False)
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
        receiver_thread.join(timeout=2.0)

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
    payload: str = Field(..., min_length=1, max_length=48)
    timeout_s: float = Field(default=5.0, ge=0.5, le=30.0)
    ack_timeout_s: float = Field(default=5.0, ge=1.0, le=30.0)
    max_attempts: int = Field(default=3, ge=1, le=5)


class GroundEventRequest(BaseModel):
    event_type: str = Field(..., min_length=1, max_length=80, pattern=r"^[a-z0-9_.-]+$")
    severity: str = Field(default="info", pattern=r"^(info|warning|critical)$")
    message: str = Field(default="", max_length=1000)
    details: dict[str, Any] = Field(default_factory=dict)


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

    history = current_store.get_history(valid_only=True)

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


@app.post("/api/send-ascii", dependencies=[Depends(command_transaction_guard)])
async def api_send_ascii(request: SendAsciiCommandRequest):
    """Transmit an ACK or a reliable command envelope over LoRa.

    Commands are assigned a stable 16-bit ID and retried until flight telemetry
    echoes that ID with ACCEPTED/DUPLICATE status. ACK,<sequence> packets are
    telemetry acknowledgements and intentionally do not require an ACK-of-ACK.
    """

    try:
        request.payload.encode("ascii")
    except UnicodeEncodeError as exc:
        raise HTTPException(
            status_code=422,
            detail="Command payload must contain ASCII characters only",
        ) from exc

    if not CONFIG.enable_radio:
        raise HTTPException(status_code=503, detail="Radio is disabled")

    current_radio = get_radio()
    if not state.snapshot()["radio_initialized"]:
        raise HTTPException(status_code=503, detail="Radio is not initialized")

    is_downlink_ack = request.payload.startswith("ACK,")
    if is_downlink_ack:
        parts = request.payload.split(",", 1)
        if len(parts) != 2 or not parts[1].isdigit() or not 0 <= int(parts[1]) <= 0xFFFF:
            raise HTTPException(status_code=422, detail="ACK must be ACK,<uint16 sequence>")
        command_id = None
        wire_payload = request.payload
        pending = None
    else:
        pending = register_pending_command()
        command_id = pending.command_id
        wire_payload = f"CMD,{command_id},{request.payload}"
        if len(wire_payload.encode("ascii")) > 64:
            with pending_command_lock:
                pending_command_acks.pop(command_id, None)
            raise HTTPException(status_code=422, detail="Encoded command exceeds flight RX limit")

    def blocking_send(payload: str):
        with radio_lock:
            try:
                return current_radio.send_packet(payload, timeout_s=request.timeout_s)
            finally:
                current_radio.start_rx_continuous()

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
            "data": backend_status_payload(),
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

    test_radio.open()

    version = test_radio.read_version()

    print(f"RFM95 version register = 0x{version:02X}")

    if version == 0x12:
        print("SPI OK")
    else:
        print("SPI ERROR: expected 0x12")


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

    return parser.parse_args()


def main():
    args = parse_args()

    CONFIG.host = args.host
    CONFIG.port = args.port
    CONFIG.enable_radio = not args.no_radio
    CONFIG.enable_csv = not args.no_csv
    CONFIG.history = args.history
    CONFIG.log_dir = args.log_dir

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
