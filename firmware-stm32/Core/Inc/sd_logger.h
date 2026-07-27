#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include "datapool.h"
#include "ff.h"
#include <stdint.h>

#define SD_LOG_ROTATE_SECONDS       60U
#define SD_LOG_FLUSH_PERIOD_MS    5000U
/* Rotation (close/open) is deferred to Coral_IsQuiescent() so it
 * doesn't stall the loop through an in-flight Coral transfer -- but if Coral
 * is disconnected/faulted and never goes idle, that deferral must not become
 * indefinite. Bounded fallback: rotate anyway once a pending rotation has
 * waited this long, regardless of Coral's state. */
#define SD_LOG_ROTATE_MAX_DEFER_MS 300000U   /* 5 min */
#define SD_LOGGER_OPEN_NAME_SIZE    96U

typedef enum
{
    SD_LOGGER_OFF = 0,
    SD_LOGGER_ACTIVE,
    SD_LOGGER_WAITING_RETRY
} SD_LoggerState_t;

typedef enum
{
    SD_LOG_OP_NONE = 0,
    SD_LOG_OP_CARD_INIT,
    SD_LOG_OP_MOUNT,
    SD_LOG_OP_OPEN,
    SD_LOG_OP_HEADER_WRITE,
    SD_LOG_OP_DATA_WRITE,
    SD_LOG_OP_SYNC,
    SD_LOG_OP_CLOSE,
    SD_LOG_OP_FORMAT_ROW
} SD_LoggerOperation_t;

extern volatile SD_LoggerState_t sd_logger_state;
extern volatile FRESULT sd_logger_last_error;
extern volatile uint32_t sd_logger_session;
extern volatile uint32_t sd_logger_rows_in_file;

typedef struct
{
    SD_LoggerState_t state;
    FRESULT last_error;
    uint8_t consecutive_faults;
    uint32_t last_success_ms;
    uint8_t file_open;
} SD_LoggerHealth_t;

typedef struct
{
    SD_LoggerState_t state;
    FRESULT last_error;
    SD_LoggerOperation_t last_operation;
    SD_LoggerOperation_t last_fault_operation;
    FRESULT last_fault_result;
    uint8_t consecutive_faults;
    uint8_t file_open;
    uint32_t total_faults;
    uint32_t last_fault_ms;
    uint32_t last_success_ms;
    uint32_t session;
    uint32_t rows_in_file;
    uint32_t buffer_used;
    uint32_t buffer_capacity;
    uint32_t total_bytes_committed;
    uint32_t successful_flushes;
    uint32_t last_flush_duration_ms;
    uint32_t max_flush_duration_ms;
    uint32_t recovery_attempts;
    uint32_t last_recovery_duration_ms;
    uint32_t rotation_count;
    uint32_t discarded_buffer_bytes;
    uint32_t last_requested_bytes;
    uint32_t last_written_bytes;
    char open_name[SD_LOGGER_OPEN_NAME_SIZE];
} SD_LoggerDiagnostics_t;

void SD_Logger_Init(SCV_t *scv);
void SD_Logger_Update(const SensorData_t *dp, SCV_t *scv);
void SD_Logger_Close(void);
void SD_Logger_GetHealth(SD_LoggerHealth_t *health);
void SD_Logger_GetDiagnostics(SD_LoggerDiagnostics_t *diagnostics);
const char *SD_Logger_StateName(SD_LoggerState_t state);
const char *SD_Logger_OperationName(SD_LoggerOperation_t operation);
const char *SD_Logger_ResultName(FRESULT result);

#endif /* SD_LOGGER_H */
