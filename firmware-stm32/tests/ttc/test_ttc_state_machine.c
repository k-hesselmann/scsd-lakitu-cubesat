#include <stdint.h>
#include <string.h>
#include "ttc/ttc.h"
#include "ttc/lora_driver.h"

static uint32_t mock_tick;
static LoRaState_t mock_state;
static LoRaStatus_t mock_status;
static LoRaStatus_t mock_init_result;
static LoRaStatus_t mock_isolate_result;
static uint8_t mock_busy;
static uint8_t mock_ready;
static uint8_t mock_rx_active;
static uint16_t mock_init_calls;
static uint16_t mock_send_calls;
static uint16_t mock_fdir_requests;

/* White-box TTC test: hardware, the LoRa driver, and FDIR's reinit-request
 * bitmask are mocked below. */
#include "../../Core/Src/ttc/ttc.c"

#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)

uint32_t HAL_GetTick(void) { return mock_tick; }

uint16_t FDIR_GetReinitRequests(void) { return mock_fdir_requests; }
void FDIR_AcknowledgeReinit(uint16_t mask) { mock_fdir_requests &= (uint16_t)~mask; }

LoRaStatus_t LoRa_Init(void)
{
    mock_init_calls++;
    if (mock_init_result == LORA_OK) {
        mock_busy = 1U; mock_ready = 0U; mock_rx_active = 0U;
        mock_state = LORA_STATE_INITIALISING; mock_status = LORA_BUSY;
    }
    return mock_init_result;
}

LoRaStatus_t LoRa_Send(const uint8_t *data, uint8_t length, uint32_t timeout)
{
    (void)data; (void)length; (void)timeout;
    mock_send_calls++;
    mock_busy = 1U; mock_rx_active = 0U;
    mock_state = LORA_STATE_TRANSMITTING; mock_status = LORA_BUSY;
    return LORA_OK;
}

LoRaStatus_t LoRa_StartReceive(void)
{
    mock_busy = 1U; mock_rx_active = 0U;
    mock_state = LORA_STATE_STARTING_RX; mock_status = LORA_BUSY;
    return LORA_OK;
}

void LoRa_Service(void) {}
LoRaStatus_t LoRa_Receive(uint8_t *data, uint8_t *length, uint8_t capacity)
{ (void)data; (void)length; (void)capacity; return LORA_NO_PACKET; }
uint8_t LoRa_IsBusy(void) { return mock_busy; }
uint8_t LoRa_IsReady(void) { return mock_ready; }
uint8_t LoRa_IsRxActive(void) { return mock_rx_active; }
uint8_t LoRa_IsIsolated(void) { return mock_state == LORA_STATE_ISOLATED; }
LoRaState_t LoRa_GetState(void) { return mock_state; }
LoRaStatus_t LoRa_GetLastStatus(void) { return mock_status; }

LoRaStatus_t LoRa_Isolate(void)
{
    if (mock_state == LORA_STATE_ISOLATED) return LORA_OK;
    mock_ready = 0U; mock_rx_active = 0U;
    if (mock_isolate_result == LORA_BUSY) {
        mock_busy = 1U; mock_state = LORA_STATE_ISOLATING; mock_status = LORA_BUSY;
    } else {
        mock_busy = 0U; mock_state = LORA_STATE_ISOLATED; mock_status = mock_isolate_result;
    }
    return mock_isolate_result;
}

static void Mock_Reset(void)
{
    mock_tick = 0U; mock_state = LORA_STATE_IDLE; mock_status = LORA_NOT_READY;
    mock_init_result = LORA_OK; mock_isolate_result = LORA_OK;
    mock_busy = 0U; mock_ready = 0U; mock_rx_active = 0U;
    mock_init_calls = 0U; mock_send_calls = 0U;
    mock_fdir_requests = 0U;
}

static void Mock_Complete(LoRaStatus_t status, uint8_t ready, uint8_t rx)
{
    mock_busy = 0U; mock_ready = ready; mock_rx_active = rx; mock_status = status;
    mock_state = status == LORA_OK ? LORA_STATE_IDLE : LORA_STATE_FAULT;
}

static void StartHealthyTtc(void)
{
    TTC_Init();
    Mock_Complete(LORA_OK, 1U, 0U); TTC_Service();
    Mock_Complete(LORA_OK, 1U, 1U); TTC_Service();
}

static int TestStartupFailureCanRecover(void)
{
    TTC_FDIR_ActionStatus_t action;
    Mock_Reset(); TTC_Init();
    Mock_Complete(LORA_SPI_ERROR, 0U, 0U); TTC_Service();
    CHECK(s_state == TTC_STATE_IDLE);
    CHECK(s_radio_faulted == 1U);
    CHECK(TTC_FDIR_RequestRecovery() == TTC_FDIR_RESULT_ACCEPTED);
    TTC_Service();
    CHECK(mock_init_calls == 2U);
    CHECK(TTC_GetHealth()->recovery_count == 1U);
    CHECK(s_state == TTC_STATE_ACTION_INIT);
    Mock_Complete(LORA_OK, 1U, 0U); TTC_Service();
    CHECK(s_state == TTC_STATE_ACTION_RX_START);
    Mock_Complete(LORA_OK, 1U, 1U); TTC_Service();
    action = TTC_FDIR_GetActionStatus();
    CHECK(action.state == TTC_FDIR_ACTION_SUCCEEDED);
    CHECK(s_radio_faulted == 0U);
    return 0;
}

static int TestRxFaultIsRecordedOnce(void)
{
    TTC_FDIR_Health_t health;
    uint8_t failures;
    Mock_Reset(); StartHealthyTtc();
    Mock_Complete(LORA_SPI_ERROR, 0U, 0U); TTC_Service();
    failures = TTC_GetHealth()->consecutive_failures;
    CHECK(failures == 1U);
    CHECK(TTC_GetHealth()->last_event == LORA_EVENT_RX_SPI_FAIL);
    TTC_FDIR_GetHealth(&health);
    CHECK(health.radio_ready == 0U && health.rx_active == 0U);
    TTC_Service();
    CHECK(TTC_GetHealth()->consecutive_failures == failures);
    CHECK(mock_init_calls == 1U);
    return 0;
}

static int TestActionIdempotencyAndIsolation(void)
{
    TTC_FDIR_ActionStatus_t action;
    Mock_Reset(); StartHealthyTtc();
    CHECK(TTC_FDIR_RequestRecovery() == TTC_FDIR_RESULT_ALREADY_COMPLETE);
    mock_isolate_result = LORA_BUSY;
    CHECK(TTC_FDIR_RequestIsolation() == TTC_FDIR_RESULT_ACCEPTED);
    CHECK(TTC_FDIR_RequestIsolation() == TTC_FDIR_RESULT_IN_PROGRESS);
    TTC_Service();
    CHECK(s_state == TTC_STATE_ACTION_ISOLATION);
    mock_busy = 0U; mock_state = LORA_STATE_ISOLATED; mock_status = LORA_OK;
    TTC_Service();
    action = TTC_FDIR_GetActionStatus();
    CHECK(action.state == TTC_FDIR_ACTION_SUCCEEDED);
    CHECK(TTC_FDIR_RequestIsolation() == TTC_FDIR_RESULT_ALREADY_COMPLETE);
    return 0;
}

static int TestIsolationPreemptsStartup(void)
{
    Mock_Reset();
    TTC_Init();
    mock_isolate_result = LORA_BUSY;
    CHECK(TTC_FDIR_RequestIsolation() == TTC_FDIR_RESULT_ACCEPTED);
    TTC_Service();
    CHECK(s_state == TTC_STATE_ACTION_ISOLATION);
    mock_busy = 0U;
    mock_state = LORA_STATE_ISOLATED;
    mock_status = LORA_OK;
    TTC_Service();
    CHECK(s_state == TTC_STATE_IDLE);
    CHECK(TTC_FDIR_RequestReturnToService() == TTC_FDIR_RESULT_ACCEPTED);
    TTC_Service();
    CHECK(mock_init_calls == 2U);
    CHECK(s_state == TTC_STATE_ACTION_INIT);
    return 0;
}

static void CompleteTransmitAndRestartRx(void)
{
    Mock_Complete(LORA_OK, 1U, 0U); TTC_Service();
    Mock_Complete(LORA_OK, 1U, 1U); TTC_Service();
}

static int TestAckRetriesAndNackCounter(void)
{
    TelemetryPacket_t packet;
    TTC_FDIR_Health_t health;
    uint8_t attempt;
    Mock_Reset(); StartHealthyTtc();
    memset(&packet, 0, sizeof(packet));
    packet.sequence_number = 42U;
    TTC_Transmit(&packet);
    for (attempt = 0U; attempt < TTC_MAX_TX_ATTEMPTS; attempt++) {
        TTC_Service();
        CHECK(mock_send_calls == (uint16_t)(attempt + 1U));
        CompleteTransmitAndRestartRx();
        mock_tick += TTC_ACK_TIMEOUT_MS;
    }
    TTC_Service(); TTC_FDIR_GetHealth(&health);
    CHECK(health.nack_counter == 1U);
    CHECK(TTC_GetHealth()->ack_timeout_count == 1U);
    CHECK(s_pending_valid == 0U);
    return 0;
}

static int TestFdirReinitBitTriggersRecovery(void)
{
    TTC_FDIR_ActionStatus_t action;
    Mock_Reset(); StartHealthyTtc();

    mock_fdir_requests = EQUIPMENT_LORA;
    TTC_Service();
    CHECK((mock_fdir_requests & EQUIPMENT_LORA) == 0U);   /* acked         */
    action = TTC_FDIR_GetActionStatus();
    CHECK(action.action == TTC_FDIR_ACTION_RECOVERY);
    CHECK((action.state == TTC_FDIR_ACTION_PENDING) ||
          (action.state == TTC_FDIR_ACTION_IN_PROGRESS));
    return 0;
}

int main(void)
{
    int result;
    result = TestStartupFailureCanRecover();
    if (result != 0) return result;
    result = TestRxFaultIsRecordedOnce();
    if (result != 0) return result;
    result = TestActionIdempotencyAndIsolation();
    if (result != 0) return result;
    result = TestIsolationPreemptsStartup();
    if (result != 0) return result;
    result = TestAckRetriesAndNackCounter();
    if (result != 0) return result;
    return TestFdirReinitBitTriggersRecovery();
}
