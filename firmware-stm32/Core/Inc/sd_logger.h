#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include "datapool.h"
#include "ff.h"
#include <stdint.h>

#define SD_LOG_ROTATE_SECONDS       60U
#define SD_LOG_FLUSH_PERIOD_MS    5000U

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

typedef struct
{
    SD_LoggerState_t state;
    FRESULT last_error;
    uint8_t consecutive_faults;
    uint32_t last_success_ms;
    uint8_t file_open;
} SD_LoggerHealth_t;

void SD_Logger_Init(SCV_t *scv);
void SD_Logger_Update(const SensorData_t *dp, SCV_t *scv);
void SD_Logger_Close(void);
void SD_Logger_GetHealth(SD_LoggerHealth_t *health);

#endif /* SD_LOGGER_H */
