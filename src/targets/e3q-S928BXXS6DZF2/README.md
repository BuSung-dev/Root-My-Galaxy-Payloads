# e3q-S928BXXS6DZF2 target profile

Galaxy S24 Ultra International (`SM-S928B`) build `S928BXXS6DZF2`, kernel
`6.1.145-android14-11-33419968-abS928BXXS6DZF2`.

The profile is derived from the device-tested `e3q-S928USQS6DZF2` profile.
The S928B kernel is byte-identical in every audited symbol and structure
layout; the only firmware-dependent difference is the `"nfnetlink_log"` string
offset used for KASLR slide recovery:

```c
#define SLIDE_NFULNL_LOGGER_OFF 0x016a622aULL
```

The P0 fingerprint table is byte-identical to the S928U1 DZF2 table.

Build fingerprint:
`samsung/e3qxxx/e3q:16/BP4A.251205.006/S928BXXS6DZF2:user/release-keys`.

KernelSU hardware check: the exact no-patch-text module loaded through the
target-specific `ksud` and survived without reboot. KernelSU Manager reported
`Working <LKM> [Jailbreak mode]`, version `32525-2`, and one superuser. The
root remains per-boot; no boot image was modified and reboot survival is not
claimed.

Detail: `docs/SM-S928B-S928BXXS6DZF2.md`. Use only on this exact firmware.
