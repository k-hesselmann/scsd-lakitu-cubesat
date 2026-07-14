#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include "datapool.h"
#include "ff.h"
#include <stdint.h>

#define SD_LOG_ROTATE_SECONDS       60U
#define SD_LOG_SYNC_ROWS             1U
#define SD_LOG_REMOUNT_THRESHOLD     3U
#define SD_LOG_RETRY_DELAY_MS     5000U

typedef enum
{
    SD_LOGGER_OFF = 0,
    SD_LOGGER_ACTIVE,
    SD_LOGGER_WAITING_RETRY
} SD_LoggerState_t;

extern volatile SD_LoggerState_t sd_logger_state;
extern volatile FRESULT sd_logger_last_error;
extern volatile uint32_t sd_logger_session;
extern volatile uint32_t sd_logger_rows_in_file;

void SD_Logger_Init(SCV_t *scv);
void SD_Logger_Update(const SensorData_t *dp, SCV_t *scv);
void SD_Logger_Close(void);

#endif /* SD_LOGGER_H */
