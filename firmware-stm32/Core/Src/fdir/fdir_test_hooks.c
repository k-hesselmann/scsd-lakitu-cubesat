#include "fdir/fdir_test_hooks.h"

#ifdef FDIR_TEST_HOOKS

#include "fdir/scv.h"
#include "main.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern UART_HandleTypeDef huart2;

/* Implausible-but-in-range constant: FMECA C4's plausibility check is an
 * accel-magnitude-range test, so this must sit outside any real flight
 * envelope (free fall ~0g .. burst/launch spikes a few g) without tripping
 * float format edge cases. */
#define TEST_HOOK_IMU_GARBAGE_G   15.0f

static uint8_t  s_imu_freeze_active;
static uint32_t s_imu_freeze_end_ms;

static uint8_t  s_adc_fault_active;
static uint32_t s_adc_fault_end_ms;

static uint8_t  s_baro_offset_active;
static float    s_baro_offset_m;

static char     s_line_buf[64];
static uint8_t  s_line_len;

static void hook_print(const char *msg)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)msg, (uint16_t)strlen(msg), 100);
}

static void hook_status(void)
{
    char buf[160];
    int len = snprintf(buf, sizeof(buf),
        "HOOK STATUS: imu_freeze=%d adc_fault=%d baro_offset=%s(%d.%02d m)\r\n",
        s_imu_freeze_active, s_adc_fault_active,
        s_baro_offset_active ? "on" : "off",
        (int)s_baro_offset_m,
        (int)((s_baro_offset_m < 0 ? -s_baro_offset_m : s_baro_offset_m) * 100) % 100);
    if (len > 0)
        HAL_UART_Transmit(&huart2, (uint8_t *)buf, (uint16_t)len, 100);
}

static void hook_clear(void)
{
    s_imu_freeze_active = 0U;
    s_adc_fault_active = 0U;
    s_baro_offset_active = 0U;
    s_baro_offset_m = 0.0f;
    hook_print("HOOK: all cleared\r\n");
}

/* Parses one already-received line (no trailing CR/LF) and applies the
 * requested hook. Unrecognised lines are echoed back with "?" so a typo at
 * the bench console is obvious immediately. */
static void hook_parse_line(char *line)
{
    char *save = NULL;
    char *tok0 = strtok_r(line, " \t", &save);
    if (tok0 == NULL || strcmp(tok0, "HOOK") != 0)
    {
        hook_print("? expected: HOOK <IMU|BARO|ADC|SCV|STATUS|CLEAR> ...\r\n");
        return;
    }

    char *tok1 = strtok_r(NULL, " \t", &save);
    if (tok1 == NULL)
    {
        hook_print("? missing subcommand\r\n");
        return;
    }

    if (strcmp(tok1, "STATUS") == 0)
    {
        hook_status();
    }
    else if (strcmp(tok1, "CLEAR") == 0)
    {
        hook_clear();
    }
    else if (strcmp(tok1, "SCV") == 0)
    {
        char *tok2 = strtok_r(NULL, " \t", &save);
        if (tok2 != NULL && strcmp(tok2, "ERASE") == 0)
        {
            SCV_Erase();
            hook_print("HOOK: SCV flash page erased (F3)\r\n");
        }
        else
        {
            hook_print("? usage: HOOK SCV ERASE\r\n");
        }
    }
    else if (strcmp(tok1, "IMU") == 0)
    {
        char *tok2 = strtok_r(NULL, " \t", &save);
        char *tok3 = strtok_r(NULL, " \t", &save);
        if (tok2 != NULL && strcmp(tok2, "FREEZE") == 0 && tok3 != NULL)
        {
            uint32_t secs = (uint32_t)strtoul(tok3, NULL, 10);
            s_imu_freeze_active = 1U;
            s_imu_freeze_end_ms = HAL_GetTick() + secs * 1000U;
            hook_print("HOOK: IMU garbage injection armed (C4)\r\n");
        }
        else
        {
            hook_print("? usage: HOOK IMU FREEZE <seconds>\r\n");
        }
    }
    else if (strcmp(tok1, "BARO") == 0)
    {
        char *tok2 = strtok_r(NULL, " \t", &save);
        char *tok3 = strtok_r(NULL, " \t", &save);
        if (tok2 != NULL && strcmp(tok2, "OFFSET") == 0 && tok3 != NULL)
        {
            s_baro_offset_m = (float)atof(tok3);
            s_baro_offset_active = 1U;
            hook_print("HOOK: baro offset injection armed (C6), HOOK CLEAR to stop\r\n");
        }
        else
        {
            hook_print("? usage: HOOK BARO OFFSET <meters>\r\n");
        }
    }
    else if (strcmp(tok1, "ADC") == 0)
    {
        char *tok2 = strtok_r(NULL, " \t", &save);
        char *tok3 = strtok_r(NULL, " \t", &save);
        if (tok2 != NULL && strcmp(tok2, "FAULT") == 0 && tok3 != NULL)
        {
            uint32_t secs = (uint32_t)strtoul(tok3, NULL, 10);
            s_adc_fault_active = 1U;
            s_adc_fault_end_ms = HAL_GetTick() + secs * 1000U;
            hook_print("HOOK: ADC fault injection armed (C8)\r\n");
        }
        else
        {
            hook_print("? usage: HOOK ADC FAULT <seconds>\r\n");
        }
    }
    else
    {
        hook_print("? unknown HOOK subcommand\r\n");
    }
}

/* Non-blocking single-byte poll (same pattern as coral.c's SOF scan: 0 ms
 * HAL timeout, HAL_TIMEOUT just means "nothing waiting this call"). Called
 * every superloop iteration so a multi-byte command line assembles across
 * several loop passes without ever blocking the loop. */
static void hook_poll_uart(void)
{
    uint8_t b;
    while (HAL_UART_Receive(&huart2, &b, 1U, 0U) == HAL_OK)
    {
        if (b == '\r' || b == '\n')
        {
            if (s_line_len > 0U)
            {
                s_line_buf[s_line_len] = '\0';
                hook_parse_line(s_line_buf);
                s_line_len = 0U;
            }
        }
        else if (s_line_len < sizeof(s_line_buf) - 1U)
        {
            s_line_buf[s_line_len++] = (char)b;
        }
        else
        {
            /* Line too long: drop it rather than parse garbage. */
            s_line_len = 0U;
        }
    }
}

void FDIR_TestHooks_Init(void)
{
    s_imu_freeze_active = 0U;
    s_adc_fault_active = 0U;
    s_baro_offset_active = 0U;
    s_baro_offset_m = 0.0f;
    s_line_len = 0U;
    hook_print("\r\nFDIR_TEST_HOOKS build - bench-only, HOOK STATUS for help\r\n");
}

void FDIR_TestHooks_Poll(SensorData_t *dp, SCV_t *scv)
{
    (void)scv;
    hook_poll_uart();

    uint32_t now_ms = HAL_GetTick();

    if (s_imu_freeze_active)
    {
        if (now_ms >= s_imu_freeze_end_ms)
        {
            s_imu_freeze_active = 0U;
        }
        else if (dp->imu_valid)
        {
            /* imu_valid stays 1: this is a plausibility fault (C4), not a
             * staleness fault - the device is "ACKing" throughout. */
            dp->imu_accel_x_g = TEST_HOOK_IMU_GARBAGE_G;
            dp->imu_accel_y_g = 0.0f;
            dp->imu_accel_z_g = 0.0f;
            dp->imu_accel_mag_g = TEST_HOOK_IMU_GARBAGE_G;
        }
    }

    if (s_baro_offset_active && dp->baro_valid)
    {
        dp->baro_alt_m += s_baro_offset_m;
    }

    if (s_adc_fault_active)
    {
        if (now_ms >= s_adc_fault_end_ms)
        {
            s_adc_fault_active = 0U;
        }
        else
        {
            dp->batt_valid = 0U;
        }
    }
}

#else /* !FDIR_TEST_HOOKS */

void FDIR_TestHooks_Init(void)
{
}

void FDIR_TestHooks_Poll(SensorData_t *dp, SCV_t *scv)
{
    (void)dp;
    (void)scv;
}

#endif /* FDIR_TEST_HOOKS */
