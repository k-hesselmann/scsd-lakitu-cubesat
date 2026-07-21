from telemetry_store import TelemetryStore


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
