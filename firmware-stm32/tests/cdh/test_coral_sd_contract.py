from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CORAL = (ROOT / "Core" / "Src" / "cdh" / "coral.c").read_text(encoding="utf-8")
SD_LOGGER = (ROOT / "Core" / "Src" / "sd_logger.c").read_text(encoding="utf-8")
CORAL_HEADER = (ROOT / "Core" / "Inc" / "cdh" / "coral.h").read_text(encoding="utf-8")


def test_remount_notifies_coral_before_unmounting() -> None:
    hook = "Coral_OnFilesystemUnmount();"
    assert hook in SD_LOGGER
    assert SD_LOGGER.index(hook) < SD_LOGGER.index("(void)f_mount(NULL, USERPath, 0U);")
    assert "void Coral_OnFilesystemUnmount(void);" in CORAL_HEADER
    assert "s_preopen_ok = 0U;" in CORAL


def test_frame_names_are_collision_safe() -> None:
    assert "F%08lu_C%03u_G%05lu.RAW" in CORAL
    assert "FA_CREATE_NEW | FA_WRITE" in CORAL
    assert "while (result == FR_EXIST);" in CORAL
    assert "coral_rename_staged_file" in CORAL


def test_preopen_does_not_precede_queued_rx_data() -> None:
    idle = CORAL[CORAL.index("case CORAL_RX_IDLE:"):CORAL.index("case CORAL_RX_SOF1:")]
    assert idle.index("coral_rx_ring_pop") < idle.index("coral_try_preopen")
    assert "CORAL_PREOPEN_QUIET_MS" in idle


if __name__ == "__main__":
    test_remount_notifies_coral_before_unmounting()
    test_frame_names_are_collision_safe()
    test_preopen_does_not_precede_queued_rx_data()
    print("Coral SD source-contract checks passed")
