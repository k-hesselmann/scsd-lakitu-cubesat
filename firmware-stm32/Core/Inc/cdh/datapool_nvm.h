#ifndef DATAPOOL_NVM_H
#define DATAPOOL_NVM_H

#include "datapool.h"
#include <stdint.h>

/*
 * Datapool black-box: an internal-flash backup of the SD housekeeping log.
 *
 * The SD card is the primary 1 Hz housekeeping record; this module keeps a
 * second, independent copy of the datapool in on-chip flash so the data
 * survives an SD-card failure -- a missing, corrupted, or unwritable card. The
 * two paths share no hardware: the flash write touches only the CPU and
 * internal flash (no SPI, no card), so a usable history remains even when the
 * SD path is completely dead. The full datapool is stored unfiltered, so each
 * snapshot preserves the \c *_valid flags and \c timestamp_ms alongside the
 * sensor values; it is a coarser record than the SD log (see the interval
 * below), which is the price of fitting a wear-safe ring in on-chip flash.
 *
 * Storage is a circular log across a reserved 104 KB region (52 x 2 KB pages)
 * at the top of flash, sized to hold the whole logging window. Each snapshot
 * carries a monotonic sequence number and is appended to the next slot; a page
 * is erased once, when the write cursor first enters it on a lap -- the same
 * wear-friendly scheme the SCV uses (fdir/scv.c), one erase per page per lap
 * rather than one per write. The sequence number recovers the write position
 * and chronological order after a reset, when HAL_GetTick() (and thus
 * timestamp_ms) restarts from zero.
 *
 * Logging stops after DATAPOOL_NVM_STOP_MS (8 h) of run time: the region holds
 * more than 8 h, so the whole flight is captured from lift-off (launch and
 * ascent included) without the ring wrapping, then the contents are frozen.
 * The stop is on HAL_GetTick(), so a CPU reset restarts the 8 h window; the
 * ring itself is the fallback and would keep the most recent 8.2 h if the
 * timer never fired.
 *
 * Sizing
 * ------
 *   record   = magic(2) + seq(2) + SensorData_t(98) + CRC-16(2)  = 104 B
 *              (already an 8-byte multiple: no padding, 19 fit per 2 KB page)
 *   region   = 104 KB = 52 pages -> 52 x 19                      = 988 slots
 *   capacity = 988 x 30 s                                        = 8.2 h
 *   cut-off  = 8 h / 30 s                                        = 960 records
 *
 * 960 records fill 988 slots without wrapping (~14 min margin), so nothing is
 * lost even if the SD card fails at t=0. The ring fills once per flight, so
 * each of the 52 pages is erased about once per flight -- ~10 000 flights
 * against the guaranteed flash endurance -- and worst-case loss on an unclean
 * reset is one 30 s interval.
 */
#define DATAPOOL_NVM_PERIOD_MS   30000U
#define DATAPOOL_NVM_STOP_MS     28800000U   /* 8 h: stop logging after this */

/* Reserved region: the top 104 KB of flash, directly below the SCV page.
 * DP  occupies 0x080E5800..0x080FF7FF (52 pages, bank 2).
 * SCV occupies 0x080FF800..0x080FFFFF (1 page,   bank 2), immediately above.
 * Both are excluded from the firmware image by the reduced FLASH length in
 * STM32L476RGTX_FLASH.ld. */
#define DATAPOOL_NVM_ADDR        0x080E5800UL
#define DATAPOOL_NVM_SIZE        0x0001A000UL   /* 104 KB = 52 x 2 KB pages */
#define DATAPOOL_NVM_MAGIC       0xDA7AU

/* Scan the reserved page and prime the store interval. Call once at boot. */
void DatapoolNVM_Init(void);

/* Rate-limited: writes one snapshot every DATAPOOL_NVM_PERIOD_MS. Call each
 * 1 Hz CDH cycle with the freshly-updated datapool. */
void DatapoolNVM_Update(const SensorData_t *dp);

/* Force one snapshot immediately (e.g. on a detected fault or shutdown). */
void DatapoolNVM_Store(const SensorData_t *dp);

/* Load the newest valid snapshot into *dp. Returns 1 if one was found. */
uint8_t DatapoolNVM_Restore(SensorData_t *dp);

#endif /* DATAPOOL_NVM_H */
