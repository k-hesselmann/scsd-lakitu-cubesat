import backend_server


class FakeRadio:
    def __init__(self, events):
        self.events = events

    def send_packet(self, payload, timeout_s):
        self.events.append(("send", payload, timeout_s))
        return True

    def start_rx_continuous(self):
        self.events.append(("rx",))


def test_automatic_ack_waits_for_flight_turnaround(monkeypatch):
    events = []
    fake_radio = FakeRadio(events)

    monkeypatch.setattr(backend_server, "radio", fake_radio)
    monkeypatch.setattr(backend_server.CONFIG, "telemetry_ack_turnaround_s", 0.75)
    monkeypatch.setattr(
        backend_server.time,
        "sleep",
        lambda delay: events.append(("sleep", delay)),
    )
    monkeypatch.setattr(backend_server, "record_uplink_log", lambda *args, **kwargs: None)
    monkeypatch.setattr(backend_server, "log_ground_event", lambda *args, **kwargs: None)

    backend_server.send_automatic_downlink_ack(42)

    assert events == [
        ("sleep", 0.75),
        ("send", "ACK,42", 5.0),
        ("rx",),
    ]
    snapshot = backend_server.state.snapshot()
    assert snapshot["last_telemetry_ack_sequence"] == 42
    assert snapshot["last_telemetry_ack_ok"] is True
