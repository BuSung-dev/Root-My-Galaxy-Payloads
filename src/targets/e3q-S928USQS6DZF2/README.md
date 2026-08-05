# e3q-S928USQS6DZF2 compatibility status

This directory contains the statically verified SM-S928U/S928U1 DZF2 target.

`target.h` uses offsets recovered from the exact DZF2 ELF/BTF and explicitly
selects the compact `rt_mutex_waiter` layout, including `wake_state`, packed
priority, and the trailing WW-context field. `p0_fingerprint.h` was generated
from the exact raw Image and all 256 source qwords were read back.

Input provenance, symbol uniqueness, decoded BTF type coverage, and the layout
compatibility decision are recorded in:

```text
analysis-s928-dzf/docs/E3Q_DZF2_COMPATIBILITY_AUDIT.md
Root-My-Galaxy-Payloads-main/docs/SM-S928U1-S928U1UES6DZF2.md
```

The sources pass host-Clang syntax checks. The Android release payload was
built twice with NDK r29 and reproduced byte-for-byte; its hash and the
matching KernelSU late-load artifacts are recorded in the target porting
record. The profile has not been run on hardware, so these build results are
not device validation and the target is not listed in the device-tested feed.
