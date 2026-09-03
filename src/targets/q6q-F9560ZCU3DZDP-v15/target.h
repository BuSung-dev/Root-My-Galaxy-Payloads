#include "../q6q-F9560ZCU3DZDP-v14/target.h"

/* v15: fix the gate-miss root cause - the forged rt_mutex owner value.
 *
 * q6q rt_mutex_adjust_prio_chain (disassembled from vmlinux.elf) gates the
 * rb_erase marker write on the deadlock check:
 *
 *   0x1228e8: ldr x8, [x24, #0x18]   ; lock->owner
 *   0x1228ec: ldr x9, [sp, #0x8]     ; caller-frame slot (stale/irq-flags)
 *   0x1228f0: and x8, x8, #~1
 *   0x1228f4: cmp x8, x9
 *   0x1228f8: b.eq -> -EDEADLK exit (unlock, ret=-35, NO marker write)
 *
 * With SLIDE_LOCK_OWNER_VALUE=1 the compare is "0 == [sp+0x8]"; on most
 * boots the slot residue is 0, so every gate trigger took the -EDEADLK
 * exit and the marker never landed - the ~7% hit rate (hits = boots/runs
 * where the residue was nonzero, e.g. after the vendor-hook tracepoint
 * spill).  v15 writes the forged task-bank pointer (task | 1, bit 0 =
 * has-waiters) into lock->owner instead: the compare can never match, and
 * the chain's post-marker get_task_struct(owner & ~1) path then refcounts
 * the forged bank's usage field, which the payload already prepares.
 *
 * The payload change is in util.c prepare_skb_payload() (owner write);
 * this header only carries the new label.
 */
#undef BUILD_VARIANT_LABEL
#define BUILD_VARIANT_LABEL \
  "q6q-F9560ZCU3DZDP-app-production-v15-fake-owner-task"
