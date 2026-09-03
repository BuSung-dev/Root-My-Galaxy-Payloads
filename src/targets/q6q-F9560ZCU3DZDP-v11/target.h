#include "../q6q-F9560ZCU3DZDP-v10/target.h"

/* v11: move the probe into the zone core.
 *
 * The v10 probe page (image offset 0x2400000, phys 0x82480000) sits only
 * 0x20000 below the pvm_fw reserved region (0x824a0000) - the rb_erase
 * fault at its vmemmap struct page (kp9: fffffffe02092010 = pfn 0x82480
 * struct page + 0x10) suggests that phys range is not kernel-readable,
 * likely a memory-bank / hyp-carveout boundary.  v11 probes at image offset
 * 0x1e00000 (phys 0x81e80000), comfortably inside the zone core
 * (zone start_pfn 530144 -> phys 0x816e0000) and 0x47a0000 below pvm_fw.
 * The fingerprint table is regenerated for key range
 * [0x1c10000, 0x1e00000].
 */
#undef P0_ORACLE_PROBE_OFFSET
#define P0_ORACLE_PROBE_OFFSET 0x1e00000ULL
#undef P0_FINGERPRINT_HEADER
#define P0_FINGERPRINT_HEADER \
  "targets/q6q-F9560ZCU3DZDP-v11/p0_fingerprint.h"
#undef BUILD_VARIANT_LABEL
#define BUILD_VARIANT_LABEL \
  "q6q-F9560ZCU3DZDP-app-production-v11-zone-core-probe"
