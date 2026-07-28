from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_gps_initialization_is_cooperative() -> None:
    source = (ROOT / "Core/Src/cdh/m10s.c").read_text(encoding="utf-8")

    assert "void M10S_InitService" in source
    assert "M10S_INIT_WAIT_RESET" in source
    assert "HAL_Delay" not in source


def test_gnss_uses_the_typical_airborne_two_g_dynamic_model() -> None:
    source = (ROOT / "Core/Src/cdh/m10s.c").read_text(encoding="utf-8")

    assert "static const uint8_t config_values[] = { 1U, 0U, 1U, 7U };" in source


def test_flight_software_requires_a_3d_fix() -> None:
    source = (ROOT / "Core/Src/fsw/fsm.c").read_text(encoding="utf-8")

    assert "dp->gps_fix_type == M10S_FIX_3D" in source


def test_fdir_recovers_after_a_sustained_sub_3d_fix() -> None:
    source = (ROOT / "Core/Src/fdir/fdir.c").read_text(encoding="utf-8")
    header = (ROOT / "Core/Inc/fdir/fdir.h").read_text(encoding="utf-8")

    assert "FDIR_RunGpsNoFixMonitor" in source
    assert "FDIR_GPS_NO_FIX_REINIT_MS" in header
    assert "dp->gps_fix_type >= M10S_FIX_3D" in source


def test_boot_does_not_run_the_pre_initialization_gps_diagnostic() -> None:
    source = (ROOT / "Core/Src/main.c").read_text(encoding="utf-8")

    assert "GPS_Diag_Test(&hi2c1);" not in source


def test_parser_accepts_one_complete_nav_pvt_frame() -> None:
    source = (ROOT / "Core/Src/cdh/m10s.c").read_text(encoding="utf-8")

    assert "i + 100U <= s_rx_index" in source


def test_gnss_diagnostics_follow_the_project_3d_fix_rule() -> None:
    source = (ROOT / "Core/Src/cdh/m10s.c").read_text(encoding="utf-8")
    header = (ROOT / "Core/Inc/cdh/m10s.h").read_text(encoding="utf-8")
    observability = (ROOT / "Core/Src/observability.c").read_text(encoding="utf-8")

    assert "last_3d_fix_ms" in header
    assert "if (fixType == M10S_FIX_3D)" in source
    assert "gnssFixOK" not in source
    assert "flags & 0x01U" not in source
    assert "gps_3d_age=" in observability
