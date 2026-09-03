#include "../q6q-F9560ZCU3DZDP-v12/target.h"

/* v13: diagnostic - read the per-CPU kstack_offset slot before each gate
 * trigger and log it, to correlate the CONFIG_RANDOMIZE_KSTACK_OFFSET
 * stack shift with the gate hit/miss pattern (S in {3, 5, 7}).
 *
 * The slot for cpu 0 (CORE) is at image offset 0xa208080
 * (symbol kstack_offset @ 0xffffffc00a208080); its linear alias is
 * P0_PAGE_OFFSET | 0x82288080 = 0xffffff8082288080 (phys 0x82288080,
 * inside the mapped zone), readable via the configfs primitive.
 */
#define APP_KSTACK_OFFSET_DIAG 1
#define KSTACK_OFFSET_SLOT_ALIAS \
  (P0_PAGE_OFFSET | (P0_KERNEL_PHYS_LOAD + 0xa208080ULL))
#undef BUILD_VARIANT_LABEL
#define BUILD_VARIANT_LABEL \
  "q6q-F9560ZCU3DZDP-app-production-v13-kstack-offset-diag"
