from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def test_usart2_debug_output_is_non_blocking_and_interrupt_driven():
    sources = "\n".join(path.read_text(encoding="utf-8") for path in (ROOT / "Core/Src").rglob("*.c"))
    logger = read("Core/Src/debug_log.c")
    interrupts = read("Core/Src/stm32l4xx_it.c")
    msp = read("Core/Src/stm32l4xx_hal_msp.c")

    direct_usart2_transmit = re.compile(
        r"HAL_UART_Transmit(?:_IT|_DMA)?\s*\(\s*&huart2"
    )
    assert direct_usart2_transmit.search(sources) is None
    assert "HAL_UART_Transmit_IT" in logger
    assert "HAL_UART_TxCpltCallback" in logger
    assert "USART2_IRQHandler" in interrupts
    assert "HAL_UART_IRQHandler(&huart2)" in interrupts
    assert "HAL_NVIC_EnableIRQ(USART2_IRQn)" in msp


def test_debug_lines_receive_enqueue_time_timestamp_atomically():
    logger = read("Core/Src/debug_log.c")

    assert "DEBUG_LOG_TIMESTAMP_PREFIX_SIZE 17U" in logger
    assert "DebugLog_FormatTimestampPrefix(HAL_GetTick()" in logger
    assert "DebugLog_CountTimestampPrefixes" in logger
    assert "DebugLog_QueueTimestampedMessage" in logger
    assert "if ((uint32_t)length >= DEBUG_LOG_QUEUE_SIZE)" in logger
    assert "if (expanded_length > free_bytes)" in logger
    assert "s_dropped_bytes += expanded_length" in logger


def test_system_stat_exposes_loop_debug_coral_and_gps_pressure():
    source = read("Core/Src/observability.c")

    assert "[SYS_STAT]" in source
    for field in (
        "loop_max=",
        "dbg_q=",
        "dbg_high=",
        "dbg_drop=",
        "coral_q=",
        "coral_high=",
        "coral_ovf=",
        "gps_msg_age=",
        "gps_3d_age=",
    ):
        assert field in source


def test_repeat_logs_are_bounded():
    validation = read("Core/Src/cdh/sensor_validation.c")
    coral = read("Core/Src/cdh/coral.c")
    gps = read("Core/Src/cdh/m10s.c")

    assert "VALIDATION_REPEAT_LOG_MS 10000U" in validation
    assert "Validation_LogFault(VALIDATION_LOG_GPS_NO_FIX" in validation
    assert "s_last_overflow_report_ms" in coral
    assert "M10S_VERBOSE_RUNTIME_LOGS 0" in gps
    assert "[GPS_PVT]" in gps


def test_sd_diagnostics_cover_filesystem_spi_and_recovery():
    logger = read("Core/Src/sd_logger.c")
    spi = read("Core/Src/sd_spi.c")
    observability = read("Core/Src/observability.c")
    platformio = read("platformio.ini")

    assert "[SD_INIT]" in logger
    assert "[SD_FAULT]" in logger
    assert "[SD_RECOVERY]" in logger
    assert "SD_Logger_ResultName" in logger
    assert "SD_LOG_OP_DATA_WRITE" in logger
    assert "SD_LOG_OP_SYNC" in logger
    assert "SD_SPI_GetDiagnostics" in spi
    assert "[SD_CMD]" in spi
    assert "[SD_STAT]" in observability
    assert "[SD_SPI]" in observability
    assert "coral.sd_error_count" in observability
    assert "[env:nucleo_l476rg_sd_trace]" in platformio
    assert "-DSD_SPI_DEBUG=1" in platformio
