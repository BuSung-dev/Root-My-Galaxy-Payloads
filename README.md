# Root My Galaxy Payloads

This repository contains the device-specific native side of
[Root My Galaxy](https://github.com/BuSung-dev/Root-My-Galaxy):

- exact firmware profiles and offsets;
- the app-domain CVE-2026-43499 exploit source and compiled payload;
- the app bootstrap helper source;
- the verified KernelSU late-load build artifacts;
- the support feed consumed by the application.

It intentionally does not contain Android application source code.

## Supported profiles

| Profile | Device | Firmware | Kernel/KMI | Status |
| --- | --- | --- | --- | --- |
| `pa3q-S938NKSUACZF1` | Galaxy S25 Ultra `SM-S938N` | `BP4A.251205.006.S938NKSUACZF1` | `android15-6.6` | Device-tested |
| `pa3q-S9380ZHUBCZF1` | Galaxy S25 Ultra `SM-S9380` | `BP4A.251205.006.S9380ZHUBCZF1` | `android15-6.6` | Payload build verified; KernelSU pending |
| `e3q-S928USQS6DZF2` | Galaxy S24 Ultra `SM-S928U/SM-S928U1` (Snapdragon 8 Gen 3) | `UP1A.231005.007.S928USQS6DZF2` | `6.1.145-android14-11-33419968-abS928USQS6DZF2` | Static analysis and payload build verified; device-untested |
| `essi-S721NKSSCDZF3` | Galaxy S24 FE `SM-S721N` | `BP4A.251205.006.S721NKSSCDZF3` | `6.1.157-android14-11` / `android14-6.1` | Static analysis and build verified; device-untested |
| `e1s-S921BXXSFDZF2` | Galaxy S24 `SM-S921B` (Exynos 2400) | `BP4A.251205.006.S921BXXSFDZF2` | `6.1.157-android14-11` / `android14-6.1` | Static analysis and build verified; device-untested |
| `a15-A155NKSS6BYH1` | Galaxy A15 5G `SM-A155N` | `AP3A.240905.015.A2.A155NKSS6BYH1` | `5.10.226-android12-9-31117096` / `android12-5.10` | Static analysis and build verified; device-untested |

Profiles are exact-firmware profiles. A matching model with a different build
is not equivalent and must be ported separately.

Root My Galaxy requires both the exact `uname -r` value in `kernelRelease` and
the complete `/proc/version` value in `kernelVersion`. This distinguishes
vendor kernels that expose the same release string but were linked from
different builds. Model and device fields are descriptive metadata; build
display ID, SDK, ABI, and page size remain part of automatic profile selection.

The port is based on the exploit source published at
<https://github.com/NebuSec/CyberMeowfia/tree/main/IonStack/CVE-2026-43499/exploit>.

## Feed delivery

Root My Galaxy resolves the payload repository's current commit first and
fetches `support/targets-v2.json` and every artifact from that immutable commit.
Per-artifact SHA-256 fields and manifest signatures are not part of schema
version 2.

## Build

```sh
make TARGET=pa3q-S938NKSUACZF1 ANDROID_NDK_HOME=/path/to/android-ndk
make TARGET=e3q-S928USQS6DZF2 ANDROID_NDK_HOME=/path/to/android-ndk
make TARGET=essi-S721NKSSCDZF3 ANDROID_NDK_HOME=/path/to/android-ndk
make TARGET=e1s-S921BXXSFDZF2 ANDROID_NDK_HOME=/path/to/android-ndk
make TARGET=a15-A155NKSS6BYH1 ANDROID_NDK_HOME=/path/to/android-ndk
```

Outputs:

```text
build/<profile>/cve-2026-43499
build/<profile>/cve-2026-43499-app.so
build/<profile>/cve-2026-43499-root
```

The release app payload is built with:

```sh
make TARGET=essi-S721NKSSCDZF3 ANDROID_NDK_HOME=/path/to/android-ndk release
```

The complete firmware-to-profile procedure is recorded in
[`docs/PORTING.md`](docs/PORTING.md). Samsung-specific KernelSU changes and
versioned artifacts are documented in [`kernelsu/README.md`](kernelsu/README.md).
The exact S921B DZF2 analysis is recorded separately in
[`docs/SM-S921B-S921BXXSFDZF2.md`](docs/SM-S921B-S921BXXSFDZF2.md), and the
S928U/S928U1 DZF2 analysis is in
[`docs/SM-S928U1-S928U1UES6DZF2.md`](docs/SM-S928U1-S928U1UES6DZF2.md). S921B
is an Exynos 2400 target and is not a Qualcomm/Snapdragon reference for E3Q.
The 5.10 A15 analysis is in
[`docs/SM-A155N-A155NKSS6BYH1.md`](docs/SM-A155N-A155NKSS6BYH1.md).

Use only on devices you own or are explicitly authorized to test.
