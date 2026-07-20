"""TTC source-contract checks runnable without target hardware.

These checks complement on-target HAL-mock tests: they protect the architectural
properties that must remain true even when the STM32 build environment is absent.
"""
from pathlib import Path

FIRMWARE = Path(__file__).resolve().parents[2]
TTC_INC = FIRMWARE / "Core" / "Inc" / "ttc"
TTC_SRC = FIRMWARE / "Core" / "Src" / "ttc"


def read(name: str) -> str:
    return (TTC_SRC / name).read_text(encoding="utf-8")


def test_driver_has_no_blocking_delay_or_tx_done_wait_loop() -> None:
    source = read("lora_driver.c")
    assert "HAL_Delay" not in source
    assert "HAL_SPI_Transmit(" not in source
    assert "HAL_SPI_Receive(" not in source
    assert "HAL_SPI_TransmitReceive(&" not in source
    assert "HAL_SPI_TransmitReceive_IT" in source
    assert "SPI1_IRQHandler" in source
    assert "HAL_SPI_TxRxCpltCallback" in source
    assert "LORA_STATE_TRANSMITTING" in source
    assert "DRIVER_TX_POLL" in source


def test_ttc_has_no_scv_or_autonomous_recovery_policy() -> None:
    source = read("ttc.c")
    assert "g_scv" not in source
    # TTC now consumes the FDIR-owned EQUIPMENT_LORA request bit (fire-and-forget
    # poll + ack in TTC_Service()), but FDIR alone still decides thresholds and
    # owns the fault bit -- TTC must never call FDIR_SetEquipmentFault itself.
    assert "FDIR_SetEquipmentFault" not in source
    assert "RECOVERY_BACKOFF" not in source
    assert "TTC_FAILURE_LIMIT" not in source
    assert "TTC_FDIR_RequestRecovery" in source
    service = source[source.index("void TTC_Service(void)"):]
    assert "LoRa_Init()" not in service


def test_fdir_interface_and_queued_operations_are_exposed() -> None:
    header = (TTC_INC / "ttc.h").read_text(encoding="utf-8")
    driver_header = (TTC_INC / "lora_driver.h").read_text(encoding="utf-8")
    for symbol in (
        "TTC_FDIR_Health_t",
        "TTC_FDIR_GetHealth",
        "TTC_FDIR_RequestIsolation",
        "TTC_FDIR_RequestRecovery",
        "TTC_FDIR_RequestRxRestart",
        "TTC_FDIR_RequestReturnToService",
        "TTC_FDIR_GetActionStatus",
    ):
        assert symbol in header
    assert "void LoRa_Service(void);" in driver_header
    assert "LoRa_IsBusy" in driver_header


def test_recovery_regressions_have_behavioral_harnesses() -> None:
    ttc_source = read("ttc.c")
    driver_source = read("lora_driver.c")
    ttc_harness = (Path(__file__).parent / "test_ttc_state_machine.c").read_text(
        encoding="utf-8"
    )
    driver_harness = (
        Path(__file__).parent / "test_lora_driver_state_machine.c"
    ).read_text(encoding="utf-8")

    assert "TTC_ReconcileDriverFault" in ttc_source
    assert "TTC_STATE_ACTION_ISOLATION" in ttc_source
    assert "LORA_SPI_ABORT_TIMEOUT_MS" in driver_source
    assert "LoRa_ForceSpiQuiesce" in driver_source
    assert "TestStartupFailureCanRecover" in ttc_harness
    assert "TestRxFaultIsRecordedOnce" in ttc_harness
    assert "TestAckRetriesAndNackCounter" in ttc_harness
    assert "TestOversizedObservationIsConsumed" in driver_harness
    assert "TestAbortRejectionStillAllowsRecovery" in driver_harness
    assert "TestAbortTimeoutStillAllowsRecovery" in driver_harness


def test_flight_radio_profile_is_869525_sf8_17_dbm() -> None:
    source = read("lora_driver.c")
    for declaration in (
        "#define LORA_FRF_MSB              0xD9U",
        "#define LORA_FRF_MID              0x61U",
        "#define LORA_FRF_LSB              0x99U",
        "#define LORA_MODEM_CONFIG_1       0x72U",
        "#define LORA_MODEM_CONFIG_2       0x84U",
        "#define LORA_MODEM_CONFIG_3       0x04U",
        "#define LORA_SYNC_WORD            0x12U",
        "#define LORA_PA_CONFIG_17_DBM     0x8FU",
        "#define LORA_PA_DAC_NORMAL        0x84U",
    ):
        assert declaration in source
    assert "{ LORA_ACTION_WRITE, REG_PA_DAC, LORA_PA_DAC_NORMAL }" in source
    assert "{ LORA_ACTION_READ_VERIFY, REG_PA_CONFIG, LORA_PA_CONFIG_17_DBM }" in source

if __name__ == "__main__":
    test_driver_has_no_blocking_delay_or_tx_done_wait_loop()
    test_ttc_has_no_scv_or_autonomous_recovery_policy()
    test_fdir_interface_and_queued_operations_are_exposed()
    test_recovery_regressions_have_behavioral_harnesses()
    test_flight_radio_profile_is_869525_sf8_17_dbm()
    print("TTC source-contract checks passed")
