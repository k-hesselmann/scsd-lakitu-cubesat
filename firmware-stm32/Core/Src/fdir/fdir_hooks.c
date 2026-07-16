#include "fdir/fdir_hooks.h"

/* Weak defaults so the firmware links before subsystem owners implement
 * their side of the FDIR interface (docs/FDIR_INTEGRATION.md). Each strong
 * definition in a subsystem module silently replaces the stub here. */

__attribute__((weak)) uint8_t TTC_ConsecutiveTxFailures(void)
{
    return 0U;   /* LoRa monitor inert until TTC reports send results */
}
