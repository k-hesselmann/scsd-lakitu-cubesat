import pytest

from uplink_auth import parse_uplink_key, sign_uplink, siphash24, verify_uplink


TEST_KEY = bytes(range(16))


def test_siphash_reference_vector_for_empty_message():
    assert siphash24(TEST_KEY, b"") == 0x726FDB47DD0E0E31


def test_signed_envelope_verifies_and_tampering_fails():
    envelope = sign_uplink("CMD,7,REQ_TELEMETRY", TEST_KEY)

    assert verify_uplink(envelope, TEST_KEY)
    assert not verify_uplink(envelope.replace("CMD,7", "CMD,8"), TEST_KEY)


def test_key_parser_is_fail_closed():
    assert parse_uplink_key(None) is None
    assert parse_uplink_key("000102030405060708090a0b0c0d0e0f") == TEST_KEY
    with pytest.raises(ValueError):
        parse_uplink_key("abcd")
