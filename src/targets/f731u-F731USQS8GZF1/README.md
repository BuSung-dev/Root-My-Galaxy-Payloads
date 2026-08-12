# f731u-F731USQS8GZF1

```text
device: Samsung Galaxy Z Flip 5 (SM-F731U, q5q / US T-Mobile carrier-locked)
firmware: F731USQS8GZF1 / TMB
fingerprint: samsung/f731usqs8gzF1/f731u:13/TP1A.220624.014/F731USQS8GZF1:user/release-keys
kernel: 5.15.189-android13-8-33404244-abF731USQS8GZF1
bootloader: locked (US carrier model, no OEM unlock option)
```

`target.h` contains the exact symbol, layout, physical-load, trace, and KASLR
values recovered from that firmware. `p0_fingerprint.h` contains 32 target
kernel page fingerprints and is checked against all 256 source qwords during
the release verification.

## Verified working (2026-08-12)

This profile is **hardware-tested and working**: the app payload reaches
`temporary-root-ready` on the first attempt (attempt 1/24, zero reboots) on a
SM-F731U/DS running F731USQS8GZF1, and KernelSU loads successfully. The
temporary root survives until reboot (carrier-locked bootloader: persistent
KernelSU is not possible without unlocking).

## Porting notes (gotchas discovered on F731U)

- `mm_struct` slab object is actually `0x400` (1024) — `kmem_cache_create_usercopy`
  aligns `0x3e8` to `0x400`. BTF `sizeof=0x3e0` does NOT include slab alignment.
- `KMALLOC_CGROUP=1` and `NR_KMALLOC_TYPES=3` (common.h defaults 2/4 are wrong
  for this kernel) — wrong CGROUP_TYPE makes pipe-buffer cache gating fail.
- Boot cmdline has `arm64.nomte + id_aa64pfr1.mte=0` — kernel MTE is disabled,
  keep `MTE=0` defaults.
- 8 cores (Snapdragon 8 Gen 2): `futex_hashsize = roundup_pow2(256*8) = 0x800`.
- `KERNELSNITCH_VERBOSE` must stay 0 — verbose printf in the futex collision
  loop breaks the ns-scale timing side-channel and causes reboots.
- `P0_ATTEMPT_TIMEOUT_SEC` + `SLIDE_P0_OFFSET` env must be passed to the app
  (Shizuku) branch too — without them the write primitive is unreliable.

`target.h` and build artifacts verified: payload md5 `3c82d4f678bd58846facf3e4ad356a33`.
