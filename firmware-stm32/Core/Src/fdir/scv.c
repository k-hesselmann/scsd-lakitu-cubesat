#include "fdir/scv.h"

#include "fdir/crc16.h"
#include "fsw/fsm.h"
#include "main.h"

#include <string.h>

/* Reserved page symbols provided by STM32L476RGTX_FLASH_SCV.ld. */
extern uint8_t __scv_flash_start__[];
extern uint8_t __scv_flash_size__[];

#define SCV_PAGE_BASE   ((uint32_t)__scv_flash_start__)
#define SCV_PAGE_SIZE   ((uint32_t)__scv_flash_size__)

/* Slot size: SCV_t rounded up to the L4's 8-byte (double-word) flash
 * programming granularity. 2048 / 32 = 64 backups per erase cycle. */
#define SCV_SLOT_SIZE   (((sizeof(SCV_t) + 7U) / 8U) * 8U)
#define SCV_SLOT_COUNT  (SCV_PAGE_SIZE / SCV_SLOT_SIZE)

static uint32_t s_mission_offset_ms;   /* mission ms accumulated before boot */
static uint32_t s_mission_epoch_ms;    /* tick when mission went active */
static uint8_t  s_mission_active;
static uint32_t s_last_backup_ms;
static uint8_t  s_last_phase;
static uint16_t s_last_faults;

/* Arm the mission clock on the STANDBY -> LAUNCH transition. Once active it
 * runs until power-off (per FMECA F4 decision); a reset mid-mission restores
 * the accumulated offset from flash via SCV_Load(). */
static void SCV_MissionTick(const SCV_t *scv)
{
    if (!s_mission_active && scv->flight_phase >= (uint8_t)PHASE_LAUNCH)
    {
        s_mission_active = 1U;
        s_mission_epoch_ms = HAL_GetTick();
    }
}

static const SCV_t *SCV_Slot(uint32_t index)
{
    return (const SCV_t *)(SCV_PAGE_BASE + (index * SCV_SLOT_SIZE));
}

static uint8_t SCV_SlotValid(const SCV_t *slot)
{
    if (slot->magic != SCV_MAGIC)
        return 0U;

    return CRC16_Ccitt((const uint8_t *)slot, offsetof(SCV_t, crc16)) ==
           slot->crc16;
}

static uint8_t SCV_SlotErased(uint32_t index)
{
    /* Erased L4 flash reads all-ones; checking the full slot (not just the
     * magic) guarantees the double-word program below never hits a
     * partially-written slot, which the ECC would fault on. */
    const uint64_t *words = (const uint64_t *)SCV_Slot(index);

    for (uint32_t i = 0U; i < (SCV_SLOT_SIZE / 8U); i++)
    {
        if (words[i] != UINT64_MAX)
            return 0U;
    }

    return 1U;
}

void SCV_Erase(void)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t page_error = 0U;
    uint32_t absolute_page = (SCV_PAGE_BASE - FLASH_BASE) / FLASH_PAGE_SIZE;

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.NbPages   = 1U;
    if (absolute_page >= 256U)
    {
        erase.Banks = FLASH_BANK_2;
        erase.Page  = absolute_page - 256U;
    }
    else
    {
        erase.Banks = FLASH_BANK_1;
        erase.Page  = absolute_page;
    }

    if (HAL_FLASH_Unlock() == HAL_OK)
    {
        (void)HAL_FLASHEx_Erase(&erase, &page_error);
        (void)HAL_FLASH_Lock();
    }
}

static void SCV_ProgramSlot(uint32_t index, const SCV_t *scv)
{
    uint8_t buffer[SCV_SLOT_SIZE];
    uint32_t address = SCV_PAGE_BASE + (index * SCV_SLOT_SIZE);

    memset(buffer, 0xFF, sizeof(buffer));
    memcpy(buffer, scv, sizeof(*scv));

    if (HAL_FLASH_Unlock() != HAL_OK)
        return;

    for (uint32_t i = 0U; i < (SCV_SLOT_SIZE / 8U); i++)
    {
        uint64_t word;
        memcpy(&word, &buffer[i * 8U], sizeof(word));
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                              address + (i * 8U), word) != HAL_OK)
            break;
    }

    (void)HAL_FLASH_Lock();
}

uint8_t SCV_Load(SCV_t *scv)
{
    uint8_t found = 0U;

#ifdef SCV_FORCE_RESET
    SCV_Erase();
#endif

    /* Slots are written in ascending order, so the newest valid record is
     * the highest-index one that passes magic + CRC. */
    for (uint32_t i = 0U; i < SCV_SLOT_COUNT; i++)
    {
        const SCV_t *slot = SCV_Slot(i);
        if (SCV_SlotValid(slot))
        {
            memcpy(scv, slot, sizeof(*scv));
            found = 1U;
        }
    }

    if (found && scv->flight_phase >= (uint8_t)PHASE_LAUNCH)
    {
        /* Mission already running before this reset: the accumulated time
         * resumes now; the outage between reset and boot is lost, which is
         * the best a tick-based clock can do. */
        s_mission_offset_ms = scv->mission_elapsed_ms;
        s_mission_epoch_ms = HAL_GetTick();
        s_mission_active = 1U;
    }

    return found;
}

uint32_t SCV_MissionElapsedMs(const SCV_t *scv)
{
    (void)scv;

    if (!s_mission_active)
        return 0U;

    return s_mission_offset_ms + (uint32_t)(HAL_GetTick() - s_mission_epoch_ms);
}

void SCV_Backup(SCV_t *scv)
{
    uint32_t slot = SCV_SLOT_COUNT;

    SCV_MissionTick(scv);
    scv->mission_elapsed_ms = SCV_MissionElapsedMs(scv);
    scv->magic = SCV_MAGIC;
    scv->crc16 = CRC16_Ccitt((const uint8_t *)scv, offsetof(SCV_t, crc16));

    for (uint32_t i = 0U; i < SCV_SLOT_COUNT; i++)
    {
        if (SCV_SlotErased(i))
        {
            slot = i;
            break;
        }
    }

    if (slot == SCV_SLOT_COUNT)
    {
        SCV_Erase();
        slot = 0U;
    }

    SCV_ProgramSlot(slot, scv);

    s_last_backup_ms = HAL_GetTick();
    s_last_phase = scv->flight_phase;
    s_last_faults = scv->equipment_faults;
}

void SCV_Update(SCV_t *scv)
{
    uint32_t now_ms = HAL_GetTick();
    uint8_t due;
    uint8_t dirty;

    SCV_MissionTick(scv);
    due = (uint32_t)(now_ms - s_last_backup_ms) >= SCV_BACKUP_PERIOD_MS;
    dirty = (scv->flight_phase != s_last_phase) ||
            (scv->equipment_faults != s_last_faults);

    if (due || dirty)
        SCV_Backup(scv);
}
