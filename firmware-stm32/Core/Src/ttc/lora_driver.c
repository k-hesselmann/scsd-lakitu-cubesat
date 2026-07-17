#include "ttc/lora_driver.h"

#include "main.h"

#include <string.h>

extern SPI_HandleTypeDef hspi1;

#define REG_FIFO                 0x00U
#define REG_OP_MODE              0x01U
#define REG_FRF_MSB              0x06U
#define REG_FRF_MID              0x07U
#define REG_FRF_LSB              0x08U
#define REG_PA_CONFIG            0x09U
#define REG_FIFO_ADDR_PTR        0x0DU
#define REG_FIFO_TX_BASE_ADDR    0x0EU
#define REG_FIFO_RX_BASE_ADDR    0x0FU
#define REG_FIFO_RX_CURRENT_ADDR 0x10U
#define REG_IRQ_FLAGS            0x12U
#define REG_RX_NB_BYTES          0x13U
#define REG_MODEM_CONFIG_1       0x1DU
#define REG_MODEM_CONFIG_2       0x1EU
#define REG_PREAMBLE_MSB         0x20U
#define REG_PREAMBLE_LSB         0x21U
#define REG_PAYLOAD_LENGTH       0x22U
#define REG_MODEM_CONFIG_3       0x26U
#define REG_SYNC_WORD            0x39U
#define REG_DIO_MAPPING_1        0x40U
#define REG_VERSION              0x42U

#define MODE_LONG_RANGE           0x80U
#define MODE_SLEEP                0x00U
#define MODE_STANDBY              0x01U
#define MODE_TX                   0x03U
#define MODE_RX_CONTINUOUS        0x05U
#define IRQ_TX_DONE               0x08U
#define IRQ_RX_DONE               0x40U
#define IRQ_PAYLOAD_CRC_ERROR     0x20U

#define LORA_SPI_TIMEOUT_MS       100U
#define LORA_SPI_ABORT_TIMEOUT_MS 100U
#define LORA_RESET_LOW_MS         10U
#define LORA_RESET_SETTLE_MS      20U
#define LORA_SLEEP_SETTLE_MS      10U
#define LORA_MAX_PAYLOAD          255U

typedef enum
{
    LORA_ACTION_WRITE = 0,
    LORA_ACTION_READ_VERIFY,
    LORA_ACTION_DELAY,
    LORA_ACTION_TX_FIFO,
    LORA_ACTION_TX_LENGTH
} LoRaActionType_t;

typedef struct
{
    LoRaActionType_t type;
    uint8_t address;
    uint8_t value;
} LoRaAction_t;

typedef enum
{
    DRIVER_IDLE = 0,
    DRIVER_INIT_RESET_LOW,
    DRIVER_INIT_RESET_SETTLE,
    DRIVER_INIT_ACTIONS,
    DRIVER_TX_ACTIONS,
    DRIVER_TX_POLL,
    DRIVER_TX_FINISH,
    DRIVER_RX_START_ACTIONS,
    DRIVER_RX_POLL,
    DRIVER_RX_LENGTH,
    DRIVER_RX_ADDRESS,
    DRIVER_RX_SET_POINTER,
    DRIVER_RX_FIFO,
    DRIVER_RX_CLEAR
} LoRaDriverState_t;

static const LoRaAction_t s_init_actions[] =
{
    { LORA_ACTION_READ_VERIFY, REG_VERSION, 0x12U },
    { LORA_ACTION_WRITE, REG_OP_MODE, (uint8_t)(MODE_LONG_RANGE | MODE_SLEEP) },
    { LORA_ACTION_DELAY, 0U, LORA_SLEEP_SETTLE_MS },
    { LORA_ACTION_WRITE, REG_FRF_MSB, 0xD9U },
    { LORA_ACTION_WRITE, REG_FRF_MID, 0x00U },
    { LORA_ACTION_WRITE, REG_FRF_LSB, 0x00U },
    { LORA_ACTION_WRITE, REG_FIFO_TX_BASE_ADDR, 0x00U },
    { LORA_ACTION_WRITE, REG_FIFO_RX_BASE_ADDR, 0x00U },
    { LORA_ACTION_WRITE, REG_FIFO_ADDR_PTR, 0x00U },
    { LORA_ACTION_WRITE, REG_MODEM_CONFIG_1, 0x72U },
    { LORA_ACTION_WRITE, REG_MODEM_CONFIG_2, 0x94U },
    { LORA_ACTION_WRITE, REG_MODEM_CONFIG_3, 0x04U },
    { LORA_ACTION_WRITE, REG_PREAMBLE_MSB, 0x00U },
    { LORA_ACTION_WRITE, REG_PREAMBLE_LSB, 0x08U },
    { LORA_ACTION_WRITE, REG_SYNC_WORD, 0x12U },
    { LORA_ACTION_WRITE, REG_PA_CONFIG, 0x8CU },
    { LORA_ACTION_WRITE, REG_IRQ_FLAGS, 0xFFU },
    { LORA_ACTION_WRITE, REG_OP_MODE, (uint8_t)(MODE_LONG_RANGE | MODE_STANDBY) },
    { LORA_ACTION_READ_VERIFY, REG_FRF_MSB, 0xD9U },
    { LORA_ACTION_READ_VERIFY, REG_FRF_MID, 0x00U },
    { LORA_ACTION_READ_VERIFY, REG_FRF_LSB, 0x00U },
    { LORA_ACTION_READ_VERIFY, REG_MODEM_CONFIG_1, 0x72U },
    { LORA_ACTION_READ_VERIFY, REG_MODEM_CONFIG_2, 0x94U },
    { LORA_ACTION_READ_VERIFY, REG_MODEM_CONFIG_3, 0x04U },
    { LORA_ACTION_READ_VERIFY, REG_SYNC_WORD, 0x12U }
};

static const LoRaAction_t s_rx_start_actions[] =
{
    { LORA_ACTION_WRITE, REG_OP_MODE, (uint8_t)(MODE_LONG_RANGE | MODE_STANDBY) },
    { LORA_ACTION_WRITE, REG_DIO_MAPPING_1, 0x00U },
    { LORA_ACTION_WRITE, REG_IRQ_FLAGS, 0xFFU },
    { LORA_ACTION_WRITE, REG_FIFO_ADDR_PTR, 0x00U },
    { LORA_ACTION_WRITE, REG_OP_MODE, (uint8_t)(MODE_LONG_RANGE | MODE_RX_CONTINUOUS) },
    { LORA_ACTION_READ_VERIFY, REG_OP_MODE, (uint8_t)(MODE_LONG_RANGE | MODE_RX_CONTINUOUS) },
    { LORA_ACTION_READ_VERIFY, REG_DIO_MAPPING_1, 0x00U }
};

static const LoRaAction_t s_tx_actions[] =
{
    { LORA_ACTION_WRITE, REG_OP_MODE, (uint8_t)(MODE_LONG_RANGE | MODE_STANDBY) },
    { LORA_ACTION_WRITE, REG_DIO_MAPPING_1, 0x40U },
    { LORA_ACTION_WRITE, REG_IRQ_FLAGS, 0xFFU },
    { LORA_ACTION_WRITE, REG_FIFO_ADDR_PTR, 0x00U },
    { LORA_ACTION_TX_FIFO, REG_FIFO, 0U },
    { LORA_ACTION_TX_LENGTH, REG_PAYLOAD_LENGTH, 0U },
    { LORA_ACTION_WRITE, REG_OP_MODE, (uint8_t)(MODE_LONG_RANGE | MODE_TX) }
};

static const LoRaAction_t s_tx_done_actions[] =
{
    { LORA_ACTION_WRITE, REG_IRQ_FLAGS, IRQ_TX_DONE },
    { LORA_ACTION_WRITE, REG_OP_MODE, (uint8_t)(MODE_LONG_RANGE | MODE_STANDBY) }
};

static const LoRaAction_t s_tx_timeout_actions[] =
{
    { LORA_ACTION_WRITE, REG_IRQ_FLAGS, 0xFFU },
    { LORA_ACTION_WRITE, REG_OP_MODE, (uint8_t)(MODE_LONG_RANGE | MODE_STANDBY) }
};

static LoRaDriverState_t s_driver_state;
static LoRaState_t s_state;
static LoRaStatus_t s_last_status = LORA_NOT_READY;
static LoRaStatus_t s_tx_completion_status;
static uint8_t s_ready;
static uint8_t s_rx_active;
static uint8_t s_action_waiting;
static uint8_t s_delay_active;
static uint8_t s_action_index;
static uint8_t s_rx_length;
static uint8_t s_tx_length;
static uint8_t s_tx_data[LORA_MAX_PAYLOAD];
static uint8_t s_rx_data[LORA_MAX_PAYLOAD];
static LoRaStatus_t s_rx_pending_status = LORA_NO_PACKET;
static uint8_t s_rx_pending_length;
static uint32_t s_deadline_ms;
static uint32_t s_tx_start_ms;

static uint8_t s_spi_tx[LORA_MAX_PAYLOAD + 1U];
static uint8_t s_spi_rx[LORA_MAX_PAYLOAD + 1U];
static volatile uint8_t s_spi_active;
static volatile uint8_t s_spi_complete;
static volatile uint8_t s_spi_error;
static volatile uint8_t s_spi_abort_pending;
static volatile uint8_t s_spi_abort_complete;
static uint8_t s_spi_finished;
static uint8_t s_spi_result_error;
static uint8_t s_spi_reinit_required;
static uint32_t s_spi_start_ms;
static uint32_t s_spi_abort_start_ms;

static uint8_t LoRa_Elapsed(uint32_t now, uint32_t start, uint32_t duration)
{
    return ((uint32_t)(now - start) >= duration) ? 1U : 0U;
}

static void LoRa_Select(void)
{
    HAL_GPIO_WritePin(LORA_CS_GPIO_Port, LORA_CS_Pin, GPIO_PIN_RESET);
}

static void LoRa_Deselect(void)
{
    HAL_GPIO_WritePin(LORA_CS_GPIO_Port, LORA_CS_Pin, GPIO_PIN_SET);
}

static void LoRa_SetFault(LoRaStatus_t status)
{
    LoRa_Deselect();
    s_ready = 0U;
    s_rx_active = 0U;
    s_action_waiting = 0U;
    s_delay_active = 0U;
    s_driver_state = DRIVER_IDLE;
    s_state = LORA_STATE_FAULT;
    s_last_status = status;
}

static uint8_t LoRa_StartTransfer(uint16_t length)
{
    HAL_StatusTypeDef hal_status;

    if (length == 0U || s_spi_active || s_spi_abort_pending)
        return 0U;

    s_spi_complete = 0U;
    s_spi_error = 0U;
    s_spi_finished = 0U;
    s_spi_active = 1U;
    s_spi_start_ms = HAL_GetTick();
    LoRa_Select();
    hal_status = HAL_SPI_TransmitReceive_IT(&hspi1, s_spi_tx, s_spi_rx, length);
    if (hal_status != HAL_OK)
    {
        LoRa_Deselect();
        s_spi_active = 0U;
        s_spi_finished = 1U;
        s_spi_result_error = 1U;
        return 0U;
    }

    return 1U;
}

static uint8_t LoRa_StartWrite(uint8_t address, uint8_t value)
{
    s_spi_tx[0] = (uint8_t)(address | 0x80U);
    s_spi_tx[1] = value;
    return LoRa_StartTransfer(2U);
}

static uint8_t LoRa_StartRead(uint8_t address)
{
    s_spi_tx[0] = (uint8_t)(address & 0x7FU);
    s_spi_tx[1] = 0U;
    return LoRa_StartTransfer(2U);
}

static uint8_t LoRa_StartFifoWrite(void)
{
    s_spi_tx[0] = (uint8_t)(REG_FIFO | 0x80U);
    memcpy(&s_spi_tx[1], s_tx_data, s_tx_length);
    return LoRa_StartTransfer((uint16_t)s_tx_length + 1U);
}

static uint8_t LoRa_StartFifoRead(void)
{
    s_spi_tx[0] = REG_FIFO & 0x7FU;
    memset(&s_spi_tx[1], 0, s_rx_length);
    return LoRa_StartTransfer((uint16_t)s_rx_length + 1U);
}

static uint8_t LoRa_ForceSpiQuiesce(void)
{
    HAL_StatusTypeDef status;

    HAL_NVIC_DisableIRQ(SPI1_IRQn);
    status = HAL_SPI_DeInit(&hspi1);
    LoRa_Deselect();
    s_spi_active = 0U;
    s_spi_complete = 0U;
    s_spi_error = 0U;
    s_spi_abort_pending = 0U;
    s_spi_abort_complete = 0U;
    s_spi_finished = 0U;
    s_spi_result_error = 0U;
    s_spi_reinit_required = 1U;
    return (status == HAL_OK) ? 1U : 0U;
}

static void LoRa_CompleteIsolation(LoRaStatus_t status)
{
    HAL_NVIC_DisableIRQ(SPI1_IRQn);
    LoRa_Deselect();
    HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_RESET);
    s_ready = 0U;
    s_rx_active = 0U;
    s_action_waiting = 0U;
    s_delay_active = 0U;
    s_driver_state = DRIVER_IDLE;
    s_state = LORA_STATE_ISOLATED;
    s_last_status = status;
}

static void LoRa_ServiceIsolation(uint32_t now)
{
    if (!s_spi_active)
    {
        LoRa_CompleteIsolation(LORA_OK);
        return;
    }

    if (s_spi_abort_pending && s_spi_abort_complete)
    {
        LoRa_Deselect();
        s_spi_active = 0U;
        s_spi_complete = 0U;
        s_spi_error = 0U;
        s_spi_abort_pending = 0U;
        s_spi_abort_complete = 0U;
        LoRa_CompleteIsolation(LORA_OK);
        return;
    }

    if (!s_spi_abort_pending ||
        LoRa_Elapsed(now, s_spi_abort_start_ms, LORA_SPI_ABORT_TIMEOUT_MS))
        LoRa_CompleteIsolation(LoRa_ForceSpiQuiesce() ? LORA_OK : LORA_SPI_ERROR);
}

static uint8_t LoRa_ConsumeSpi(uint8_t *error)
{
    if (!s_spi_finished)
        return 0U;

    s_spi_finished = 0U;
    *error = s_spi_result_error;
    return 1U;
}

static void LoRa_CompleteOperation(LoRaStatus_t status)
{
    s_action_waiting = 0U;
    s_delay_active = 0U;
    s_last_status = status;
    s_state = (status == LORA_OK) ? LORA_STATE_IDLE : LORA_STATE_FAULT;
    s_driver_state = DRIVER_IDLE;
    if (status != LORA_OK)
    {
        s_ready = 0U;
        s_rx_active = 0U;
    }
}

static uint8_t LoRa_RunActions(const LoRaAction_t *actions, uint8_t count)
{
    const LoRaAction_t *action;
    uint8_t error;
    uint32_t now;

    if (s_action_waiting)
    {
        if (!LoRa_ConsumeSpi(&error))
            return 0U;
        s_action_waiting = 0U;
        if (error)
        {
            LoRa_SetFault(LORA_SPI_ERROR);
            return 0U;
        }
        action = &actions[s_action_index];
        if (action->type == LORA_ACTION_READ_VERIFY && s_spi_rx[1] != action->value)
        {
            LoRa_SetFault((action->address == REG_VERSION) ? LORA_VERSION_MISMATCH :
                                                            LORA_CONFIG_ERROR);
            return 0U;
        }
        s_action_index++;
        return 0U;
    }

    if (s_action_index >= count)
        return 1U;

    action = &actions[s_action_index];
    if (action->type == LORA_ACTION_DELAY)
    {
        now = HAL_GetTick();
        if (!s_delay_active)
        {
            s_deadline_ms = now;
            s_delay_active = 1U;
            return 0U;
        }
        if (!LoRa_Elapsed(now, s_deadline_ms, action->value))
            return 0U;
        s_delay_active = 0U;
        s_action_index++;
        return 0U;
    }

    if (action->type == LORA_ACTION_WRITE)
        error = (uint8_t)!LoRa_StartWrite(action->address, action->value);
    else if (action->type == LORA_ACTION_READ_VERIFY)
        error = (uint8_t)!LoRa_StartRead(action->address);
    else if (action->type == LORA_ACTION_TX_FIFO)
        error = (uint8_t)!LoRa_StartFifoWrite();
    else
        error = (uint8_t)!LoRa_StartWrite(action->address, s_tx_length);

    if (error)
    {
        LoRa_SetFault(LORA_SPI_ERROR);
        return 0U;
    }
    s_action_waiting = 1U;
    return 0U;
}

static void LoRa_ServiceInit(void)
{
    uint32_t now = HAL_GetTick();

    if (s_driver_state == DRIVER_INIT_RESET_LOW)
    {
        HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_RESET);
        s_deadline_ms = now;
        s_driver_state = DRIVER_INIT_RESET_SETTLE;
        return;
    }

    if (s_driver_state == DRIVER_INIT_RESET_SETTLE)
    {
        if (!LoRa_Elapsed(now, s_deadline_ms, LORA_RESET_LOW_MS))
            return;
        HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET);
        s_deadline_ms = now;
        s_driver_state = DRIVER_INIT_ACTIONS;
        return;
    }

    if (s_driver_state == DRIVER_INIT_ACTIONS)
    {
        if (!LoRa_Elapsed(now, s_deadline_ms, LORA_RESET_SETTLE_MS))
            return;
        if (!LoRa_RunActions(s_init_actions,
                             (uint8_t)(sizeof(s_init_actions) / sizeof(s_init_actions[0]))))
            return;
        s_ready = 1U;
        s_rx_active = 0U;
        LoRa_CompleteOperation(LORA_OK);
    }
}

static void LoRa_ServiceTx(void)
{
    uint8_t error;
    uint8_t irq;
    uint32_t now = HAL_GetTick();

    if (s_driver_state == DRIVER_TX_ACTIONS)
    {
        if (!LoRa_RunActions(s_tx_actions,
                             (uint8_t)(sizeof(s_tx_actions) / sizeof(s_tx_actions[0]))))
            return;
        s_tx_start_ms = now;
        s_driver_state = DRIVER_TX_POLL;
        return;
    }

    if (s_driver_state == DRIVER_TX_POLL)
    {
        if (s_action_waiting)
        {
            if (!LoRa_ConsumeSpi(&error))
                return;
            s_action_waiting = 0U;
            if (error)
            {
                LoRa_SetFault(LORA_SPI_ERROR);
                return;
            }
            irq = s_spi_rx[1];
            if ((irq & IRQ_TX_DONE) != 0U)
            {
                s_tx_completion_status = LORA_OK;
                s_action_index = 0U;
                s_driver_state = DRIVER_TX_FINISH;
                return;
            }
        }

        if (LoRa_Elapsed(now, s_tx_start_ms, s_deadline_ms))
        {
            s_tx_completion_status = LORA_TIMEOUT;
            s_action_index = 0U;
            s_driver_state = DRIVER_TX_FINISH;
            return;
        }

        if (!LoRa_StartRead(REG_IRQ_FLAGS))
        {
            LoRa_SetFault(LORA_SPI_ERROR);
            return;
        }
        s_action_waiting = 1U;
        return;
    }

    if (s_driver_state == DRIVER_TX_FINISH)
    {
        const LoRaAction_t *actions = (s_tx_completion_status == LORA_OK) ?
                                      s_tx_done_actions : s_tx_timeout_actions;
        uint8_t action_count = (s_tx_completion_status == LORA_OK) ?
                               (uint8_t)(sizeof(s_tx_done_actions) / sizeof(s_tx_done_actions[0])) :
                               (uint8_t)(sizeof(s_tx_timeout_actions) / sizeof(s_tx_timeout_actions[0]));
        if (!LoRa_RunActions(actions, action_count))
            return;
        s_rx_active = 0U;
        LoRa_CompleteOperation(s_tx_completion_status);
    }
}

static void LoRa_ServiceRx(void)
{
    uint8_t error;
    uint8_t irq;

    if (s_action_waiting)
    {
        if (!LoRa_ConsumeSpi(&error))
            return;
        s_action_waiting = 0U;
        if (error)
        {
            LoRa_SetFault(LORA_SPI_ERROR);
            return;
        }

        if (s_driver_state == DRIVER_RX_POLL)
        {
            irq = s_spi_rx[1];
            if ((irq & IRQ_RX_DONE) == 0U)
                return;
            if ((irq & IRQ_PAYLOAD_CRC_ERROR) != 0U)
            {
                s_rx_pending_status = LORA_RX_CRC_ERROR;
                if (!LoRa_StartWrite(REG_IRQ_FLAGS, 0xFFU))
                {
                    LoRa_SetFault(LORA_SPI_ERROR);
                    return;
                }
                s_action_waiting = 1U;
                s_driver_state = DRIVER_RX_CLEAR;
                return;
            }
            if (!LoRa_StartRead(REG_RX_NB_BYTES))
            {
                LoRa_SetFault(LORA_SPI_ERROR);
                return;
            }
            s_action_waiting = 1U;
            s_driver_state = DRIVER_RX_LENGTH;
            return;
        }

        if (s_driver_state == DRIVER_RX_LENGTH)
        {
            s_rx_length = s_spi_rx[1];
            if (s_rx_length == 0U)
            {
                s_rx_pending_status = LORA_LENGTH_ERROR;
                if (!LoRa_StartWrite(REG_IRQ_FLAGS, 0xFFU))
                {
                    LoRa_SetFault(LORA_SPI_ERROR);
                    return;
                }
                s_action_waiting = 1U;
                s_driver_state = DRIVER_RX_CLEAR;
                return;
            }
            if (!LoRa_StartRead(REG_FIFO_RX_CURRENT_ADDR))
            {
                LoRa_SetFault(LORA_SPI_ERROR);
                return;
            }
            s_action_waiting = 1U;
            s_driver_state = DRIVER_RX_ADDRESS;
            return;
        }

        if (s_driver_state == DRIVER_RX_ADDRESS)
        {
            if (!LoRa_StartWrite(REG_FIFO_ADDR_PTR, s_spi_rx[1]))
            {
                LoRa_SetFault(LORA_SPI_ERROR);
                return;
            }
            s_action_waiting = 1U;
            s_driver_state = DRIVER_RX_SET_POINTER;
            return;
        }

        if (s_driver_state == DRIVER_RX_SET_POINTER)
        {
            if (!LoRa_StartFifoRead())
            {
                LoRa_SetFault(LORA_SPI_ERROR);
                return;
            }
            s_action_waiting = 1U;
            s_driver_state = DRIVER_RX_FIFO;
            return;
        }

        if (s_driver_state == DRIVER_RX_FIFO)
        {
            memcpy(s_rx_data, &s_spi_rx[1], s_rx_length);
            s_rx_pending_length = s_rx_length;
            s_rx_pending_status = LORA_OK;
            if (!LoRa_StartWrite(REG_IRQ_FLAGS, 0xFFU))
            {
                LoRa_SetFault(LORA_SPI_ERROR);
                return;
            }
            s_action_waiting = 1U;
            s_driver_state = DRIVER_RX_CLEAR;
            return;
        }

        if (s_driver_state == DRIVER_RX_CLEAR)
        {
            s_driver_state = DRIVER_RX_POLL;
            return;
        }
    }

    if (s_driver_state == DRIVER_RX_POLL)
    {
        if (!LoRa_StartRead(REG_IRQ_FLAGS))
        {
            LoRa_SetFault(LORA_SPI_ERROR);
            return;
        }
        s_action_waiting = 1U;
    }
}

void SPI1_IRQHandler(void)
{
    HAL_SPI_IRQHandler(&hspi1);
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi == &hspi1)
        s_spi_complete = 1U;
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi == &hspi1)
    {
        s_spi_error = 1U;
        s_spi_complete = 1U;
    }
}

void HAL_SPI_AbortCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi == &hspi1)
        s_spi_abort_complete = 1U;
}

LoRaStatus_t LoRa_Init(void)
{
    if (s_spi_active || s_spi_abort_pending || LoRa_IsBusy())
        return LORA_BUSY;

    if (s_spi_reinit_required)
    {
        if (HAL_SPI_Init(&hspi1) != HAL_OK)
        {
            s_state = LORA_STATE_FAULT;
            s_last_status = LORA_SPI_ERROR;
            return LORA_SPI_ERROR;
        }
        s_spi_reinit_required = 0U;
    }

    HAL_NVIC_SetPriority(SPI1_IRQn, 5U, 0U);
    HAL_NVIC_EnableIRQ(SPI1_IRQn);
    LoRa_Deselect();
    s_ready = 0U;
    s_rx_active = 0U;
    s_action_index = 0U;
    s_action_waiting = 0U;
    s_delay_active = 0U;
    s_last_status = LORA_BUSY;
    s_state = LORA_STATE_INITIALISING;
    s_driver_state = DRIVER_INIT_RESET_LOW;
    return LORA_OK;
}

LoRaStatus_t LoRa_Send(const uint8_t *data, uint8_t length, uint32_t timeout_ms)
{
    if (data == NULL || length == 0U)
        return LORA_LENGTH_ERROR;
    if (LoRa_IsIsolated())
        return LORA_ISOLATED;
    if (!s_ready)
        return LORA_NOT_READY;
    if (LoRa_IsBusy())
        return LORA_BUSY;

    memcpy(s_tx_data, data, length);
    s_tx_length = length;
    s_deadline_ms = timeout_ms;
    s_action_index = 0U;
    s_action_waiting = 0U;
    s_rx_active = 0U;
    s_last_status = LORA_BUSY;
    s_state = LORA_STATE_TRANSMITTING;
    s_driver_state = DRIVER_TX_ACTIONS;
    return LORA_OK;
}

LoRaStatus_t LoRa_StartReceive(void)
{
    if (LoRa_IsIsolated())
        return LORA_ISOLATED;
    if (!s_ready)
        return LORA_NOT_READY;
    if (LoRa_IsBusy())
        return LORA_BUSY;

    s_action_index = 0U;
    s_action_waiting = 0U;
    s_rx_active = 0U;
    s_last_status = LORA_BUSY;
    s_state = LORA_STATE_STARTING_RX;
    s_driver_state = DRIVER_RX_START_ACTIONS;
    return LORA_OK;
}

void LoRa_Service(void)
{
    uint32_t now = HAL_GetTick();

    if (s_state == LORA_STATE_ISOLATING)
    {
        LoRa_ServiceIsolation(now);
        return;
    }

    if (s_spi_active)
    {
        if (!s_spi_abort_pending && LoRa_Elapsed(now, s_spi_start_ms, LORA_SPI_TIMEOUT_MS))
        {
            if (HAL_SPI_Abort_IT(&hspi1) == HAL_OK)
            {
                s_spi_abort_pending = 1U;
                s_spi_abort_start_ms = now;
            }
            else
            {
                (void)LoRa_ForceSpiQuiesce();
                s_spi_finished = 1U;
                s_spi_result_error = 1U;
            }
        }

        if (s_spi_abort_pending)
        {
            if (!s_spi_abort_complete &&
                !LoRa_Elapsed(now, s_spi_abort_start_ms, LORA_SPI_ABORT_TIMEOUT_MS))
                return;
            if (s_spi_abort_complete)
            {
                LoRa_Deselect();
                s_spi_abort_pending = 0U;
                s_spi_abort_complete = 0U;
                s_spi_active = 0U;
            }
            else
            {
                (void)LoRa_ForceSpiQuiesce();
            }
            s_spi_finished = 1U;
            s_spi_result_error = 1U;
        }
        else if (s_spi_complete)
        {
            LoRa_Deselect();
            s_spi_active = 0U;
            s_spi_complete = 0U;
            s_spi_finished = 1U;
            s_spi_result_error = s_spi_error;
            s_spi_error = 0U;
        }
        else
        {
            return;
        }
    }

    if (s_state == LORA_STATE_INITIALISING)
    {
        LoRa_ServiceInit();
        return;
    }
    if (s_state == LORA_STATE_TRANSMITTING)
    {
        LoRa_ServiceTx();
        return;
    }
    if (s_state == LORA_STATE_STARTING_RX)
    {
        if (LoRa_RunActions(s_rx_start_actions,
                            (uint8_t)(sizeof(s_rx_start_actions) / sizeof(s_rx_start_actions[0]))))
        {
            s_rx_active = 1U;
            LoRa_CompleteOperation(LORA_OK);
        }
        return;
    }
    if (s_state == LORA_STATE_IDLE && s_rx_active)
        LoRa_ServiceRx();
}

LoRaStatus_t LoRa_Receive(uint8_t *data, uint8_t *length, uint8_t capacity)
{
    LoRaStatus_t status = s_rx_pending_status;

    if (status == LORA_NO_PACKET)
        return LORA_NO_PACKET;
    if (status != LORA_OK)
    {
        s_rx_pending_status = LORA_NO_PACKET;
        return status;
    }
    if (length != NULL)
        *length = s_rx_pending_length;
    if (data == NULL || length == NULL || capacity < s_rx_pending_length)
    {
        s_rx_pending_status = LORA_NO_PACKET;
        return LORA_LENGTH_ERROR;
    }

    memcpy(data, s_rx_data, s_rx_pending_length);
    *length = s_rx_pending_length;
    s_rx_pending_status = LORA_NO_PACKET;
    return LORA_OK;
}

uint8_t LoRa_IsBusy(void)
{
    return (s_state == LORA_STATE_INITIALISING ||
            s_state == LORA_STATE_TRANSMITTING ||
            s_state == LORA_STATE_STARTING_RX ||
            s_state == LORA_STATE_ISOLATING) ? 1U : 0U;
}

uint8_t LoRa_IsReady(void)
{
    return s_ready;
}

uint8_t LoRa_IsRxActive(void)
{
    return s_rx_active;
}

uint8_t LoRa_IsIsolated(void)
{
    return (s_state == LORA_STATE_ISOLATED) ? 1U : 0U;
}

LoRaState_t LoRa_GetState(void)
{
    return s_state;
}

LoRaStatus_t LoRa_GetLastStatus(void)
{
    return s_last_status;
}

LoRaStatus_t LoRa_Isolate(void)
{
    if (LoRa_IsIsolated())
        return LORA_OK;

    if (s_state == LORA_STATE_ISOLATING)
        return LORA_BUSY;

    LoRa_Deselect();
    HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_RESET);
    s_ready = 0U;
    s_rx_active = 0U;
    s_action_waiting = 0U;
    s_delay_active = 0U;
    s_driver_state = DRIVER_IDLE;

    if (!s_spi_active)
    {
        LoRa_CompleteIsolation(LORA_OK);
        return LORA_OK;
    }

    s_state = LORA_STATE_ISOLATING;
    s_last_status = LORA_BUSY;
    if (!s_spi_abort_pending)
    {
        s_spi_abort_start_ms = HAL_GetTick();
        if (HAL_SPI_Abort_IT(&hspi1) == HAL_OK)
            s_spi_abort_pending = 1U;
    }
    return LORA_BUSY;
}
