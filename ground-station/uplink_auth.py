"""Authenticated ASCII uplink envelopes shared with the flight TTC code."""

import hmac


MASK64 = (1 << 64) - 1


def parse_uplink_key(value: str | None) -> bytes | None:
    if value is None or not value.strip():
        return None
    try:
        key = bytes.fromhex(value.strip())
    except ValueError as exc:
        raise ValueError("TTC_RF_AUTH_KEY_HEX must contain exactly 32 hex digits") from exc
    if len(key) != 16:
        raise ValueError("TTC_RF_AUTH_KEY_HEX must contain exactly 32 hex digits")
    return key


def _rotate_left(value: int, bits: int) -> int:
    return ((value << bits) | (value >> (64 - bits))) & MASK64


def _round(values: tuple[int, int, int, int]) -> tuple[int, int, int, int]:
    v0, v1, v2, v3 = values
    v0 = (v0 + v1) & MASK64
    v1 = _rotate_left(v1, 13) ^ v0
    v0 = _rotate_left(v0, 32)
    v2 = (v2 + v3) & MASK64
    v3 = _rotate_left(v3, 16) ^ v2
    v0 = (v0 + v3) & MASK64
    v3 = _rotate_left(v3, 21) ^ v0
    v2 = (v2 + v1) & MASK64
    v1 = _rotate_left(v1, 17) ^ v2
    v2 = _rotate_left(v2, 32)
    return v0, v1, v2, v3


def siphash24(key: bytes, message: bytes) -> int:
    if len(key) != 16:
        raise ValueError("SipHash-2-4 requires a 16-byte key")
    k0 = int.from_bytes(key[:8], "little")
    k1 = int.from_bytes(key[8:], "little")
    values = (
        0x736F6D6570736575 ^ k0,
        0x646F72616E646F6D ^ k1,
        0x6C7967656E657261 ^ k0,
        0x7465646279746573 ^ k1,
    )

    offset = 0
    while offset + 8 <= len(message):
        word = int.from_bytes(message[offset:offset + 8], "little")
        v0, v1, v2, v3 = values
        v3 ^= word
        values = _round(_round((v0, v1, v2, v3)))
        v0, v1, v2, v3 = values
        values = (v0 ^ word, v1, v2, v3)
        offset += 8

    final = len(message) << 56
    for index, byte in enumerate(message[offset:]):
        final |= byte << (8 * index)
    v0, v1, v2, v3 = values
    v3 ^= final
    values = _round(_round((v0, v1, v2, v3)))
    v0, v1, v2, v3 = values
    v0 ^= final
    v2 ^= 0xFF
    values = (v0, v1, v2, v3)
    for _ in range(4):
        values = _round(values)
    return values[0] ^ values[1] ^ values[2] ^ values[3]


def sign_uplink(payload: str, key: bytes) -> str:
    encoded = payload.encode("ascii")
    return payload + "," + format(siphash24(key, encoded), "016x")


def verify_uplink(envelope: str, key: bytes) -> bool:
    payload, separator, tag = envelope.rpartition(",")
    if separator != "," or len(tag) != 16:
        return False
    expected = sign_uplink(payload, key).rsplit(",", 1)[1]
    return hmac.compare_digest(tag.lower(), expected)
