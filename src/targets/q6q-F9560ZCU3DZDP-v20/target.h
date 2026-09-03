#include "../q6q-F9560ZCU3DZDP-v19/target.h"

/* v20: canonical Image addressing (kp18 fix) - retire the boot_id route.
 *
 * The v19 run reached the FOPS production trigger and the chain walked the
 * bank correctly, but the rb_erase marker store faulted with pgd=0 at
 * 0xffffff80824bb5b0 = data_addr(ASHMEM_MISC_FOPS) - the LINEAR ALIAS of
 * the ashmem_misc fops slot.  kp18 therefore proves on hardware that the
 * DIRECT MAP on q6q has the same hole as the vmemmap: phys
 * [0x80000000, 0x88000000) (the whole kernel Image span) is unmapped in
 * BOTH maps.  The v10 note claiming Image symbols >= 0x16a0000 are
 * "linearly readable" was never hardware-verified and is wrong: every
 * Image access must go through the CANONICAL (text) mapping, which the
 * kernel itself uses and which is read-write.
 *
 * This also kills the boot_id route for good: its marker write target is
 * the boot_id slot's linear alias (unmapped), and a canonical candidate
 * loop would fault on every wrong candidate - so APP_SLIDE_BOOTID_ROUTE is
 * undefined and the route removed from the build.  The tracefs slide (v19,
 * hardware-proven) remains the only slide route before the physical
 * fallback.
 *
 * v20 changes every Image .data consumer in the FOPS/root flow from
 * data_addr() (linear) to text_addr() (canonical):
 *   - the FOPS production marker target (slide_bank_targets[0]) and the
 *     production fd-set words (stack_pi_right, the production-stack check);
 *   - fake_right / write_right in the FOPS payload construction;
 *   - fops.c try_cfi_stage's misc_fops configfs read/write target;
 *   - root.c's selinux enforcing write target and the system_unbound_wq
 *     slot read (switched to configfs_read_once);
 *   - pipe.c's cache-gate reads of kmalloc_caches and the cgroup pipe slot
 *     (the configfs primitives, which those calls already use, work with
 *     canonical addresses).
 * APP_SLIDE_NOPHYS_ROUTE marks the shared non-physical-slide machinery
 * (the in-process slide flag for the fresh-P0-session gate) now used by
 * the tracefs route alone.
 */
#undef APP_SLIDE_BOOTID_ROUTE
#define APP_SLIDE_NOPHYS_ROUTE 1

#undef BUILD_VARIANT_LABEL
#define BUILD_VARIANT_LABEL \
  "q6q-F9560ZCU3DZDP-app-production-v20-canonical-fops"
