# Root My Galaxy Payloads

This repository contains the device-specific native side of
[Root My Galaxy](https://github.com/BuSung-dev/Root-My-Galaxy):

- exact firmware profiles and offsets;
- the app-domain CVE-2026-43499 exploit source and compiled payload;
- the app bootstrap helper source;
- the verified KernelSU late-load build artifacts;
- the support feed consumed by the application.

It intentionally does not contain Android application source code.

## Supported payloads

| Payload | Compatible models | Kernel version | Status |
| --- | --- | --- | --- |
| `galaxy-s25-series-2026-06-07` | Galaxy S25, S25+, S25 Edge, and S25 Ultra regional models | `6.6.98` | Device-tested |
| `e3q-S928USQS6DZF2` | Galaxy S24 Ultra `SM-S928U` | `6.1.145` | Hardware debugging in progress |
| `e2s-S926BXXUEDZDR` | Galaxy S24+ `SM-S926B` | `6.1.157` | Device-tested |
| `essi-A566EXXSCCZG6` | Galaxy A56 5G `SM-A566E` | `6.6.102` | Device-tested |
| `a36xq-A366WVLS3AYG1` | Galaxy A36 5G `SM-A366W` | `6.6.46` | Device-tested |
| `dm3q-S9180ZHS8FZF5` | Galaxy S23 Ultra `SM-S9180` | `5.15.189` | Test in progress |

Schema version 3 keeps each exploit and KernelSU artifact once. Its flat
`models` and `kernelVersions` arrays define runtime compatibility. See
[`support/README.md`](support/README.md) for the matching rules.

The port is based on the exploit source published at
<https://github.com/NebuSec/CyberMeowfia/tree/main/IonStack/CVE-2026-43499/exploit>.

## Feed delivery

Root My Galaxy resolves the payload repository's current commit first and
fetches `support/targets-v3.json` and every artifact from that immutable
commit. Per-artifact SHA-256 fields and manifest signatures are not part of
schema version 3. `targets-v2.json` is retained for released 0.2.3 clients.

## Build

```sh
make TARGET=pa3q-S938NKSUACZF1 ANDROID_NDK_HOME=/path/to/android-ndk
make TARGET=e3q-S928USQS6DZF2 ANDROID_NDK_HOME=/path/to/android-ndk
make TARGET=e2s-S926BXXUEDZDR ANDROID_NDK_HOME=/path/to/android-ndk
make TARGET=essi-S721NKSSCDZF3 ANDROID_NDK_HOME=/path/to/android-ndk
make TARGET=e1s-S921BXXSFDZF2 ANDROID_NDK_HOME=/path/to/android-ndk
make TARGET=a15-A155NKSS6BYH1 ANDROID_NDK_HOME=/path/to/android-ndk
make TARGET=essi-A566EXXSCCZG6 ANDROID_NDK_HOME=/path/to/android-ndk
make TARGET=a36xq-A366WVLS3AYG1 ANDROID_NDK_HOME=/path/to/android-ndk
make TARGET=dm3q-S9180ZHS8FZF5 ANDROID_NDK_HOME=/path/to/android-ndk
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
The SM-A566E CCZG6 analysis and validation record is in
[`docs/SM-A566E-A566EXXSCCZG6.md`](docs/SM-A566E-A566EXXSCCZG6.md).
The SM-S926B DZDR analysis and device-validation record is in
[`docs/SM-S926B-S926BXXUEDZDR.md`](docs/SM-S926B-S926BXXUEDZDR.md).
The SM-A366W AYG1 device validation is in
[`docs/SM-A366W-A366WVLS3AYG1.md`](docs/SM-A366W-A366WVLS3AYG1.md).

Use only on devices you own or are explicitly authorized to test.
