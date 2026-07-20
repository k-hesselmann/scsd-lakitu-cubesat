#include "cdh/datapool_nvm.h"

#include "fdir/crc16.h"
#include "main.h"

#include <stddef.h>
#include <string.h>

/*
 * One stored snapshot. Packed, so its size is exactly the sum of its fields
 * (104 B = an 8-byte multiple, no padding). The CRC covers everything before
 * crc16. The 16-bit seq makes each record self-ordering: it recovers the write
 * position and chronological order after a reset, when timestamp_ms restarts.
 */
typedef struct __attribute__((packed)) {
    uint16_t     magic;   /* DATAPOOL_NVM_MAGIC */
    uint16_t     seq;     /* monotonic, wraps at 65536 (span < ring size) */
    SensorData_t data;    /* full datapool snapshot */
    uint16_t     crc16;   /* CRC-16/CCITT over magic..data */
} DatapoolNvmRecord_t;

#define NVM_SLOT_SIZE      (((sizeof(DatapoolNvmRecord_t) + 7U) / 8U) * 8U)
#define NVM_SLOTS_PER_PAGE (FLASH_PAGE_SIZE / NVM_SLOT_SIZE)          /* 19 */
#define NVM_PAGE_COUNT     (DATAPOOL_NVM_SIZE / FLASH_PAGE_SIZE)      /* 40 */
#define NVM_SLOT_COUNT     (NVM_PAGE_COUNT * NVM_SLOTS_PER_PAGE)      /* 760 */

/* RAM-resident write cursor, recovered from flash at init. */
static uint32_t s_write_index;   /* next slot to program */
static uint16_t s_next_seq;      /* seq to stamp on the next record */
static uint32_t s_last_store_ms;

/* Absolute flash address of ring slot i. Slots never straddle a 2 KB page
 * (19 * 104 = 1976 <= 2048), so a page erase invalidates exactly its slots. */
static uint32_t NVM_SlotAddr(uint32_t i)
{
    uint32_t page = i / NVM_SLOTS_PER_PAGE;
    uint32_t slot = i % NVM_SLOTS_PER_PAGE;
    return DATAPOOL_NVM_ADDR + (page * FLASH_PAGE_SIZE) + (slot * NVM_SLOT_SIZE);
}

static uint8_t NVM_SlotValid(const DatapoolNvmRecord_t *slot)
{
    if (slot->magic != DATAPOOL_NVM_MAGIC)
        return 0U;

    return CRC16_Ccitt((const uint8_t *)slot, offsetof(DatapoolNvmRecord_t, crc16)) ==
           slot->crc16;
}

/* Erase the 2 KB page that contains slot i (called only when i is that page's
 * first slot, so the whole page is blanked before its slots are programmed). */
static void NVM_ErasePageOf(uint32_t i)
{
    uint32_t page_base = DATAPOOL_NVM_ADDR +
                         ((i / NVM_SLOTS_PER_PAGE) * FLASH_PAGE_SIZE);
    uint32_t absolute_page = (page_base - FLASH_BASE) / FLASH_PAGE_SIZE;
    FLASH_EraseInitTypeDef erase;
    uint32_t page_error = 0U;

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

static void NVM_ProgramSlot(uint32_t i, const DatapoolNvmRecord_t *rec)
{
    uint8_t buffer[NVM_SLOT_SIZE];
    uint32_t address = NVM_SlotAddr(i);

    memset(buffer, 0xFF, sizeof(buffer));
    memcpy(buffer, rec, sizeof(*rec));

    if (HAL_FLASH_Unlock() != HAL_OK)
        return;

    for (uint32_t w = 0U; w < (NVM_SLOT_SIZE / 8U); w++)
    {
        uint64_t word;
        memcpy(&word, &buffer[w * 8U], sizeof(word));
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                              address + (w * 8U), word) != HAL_OK)
            break;
    }

    (void)HAL_FLASH_Lock();
}

/* Locate the newest valid record (highest seq under 16-bit wrap-aware compare).
 * Returns its index, or -1 if the region holds no valid record. */
static int32_t NVM_FindNewest(uint16_t *seq_out)
{
    int32_t  best_index = -1;
    uint16_t best_seq   = 0U;

    for (uint32_t i = 0U; i < NVM_SLOT_COUNT; i++)
    {
        const DatapoolNvmRecord_t *slot =
            (const DatapoolNvmRecord_t *)NVM_SlotAddr(i);

        if (NVM_SlotValid(slot))
        {
            if (best_index < 0 ||
                (int16_t)(slot->seq - best_seq) > 0)
            {
                best_index = (int32_t)i;
                best_seq   = slot->seq;
            }
        }
    }

    if (seq_out != NULL)
        *seq_out = best_seq;
    return best_index;
}

void DatapoolNVM_Init(void)
{
    uint16_t newest_seq = 0U;
    int32_t  newest = NVM_FindNewest(&newest_seq);

    if (newest >= 0)
    {
        /* Resume the ring after the newest record. The slot at write_index is
         * guaranteed erased: a page is fully blanked when the cursor enters it,
         * so every slot past the last write in the current page is still 1s. */
        s_next_seq    = (uint16_t)(newest_seq + 1U);
        s_write_index = ((uint32_t)newest + 1U) % NVM_SLOT_COUNT;
    }
    else
    {
        s_next_seq    = 0U;
        s_write_index = 0U;
    }

    s_last_store_ms = HAL_GetTick();
}

void DatapoolNVM_Store(const SensorData_t *dp)
{
    DatapoolNvmRecord_t rec;
    uint32_t i = s_write_index;

    if (dp == NULL)
        return;

    rec.magic = DATAPOOL_NVM_MAGIC;
    rec.seq   = s_next_seq;
    rec.data  = *dp;
    rec.crc16 = CRC16_Ccitt((const uint8_t *)&rec,
                            offsetof(DatapoolNvmRecord_t, crc16));

    /* Entering a page: erase it once, reclaiming the oldest lap's 19 records. */
    if ((i % NVM_SLOTS_PER_PAGE) == 0U)
        NVM_ErasePageOf(i);

    NVM_ProgramSlot(i, &rec);

    s_next_seq++;
    s_write_index   = (i + 1U) % NVM_SLOT_COUNT;
    s_last_store_ms = HAL_GetTick();
}

void DatapoolNVM_Update(const SensorData_t *dp)
{
    /* Stop after the logging window: the region is sized to hold it without
     * wrapping, so this freezes a complete flight in flash. Manual
     * DatapoolNVM_Store() still works (e.g. a final snapshot on shutdown). */
    if (HAL_GetTick() >= DATAPOOL_NVM_STOP_MS)
        return;

    if ((uint32_t)(HAL_GetTick() - s_last_store_ms) >= DATAPOOL_NVM_PERIOD_MS)
        DatapoolNVM_Store(dp);
}

uint8_t DatapoolNVM_Restore(SensorData_t *dp)
{
    int32_t newest;

    if (dp == NULL)
        return 0U;

    newest = NVM_FindNewest(NULL);
    if (newest < 0)
        return 0U;

    *dp = ((const DatapoolNvmRecord_t *)NVM_SlotAddr((uint32_t)newest))->data;
    return 1U;
}
