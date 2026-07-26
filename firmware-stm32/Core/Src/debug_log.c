#include "debug_log.h"

#include <string.h>

#define DEBUG_LOG_QUEUE_SIZE 4096U
#define DEBUG_LOG_QUEUE_MASK (DEBUG_LOG_QUEUE_SIZE - 1U)
#define DEBUG_LOG_TIMESTAMP_PREFIX_SIZE 17U

_Static_assert((DEBUG_LOG_QUEUE_SIZE & DEBUG_LOG_QUEUE_MASK) == 0U,
               "debug queue size must be a power of two");

static UART_HandleTypeDef *s_uart;
static uint8_t s_queue[DEBUG_LOG_QUEUE_SIZE];
static volatile uint16_t s_head;
static volatile uint16_t s_tail;
static volatile uint16_t s_tx_length;
static volatile uint8_t s_tx_active;
static volatile uint16_t s_queue_high_water;
static volatile uint32_t s_enqueued_messages;
static volatile uint32_t s_dropped_messages;
static volatile uint32_t s_dropped_bytes;
static volatile uint32_t s_tx_start_failures;

static void DebugLog_FormatTimestampPrefix(
    uint32_t timestamp_ms,
    uint8_t prefix[DEBUG_LOG_TIMESTAMP_PREFIX_SIZE])
{
    prefix[0] = '[';
    prefix[1] = 't';
    prefix[2] = '=';
    for (int32_t index = 12; index >= 3; index--)
    {
        prefix[index] = (uint8_t)('0' + (timestamp_ms % 10U));
        timestamp_ms /= 10U;
    }
    prefix[13] = 'm';
    prefix[14] = 's';
    prefix[15] = ']';
    prefix[16] = ' ';
}

static uint16_t DebugLog_CountTimestampPrefixes(const uint8_t *bytes, int32_t length)
{
    uint16_t count = 0U;
    uint8_t line_has_text = 0U;

    for (int32_t index = 0; index < length; index++)
    {
        if (bytes[index] == '\n')
        {
            if (line_has_text)
                count++;
            line_has_text = 0U;
        }
        else if (bytes[index] != '\r')
        {
            line_has_text = 1U;
        }
    }
    if (line_has_text)
        count++;

    return count;
}

static uint8_t DebugLog_LineHasText(
    const uint8_t *bytes,
    int32_t start,
    int32_t end)
{
    for (int32_t index = start; index < end; index++)
    {
        if (bytes[index] != '\r' && bytes[index] != '\n')
            return 1U;
    }
    return 0U;
}

static void DebugLog_QueueByte(uint8_t byte)
{
    s_queue[s_head] = byte;
    s_head = (uint16_t)((s_head + 1U) & DEBUG_LOG_QUEUE_MASK);
}

static void DebugLog_QueueTimestampedMessage(
    const uint8_t *bytes,
    int32_t length,
    const uint8_t prefix[DEBUG_LOG_TIMESTAMP_PREFIX_SIZE])
{
    int32_t line_start = 0;

    while (line_start < length)
    {
        int32_t line_end = line_start;
        while (line_end < length && bytes[line_end] != '\n')
            line_end++;
        if (line_end < length)
            line_end++; /* Include the line-feed in this physical line. */

        if (DebugLog_LineHasText(bytes, line_start, line_end))
        {
            for (uint16_t index = 0U; index < DEBUG_LOG_TIMESTAMP_PREFIX_SIZE; index++)
                DebugLog_QueueByte(prefix[index]);
        }
        for (int32_t index = line_start; index < line_end; index++)
            DebugLog_QueueByte(bytes[index]);

        line_start = line_end;
    }
}

static uint16_t DebugLog_QueuedBytes(void)
{
    return (uint16_t)((s_head - s_tail) & DEBUG_LOG_QUEUE_MASK);
}

static uint32_t DebugLog_EnterCritical(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void DebugLog_ExitCritical(uint32_t primask)
{
    if (primask == 0U)
        __enable_irq();
}

void DebugLog_Init(UART_HandleTypeDef *uart)
{
    uint32_t primask = DebugLog_EnterCritical();

    s_uart = uart;
    s_head = 0U;
    s_tail = 0U;
    s_tx_length = 0U;
    s_tx_active = 0U;
    s_queue_high_water = 0U;
    s_enqueued_messages = 0U;
    s_dropped_messages = 0U;
    s_dropped_bytes = 0U;
    s_tx_start_failures = 0U;

    DebugLog_ExitCritical(primask);
}

void DebugLog_Service(void)
{
    uint16_t contiguous;
    HAL_StatusTypeDef status;
    uint32_t primask;

    if (s_uart == NULL)
        return;

    primask = DebugLog_EnterCritical();
    if (s_tx_active || s_head == s_tail)
    {
        DebugLog_ExitCritical(primask);
        return;
    }

    contiguous = (s_head > s_tail) ? (uint16_t)(s_head - s_tail) :
                                     (uint16_t)(DEBUG_LOG_QUEUE_SIZE - s_tail);
    s_tx_length = contiguous;
    s_tx_active = 1U;
    status = HAL_UART_Transmit_IT(s_uart, &s_queue[s_tail], contiguous);
    if (status != HAL_OK)
    {
        s_tx_active = 0U;
        s_tx_length = 0U;
        s_tx_start_failures++;
    }
    DebugLog_ExitCritical(primask);
}

void DebugLog_Write(const char *message)
{
    if (message != NULL)
        DebugLog_WriteN(message, (int32_t)strlen(message));
}

void DebugLog_WriteN(const void *data, int32_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint8_t timestamp_prefix[DEBUG_LOG_TIMESTAMP_PREFIX_SIZE];
    uint16_t prefix_count;
    uint32_t expanded_length;
    uint16_t queued;
    uint16_t free_bytes;
    uint16_t new_queued;
    uint32_t primask;

    if (bytes == NULL || length <= 0)
        return;

    /* No message this large could fit in the queue even without prefixes.
     * Reject it before scanning caller memory or doing length arithmetic. */
    if ((uint32_t)length >= DEBUG_LOG_QUEUE_SIZE)
    {
        primask = DebugLog_EnterCritical();
        s_dropped_messages++;
        s_dropped_bytes += (uint32_t)length;
        DebugLog_ExitCritical(primask);
        return;
    }

    /* Capture time before formatting/capacity work: this is the event enqueue
     * time, independent of when the interrupt-driven UART drains the queue. */
    DebugLog_FormatTimestampPrefix(HAL_GetTick(), timestamp_prefix);
    prefix_count = DebugLog_CountTimestampPrefixes(bytes, length);
    expanded_length = (uint32_t)length +
                      ((uint32_t)prefix_count * DEBUG_LOG_TIMESTAMP_PREFIX_SIZE);

    primask = DebugLog_EnterCritical();
    queued = DebugLog_QueuedBytes();
    free_bytes = (uint16_t)(DEBUG_LOG_QUEUE_SIZE - 1U - queued);
    if (expanded_length > free_bytes)
    {
        s_dropped_messages++;
        s_dropped_bytes += expanded_length;
        DebugLog_ExitCritical(primask);
        return;
    }

    DebugLog_QueueTimestampedMessage(bytes, length, timestamp_prefix);
    s_enqueued_messages++;
    new_queued = DebugLog_QueuedBytes();
    if (new_queued > s_queue_high_water)
        s_queue_high_water = new_queued;
    DebugLog_ExitCritical(primask);

    DebugLog_Service();
}

void DebugLog_GetStats(DebugLogStats_t *stats)
{
    uint32_t primask;

    if (stats == NULL)
        return;

    primask = DebugLog_EnterCritical();
    stats->queued_bytes = DebugLog_QueuedBytes();
    stats->queue_high_water = s_queue_high_water;
    stats->enqueued_messages = s_enqueued_messages;
    stats->dropped_messages = s_dropped_messages;
    stats->dropped_bytes = s_dropped_bytes;
    stats->tx_start_failures = s_tx_start_failures;
    DebugLog_ExitCritical(primask);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart)
{
    if (s_uart == NULL || uart == NULL || uart->Instance != s_uart->Instance)
        return;

    s_tail = (uint16_t)((s_tail + s_tx_length) & DEBUG_LOG_QUEUE_MASK);
    s_tx_length = 0U;
    s_tx_active = 0U;
}
