#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include "stm32l4xx_hal.h"
#include <stdint.h>

typedef struct
{
    uint16_t queued_bytes;
    uint16_t queue_high_water;
    uint32_t enqueued_messages;
    uint32_t dropped_messages;
    uint32_t dropped_bytes;
    uint32_t tx_start_failures;
} DebugLogStats_t;

/* Non-blocking USART debug output. Each non-empty physical line is prefixed
 * with its enqueue-time HAL tick (`[t=0000000000ms] `), then copied into a
 * bounded queue and transmitted with UART interrupts. Debug congestion drops
 * complete messages instead of delaying the flight superloop. */
void DebugLog_Init(UART_HandleTypeDef *uart);
void DebugLog_Service(void);
void DebugLog_Write(const char *message);
void DebugLog_WriteN(const void *data, int32_t length);
void DebugLog_GetStats(DebugLogStats_t *stats);

#endif /* DEBUG_LOG_H */
