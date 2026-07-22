#ifndef TTC_HOST_TEST_MAIN_H
#define TTC_HOST_TEST_MAIN_H

#include <stdint.h>

typedef struct { uint32_t unused; } SPI_HandleTypeDef;
typedef struct { uint32_t unused; } GPIO_TypeDef;
typedef int32_t IRQn_Type;

typedef enum {
    HAL_OK = 0,
    HAL_ERROR = 1,
    HAL_BUSY = 2,
    HAL_TIMEOUT = 3
} HAL_StatusTypeDef;

typedef enum {
    GPIO_PIN_RESET = 0,
    GPIO_PIN_SET
} GPIO_PinState;

#define SPI1_IRQn ((IRQn_Type)35)
#define GPIO_PIN_6 ((uint16_t)(1U << 6))
#define GPIO_PIN_7 ((uint16_t)(1U << 7))
#define GPIOB ((GPIO_TypeDef *)0x40020400UL)
#define GPIOC ((GPIO_TypeDef *)0x40020800UL)
#define LORA_CS_Pin GPIO_PIN_6
#define LORA_CS_GPIO_Port GPIOB
#define LORA_RST_Pin GPIO_PIN_7
#define LORA_RST_GPIO_Port GPIOC

uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_SPI_TransmitReceive_IT(
    SPI_HandleTypeDef *hspi,
    const uint8_t *tx,
    uint8_t *rx,
    uint16_t size
);
HAL_StatusTypeDef HAL_SPI_Abort_IT(SPI_HandleTypeDef *hspi);
HAL_StatusTypeDef HAL_SPI_DeInit(SPI_HandleTypeDef *hspi);
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *hspi);
void HAL_SPI_IRQHandler(SPI_HandleTypeDef *hspi);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
void HAL_NVIC_SetPriority(IRQn_Type irq, uint32_t preempt, uint32_t sub);
void HAL_NVIC_EnableIRQ(IRQn_Type irq);
void HAL_NVIC_DisableIRQ(IRQn_Type irq);

#endif
