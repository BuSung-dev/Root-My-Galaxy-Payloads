# Device-tested SM-X916B EZI2 payload

This payload is for Galaxy Tab S9 Ultra 5G `SM-X916B` running firmware
`X916BXXS6EZI2` and kernel `5.15.189-android13-8-33413632-abX916BXXS6EZI2`
only. See [`../../docs/SM-X916B-X916BXXS6EZI2.md`](../../docs/SM-X916B-X916BXXS6EZI2.md)
for the full device-tested record.

The MCAST build completed the full chain on real hardware three times
(`attempt=1/8` each): tracefs KASLR discovery, controlled `mm_struct`
collection, shaped SKB reclaim, fake fops, configfs ARW, pipe physical
read/write, and root UMH. The root client reported `uid=0`, `u:r:kernel:s0`,
SELinux enforcing, `warranty_bit=0`, verified boot green, throughout.

KernelSU late-load also works on this build: the FZG1 module with a
same-length vermagic patch loads through `toybox insmod` once `ksud` is
bind-mounted over `/system/bin/logcat` to satisfy Samsung DEFEX safeplace.
KernelSU Manager v3.2.5 reports `Working <LKM> [Jailbreak mode]` with
SELinux enforcing.

This profile is not listed in the Root My Galaxy support feed. The proven
launch context is `adb shell` (`uid=2000`, `u:r:shell:s0`); direct app-domain
execution is not validated for this target.

## Files

| File | SHA-256 |
| --- | --- |
| `cve-2026-43499-app.so` | `10bb2acb0c9829319c559f838cb8b3d6c44e9cf0450d4a6bf42595514675fe1b` |
| `../../kernelsu/android13-5.15.189_kernelsu-gts9u-X916BXXS6EZI2.ko` | `f6e898be48ffd733d8db7adc7470d65a8b945b661143a4bdf698a618fe51a715` |
| `../../kernelsu/ksud-dm2q-S916BXXSAFZG1-kdp` (reused verbatim) | `5da5818d36da2d589496f91016078a43f50489e5c98b319db4eaa5ee475b86bd` |

## Build

```sh
make TARGET=gts9u-X916BXXS6EZI2 API=33 ANDROID_NDK_HOME=/path/to/android-ndk all
truncate -s 104128 build/gts9u-X916BXXS6EZI2/cve-2026-43499-app.release.so   # macOS: manual size gate
```

Use only on devices you own or are explicitly authorized to test.
