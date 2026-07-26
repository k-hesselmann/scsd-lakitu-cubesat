"""TTC source-contract checks runnable without target hardware.

These checks complement on-target HAL-mock tests: they protect the architectural
properties that must remain true even when the STM32 build environment is absent.
"""
from pathlib import Path

FIRMWARE = Path(__file__).resolve().parents[2]
CORE_INC = FIRMWARE / "Core" / "Inc"
CORE_SRC = FIRMWARE / "Core" / "Src"
TTC_INC = FIRMWARE / "Core" / "Inc" / "ttc"
TTC_SRC = FIRMWARE / "Core" / "Src" / "ttc"


def read(name: str) -> str:
    return (TTC_SRC / name).read_text(encoding="utf-8")


def without_function(source: str, signature: str) -> str:
    start = source.index(signature)
    end = source.index("\n}\n", start) + 3
    return source[:start] + source[end:]


def test_driver_has_no_blocking_delay_or_tx_done_wait_loop() -> None:
    source = read("lora_driver.c")
    assert "HAL_Delay" not in source
    assert "HAL_SPI_Transmit(" not in source
    assert "HAL_SPI_Receive(" not in source
    assert "HAL_SPI_TransmitReceive_IT" in source
    assert "SPI1_IRQHandler" in source
    assert "HAL_SPI_TxRxCpltCallback" in source
    assert "LORA_STATE_TRANSMITTING" in source
    assert "DRIVER_TX_POLL" in source
    assert "#define LORA_TX_IRQ_POLL_INTERVAL_MS 2U" in source
    assert "#define LORA_RX_IRQ_POLL_INTERVAL_MS 5U" in source

    # A bounded, synchronous read is permitted only for explicit bench/debug
    # register snapshots. The operational driver paths must stay IRQ-driven.
    assert source.count("HAL_SPI_TransmitReceive(&") == 1
    assert "static uint8_t LoRa_ReadRegisterBlocking" in source
    debug_reader = source[
        source.index("LoRaStatus_t LoRa_ReadDebugRegisters(void)") :
        source.index("void LoRa_GetDebugStatus")
    ]
    assert "LoRa_ReadRegisterBlocking" in debug_reader
    operational_source = without_function(
        source, "static uint8_t LoRa_ReadRegisterBlocking"
    ).replace(debug_reader, "")
    assert "HAL_SPI_TransmitReceive(&" not in operational_source
    assert "LoRa_ReadRegisterBlocking" not in operational_source

    # TTC must use the driver's cached register state. Automatic synchronous
    # snapshots used to add eleven SPI transactions after every init and TX.
    assert "LoRa_ReadDebugRegisters" not in read("ttc.c")


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
    assert "TestNoAckIsReportedWithoutRetry" in ttc_harness
    assert "TestOversizedObservationIsConsumed" in driver_harness
    assert "TestRxStartCompletionEntersPolling" in driver_harness
    assert "TestAbortRejectionStillAllowsRecovery" in driver_harness
    assert "TestAbortTimeoutStillAllowsRecovery" in driver_harness
    assert "TestSpiTimeoutTraceCapturesExactOperation" in driver_harness
    assert "TestLateServiceAcceptsCompletedTransfer" in driver_harness
    assert "TestTxDoneIsVisibleBeforeCleanupCompletes" in driver_harness
    assert "TestRxPollingIsCappedAtFiveMilliseconds" in driver_harness
    assert "TestAckTimerStartsOnlyAfterRxIsActive" in ttc_harness
    assert "TestOnAirTxSurvivesCleanupFailure" in ttc_harness


def test_ttc_tracks_rx_availability_after_tx_done() -> None:
    ttc = read("ttc.c")

    assert "TTC_FDIR_RequestRxRestart" in ttc
    assert "TTC_FDIR_RequestRecovery" in ttc
    assert "LoRa_WasLastTxOnAir" in ttc
    assert "LORA_EVENT_ACK_RX_UNAVAILABLE" in ttc


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


def test_sd_recovery_policy_is_owned_by_fdir() -> None:
    sd_logger = (CORE_SRC / "sd_logger.c").read_text(encoding="utf-8")
    fdir = (CORE_SRC / "fdir" / "fdir.c").read_text(encoding="utf-8")
    sd_header = (CORE_INC / "sd_logger.h").read_text(encoding="utf-8")

    assert "SD_LoggerHealth_t" in sd_header
    assert "SD_Logger_GetHealth" in sd_header

    assert "FDIR_SetEquipmentFault" not in sd_logger
    assert "FDIR_GetReinitRequests() & EQUIPMENT_SD" in sd_logger
    assert "FDIR_AcknowledgeReinit(EQUIPMENT_SD)" in sd_logger
    assert "USER_force_reinitialize();" in sd_logger

    assert "SD_Logger_GetHealth" in fdir
    assert "FDIR_SetEquipmentFault(EQUIPMENT_SD" in fdir
    assert "s_reinit_requests |= EQUIPMENT_SD" in fdir
    assert "FDIR_SD_REINIT_PERIOD_MS" in fdir


def test_coral_silent_link_ages_out_last_good_frame() -> None:
    coral = (CORE_SRC / "cdh" / "coral.c").read_text(encoding="utf-8")
    fdir_header = (CORE_INC / "fdir" / "fdir.h").read_text(encoding="utf-8")

    assert "FRAME_STALE_TIMEOUT_MS" in coral
    assert "#define FRAME_STALE_TIMEOUT_MS CORAL_DEFAULT_INTERVAL_MS" in coral
    assert "#define FDIR_CORAL_TIMEOUT_MS         5000U" in fdir_header
    assert "s_last_good_frame_ms = now" in coral
    assert "s_seen_good_frame = 1U" in coral
    assert "dp->coral_valid = 0U;" in coral
    assert "dp->coral_block[7] = CORAL_STATUS_TIMEOUT" in coral
    assert "[CORAL] !!! Frame freshness timeout" in coral

if __name__ == "__main__":
    test_driver_has_no_blocking_delay_or_tx_done_wait_loop()
    test_ttc_has_no_scv_or_autonomous_recovery_policy()
    test_fdir_interface_and_queued_operations_are_exposed()
    test_recovery_regressions_have_behavioral_harnesses()
    test_ttc_tracks_rx_availability_after_tx_done()
    test_flight_radio_profile_is_869525_sf8_17_dbm()
    test_sd_recovery_policy_is_owned_by_fdir()
    test_coral_silent_link_ages_out_last_good_frame()
    print("TTC source-contract checks passed")
