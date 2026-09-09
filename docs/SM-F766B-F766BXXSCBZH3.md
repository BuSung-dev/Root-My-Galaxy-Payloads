# SM-F766B / F766BXXSCBZH3 port record

This record contains the exact inputs and derived values for the hardware-
validated Galaxy Z Flip 7 profile `b7s-F766BXXSCBZH3`. The device uses
Exynos 2500 with Samsung KDP/RKP/DEFEX, requiring the no-patch-text
KernelSU path.

## 1. Firmware identity

```text
model: SM-F766B (Galaxy Z Flip 7)
device: b7s
region / multi-CSC: ODM / OXM
AP/PDA: F766BXXSCBZH3
CSC: F766BODMBCZH3
display build: BP4A.251205.006.F766BXXSCBZH3
fingerprint: samsung/b7sxxx/essi:16/BP4A.251205.006/F766BXXSCBZH3:user/release-keys
SDK: 36
ABI: arm64-v8a
page size: 4096
kernel release: 6.6.102-android15-8-abogkiF766BXXSCBZH3-4k
SoC: Exynos 2500
```

This device uses the `abogki` GKI base, **not** the `pa3q` GKI of the
S25 Ultra (`SM-S938x`). The shared `galaxy-s25-series` payload is built from
`pa3q-S938NKSUACZF1` and does not work on this device.

## 2. Kernel chain

| Object | Size (bytes) | SHA-256 |
| --- | ---: | --- |
| Raw kernel payload | 38,844,928 | `9269e1ca380ace587b1eafe196418150470c089a8202d934f6a512f75f04e344` |
| Recovered `vmlinux.elf` | 44,321,908 | `076dd96506fc342f1f6fca8972f2139ff90f73af3909c70f565d9eafcb1613e2` |

The raw kernel was extracted from the stock `boot.img` using the standard
header-version-4 page-aligned extraction procedure (see `docs/PORTING.md`).
The `vmlinux.elf` was recovered using `vmlinux-to-elf` at image base
`0xffffffc008000000`.

## 3. Symbol and BTF recovery

`vmlinux-to-elf` recovered symbols at image base `0xffffffc008000000`.
Raw BTF was extracted from the kernel using the procedure in
[`PORTING.md`](PORTING.md). Exact member offsets below came from the
raw BTF, not the C declaration view.

## 4. Physical load proof

The raw ARM64 Image has `text_offset == 0`. In `sboot.bin`, the code
referencing `Starting kernel...` loads the Image text offset, adds
`0x80000000`, and branches to the resulting entry point. Therefore:

```c
#define P0_PHYS_OFFSET       0x80000000ULL
#define P0_KERNEL_PHYS_LOAD  0x80000000ULL
```

Exynos device; the BL is `sboot.bin` (not UEFI). The load address is
`0x80000000` from the device tree, matching the Exynos 2500 memory layout.

## 5. SLIDE_PSELECT_WORD_SHIFT

```c
#define SLIDE_PSELECT_WORD_SHIFT 0
```

Derived, not a guess. Non-LEGACY `rt_mutex_waiter` uses words 0-13; with the
global `PSELECT_ROUTE_NFDS=320` the logical fd_set array is 15 qwords, so
`SHIFT + 13 <= 14` ⇒ `SHIFT <= 1`. A value of 3 is impossible under `nfds=320`.

## 6. p0 fingerprint

Regenerated from this kernel's raw Image with
`tools/generate_p0_fingerprint.pl` (`PROBE_OFFSET=0x1f0000`). The p0
fingerprint table is stored in `src/targets/b7s-F766BXXSCBZH3/p0_fingerprint.h`
and is specific to this kernel. All 8 fingerprint rows were confirmed against
the live kernel.

## 7. KernelSU

Device-specific KernelSU module built with Samsung KDP/RKP/DEFEX patch
(no-patch-text path for Exynos EL2 safety). The generic `ksud-s25u-kdp`
does work, but a device-specific build is preferred for stability.

```text
vermagic: 6.6.102-android15-8-abogkiF766BXXSCBZH3-4k SMP preempt mod_unload modversions aarch64
```

The target config has:

```text
CONFIG_MODULES=y
CONFIG_MODVERSIONS=y
CONFIG_MODULE_SIG_ALL=y
# CONFIG_MODULE_SIG_FORCE is not set
# CONFIG_MODULE_FORCE_LOAD is not set
CONFIG_TRIM_UNUSED_KSYMS=y
CONFIG_KALLSYMS_ALL=y
CONFIG_KDP=y
CONFIG_RKP=y
CONFIG_SECURITY_DEFEX=y
```

For this Exynos target, the module is built with:

```text
CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y
```

That target flag makes KernelSU refuse `ksu_patch_text()` live writes instead
of entering `stop_machine`. The syscall-table dispatcher then falls through to
the Samsung RKP kretprobe/kprobe fallback, and selinux-hide slot patches fail
closed instead of triggering Samsung EL2.

The stripped standalone KO is:

```text
kernelsu/android15-6.6_kernelsu-b7s-F766BXXSCBZH3-kdp.ko
size: 327,464
SHA-256: 736268f626ea199362cac47523b2d030ac8569bd61eea232a381d389ad10dd20
```

Static checks passed:

```text
check_symbol: pass (exit 0)
```

The Android/AArch64 `ksud` binary embeds the B7S KO as
`android15-6.6_kernelsu.ko`:

```text
kernelsu/ksud-b7s-F766BXXSCBZH3-kdp
size: 3,483,696
SHA-256: 67191b0d6b51e2dd0269fde322a8cad2bdbe6368f5f30604628bf49c9575338e
```

Samsung DEFEX Safeplace blocks executing ksud from `/data/local/tmp/`; the
daemon's `--late-load` bypasses it by bind-mounting ksud over
`/system/bin/logcat` in a private mount namespace. KSU comes up in LKM
jailbreak mode (no `/sys/module/kernelsu`; verify via `/proc/modules` or the
KernelSU manager).

## 8. Payload build

Built with Android NDK on Linux (clang `aarch64-linux-android35`, `-Oz`):

```sh
make TARGET=b7s-F766BXXSCBZH3 \
  ANDROID_NDK_HOME=/path/to/android-ndk-r29 all release
```

The fixed-size result is published at

```text
artifacts/b7s-F766BXXSCBZH3/cve-2026-43499-app.so
size: 125,616
SHA-256: 6bd824268d1923c60f5d86b20bc43b31d2febbdd04e6f3577a02da62c93808da
```

The kernel extraction + offset derivation procedure is in
[`docs/PORTING.md`](PORTING.md); this record only lists the device-specific
results.

## 9. Support

Feed updated in `targets-v3.json` (payload `b7s-F766BXXSCBZH3`, models
`SM-F766B`, kernel `6.6.102`).

## 10. Hardware validation

The exact SM-F766B device reached bootstrap root on exploit attempt:

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

## 11. Scope

Verified only for `SM-F766B` / `F766BXXSCBZH3` (Exynos 2500, `abogki` GKI).
`targets-v3.json` lists only `SM-F766B` for this payload.

Both temp root and KSU are per-boot (locked bootloader; no persistent boot.img
modification). Re-establish after reboot: exploit (`--run-payload`) →
`kptr_restrict=1` → recreate `/data/local/tmp/.ksud-stage` → `--late-load`.
The KernelSU configuration under `/data/adb/ksu` survives reboots; only the
privilege itself must be re-acquired.
