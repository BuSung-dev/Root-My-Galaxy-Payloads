# SM-F9360 F9360ZCSAIZF1 validation

This profile was validated on a Chinese Galaxy Z Fold4 (`q4q`, SM8450 /
Snapdragon 8+ Gen 1) running the exact firmware and kernel below, with a
locked bootloader and no image flashed.

| Field | Value |
| --- | --- |
| Model | `SM-F9360` |
| Device | `q4q` |
| Firmware | `F9360ZCSAIZF1` |
| Android | 16 / BP2A.250605.031.A3 |
| Page size | 4096 |
| Kernel | `5.10.236-android12-9-2755199-abF9360ZCSAIZF1` |
| Toolchain | clang 12.0.5 (`r416183b` AOSP / focal 12.0.1) |

## Verification record

- **2026-08-12** — first full-chain success: bootstrap root via the
  LD_PRELOAD supervisor, KernelSU module late-loaded, 15/15 init marks,
  `su` returns `u:r:ksu:s0`, KernelSU Manager v3.2.5 reports the kernel.
- **2026-09-01** — re-verified after a reboot: attempt-1 success, root
  channel `uid=0 (kernel:s0)`, module `Live`, 15/15 init marks, Manager
  reports `Working <LKM> [Jailbreak mode]`, version `32525-2`.
- **2026-09-02** — Manager auto-updated to v3.3.0 (32601-2); module
  rebuilt with `KSU_VERSION=32601` from the same Samsung kernel tree
  (only the version constant changed), re-verified after reboot:
  attempt-1 success, `init_module` via `ksu-load.so` (manual relocation,
  201 symbols), Manager reports `Working <LKM> [Jailbreak mode]`,
  version `32601-2`, no version-mismatch warning.

Shell verification:

```text
$ su -c id
uid=0(root) gid=0(root) groups=0(root) context=u:r:ksu:s0

$ cat /proc/modules | grep kernelsu
kernelsu 147456 0 - Live 0xffffffc003646000 (O)
```

## What is different on this target (5.10 / SM8450)

- `CONFIG_LTO_CLANG_THIN` on the stock kernel produces a
  function-sections module layout (447 ALLOC sections) that panics on
  load — even a stub `init_module` panics. The published module is built
  with **LTO/CFI disabled** and the device's own compiler generation,
  giving a traditional unified `.text` layout that loads cleanly.
- `mod->init` resolves exclusively through the CFI jump-table slot
  `__cfi_jt_init_module`; the no-LTO build keeps that symbol, so the
  init actually runs.
- KernelSU v3.2.5 needs the Samsung KDP/RKP/DEFEX guards; the module
  uses the KDP task-scoped credential install path
  (`Samsung KDP task-scoped credential install` in dmesg).
- 201 undefined symbols are resolved by manual relocation against
  `/proc/kallsyms` (loader: manual-relocation constructor `.so`), since
  `TRIM_UNUSED_KSYMS` removed ~40 needed exports and
  `MODULE_FORCE_LOAD=n` blocks every forcing path.
- Futex hash size must be forced to 2048 (`futex_init` rounds over 8
  physical cores; the reported-online CPU count would wrongly suggest
  4096).
- SKB order-3 frag payload bias is `-0xe80` (linear head), verified on
  device.

## Published artifacts

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernelsu/android12-5.10_kernelsu-F9360ZCSAIZF1-c12-nolto.ko` | 5277472 | `e8ba3a6ea6082c5239aaaf4b1ed7e62195d96abdfe14350d9a7e1592354cefcb` |

The module has exact vermagic:

```text
5.10.236-android12-9-2755199-abF9360ZCSAIZF1 SMP preempt mod_unload modversions aarch64
```

The `.ko` is loaded via `init_module()` from a kernel-domain root process
obtained by the CVE-2026-43499 chain (LD_PRELOAD root-UMH route), not via
`ksud late-load`; the `201` UND symbols are relocated manually by the
loader before the syscall. A matching `ksud-F9360ZCSAIZF1-kdp` late-load
binary (embedding this module for the app route) is pending. A reboot
removes KernelSU and the bootstrap process must run again (~3 minutes,
scripted).

## App payload route: unproven on hardware (known gap)

The `APP_PAYLOAD` build (`cve-2026-43499-app.so` from this tree, fresh-P0
oracle route) was exercised from `adb shell` exactly like the
`gts9u-X916BXXS6EZG3` validation: with `SLIDE_KSNITCH_APPENDED_FUTEXES`
aligned to the proven 1024, attempts get past the pipe sizing and into
the KernelSnitch/reclaim stage, then the kernel panics and the device
reboots cleanly (verified boot intact, no side effects; log ends right
after the KernelSnitch profile line, consistent with a mid-reclaim
crash). The root-UMH supervisor route documented above is the proven
path on this device. Porting the app route to green (or producing the
`ksud-F9360ZCSAIZF1-kdp` late-load binary embedding the no-LTO module)
is left as follow-up work.

This profile is exact-build support; it does not claim compatibility
with other Z Fold4 models, firmware, or kernel releases.
