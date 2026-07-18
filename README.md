# Root My Galaxy Payloads

This repository contains the device-specific native side of
[Root My Galaxy](https://github.com/BuSung-dev/Root-My-Galaxy):

- exact firmware profiles and offsets;
- the app-domain CVE-2026-43499 exploit source and compiled payload;
- the app bootstrap helper source;
- the verified KernelSU late-load build artifacts;
- the Ed25519-signed support feed consumed by the application.

It intentionally does not contain Android application source code.

## Supported profiles

| Profile | Device | Firmware | Kernel/KMI | Status |
| --- | --- | --- | --- | --- |
| `pa3q-S938NKSUACZF1` | Galaxy S25 Ultra `SM-S938N` | `BP4A.251205.006.S938NKSUACZF1` | `android15-6.6` | Device-tested |
| `essi-S721NKSSCDZF3` | Galaxy S24 FE `SM-S721N` | `BP4A.251205.006.S721NKSSCDZF3` | `6.1.157-android14-11` / `android14-6.1` | Static analysis and build verified; device-untested |

Profiles are exact-firmware profiles. A matching model with a different build
is not equivalent and must be ported separately.

The port is based on the exploit source published at
<https://github.com/NebuSec/CyberMeowfia/tree/main/IonStack/CVE-2026-43499/exploit>.

## Feed integrity

`support/targets-v2.json` is signed with Ed25519. Root My Galaxy resolves the
payload repository's current commit first and fetches the manifest, signature,
and every artifact from that immutable commit. Per-artifact SHA-256 fields are
not part of schema version 2.

The signing private key is not stored in this repository.

## Build

```sh
make TARGET=pa3q-S938NKSUACZF1 ANDROID_NDK_HOME=/path/to/android-ndk
make TARGET=essi-S721NKSSCDZF3 ANDROID_NDK_HOME=/path/to/android-ndk
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

Use only on devices you own or are explicitly authorized to test.
