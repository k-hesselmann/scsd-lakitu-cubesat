import time

import pytest
from fastapi import HTTPException

import backend_server


def test_command_id_survives_backend_restart(tmp_path, monkeypatch):
    monkeypatch.setattr(backend_server.CONFIG, "log_dir", str(tmp_path))
    monkeypatch.setattr(backend_server, "store", None)
    monkeypatch.setattr(backend_server, "next_command_id", 41)
    monkeypatch.setattr(backend_server, "pending_command_acks", {})

    pending = backend_server.register_pending_command()
    assert pending.command_id == 41
    assert backend_server.next_command_id == 42

    backend_server.next_command_id = 1
    backend_server.load_command_id_state()
    assert backend_server.next_command_id == 42


def test_corrupt_command_state_fails_closed(tmp_path, monkeypatch):
    monkeypatch.setattr(backend_server.CONFIG, "log_dir", str(tmp_path))
    (tmp_path / backend_server.CONFIG.command_state_filename).write_text(
        "not-json",
        encoding="utf-8",
    )

    with pytest.raises(RuntimeError, match="Refusing to reuse command IDs"):
        backend_server.load_command_id_state()


def test_command_id_space_never_wraps_under_one_rf_key(tmp_path, monkeypatch):
    monkeypatch.setattr(backend_server.CONFIG, "log_dir", str(tmp_path))
    monkeypatch.setattr(backend_server, "store", None)
    monkeypatch.setattr(backend_server, "next_command_id", 0xFFFF)
    monkeypatch.setattr(backend_server, "pending_command_acks", {})

    pending = backend_server.register_pending_command()
    assert pending.command_id == 0xFFFF
    assert backend_server.next_command_id == 0x10000

    backend_server.pending_command_acks.clear()
    with pytest.raises(HTTPException, match="command-ID space is exhausted"):
        backend_server.register_pending_command()
    with pytest.raises(HTTPException, match="Command-ID space exhausted"):
        backend_server.arm_commands()

    backend_server.next_command_id = 1
    backend_server.load_command_id_state()
    assert backend_server.next_command_id == 0x10000


def test_command_ack_must_be_newer_than_registration_baseline(monkeypatch):
    pending = backend_server.PendingCommandAck(7, baseline_sequence=100)
    pending.first_transmit_time_unix = time.time() - 1.0
    monkeypatch.setattr(backend_server, "pending_command_acks", {7: pending})

    backend_server.observe_command_ack({
        "telemetry_valid": True,
        "uplink_last_command_id": 7,
        "uplink_last_status": backend_server.UPLINK_STATUS_ACCEPTED,
        "sequence_number": 100,
        "pc_receive_time_unix": time.time(),
    })
    assert pending.event.is_set() is False

    backend_server.observe_command_ack({
        "telemetry_valid": True,
        "uplink_last_command_id": 7,
        "uplink_last_status": backend_server.UPLINK_STATUS_ACCEPTED,
        "sequence_number": 101,
        "pc_receive_time_unix": time.time(),
    })
    assert pending.event.is_set() is True
    assert pending.telemetry_sequence == 101


def test_command_ack_requires_ground_receive_timestamp(monkeypatch):
    pending = backend_server.PendingCommandAck(8, baseline_sequence=None)
    pending.first_transmit_time_unix = time.time() - 1.0
    monkeypatch.setattr(backend_server, "pending_command_acks", {8: pending})

    backend_server.observe_command_ack({
        "telemetry_valid": True,
        "uplink_last_command_id": 8,
        "uplink_last_status": backend_server.UPLINK_STATUS_ACCEPTED,
        "sequence_number": 101,
    })

    assert pending.event.is_set() is False


def test_command_arm_is_one_shot(monkeypatch):
    monkeypatch.setattr(backend_server.CONFIG, "command_arm_duration_s", 60.0)
    monkeypatch.setattr(backend_server.CONFIG, "command_min_interval_s", 0.0)
    monkeypatch.setattr(backend_server, "last_manual_command_monotonic", 0.0)
    monkeypatch.setattr(backend_server, "next_command_id", 1)
    backend_server.disarm_commands()

    with pytest.raises(HTTPException) as error:
        backend_server.consume_command_arm()
    assert error.value.status_code == 423

    assert backend_server.arm_commands()["armed"] is True
    backend_server.consume_command_arm()
    assert backend_server.command_safety_snapshot()["armed"] is False

    with pytest.raises(HTTPException) as error:
        backend_server.consume_command_arm()
    assert error.value.status_code == 423


def test_only_explicit_loopback_hosts_are_accepted():
    assert backend_server.host_is_loopback("127.0.0.1")
    assert backend_server.host_is_loopback("localhost")
    assert backend_server.host_is_loopback("::1")
    assert not backend_server.host_is_loopback("0.0.0.0")
