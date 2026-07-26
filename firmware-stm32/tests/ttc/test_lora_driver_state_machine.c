#include <stdint.h>
#include <string.h>
#include "main.h"
#include "ttc/lora_driver.h"

SPI_HandleTypeDef hspi1;
static uint32_t mock_tick;
static HAL_StatusTypeDef mock_transfer_result;
static HAL_StatusTypeDef mock_abort_result;
static HAL_StatusTypeDef mock_deinit_result;
static HAL_StatusTypeDef mock_init_result;
static uint16_t mock_transfer_size;
static uint16_t mock_abort_calls;
static uint16_t mock_deinit_calls;
static uint16_t mock_init_calls;

/* White-box driver test: every HAL operation is mocked below. */
#include "../../Core/Src/ttc/lora_driver.c"

#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)

uint32_t HAL_GetTick(void) { return mock_tick; }
HAL_StatusTypeDef HAL_SPI_TransmitReceive_IT(SPI_HandleTypeDef *hspi,
    const uint8_t *tx, uint8_t *rx, uint16_t size)
{
    (void)hspi; (void)tx; (void)rx;
    mock_transfer_size = size;
    return mock_transfer_result;
}
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *hspi,
    const uint8_t *tx, uint8_t *rx, uint16_t size, uint32_t timeout)
{
    (void)hspi; (void)tx; (void)rx; (void)size; (void)timeout;
    return mock_transfer_result;
}
uint32_t HAL_SPI_GetError(const SPI_HandleTypeDef *hspi)
{ return hspi->ErrorCode; }
HAL_StatusTypeDef HAL_SPI_Abort_IT(SPI_HandleTypeDef *hspi)
{ (void)hspi; mock_abort_calls++; return mock_abort_result; }
HAL_StatusTypeDef HAL_SPI_DeInit(SPI_HandleTypeDef *hspi)
{ (void)hspi; mock_deinit_calls++; return mock_deinit_result; }
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *hspi)
{ (void)hspi; mock_init_calls++; return mock_init_result; }
void HAL_SPI_IRQHandler(SPI_HandleTypeDef *hspi) { (void)hspi; }
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{ (void)port; (void)pin; (void)state; }
void HAL_NVIC_SetPriority(IRQn_Type irq, uint32_t preempt, uint32_t sub)
{ (void)irq; (void)preempt; (void)sub; }
void HAL_NVIC_EnableIRQ(IRQn_Type irq) { (void)irq; }
void HAL_NVIC_DisableIRQ(IRQn_Type irq) { (void)irq; }

static void Mock_Reset(void)
{
    memset(&hspi1, 0, sizeof(hspi1));
    mock_tick = 0U;
    mock_transfer_result = HAL_OK;
    mock_abort_result = HAL_OK;
    mock_deinit_result = HAL_OK;
    mock_init_result = HAL_OK;
    mock_transfer_size = 0U;
    mock_abort_calls = 0U;
    mock_deinit_calls = 0U;
    mock_init_calls = 0U;
    s_driver_state = DRIVER_IDLE;
    s_state = LORA_STATE_IDLE;
    s_last_status = LORA_NOT_READY;
    s_ready = 0U;
    s_rx_active = 0U;
    s_action_waiting = 0U;
    s_delay_active = 0U;
    s_rx_pending_status = LORA_NO_PACKET;
    s_rx_pending_length = 0U;
    s_spi_active = 0U;
    s_spi_complete = 0U;
    s_spi_error = 0U;
    s_spi_abort_pending = 0U;
    s_spi_abort_complete = 0U;
    s_spi_finished = 0U;
    s_spi_result_error = 0U;
    s_spi_reinit_required = 0U;
    s_tx_done_seen = 0U;
    s_last_tx_irq_poll_ms = 0U;
    s_last_rx_irq_poll_ms = 0U;
    memset(&s_runtime_stats, 0, sizeof(s_runtime_stats));
    s_fault_trace_head = 0U;
    s_fault_trace_tail = 0U;
    s_fault_occurrence = 0U;
}

static int TestOversizedObservationIsConsumed(void)
{
    uint8_t data[64];
    uint8_t length = 0U;
    Mock_Reset();
    s_rx_pending_status = LORA_OK;
    s_rx_pending_length = 65U;
    CHECK(LoRa_Receive(data, &length, sizeof(data)) == LORA_LENGTH_ERROR);
    CHECK(length == 65U);
    CHECK(LoRa_Receive(data, &length, sizeof(data)) == LORA_NO_PACKET);
    return 0;
}

static int TestFullLengthFifoTransferDoesNotWrap(void)
{
    Mock_Reset();
    s_tx_length = UINT8_MAX;
    memset(s_tx_data, 0xA5, sizeof(s_tx_data));
    CHECK(LoRa_StartFifoWrite() == 1U);
    CHECK(mock_transfer_size == 256U);
    return 0;
}

static int TestRxStartCompletionEntersPolling(void)
{
    Mock_Reset();
    s_state = LORA_STATE_STARTING_RX;
    s_driver_state = DRIVER_RX_START_ACTIONS;
    LoRa_CompleteReceiveStart();
    CHECK(s_state == LORA_STATE_IDLE);
    CHECK(s_rx_active == 1U);
    CHECK(s_driver_state == DRIVER_RX_POLL);
    CHECK(LoRa_GetLastStatus() == LORA_OK);
    return 0;
}

static int TestAbortRejectionStillAllowsRecovery(void)
{
    Mock_Reset();
    s_spi_active = 1U;
    s_state = LORA_STATE_TRANSMITTING;
    mock_abort_result = HAL_ERROR;
    CHECK(LoRa_Isolate() == LORA_BUSY);
    CHECK(LoRa_GetState() == LORA_STATE_ISOLATING);
    LoRa_Service();
    CHECK(LoRa_IsIsolated() == 1U);
    CHECK(LoRa_GetLastStatus() == LORA_OK);
    CHECK(mock_deinit_calls == 1U);
    CHECK(LoRa_Init() == LORA_OK);
    CHECK(mock_init_calls == 1U);
    CHECK(LoRa_GetState() == LORA_STATE_INITIALISING);
    return 0;
}

static int TestAbortTimeoutStillAllowsRecovery(void)
{
    Mock_Reset();
    s_spi_active = 1U;
    s_state = LORA_STATE_TRANSMITTING;
    CHECK(LoRa_Isolate() == LORA_BUSY);
    CHECK(mock_abort_calls == 1U);
    mock_tick = LORA_SPI_ABORT_TIMEOUT_MS;
    LoRa_Service();
    CHECK(LoRa_IsIsolated() == 1U);
    CHECK(mock_deinit_calls == 1U);
    CHECK(LoRa_Init() == LORA_OK);
    CHECK(mock_init_calls == 1U);
    return 0;
}

static int TestAbortCallbackCompletesWithoutReset(void)
{
    Mock_Reset();
    s_spi_active = 1U;
    s_state = LORA_STATE_TRANSMITTING;
    CHECK(LoRa_Isolate() == LORA_BUSY);
    HAL_SPI_AbortCpltCallback(&hspi1);
    LoRa_Service();
    CHECK(LoRa_IsIsolated() == 1U);
    CHECK(mock_deinit_calls == 0U);
    return 0;
}

static int TestSpiTimeoutTraceCapturesExactOperation(void)
{
    LoRaFaultTrace_t trace;

    Mock_Reset();
    s_state = LORA_STATE_TRANSMITTING;
    s_driver_state = DRIVER_TX_POLL;
    CHECK(LoRa_StartRead(REG_IRQ_FLAGS) == 1U);
    mock_tick = LORA_SPI_TIMEOUT_MS;
    LoRa_Service();

    CHECK(LoRa_TakeFaultTrace(&trace) == 1U);
    CHECK(trace.cause == LORA_SPI_FAIL_TRANSFER_TIMEOUT);
    CHECK(trace.phase == LORA_PHASE_TX_POLL);
    CHECK(trace.register_address == REG_IRQ_FLAGS);
    CHECK(trace.operation == LORA_OP_READ);
    CHECK(trace.spi_active == 1U);
    CHECK(s_runtime_stats.spi_timeout_count == 1U);
    return 0;
}

static int TestLateServiceAcceptsCompletedTransfer(void)
{
    Mock_Reset();
    s_state = LORA_STATE_TRANSMITTING;
    s_driver_state = DRIVER_TX_POLL;
    s_action_waiting = 1U;
    s_deadline_ms = 1000U;
    CHECK(LoRa_StartRead(REG_IRQ_FLAGS) == 1U);
    HAL_SPI_TxRxCpltCallback(&hspi1);

    /* The ISR completed on time even though the superloop services the result
     * only after the wall-time timeout threshold. */
    mock_tick = LORA_SPI_TIMEOUT_MS;
    LoRa_Service();
    CHECK(mock_abort_calls == 0U);
    CHECK(s_runtime_stats.spi_timeout_count == 0U);
    CHECK(s_runtime_stats.spi_completion_count == 1U);
    return 0;
}

static int TestTxDoneIsVisibleBeforeCleanupCompletes(void)
{
    Mock_Reset();
    s_state = LORA_STATE_TRANSMITTING;
    s_driver_state = DRIVER_TX_POLL;
    s_action_waiting = 1U;
    s_spi_finished = 1U;
    s_spi_result_error = 0U;
    s_spi_rx[1] = IRQ_TX_DONE;
    LoRa_Service();
    CHECK(LoRa_WasLastTxOnAir() == 1U);
    CHECK(s_driver_state == DRIVER_TX_FINISH);
    CHECK(LoRa_IsBusy() == 1U);
    return 0;
}

static int TestRxPollingIsCappedAtFiveMilliseconds(void)
{
    Mock_Reset();
    s_state = LORA_STATE_IDLE;
    s_ready = 1U;
    s_rx_active = 1U;
    s_driver_state = DRIVER_RX_POLL;
    s_last_rx_irq_poll_ms = 10U;

    mock_tick = 14U;
    LoRa_Service();
    CHECK(s_runtime_stats.rx_irq_poll_count == 0U);
    mock_tick = 15U;
    LoRa_Service();
    CHECK(s_runtime_stats.rx_irq_poll_count == 1U);
    CHECK(s_spi_active == 1U);
    return 0;
}

int main(void)
{
    int result;
    result = TestOversizedObservationIsConsumed();
    if (result != 0) return result;
    result = TestFullLengthFifoTransferDoesNotWrap();
    if (result != 0) return result;
    result = TestRxStartCompletionEntersPolling();
    if (result != 0) return result;
    result = TestAbortRejectionStillAllowsRecovery();
    if (result != 0) return result;
    result = TestAbortTimeoutStillAllowsRecovery();
    if (result != 0) return result;
    result = TestAbortCallbackCompletesWithoutReset();
    if (result != 0) return result;
    result = TestSpiTimeoutTraceCapturesExactOperation();
    if (result != 0) return result;
    result = TestLateServiceAcceptsCompletedTransfer();
    if (result != 0) return result;
    result = TestTxDoneIsVisibleBeforeCleanupCompletes();
    if (result != 0) return result;
    return TestRxPollingIsCappedAtFiveMilliseconds();
}
