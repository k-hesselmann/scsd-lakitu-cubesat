from pathlib import Path


FIRMWARE_ROOT = Path(__file__).resolve().parents[2]
TELEMETRY_SOURCE = (
    FIRMWARE_ROOT / "Core" / "Src" / "ttc" / "ttc_telemetry.c"
).read_text(encoding="utf-8")


def test_gnss_validity_bit_requires_a_fresh_3d_fix():
    assert "gps_transport_fresh = dp->gps_valid ? 1U : 0U;" in TELEMETRY_SOURCE
    assert "dp->gps_fix_type == M10S_FIX_3D" in TELEMETRY_SOURCE
    assert "if (gps_fix_usable)" in TELEMETRY_SOURCE
    assert "pkt->validity_flags |= TELEMETRY_VALID_GPS;" in TELEMETRY_SOURCE


def test_no_fix_still_downlinks_fix_diagnostics():
    assert "if (gps_transport_fresh && !gps_fix_usable)" in TELEMETRY_SOURCE
    assert "pkt->gnss_satellites = dp->gps_num_satellites;" in TELEMETRY_SOURCE
    assert "pkt->gnss_fix_type = dp->gps_fix_type;" in TELEMETRY_SOURCE
