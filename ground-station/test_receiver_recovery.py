import backend_server


class FakeRadio:
    def __init__(self, *, fail_read=False, stop_after_read=False):
        self.fail_read = fail_read
        self.stop_after_read = stop_after_read
        self.closed = False
        self.read_count = 0

    def open(self):
        pass

    def read_version(self):
        return 0x12

    def init_rx(self):
        pass

    def read_packet_if_available(self):
        self.read_count += 1
        if self.fail_read:
            raise RuntimeError("transient CH347 SPI failure")
        if self.stop_after_read:
            backend_server.stop_event.set()
        return None

    def close(self):
        self.closed = True


def configure_worker_test(monkeypatch, radio_factory, events, max_attempts):
    monkeypatch.setattr(backend_server, "state", backend_server.ReceiverState())
    monkeypatch.setattr(backend_server, "radio", None)
    monkeypatch.setattr(backend_server, "stop_event", backend_server.threading.Event())
    monkeypatch.setattr(backend_server, "RFM95Radio", radio_factory)
    monkeypatch.setattr(backend_server.CONFIG, "radio_reconnect_max_attempts", max_attempts)
    monkeypatch.setattr(backend_server.CONFIG, "radio_reconnect_initial_backoff_s", 0.0)
    monkeypatch.setattr(backend_server.CONFIG, "radio_reconnect_max_backoff_s", 0.0)
    monkeypatch.setattr(backend_server.time, "sleep", lambda _delay: None)
    monkeypatch.setattr(backend_server, "schedule_broadcast", lambda _message: None)
    monkeypatch.setattr(
        backend_server,
        "log_ground_event",
        lambda event_type, *args, **kwargs: events.append(event_type),
    )


def test_transient_spi_failure_reopens_radio_and_resumes_polling(monkeypatch):
    instances = []
    behaviors = [
        {"fail_read": True},
        {"stop_after_read": True},
    ]
    events = []

    def radio_factory(**_kwargs):
        instance = FakeRadio(**behaviors.pop(0))
        instances.append(instance)
        return instance

    configure_worker_test(monkeypatch, radio_factory, events, max_attempts=3)

    backend_server.telemetry_receiver_worker()

    snapshot = backend_server.state.snapshot()
    assert len(instances) == 2
    assert all(instance.closed for instance in instances)
    assert snapshot["running"] is False
    assert snapshot["radio_initialized"] is False
    assert snapshot["radio_io_failures"] == 1
    assert snapshot["radio_reconnect_attempts"] == 1
    assert snapshot["radio_reconnect_successes"] == 1
    assert snapshot["consecutive_radio_failures"] == 0
    assert snapshot["last_error"] is None
    assert "radio_recovered" in events
    assert "receiver_error" not in events


def test_persistent_spi_failure_stops_at_configured_limit(monkeypatch):
    instances = []
    events = []

    def radio_factory(**_kwargs):
        instance = FakeRadio(fail_read=True)
        instances.append(instance)
        return instance

    configure_worker_test(monkeypatch, radio_factory, events, max_attempts=2)
    monkeypatch.setattr(backend_server.traceback, "print_exc", lambda: None)

    backend_server.telemetry_receiver_worker()

    snapshot = backend_server.state.snapshot()
    assert len(instances) == 2
    assert all(instance.closed for instance in instances)
    assert snapshot["running"] is False
    assert snapshot["radio_initialized"] is False
    assert snapshot["radio_io_failures"] == 2
    assert snapshot["radio_reconnect_attempts"] == 1
    assert snapshot["radio_reconnect_successes"] == 0
    assert snapshot["consecutive_radio_failures"] == 2
    assert snapshot["last_message"] == "Receiver stopped due to error"
    assert "Radio recovery exhausted after 2 consecutive I/O failures" in snapshot["last_error"]
    assert "receiver_error" in events

