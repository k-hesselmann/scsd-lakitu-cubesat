import backend_server

TEST_KEY_HEX = "000102030405060708090a0b0c0d0e0f"

class FakeRadio:
    def __init__(self, events):
        self.events = events

    def send_packet(self, payload, timeout_s, cancel_event=None):
        assert cancel_event is backend_server.stop_event
        self.events.append(("send", payload, timeout_s))
        return True

    def start_rx_continuous(self):
        self.events.append(("rx",))


def test_automatic_ack_waits_for_flight_turnaround(monkeypatch):
    events = []
    fake_radio = FakeRadio(events)

    monkeypatch.setattr(backend_server, "radio", fake_radio)
    monkeypatch.setattr(backend_server.CONFIG, "rf_auth_key_hex", TEST_KEY_HEX)
    assert backend_server.CONFIG.telemetry_ack_turnaround_s == 3.0
    class RecordingStopEvent:
        def wait(self, delay):
            events.append(("wait", delay))
            return False

        def is_set(self):
            return False

    monkeypatch.setattr(backend_server, "stop_event", RecordingStopEvent())
    monkeypatch.setattr(backend_server, "record_uplink_log", lambda *args, **kwargs: None)
    monkeypatch.setattr(backend_server, "log_ground_event", lambda *args, **kwargs: None)

    backend_server.send_automatic_downlink_ack(3, 42, 99)

    assert events == [
        ("wait", 3.0),
        (
            "send",
            backend_server.sign_uplink(
                "ACK,3,42,99",
                bytes.fromhex(TEST_KEY_HEX),
            ),
            5.0,
        ),
        ("rx",),
    ]
    snapshot = backend_server.state.snapshot()
    assert snapshot["last_telemetry_ack_sequence"] == 42
    assert snapshot["last_telemetry_ack_ok"] is True


def test_automatic_ack_is_cancelled_during_turnaround(monkeypatch):
    events = []
    fake_radio = FakeRadio(events)

    class StoppedEvent:
        def wait(self, _delay):
            return True

        def is_set(self):
            return True

    monkeypatch.setattr(backend_server, "radio", fake_radio)
    monkeypatch.setattr(backend_server.CONFIG, "rf_auth_key_hex", TEST_KEY_HEX)
    monkeypatch.setattr(backend_server, "stop_event", StoppedEvent())

    backend_server.send_automatic_downlink_ack(3, 43, 100)

    assert events == []
