#include "../q6q-F9560ZCU3DZDP-v15/target.h"

/* v16: fix kp14 - the gate-marker rb_erase write poisons the kmalloc-2k
 * freelist (see docs/SM-F9560-F9560ZCU3DZDP.md, kp14 analysis).
 *
 * kp14 was NOT caused by the v15 fake-task owner dance (all of its stores
 * land inside the payload fragment).  The corrupted value decodes exactly:
 *
 *   c->freelist = 0x68613c9e76a95121
 *              = parent ^ s->random ^ swab64(slot_addr)
 *   parent     = 0xfffffffe2906be00 = direct_to_page(leaked base)
 *              = the gate trigger's slide_oracle_parent
 *   slot_addr  = 0xffffff8a2c540800 = pipebuf_page_base + 0x800
 *              = the gate-marker target (slot 0 .page field of the ring)
 *
 * On q6q the pipe ring is a kmalloc-2k object at page offset +0x800
 * (KMALLOC_PIPE_INDEX=11), and kmalloc-2k has s->offset == 0x800, so the
 * freeptr slot of the page's object 0 sits exactly at +0x800 = the ring's
 * entry-0 .page field.  Whenever object 0 is free, the marker write
 * overwrites the live freelist link; the next kmalloc-2k allocation from
 * that slab decodes garbage and faults (kp14: ActivityManager seq_read ->
 * kvmalloc -> __kmalloc_node -> __kmem_cache_alloc_node).
 *
 * Fix: shift the gate marker to the ring's entry 1 (.page at +0x828) and
 * the probe marker to entry 2 (.page at +0x850); neither address lies on
 * any kmalloc freeptr grid (+0x800/+0x1000/... for the 2k cache).  The
 * gate pipes are filled with two marker pages and the gate verify reads
 * two pages, so the corrupted entry 1 is actually drained and scanned.
 * The verify's trailing one-page re-write then leaves entry 2 active,
 * which is exactly what the shifted probe target expects (same slot-advance
 * logic the old probe target +0x828 relied on after entry 0 was drained).
 *
 * The v15 owner change (lock->owner = fake_task | 1) is KEPT: its
 * refcount/pi_lock dance is payload-internal and it avoids the
 * wake_up_state(fake_task) path taken when owner <= 1.
 *
 * Also corrects the v15 header's gate theory: rt_mutex_adjust_pi calls
 * rt_mutex_adjust_prio_chain with 6 arguments (task, chwalk=0,
 * orig_lock=NULL, next_lock=waiter->lock, orig_waiter=NULL, top_task=task),
 * so the deadlock-gate slot [sp+0x8] is top_task (the real consumer task),
 * NOT a stale stack residue.  The gate can never fire for owner=1 or
 * fake_task|1; the clean misses come from the chain exiting early at
 * "waiter->lock != next_lock" (futex pi_state reclaim miss), and the ~7%
 * hit rate is the same reclaim choreography variance documented before.
 */
#define APP_P0_ORACLE_SLOT1_GATE 1

#undef BUILD_VARIANT_LABEL
#define BUILD_VARIANT_LABEL \
  "q6q-F9560ZCU3DZDP-app-production-v16-gate-entry1"
