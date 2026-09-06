# SM-S906B S906BXXSNGZD7 validation

This profile ports the IonStack CVE-2026-43499 exploit to the Galaxy S22+
Exynos (g0s) on the GZD7 firmware. It is based on the S22 Ultra (b0s)
`S908BXXSMGZB2` work and reuses the S22 (r0s) `S901BXXSNGZD7` KernelSU
KDP pair.

| Field | Value |
| --- | --- |
| Model | `SM-S906B` |
| Device | `g0s` |
| Firmware | `S906BXXSNGZD7` |
| Android | 16 / API 36 |
| Page size | 4096 |
| Kernel | `5.10.237-android12-9-31999025-abS906BXXSNGZD7` |
| SoC | Exynos 2200 |
| Build fingerprint | `samsung/g0sxxx/essi:16/BP2A.250605.031.A3/S906BXXSNGZD7:user/release-keys` |

## Root chain (device-verified, incl. through the app)

The payload ran from the application domain via `LD_PRELOAD`, found the
virtual KASLR slide through the tracefs caller leak, leaked an `mm_struct`
with KernelSnitch, reclaimed the target page through order-3 SKB pressure,
ran the 32-bit `exp32` futex/`setsockopt` stage to install the fake fops,
entered the CFI stage, scanned physical slots, matched the leaked `mm` page
(`PHYS_SLOT_MATCH`), and established the pipe physical read/write primitive.

The full chain has been confirmed end-to-end both from an ADB shell and
through the Root My Galaxy app (Shizuku mode), where the app's unprivileged
entry drives the exploit through to the KernelSU late-load.

From there the exploit:

1. clears `selinux_state.enforcing` (`+0x02258d58`, the runtime byte verified
   via `sel_write_enforce`'s `strb`), and
2. clears the four Samsung DEFEX per-feature runtime status bytes via the
   same pipe primitive (see [DEFEX bypass](#samsung-defex-bypass)),

then queues a `call_usermodehelper` work item on `system_unbound_wq` that
executes `/data/local/tmp/cve-2026-43499-root --umh`. The UMH-spawned root
helper drops to `u:r:kernel:s0` uid 0 and serves the `temp_su.sock` su
bridge.

A full run with `EXPLOIT_ATTEMPTS=24` achieved root on attempt 1:

```text
[+] [cfi-trace6-physbase] PHYS_SLOT_MATCH slot=15 phys_slide=00078000 ...
[+] pipe physrw ... read_ok=1 write_ok=1 rw64=1/1 uid=2000->0
[+] exploit completed attempt=1/24
```

```text
$ /data/local/tmp/cve-2026-43499-root -c 'id; whoami; getenforce'
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
root
Permissive
```

## Samsung DEFEX bypass

Samsung DEFEX (Safeplace/Privesc/Integrity/Immutable) is independent of
SELinux, so setting SELinux `Permissive` alone does not allow the root
shell to exec binaries from `/data`. Without DEFEX disabled, `ksud
late-load` is killed on exec:

```text
[DEFEX] Safeplace violation [task=sh (/system/bin/sh), child=/data/local/tmp/ksud, uid=0]
```

`defex_get_features` (`0xffffffc00865ea88`) derives the feature mask from
two runtime bytes; the per-feature gates are separate globals. The exploit
zeros all four through the pipe write primitive, right after the SELinux
enforcing write:

| Symbol | Static VA | Offset from `KIMAGE_TEXT_BASE` |
| --- | --- | --- |
| `global_privesc_status` | `0xffffffc00a022158` | `0x02022158` |
| `global_safeplace_status` | `0xffffffc00a02215c` | `0x0202215c` |
| `global_integrity_status` | `0xffffffc00a022160` | `0x02022160` |
| `global_immutable_status` | `0xffffffc00a022164` | `0x02022164` |

Each is a `u8` (`0`=off, `1`=partial, `2`=full). With all four cleared the
UMH-spawned root shell can exec `/data/local/tmp/ksud` for the KernelSU
late-load. The expected log lines are:

```text
[*] root umh selinux enforcing byte 1 -> 0 (addr=ffffff80022d0d58)
[*] root umh defex[0] 2 -> 0 (addr=...) wr=1
[*] root umh defex[1] 2 -> 0 (addr=...) wr=1
[*] root umh defex[2] 2 -> 0 (addr=...) wr=1
[*] root umh defex[3] 2 -> 0 (addr=...) wr=1
```

## KernelSU

The KernelSU pair is reused verbatim from the S22 (r0s) `S901BXXSNGZD7` build:
same `android12-5.10` KMI, same Samsung KDP/RKP/DEFEX source patch set, and
the same kallsyms-aware late-load `ksud`. The S22+ GZD7 kernel is the same
family as the S22 GZD7 kernel, so the module's hardcoded struct layouts and
symbol semantics match. No device-specific KernelSU module build is required.

The `ksud late-load` is run from the root shell after the exploit completes
(per-boot; no reboot in between, since the DEFEX disable and the root daemon
are volatile). KernelSU Manager then reports
`Working <LKM> [Jailbreak mode]`, version `32525-2` (confirmed on-device).

## Kernel image layout

| Constant | Value | Source |
| --- | --- | --- |
| `KIMAGE_TEXT_BASE` | `0xffffffc008000000` | static kernel text base |
| `P0_PAGE_OFFSET` | `0xffffff8000000000` | 39-bit VA `PAGE_OFFSET` |
| `P0_PHYS_OFFSET` | `0x80000000` | `memstart_addr` |
| `P0_KERNEL_PHYS_LOAD` | `0x80000000` | GZD7 sboot pre-slide base |

`P0_KERNEL_PHYS_LOAD = 0x80000000` was determined from sboot analysis (the
qword `0x80000000` appears 30 times; `0xa8000000` appears zero times) plus
`text_offset=0` from the `Image` header, consistent with the S901B GZD7
notes in the same firmware family.

The kernel is KCFI (`CONFIG_CFI_CLANG=y`, `CONFIG_CFI_CLANG_SHADOW=y`) with
an empty `__cfi_jt` table (`__cfi_jt_start == __cfi_jt_end`), so the
generator's `_JT_OFF` values are raw function addresses, not jump-table
offsets.

## Source layout

The IonStack exploit source is a self-contained tree under
`src/targets/g0s-S906BXXSNGZD7/` (Makefile, `src/`, `target_generator/`).
The device-specific offsets live in
`src/targets/S906BXXSNGZD7/target.h`, generated from the GZD7 kernel
`Image`/`vmlinux` via `target_generator/generate_target.py` and then
patched with the S906B identity, fingerprint, build variant, and the
DEFEX global offsets.

Build (NDK 28+, from the target directory):

```sh
make all PROJECT=S906BXXSNGZD7 ANDROID_NDK_HOME=/path/to/android-ndk
```

Outputs `build/S906BXXSNGZD7/bin/cve-2026-43499` (the `LD_PRELOAD`
payload, also published as `cve-2026-43499-app.so`) and
`build/S906BXXSNGZD7/bin/cve-2026-43499-root` (the root helper). The 32-bit
`exp32` stage is embedded in the preload; rebuilding it from scratch needs
an `arm-linux-gnueabi` cross-libc (errno.h) on the host.

## Published artifacts

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `artifacts/g0s-S906BXXSNGZD7/cve-2026-43499-app.so` | 1761720 | `500355878433b206c6a481e3706dda3d8b7ed95a362dd369692fa4f4b268f969` |
| `kernelsu/ksud-r0s-S901BXXSNGZD7-kdp` | 4621280 | `fc0097be827dab2078ba23e3e7af223905cc8ab38c763a602982071439ae7ed962` |
| `kernelsu/android12-5.10_kernelsu-r0s-S901BXXSNGZD7-kdp.ko` | 323168 | `47a66801c8a1e94a757924fd30099065cea62edc80e14b83b0879cca22fef568` |

The KernelSU module has exact vermagic:

```text
5.10.237-android12-9-31999025-abS901BXXSNGZD7 SMP preempt mod_unload modversions aarch64
```

The `__versions` section is zero-length for the kallsyms-aware manual loader;
plain `insmod` is not supported, use `ksud late-load`.

## Feed

`support/targets-v3.json` carries the `g0s-S906BXXSNGZD7` entry. Only this
profile's artifact URLs point to this fork's branch; every other entry
remains pointed at upstream (`BuSung-dev/Root-My-Galaxy-Payloads`), so this
fork only hosts its own payload.

## Status

- Root chain: device-verified (uid 0, `u:r:kernel:s0`, SELinux `Permissive`).
- DEFEX bypass: confirmed on-device — the four `global_*_status` bytes clear
  to 0 and the UMH-spawned root shell can exec `/data` binaries.
- Full chain through the app: confirmed working via the Root My Galaxy app
  (Shizuku mode) — the app's unprivileged entry runs the exploit, achieves
  root, and proceeds through the KernelSU late-load end-to-end.
- KernelSU: confirmed working — `ksud late-load` succeeds after the DEFEX
  bypass; KernelSU Manager reports `Working <LKM> [Jailbreak mode]`,
  version `32525-2`, and `su` is granted.
- Crash mitigation: after a confirmed hijack the payload restores
  `ashmem_misc.fops` before doing more work; the 32-bit waiter is left
  alive after the trigger; uncertain misses hold reclaim pages and refuse
  further attempts on that boot instead of retrying into a recycled fake
  table (the previous reboot class).

The result is a volatile root and LKM installation. A reboot removes root,
KernelSU, and the DEFEX/SELinux clears; the exploit and late-load must be
run again. No boot image is flashed.

This profile is exact-build support for `SM-S906B` on `S906BXXSNGZD7` only;
it does not claim compatibility with other Galaxy S22+ models, firmware, or
kernel releases.
