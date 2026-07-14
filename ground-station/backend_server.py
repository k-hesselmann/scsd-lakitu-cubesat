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
from fastapi import FastAPI, WebSocket, WebSocketDisconnect, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field

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
radio: RFM95Radio | None = None
radio_lock = threading.Lock()

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

    stats = None
    latest = None

    if current_store is not None:
        stats = current_store.get_stats()
        latest = current_store.get_latest()

    return {
        "receiver": state.snapshot(),
        "stats": stats,
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

            state.update(
                last_packet_time_unix=time.time(),
                last_error=None,
                last_message=f"Received telemetry sequence {telemetry.sequence_number}",
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

        print("Receiver thread error:")
        traceback.print_exc()

        schedule_broadcast({
            "type": "receiver_error",
            "error": str(e),
            "status": backend_status_payload(),
        })

    finally:
        state.update(running=False)


# ============================================================
# FastAPI app lifecycle
# ============================================================

@asynccontextmanager
async def lifespan(app: FastAPI):
    global store
    global receiver_thread
    global main_loop

    main_loop = asyncio.get_running_loop()

    store = TelemetryStore(
        maxlen=CONFIG.history,
        log_dir=CONFIG.log_dir,
        enable_csv=CONFIG.enable_csv,
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

    yield

    stop_event.set()

    if receiver_thread is not None:
        receiver_thread.join(timeout=2.0)

    if store is not None:
        store.close()


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
    payload: str = Field(..., min_length=1, max_length=255)
    timeout_s: float = Field(default=5.0, ge=0.5, le=30.0)


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

    history = current_store.get_history()

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
    }


@app.post("/api/send-ascii")
async def api_send_ascii(request: SendAsciiCommandRequest):
    """
    Optional command/uplink endpoint.

    This sends one ASCII LoRa packet from the ground station and then
    returns the radio to continuous RX mode.

    Example payload:
        PING_GROUND_0001
    """

    if not CONFIG.enable_radio:
        raise HTTPException(status_code=503, detail="Radio is disabled")

    current_radio = get_radio()

    if not state.snapshot()["radio_initialized"]:
        raise HTTPException(status_code=503, detail="Radio is not initialized")

    def blocking_send():
        with radio_lock:
            ok = current_radio.send_packet(
                request.payload,
                timeout_s=request.timeout_s,
            )

            current_radio.start_rx_continuous()

            return ok

    ok = await asyncio.to_thread(blocking_send)

    if ok:
        state.increment("command_tx_count")
        state.update(last_message=f"Sent command: {request.payload}")

        now = datetime.now(timezone.utc)

        result = {
            "ok": True,
            "payload": request.payload,
            "pc_time_iso": now.isoformat(),
            "pc_time_unix": now.timestamp(),
        }

        await manager.broadcast_json({
            "type": "command_tx",
            "data": result,
            "status": backend_status_payload(),
        })

        return result

    state.increment("command_tx_failures")
    state.update(last_message=f"Command TX failed: {request.payload}")

    raise HTTPException(status_code=500, detail="Command TX timeout/error")


# ============================================================
# WebSocket route
# ============================================================

@app.websocket("/ws/telemetry")
async def websocket_telemetry(websocket: WebSocket):
    await manager.connect(websocket)

    try:
        await websocket.send_json({
            "type": "hello",
            "data": backend_status_payload(),
        })

        while True:
            # The frontend does not need to send anything, but keeping this
            # receive loop open lets us detect disconnects cleanly.
            await websocket.receive_text()

    except WebSocketDisconnect:
        await manager.disconnect(websocket)

    except Exception:
        await manager.disconnect(websocket)


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
        help="Directory for CSV telemetry logs",
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