# SM-F766B / F766BXXSCBZH3 porting record

Device-tested temporary root + KernelSU (LKM jailbreak mode) on 2026-09-09.
Locked bootloader; no persistent boot.img modification. Re-establish per boot.

## Evidence

| Field | Value |
| --- | --- |
| Package model | `SM-F766B` (Galaxy Z Flip 7) |
| AP/PDA package | `F766BXXSCBZH3` |
| Device codename | `b7s` |
| Product name | `b7sxxx` |
| Android build ID | `BP4A.251205.006` |
| Build fingerprint | `samsung/b7sxxx/essi:16/BP4A.251205.006/F766BXXSCBZH3:user/release-keys` |
| Kernel release | `6.6.102-android15-8-abogkiF766BXXSCBZH3-4k` |
| SoC | Exynos 2500 |

## Kernel chain

| Object | Size (bytes) | SHA-256 |
| --- | ---: | --- |
| Raw kernel payload | 38,844,928 | `9269e1ca380ace587b1eafe196418150470c089a8202d934f6a512f75f04e344` |
| Recovered `vmlinux.elf` | 44,321,908 | `076dd96506fc342f1f6fca8972f2139ff90f73af3909c70f565d9eafcb1613e2` |

The raw kernel was extracted from the stock `boot.img` using the standard
header-version-4 page-aligned extraction procedure (see `docs/PORTING.md`).
The `vmlinux.elf` was recovered using `bootimg.py` with the `--unpack` flag
and the Exynos-specific ELF recovery path.

## Physical load address

```c
#define P0_PHYS_OFFSET       0x80000000ULL   /* vendor_boot DTB */
#define P0_KERNEL_PHYS_LOAD  0x80000000ULL   /* sboot.bin chain, Exynos BL */
```

Exynos device; the BL is `sboot.bin` (not UEFI). The load address is
`0x80000000` from the device tree, matching the Exynos 2500 memory layout.

## p0 fingerprint

Regenerated from this kernel's raw Image with
`tools/generate_p0_fingerprint.pl` (`PROBE_OFFSET=0x1f0000`). The p0
fingerprint table is stored in `src/targets/b7s-F766BXXSCBZH3/p0_fingerprint.h`
and is specific to this kernel.

## KernelSU late-load module

KernelSU v3.2.5 (commit `b0bc817b4e966aa6aa830834eaf6ef765d821d40`) patched
with the repository's Samsung KDP/RKP/DEFEX patch, built in the DDK container
`ghcr.io/ylarod/ddk-min:android15-6.6-20260313` with the generated release
replaced by the exact target release:

```text
vermagic: 6.6.102-android15-8-abogkiF766BXXSCBZH3-4k SMP preempt mod_unload modversions aarch64
```

`check_symbol` passes against the recovered F766B `vmlinux.elf`. The Samsung
no-patch-text path is enabled, so the module has no `stop_machine` import. The
module was stripped (`327,680` bytes), embedded in `ksud` as the
`android15-6.6_kernelsu.ko` asset, and `ksud` rebuilt with NDK.

| File | Size (bytes) | Purpose |
| --- | ---: | --- |
| `kernelsu/android15-6.6_kernelsu-b7s-F766BXXSCBZH3-kdp.ko` | 327,680 | exact-release no-patch-text module |
| `kernelsu/ksud-b7s-F766BXXSCBZH3-kdp` | 3,563,520 | late-load binary embedding the module |

```text
android15-6.6_kernelsu-b7s-F766BXXSCBZH3-kdp.ko
SHA-256 736268f626ea199362cac47523b2d030ac8569bd61eea232a381d389ad10dd20

ksud-b7s-F766BXXSCBZH3-kdp
SHA-256 67191b0d6b51e2dd0269fde322a8cad2bdbe6368f5f30604628bf49c9575338e
```

## Payload build

Built with Android NDK on Linux (clang `aarch64-linux-android35`, `-Oz`):

```text
artifacts/b7s-F766BXXSCBZH3/cve-2026-43499-app.so
size 125616
SHA-256 6bd824268d1923c60f5d86b20bc43b31d2febbdd04e6f3577a02da62c93808da
```

The kernel extraction + offset derivation procedure is in
[`docs/PORTING.md`](PORTING.md); this record only lists the device-specific
results.

## Support

Feed updated in `targets-v3.json` (payload `b7s-F766BXXSCBZH3`, models
`SM-F766B`, kernel `6.6.102`).

## Hardware validation

The exact F766B device reached bootstrap root on exploit attempt:

```text
root umh result wake=1 complete=1 retval=0 socket=1
[+] pipe physrw done=1 root=1 read_ok=1 write_ok=1 rw64=1/1
[+] exploit completed
```

The replacement no-patch-text `ksud` then loaded the embedded module without a
reboot. KernelSU logged `kernelsu.ko loaded successfully!` and entered
`u:r:ksu:s0`; `/proc/modules` reported `kernelsu` and the boot ID remained
unchanged. KernelSU Manager then reported `Working <LKM> [Jailbreak mode]`,
version `32525-2`, one superuser, the exact F766B kernel release, and SELinux
Enforcing. The root is still per-boot because no boot image was modified;
persistent boot survival remains untested.

### KernelSU re-establish

```sh
adb shell "/data/local/tmp/cve-2026-43499-root -c 'echo 1 > /proc/sys/kernel/kptr_restrict'"
adb shell "cp /data/local/tmp/ksud-b7s-F766BXXSCBZH3-kdp /data/local/tmp/.ksud-stage && chmod 755 /data/local/tmp/.ksud-stage"
adb shell "/data/local/tmp/cve-2026-43499-root --late-load"
```

`--late-load` is silent on success (it restores Enforcing, which disconnects
the `cve-2026-43499-root` client). Recreate `.ksud-stage` before every
`--late-load` (the loader renames it to `/data/adb/ksud`).

### Verification

```sh
cat /proc/modules | grep kernelsu
kernelsu 172032 4 - Live 0xffffffc0XXXXXXXX (O)
```
