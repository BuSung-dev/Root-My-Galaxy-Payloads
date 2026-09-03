#include "../q6q-F9560ZCU3DZDP-v5/target.h"

/* v6: q6q's kernel builds futex_hashsize from NR_CPUS=32 (0x2000),
 * not from the 16 CPUs currently online. Keep every other v5 parameter
 * unchanged so this is an auditable one-variable correction. */
#undef BUILD_VARIANT_LABEL
#define BUILD_VARIANT_LABEL "q6q-F9560ZCU3DZDP-app-production-v6-futex-0x2000"
#undef KERNELSNITCH_FUTEX_HASH_SIZE
#define KERNELSNITCH_FUTEX_HASH_SIZE 0x2000
