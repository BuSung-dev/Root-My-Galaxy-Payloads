#include <stdint.h>

#include "offset.h"

#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
/* same include the APP payload does from pipe.c */
#include P0_FINGERPRINT_HEADER
#endif


int header_check_main(void);
int header_check_main(void) {
  return (int)((KIMAGE_TEXT_BASE >> 61) ^ ASHMEM_IOCTL ^ INIT_TASK ^
               SELINUX_ENFORCING ^ COPY_SPLICE_READ ^
               SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_IMAGE ^
               (SLIDE_P0_OFFSET_CANDIDATES ? 0 : 1));
}

#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
/* fingerprint table must be well-formed and 32 rows */
int p0_rows_check = (int)(sizeof(p0_fingerprints) / sizeof(p0_fingerprints[0]));
_Static_assert(sizeof(p0_fingerprints) / sizeof(p0_fingerprints[0]) == 32,
               "p0 fingerprint must have 32 slide rows");
_Static_assert(P0_FINGERPRINT_WORDS == 8, "p0 fingerprint word count");
#endif

