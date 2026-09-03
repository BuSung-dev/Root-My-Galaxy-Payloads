#include "../q6q-F9560ZCU3DZDP-v11/target.h"

/* v12: diagnostic - sweep the pselect word shift per fresh attempt.
 *
 * The kp10 panic (gate trigger, attempt 2):
 *   Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
 *   PC is at _raw_spin_trylock+0x1c/0xa4
 * is the exact e2s-documented stale-waiter misalignment (waiter->lock read
 * as NULL).  Combined with the ~7% gate-hit rate, this suggests the q6q
 * hardware shift differs from the statically derived 3 on most attempts.
 * Each fresh attempt now tries one candidate shift in the order
 * {3, 2, 4, 1, 5, 0, 6, 7}; a gate hit logs the shift that worked.
 */
#define APP_PSELECT_SHIFT_SWEEP 1
#undef BUILD_VARIANT_LABEL
#define BUILD_VARIANT_LABEL \
  "q6q-F9560ZCU3DZDP-app-production-v12-shift-sweep"
