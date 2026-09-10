# Galaxy Tab S9+ 5G SM-X818U payload

This payload targets the exact Android 16 firmware
`BP4A.251205.006.X818USQS7EZF1` on Galaxy Tab S9+ 5G `SM-X818U` (`gts9p`).
The matching kernel is
`5.15.189-android13-8-33413632-abX818USQS7EZF1`. Do not use these files on a
different model, build ID, or kernel family.

## Hardware evidence

Root My Galaxy `0.3.11-x818u-kernelsu-embedded-wait` completed the exploit,
root hand-off, and KernelSU late-load in Shizuku USB ADB mode on 2026-09-10.
The run recorded `done=1 root=1 kaslr=1`, `uid=2000->0`, and
`KernelSU control verified version=32525 flags=0x5 uapi=2 features=0x5`.
The embedded module was live in `/proc/modules` and `ksud debug info` reported
`lkm: true` and `late_load: true`.

This is a temporary per-boot LKM load. It does not modify the boot image or a
partition, and reboot removes the root state. KernelSU Manager installation or
Manager recognition was not part of this validation.

## Files

| File | SHA-256 |
| --- | --- |
| `cve-2026-43499` | `cb347b1e8456e40f3c5bbe7b587e5f3b202fe626254f362d7f31764f7f743234` |
| `cve-2026-43499-app.so` | `cfcbf6db58f4ad8537728f9b1888e5754324a5170efb7d01125ab45cd451861e` |
| `cve-2026-43499-root` | `f4459a7ca01fad96387ce535fb606fbad8f574d8c0d1e783f5675cb631687714` |

The matching KernelSU loader is
[`../../kernelsu/ksud-gts9p-X818USQS7EZF1-kdp`](../../kernelsu/ksud-gts9p-X818USQS7EZF1-kdp).
The standalone `.ko` is retained in `kernelsu/` for audit and is not
interchangeable with another 5.15.189 target.

## Build

```sh
make TARGET=gts9p-X818USQS7EZF1 ANDROID_NDK_HOME=/path/to/android-ndk
```

See [`docs/SM-X818U-X818USQS7EZF1.md`](../../docs/SM-X818U-X818USQS7EZF1.md)
for the full device record and the support-feed entry.

Use only on devices you own or are explicitly authorized to test.
