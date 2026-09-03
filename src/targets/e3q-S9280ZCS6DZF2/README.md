# e3q-S9280ZCS6DZF2 target profile

Galaxy S24 Ultra China (`SM-S9280`) build `S9280ZCS6DZF2`, kernel
`6.1.145-android14-11-3254743-abS9280ZCS6DZF2`.

This is the **China (CHC)** variant of the S24 Ultra. The S928B/S928U1 DZF2
profiles target the international and US variants; this profile adds the
China-firmware offsets, which differ from the international kernel in the
`kmalloc_caches` and `"nfnetlink_log"` string locations.

Differences from `e3q-S928BXXS6DZF2` (international):

```c
#define KMALLOC_CACHES_OFF 0x0176cbb8ULL   /* CHC: 0x0176c6f8 on intl */
#define SLIDE_NFULNL_LOGGER_OFF 0x016a61b8ULL /* CHC: 0x016a622a on intl */
```

The P0 fingerprint table is generated from the exact China raw kernel image
and is distinct from the international table.

Build fingerprint:
`samsung/e3qsqw/e3q:14/UP1A.231005.007/S9280ZCS6DZF2:user/release-keys`.

This payload is hardware-validated: the exact `cve-2026-43499-app.so` shipped
in `artifacts/e3q-S9280ZCS6DZF2/` completed the full chain (KASLR slide
recovery, CFI fops slide, physical read/write, KernelSU late-load, granted
`su`) on an SM-S9280 running this firmware, with the exploit triggered from
the app domain (Shizuku mode).

Detail: `docs/SM-S9280-S9280ZCS6DZF2.md`. Use only on this exact firmware.
