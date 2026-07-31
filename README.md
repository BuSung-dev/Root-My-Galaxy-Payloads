# Root My Galaxy Payloads

This repository contains the device-specific native side of
[Root My Galaxy](https://github.com/arda-y-28/Root-My-Galaxy-Payloads):

- exact firmware profiles and offsets;
- the app-domain CVE-2026-43499 exploit source and compiled payload;
- the app bootstrap helper source;
- the verified KernelSU late-load build artifacts;
- the support feed consumed by the application.

It intentionally does not contain Android application source code.

## Supported payloads

| Payload | Compatible models | Kernel version | Status |
| --- | --- | --- | --- |
| `r9q-G990B2XXSJIZF1` | Galaxy S21 FE 5G `SM-G990B2` | `5.4.289` | Device-tested |
| `galaxy-s25-series-2026-06-07` | Galaxy S25, S25+, S25 Edge, and S25 Ultra regional models | `6.6.98` | Device-tested |
| `e3q-S928USQS6DZF2` | Galaxy S24 Ultra `SM-S928U` | `6.1.145` | Hardware debugging in progress |
| `essi-A566EXXSCCZG6` | Galaxy A56 5G `SM-A566E` | `6.6.102` | Device-tested |

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
# Linux / Mac NDK
make TARGET=r9q-G990B2XXSJIZF1 ANDROID_NDK_HOME=/path/to/android-ndk
make TARGET=pa3q-S938NKSUACZF1 ANDROID_NDK_HOME=/path/to/android-ndk
make TARGET=e3q-S928USQS6DZF2 ANDROID_NDK_HOME=/path/to/android-ndk
make TARGET=essi-S721NKSSCDZF3 ANDROID_NDK_HOME=/path/to/android-ndk
make TARGET=e1s-S921BXXSFDZF2 ANDROID_NDK_HOME=/path/to/android-ndk
make TARGET=a15-A155NKSS6BYH1 ANDROID_NDK_HOME=/path/to/android-ndk
make TARGET=essi-A566EXXSCCZG6 ANDROID_NDK_HOME=/path/to/android-ndk

# Windows (Python)
python build_win.py r9q-G990B2XXSJIZF1
