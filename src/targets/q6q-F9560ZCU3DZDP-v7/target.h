#include "../q6q-F9560ZCU3DZDP-v5/target.h"

/* v7: the live q6q CPU possible mask is 0-7, so futex_init allocates
 * 8 * 256 = 0x800 hash buckets. Keep every other v5 parameter unchanged. */
#undef BUILD_VARIANT_LABEL
#define BUILD_VARIANT_LABEL "q6q-F9560ZCU3DZDP-app-production-v7-futex-0x800"
#undef KERNELSNITCH_FUTEX_HASH_SIZE
#define KERNELSNITCH_FUTEX_HASH_SIZE 0x800
