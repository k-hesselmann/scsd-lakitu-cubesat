#ifndef FDIR_FDIR_HOOKS_H
#define FDIR_FDIR_HOOKS_H

#include <stdint.h>

/* Functions FDIR needs from subsystem owners, declared here so FDIR builds
 * without touching subsystem code. Weak default implementations live in
 * fdir_hooks.c and make the corresponding monitor inert; a subsystem owner
 * takes over by defining the strong symbol in their own module.
 *
 * See docs/FDIR_INTEGRATION.md for what each owner is asked to implement. */

/* TTC owner: consecutive failed LoRa transmit attempts, 0 after any success.
 * Weak default returns 0 (LoRa monitor inert, FMECA T1 not covered). */
uint8_t TTC_ConsecutiveTxFailures(void);

#endif /* FDIR_FDIR_HOOKS_H */
