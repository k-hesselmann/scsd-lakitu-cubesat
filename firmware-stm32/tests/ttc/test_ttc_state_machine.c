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
static uint8_t mock_last_send_length;
static uint16_t mock_fdir_requests;

#define TTC_AUTH_KEY_0 UINT64_C(0x0706050403020100)
#define TTC_AUTH_KEY_1 UINT64_C(0x0f0e0d0c0b0a0908)

/* White-box TTC test: hardware, the LoRa driver, and FDIR's reinit-request
 * bitmask are mocked below. */
#include "../../Core/Src/ttc/ttc_auth.c"
#include "../../Core/Src/ttc/ttc.c"

#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)

static uint8_t Mock_Authenticated(const char *payload, uint8_t *envelope)
{
    static const char hex[] = "0123456789abcdef";
    uint8_t length = (uint8_t)strlen(payload);
    uint64_t tag = TTC_AuthTag((const uint8_t *)payload, length);
    uint8_t index;

    memcpy(envelope, payload, length);
    envelope[length++] = (uint8_t)',';
    for (index = 0U; index < TTC_AUTH_TAG_HEX_LENGTH; index++)
    {
        uint8_t shift = (uint8_t)(60U - (index * 4U));
        envelope[length++] = (uint8_t)hex[(tag >> shift) & 0x0FU];
    }
    return length;
}

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
    (void)data; (void)timeout;
    mock_send_calls++;
    mock_last_send_length = length;
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
    mock_last_send_length = 0U; mock_fdir_requests = 0U;
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
        CHECK(mock_last_send_length == TELEMETRY_PACKET_V8_SIZE);
        CompleteTransmitAndRestartRx();
        mock_tick += TTC_ACK_TIMEOUT_MS;
    }
    TTC_Service(); TTC_FDIR_GetHealth(&health);
    CHECK(health.nack_counter == 1U);
    CHECK(TTC_GetHealth()->ack_timeout_count == 1U);
    CHECK(s_pending_valid == 0U);
    return 0;
}

static int TestCommandAndAckLatchesAreIndependent(void)
{
    uint8_t command[TTC_UPLINK_MAX_LENGTH];
    uint8_t command_length;
    uint8_t command_status;

    Mock_Reset(); StartHealthyTtc();
    command_length = Mock_Authenticated("CMD,123,REQ_TELEMETRY", command);
    TTC_ProcessUplink(command, command_length);
    CHECK(s_uplink.last_command_id == 123U);
    CHECK(s_uplink.last_command_status == UPLINK_STATUS_ACCEPTED);
    command_status = s_uplink.last_command_status;

    s_pending_valid = 1U;
    s_pending_packet.boot_count_sat = 3U;
    s_pending_packet.sequence_number = 77U;
    s_pending_packet.tx_uptime_s = 456U;
    command_length = Mock_Authenticated("ACK,3,77,456", command);
    TTC_ProcessUplink(command, command_length);
    CHECK(s_uplink.last_ack_status == UPLINK_STATUS_ACCEPTED);
    CHECK(s_uplink.last_ack_sequence == 77U);
    CHECK(s_uplink.last_command_id == 123U);
    CHECK(s_uplink.last_command_status == command_status);

    s_pending_valid = 1U;
    s_pending_packet.boot_count_sat = 4U;
    s_pending_packet.sequence_number = 77U;
    s_pending_packet.tx_uptime_s = 1U;
    TTC_ProcessUplink(command, command_length);
    CHECK(s_pending_valid == 1U);
    CHECK(s_uplink.last_ack_status == UPLINK_STATUS_DUPLICATE);
    return 0;
}

static int TestCommandReplayDistinguishesDuplicateFromStale(void)
{
    uint8_t envelope[TTC_UPLINK_MAX_LENGTH];
    uint8_t length;

    Mock_Reset(); StartHealthyTtc();
    length = Mock_Authenticated("CMD,100,REQ_TELEMETRY", envelope);
    TTC_ProcessUplink(envelope, length);
    CHECK(s_uplink.last_command_status == UPLINK_STATUS_ACCEPTED);
    CHECK(s_uplink.command_count == 1U);

    TTC_ProcessUplink(envelope, length);
    CHECK(s_uplink.last_command_status == UPLINK_STATUS_DUPLICATE);
    CHECK(s_uplink.command_count == 1U);

    length = Mock_Authenticated("CMD,50,REQ_TELEMETRY", envelope);
    TTC_ProcessUplink(envelope, length);
    CHECK(s_uplink.last_command_status == UPLINK_STATUS_STALE);
    CHECK(s_uplink.command_count == 1U);
    return 0;
}

static int TestNewCommandsAreRateLimitedWithoutConsumingTheirId(void)
{
    uint8_t envelope[TTC_UPLINK_MAX_LENGTH];
    uint8_t length;

    Mock_Reset(); StartHealthyTtc();
    length = Mock_Authenticated("CMD,200,REQ_TELEMETRY", envelope);
    TTC_ProcessUplink(envelope, length);
    CHECK(s_uplink.last_command_status == UPLINK_STATUS_ACCEPTED);

    mock_tick = TTC_COMMAND_MIN_INTERVAL_MS - 1U;
    length = Mock_Authenticated("CMD,201,REQ_TELEMETRY", envelope);
    TTC_ProcessUplink(envelope, length);
    CHECK(s_uplink.last_command_status == UPLINK_STATUS_RATE_LIMITED);
    CHECK(s_command_high_water == 200U);

    mock_tick = TTC_COMMAND_MIN_INTERVAL_MS;
    TTC_ProcessUplink(envelope, length);
    CHECK(s_uplink.last_command_status == UPLINK_STATUS_ACCEPTED);
    CHECK(s_command_high_water == 201U);
    return 0;
}

static int TestUplinkAuthenticationRejectsTampering(void)
{
    uint8_t envelope[TTC_UPLINK_MAX_LENGTH];
    uint8_t length;

    CHECK(TTC_AuthTag(NULL, 0U) == UINT64_C(0x726fdb47dd0e0e31));
    Mock_Reset(); StartHealthyTtc();
    length = Mock_Authenticated("CMD,300,REQ_TELEMETRY", envelope);
    envelope[4] = (uint8_t)'4';
    TTC_ProcessUplink(envelope, length);
    CHECK(s_uplink.last_command_status == UPLINK_STATUS_NONE);
    CHECK(s_uplink.last_command_id == 0U);
    CHECK(s_uplink.command_count == 0U);
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
    result = TestCommandAndAckLatchesAreIndependent();
    if (result != 0) return result;
    result = TestCommandReplayDistinguishesDuplicateFromStale();
    if (result != 0) return result;
    result = TestNewCommandsAreRateLimitedWithoutConsumingTheirId();
    if (result != 0) return result;
    result = TestUplinkAuthenticationRejectsTampering();
    if (result != 0) return result;
    return TestFdirReinitBitTriggersRecovery();
}
