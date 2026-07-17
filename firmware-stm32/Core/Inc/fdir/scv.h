#ifndef FDIR_SCV_H
#define FDIR_SCV_H

#include "datapool.h"

/* SCV flash persistence — owned by FDIR.
 *
 * One RAM working copy (g_scv) + one reserved 2 KB flash page (bank 2, last
 * page, see STM32L476RGTX_FLASH_SCV.ld) as backup. Records are appended into
 * fixed-size slots so the page is erased only when full (wear limiting);
 * SCV_Load() restores the newest valid record (magic + CRC16).
 *
 * crc16 is computed exactly once per backup, here, immediately before
 * programming — subsystems that modify the RAM copy never touch it.
 *
 * The page lies outside the firmware image, so the SCV persists across
 * reflashing by default. Reset is explicit only: build with -DSCV_FORCE_RESET
 * to erase it at the next boot, or call SCV_Reset() (e.g. from a debug hook).
 */

#define SCV_BACKUP_PERIOD_MS  60000U

/* Restore the newest valid record into *scv. Returns 1 on success, 0 if the
 * page holds no valid record (caller initialises defaults). Also derives the
 * mission-time boot offset from the restored record. */
uint8_t SCV_Load(SCV_t *scv);

/* Refresh mission_elapsed_ms, compute crc16 and append *scv to flash now. */
void SCV_Backup(SCV_t *scv);

/* Periodic call from the superloop: backs up every SCV_BACKUP_PERIOD_MS, or
 * immediately when flight_phase or equipment_faults changed since the last
 * backup. */
void SCV_Update(SCV_t *scv);

/* Erase the SCV flash page (explicit reset). The RAM copy is untouched;
 * the caller decides whether to re-initialise defaults and/or re-backup. */
void SCV_Erase(void);

/* Mission time: offset restored at SCV_Load + time since boot, accumulating
 * from PHASE_LAUNCH until power-off. Returns 0 while in PHASE_STANDBY. */
uint32_t SCV_MissionElapsedMs(const SCV_t *scv);

#endif /* FDIR_SCV_H */
