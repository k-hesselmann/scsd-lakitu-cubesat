from telemetry_store import TelemetryStore


class InvalidPacket:
    validation_ok = False
    packet_type_ok = False
    protocol_version_ok = True
    crc_ok = False
    sequence_number = 0
    boot_count = 0
    obc_uptime_ms = 0


def test_get_history_limit_returns_newest_valid_rows_in_time_order():
    store = TelemetryStore(maxlen=10, enable_csv=False)
    store.history.extend(
        [
            {"sequence_number": 1, "telemetry_valid": True},
            {"sequence_number": 2, "telemetry_valid": False},
            {"sequence_number": 3, "telemetry_valid": True},
            {"sequence_number": 4, "telemetry_valid": True},
        ]
    )

    rows = store.get_history(valid_only=True, limit=2)

    assert [row["sequence_number"] for row in rows] == [3, 4]


def test_get_history_limit_returns_copies():
    store = TelemetryStore(maxlen=10, enable_csv=False)
    store.history.append({"sequence_number": 1, "telemetry_valid": True})

    rows = store.get_history(limit=1)
    rows[0]["sequence_number"] = 99

    assert store.history[0]["sequence_number"] == 1


def test_backward_packet_is_out_of_order_not_uint16_wrap_loss():
    store = TelemetryStore(maxlen=10, enable_csv=False)

    assert store._calculate_sequence_health(100, 1, 1000) == (0, False, False, False)
    assert store._calculate_sequence_health(90, 1, 1100) == (0, False, False, True)
    assert store.total_lost_packets == 0
    assert store.total_out_of_order_packets == 1
    assert store.previous_sequence_number == 100


def test_uint16_sequence_wrap_remains_in_order():
    store = TelemetryStore(maxlen=10, enable_csv=False)

    store._calculate_sequence_health(0xFFFF, 1, 1000)
    assert store._calculate_sequence_health(0, 1, 1100) == (0, False, False, False)


def test_rejected_length_observation_is_retained():
    store = TelemetryStore(maxlen=10, enable_csv=False)

    row = store.add_rejected_frame(b"bad")

    assert row["telemetry_valid"] is False
    assert row["length_ok"] is False
    assert store.get_stats()["total_length_errors"] == 1
    assert len(store.get_history()) == 1


def test_decodable_invalid_frame_keeps_reason_and_raw_bytes(monkeypatch):
    store = TelemetryStore(maxlen=10, enable_csv=False)
    monkeypatch.setattr(
        store,
        "_packet_to_row",
        lambda **_kwargs: {"telemetry_valid": False},
    )

    row = store.add_packet(InvalidPacket(), raw_payload=b"\x01\x02")

    assert row["rejection_reason"] == "unsupported_packet_type,crc_mismatch"
    assert row["raw_payload_hex"] == "01 02"
