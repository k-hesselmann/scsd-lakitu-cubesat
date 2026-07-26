from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_gps_initialization_is_cooperative() -> None:
    source = (ROOT / "Core/Src/cdh/m10s.c").read_text(encoding="utf-8")

    assert "void M10S_InitService" in source
    assert "M10S_INIT_WAIT_RESET" in source
    assert "HAL_Delay" not in source


def test_flight_software_requires_a_3d_fix() -> None:
    source = (ROOT / "Core/Src/fsw/fsm.c").read_text(encoding="utf-8")

    assert "dp->gps_fix_type == M10S_FIX_3D" in source


def test_fdir_tracks_no_fix_separately_from_transport() -> None:
    source = (ROOT / "Core/Src/fdir/fdir.c").read_text(encoding="utf-8")
    header = (ROOT / "Core/Inc/fdir/fdir.h").read_text(encoding="utf-8")

    assert "FDIR_RunGpsNoFixMonitor" in source
    assert "FDIR_GPS_NO_FIX_REINIT_MS" in header
    assert "M10S_FIX_NONE" in source


def test_parser_accepts_one_complete_nav_pvt_frame() -> None:
    source = (ROOT / "Core/Src/cdh/m10s.c").read_text(encoding="utf-8")

    assert "i + 100U <= s_rx_index" in source
