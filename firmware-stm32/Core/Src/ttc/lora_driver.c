#include "ttc/lora_driver.h"

#include "main.h"

extern SPI_HandleTypeDef hspi1;

#define REG_FIFO              0x00U
#define REG_OP_MODE           0x01U
#define REG_FRF_MSB           0x06U
#define REG_FRF_MID           0x07U
#define REG_FRF_LSB           0x08U
#define REG_PA_CONFIG         0x09U
#define REG_LNA               0x0CU
#define REG_FIFO_ADDR_PTR     0x0DU
#define REG_FIFO_TX_BASE_ADDR 0x0EU
#define REG_FIFO_RX_BASE_ADDR 0x0FU
#define REG_IRQ_FLAGS         0x12U
#define REG_MODEM_CONFIG_1    0x1DU
#define REG_MODEM_CONFIG_2    0x1EU
#define REG_PREAMBLE_MSB      0x20U
#define REG_PREAMBLE_LSB      0x21U
#define REG_PAYLOAD_LENGTH    0x22U
#define REG_MODEM_CONFIG_3    0x26U
#define REG_SYNC_WORD         0x39U
#define REG_DIO_MAPPING_1     0x40U
#define REG_VERSION           0x42U

#define MODE_LONG_RANGE       0x80U
#define MODE_SLEEP            0x00U
#define MODE_STANDBY          0x01U
#define MODE_TX               0x03U
#define IRQ_TX_DONE           0x08U

#define LORA_FREQUENCY_HZ     868000000UL
#define LORA_SPI_TIMEOUT_MS   100U
#define LORA_EXPECTED_FRF_MSB 0xD9U
#define LORA_EXPECTED_FRF_MID 0x00U
#define LORA_EXPECTED_FRF_LSB 0x00U

#define LORA_OP_READ          1U
#define LORA_OP_WRITE         2U
#define LORA_OP_BURST_WRITE   3U

static LoRaDebugStatus_t s_debug = {0};

static void LoRa_RecordStatus(LoRaStatus_t status)
{
    s_debug.last_status = status;
    if (status == LORA_OK)
    {
        s_debug.last_failed_reg = 0U;
        s_debug.last_failed_op = 0U;
        s_debug.last_hal_status = HAL_OK;
        s_debug.spi_error_code = 0U;
    }
}

static LoRaStatus_t LoRa_RecordHalFailure(uint8_t address, uint8_t op,
                                          HAL_StatusTypeDef hal_status)
{
    s_debug.last_status = LORA_ERROR;
    s_debug.last_failed_reg = address;
    s_debug.last_failed_op = op;
    s_debug.last_hal_status = (uint32_t)hal_status;
    s_debug.spi_error_code = HAL_SPI_GetError(&hspi1);
    return LORA_ERROR;
}

static void LoRa_Select(void)
{
    HAL_GPIO_WritePin(LORA_CS_GPIO_Port, LORA_CS_Pin, GPIO_PIN_RESET);
}

static void LoRa_Deselect(void)
{
    HAL_GPIO_WritePin(LORA_CS_GPIO_Port, LORA_CS_Pin, GPIO_PIN_SET);
}

static uint8_t LoRa_ReadRegister(uint8_t address, LoRaStatus_t *status)
{
    uint8_t tx[2] = { (uint8_t)(address & 0x7FU), 0U };
    uint8_t rx[2] = { 0U, 0U };
    HAL_StatusTypeDef hal_status;

    LoRa_Select();
    hal_status = HAL_SPI_TransmitReceive(&hspi1, tx, rx, sizeof(tx), LORA_SPI_TIMEOUT_MS);
    if (hal_status != HAL_OK)
    {
        LoRa_Deselect();
        if (status != NULL)
            *status = LORA_ERROR;
        (void)LoRa_RecordHalFailure(address, LORA_OP_READ, hal_status);
        return 0U;
    }
    LoRa_Deselect();
    if (status != NULL)
        *status = LORA_OK;
    return rx[1];
}

static LoRaStatus_t LoRa_WriteRegister(uint8_t address, uint8_t value)
{
    uint8_t tx[2] = { (uint8_t)(address | 0x80U), value };
    HAL_StatusTypeDef hal_status;

    LoRa_Select();
    hal_status = HAL_SPI_Transmit(&hspi1, tx, sizeof(tx), LORA_SPI_TIMEOUT_MS);
    if (hal_status != HAL_OK)
    {
        LoRa_Deselect();
        return LoRa_RecordHalFailure(address, LORA_OP_WRITE, hal_status);
    }
    LoRa_Deselect();
    return LORA_OK;
}

static LoRaStatus_t LoRa_BurstWrite(uint8_t address, const uint8_t *data, uint8_t length)
{
    uint8_t command = (uint8_t)(address | 0x80U);
    HAL_StatusTypeDef hal_status;

    LoRa_Select();
    hal_status = HAL_SPI_Transmit(&hspi1, &command, 1U, LORA_SPI_TIMEOUT_MS);
    if (hal_status != HAL_OK)
    {
        LoRa_Deselect();
        return LoRa_RecordHalFailure(address, LORA_OP_BURST_WRITE, hal_status);
    }

    hal_status = HAL_SPI_Transmit(&hspi1, (uint8_t *)data, length, LORA_SPI_TIMEOUT_MS);
    if (hal_status != HAL_OK)
    {
        LoRa_Deselect();
        return LoRa_RecordHalFailure(address, LORA_OP_BURST_WRITE, hal_status);
    }
    LoRa_Deselect();
    return LORA_OK;
}

static LoRaStatus_t LoRa_SetMode(uint8_t mode)
{
    return LoRa_WriteRegister(REG_OP_MODE, (uint8_t)(MODE_LONG_RANGE | mode));
}

static LoRaStatus_t LoRa_CheckRegister(uint8_t address, uint8_t expected)
{
    LoRaStatus_t status = LORA_OK;
    uint8_t value = LoRa_ReadRegister(address, &status);

    if (status != LORA_OK)
        return status;

    if (value != expected)
    {
        s_debug.last_status = LORA_ERROR;
        s_debug.last_failed_reg = address;
        s_debug.last_failed_op = LORA_OP_READ;
        return LORA_ERROR;
    }

    return LORA_OK;
}

LoRaStatus_t LoRa_ReadDebugRegisters(void)
{
    LoRaStatus_t status = LORA_OK;

    s_debug.version = LoRa_ReadRegister(REG_VERSION, &status);
    if (status != LORA_OK)
        return status;
    s_debug.op_mode = LoRa_ReadRegister(REG_OP_MODE, &status);
    if (status != LORA_OK)
        return status;
    s_debug.irq_flags = LoRa_ReadRegister(REG_IRQ_FLAGS, &status);
    if (status != LORA_OK)
        return status;
    s_debug.frf_msb = LoRa_ReadRegister(REG_FRF_MSB, &status);
    if (status != LORA_OK)
        return status;
    s_debug.frf_mid = LoRa_ReadRegister(REG_FRF_MID, &status);
    if (status != LORA_OK)
        return status;
    s_debug.frf_lsb = LoRa_ReadRegister(REG_FRF_LSB, &status);
    if (status != LORA_OK)
        return status;
    s_debug.modem_config_1 = LoRa_ReadRegister(REG_MODEM_CONFIG_1, &status);
    if (status != LORA_OK)
        return status;
    s_debug.modem_config_2 = LoRa_ReadRegister(REG_MODEM_CONFIG_2, &status);
    if (status != LORA_OK)
        return status;
    s_debug.modem_config_3 = LoRa_ReadRegister(REG_MODEM_CONFIG_3, &status);
    if (status != LORA_OK)
        return status;
    s_debug.sync_word = LoRa_ReadRegister(REG_SYNC_WORD, &status);
    if (status != LORA_OK)
        return status;
    s_debug.payload_length = LoRa_ReadRegister(REG_PAYLOAD_LENGTH, &status);

    return status;
}

void LoRa_GetDebugStatus(LoRaDebugStatus_t *status)
{
    if (status == NULL)
        return;

    *status = s_debug;
}

LoRaStatus_t LoRa_Init(void)
{
    uint64_t frf;
    LoRaStatus_t status = LORA_OK;
    uint8_t version;

    LoRa_RecordStatus(LORA_OK);

    LoRa_Deselect();
    HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(10U);
    HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(20U);

    version = LoRa_ReadRegister(REG_VERSION, &status);
    s_debug.version = version;
    if (status != LORA_OK)
        return status;

    if (version != 0x12U)
    {
        s_debug.last_status = LORA_ERROR;
        s_debug.last_failed_reg = REG_VERSION;
        s_debug.last_failed_op = LORA_OP_READ;
        return LORA_ERROR;
    }

    if (LoRa_SetMode(MODE_SLEEP) != LORA_OK)
        return LORA_ERROR;
    HAL_Delay(10U);

    frf = ((uint64_t)LORA_FREQUENCY_HZ << 19) / 32000000ULL;
    if (LoRa_WriteRegister(REG_FRF_MSB, (uint8_t)(frf >> 16)) != LORA_OK ||
        LoRa_WriteRegister(REG_FRF_MID, (uint8_t)(frf >> 8)) != LORA_OK ||
        LoRa_WriteRegister(REG_FRF_LSB, (uint8_t)frf) != LORA_OK ||
        LoRa_WriteRegister(REG_FIFO_TX_BASE_ADDR, 0x00U) != LORA_OK ||
        LoRa_WriteRegister(REG_FIFO_RX_BASE_ADDR, 0x00U) != LORA_OK ||
        LoRa_WriteRegister(REG_FIFO_ADDR_PTR, 0x00U) != LORA_OK ||
        LoRa_WriteRegister(REG_MODEM_CONFIG_1, 0x72U) != LORA_OK ||
        LoRa_WriteRegister(REG_MODEM_CONFIG_2, 0x94U) != LORA_OK ||
        LoRa_WriteRegister(REG_MODEM_CONFIG_3, 0x04U) != LORA_OK ||
        LoRa_WriteRegister(REG_PREAMBLE_MSB, 0x00U) != LORA_OK ||
        LoRa_WriteRegister(REG_PREAMBLE_LSB, 0x08U) != LORA_OK ||
        LoRa_WriteRegister(REG_SYNC_WORD, 0x12U) != LORA_OK ||
        LoRa_WriteRegister(REG_PA_CONFIG, 0x8CU) != LORA_OK ||
        LoRa_WriteRegister(REG_IRQ_FLAGS, 0xFFU) != LORA_OK)
        return LORA_ERROR;

    if (LoRa_CheckRegister(REG_FRF_MSB, LORA_EXPECTED_FRF_MSB) != LORA_OK ||
        LoRa_CheckRegister(REG_FRF_MID, LORA_EXPECTED_FRF_MID) != LORA_OK ||
        LoRa_CheckRegister(REG_FRF_LSB, LORA_EXPECTED_FRF_LSB) != LORA_OK ||
        LoRa_CheckRegister(REG_MODEM_CONFIG_1, 0x72U) != LORA_OK ||
        LoRa_CheckRegister(REG_MODEM_CONFIG_2, 0x94U) != LORA_OK ||
        LoRa_CheckRegister(REG_MODEM_CONFIG_3, 0x04U) != LORA_OK ||
        LoRa_CheckRegister(REG_SYNC_WORD, 0x12U) != LORA_OK)
        return LORA_ERROR;

    status = LoRa_SetMode(MODE_STANDBY);
    if (status == LORA_OK)
    {
        (void)LoRa_ReadDebugRegisters();
        LoRa_RecordStatus(LORA_OK);
    }

    return status;
}

LoRaStatus_t LoRa_Send(const uint8_t *data, uint8_t length, uint32_t timeout_ms)
{
    uint32_t start;
    LoRaStatus_t status = LORA_OK;
    uint8_t irq_flags = 0U;

    if (data == NULL || length == 0U)
    {
        s_debug.last_status = LORA_LENGTH_ERROR;
        return LORA_LENGTH_ERROR;
    }

    if (LoRa_SetMode(MODE_STANDBY) != LORA_OK ||
        LoRa_WriteRegister(REG_DIO_MAPPING_1, 0x40U) != LORA_OK ||
        LoRa_WriteRegister(REG_IRQ_FLAGS, 0xFFU) != LORA_OK ||
        LoRa_WriteRegister(REG_FIFO_ADDR_PTR, 0x00U) != LORA_OK ||
        LoRa_BurstWrite(REG_FIFO, data, length) != LORA_OK ||
        LoRa_WriteRegister(REG_PAYLOAD_LENGTH, length) != LORA_OK ||
        LoRa_SetMode(MODE_TX) != LORA_OK)
        return LORA_ERROR;

    s_debug.payload_length = length;

    start = HAL_GetTick();
    while ((uint32_t)(HAL_GetTick() - start) < timeout_ms)
    {
        irq_flags = LoRa_ReadRegister(REG_IRQ_FLAGS, &status);
        s_debug.irq_flags = irq_flags;
        if ((irq_flags & IRQ_TX_DONE) != 0U)
        {
            (void)LoRa_WriteRegister(REG_IRQ_FLAGS, IRQ_TX_DONE);
            (void)LoRa_SetMode(MODE_STANDBY);
            (void)LoRa_ReadDebugRegisters();
            LoRa_RecordStatus(LORA_OK);
            return LORA_OK;
        }
        if (status != LORA_OK)
            break;
    }

    (void)LoRa_SetMode(MODE_STANDBY);
    (void)LoRa_ReadDebugRegisters();
    if (status == LORA_OK)
    {
        s_debug.last_status = LORA_TIMEOUT;
        return LORA_TIMEOUT;
    }

    return status;
}
