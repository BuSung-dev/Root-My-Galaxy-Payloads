# Root My Galaxy Payloads

Native payloads, firmware profiles and KernelSU runtimes for
[Root My Galaxy](https://github.com/BuSung-dev/Root-My-Galaxy). The Android app
is maintained in its own repository.

This fork also keeps the SM-F956B firmware work used by PomPomd. Each Fold6
build has its own offsets and runtime binding; a shared device name or Linux
version does not establish compatibility.

## Payloads

The following status comes from the validation records included with each
profile.

| Payload | Model | Kernel | Status |
| --- | --- | --- | --- |
| `galaxy-s25-series-2026-06-07` | Galaxy S25 series regional models | `6.6.98` | Device-tested |
| `e3q-S928USQS6DZF2` | `SM-S928U1` | `6.1.145` | Device-tested |
| `e3q-S9280ZCS6DZF2` | `SM-S9280` | `6.1.145` | Device-tested |
| `e2s-S926BXXUEDZDR` | `SM-S926B` | `6.1.157` | Device-tested |
| `essi-A566EXXSCCZG6` | `SM-A566E` | `6.6.102` | Device-tested |
| `a36xq-A366WVLS3AYG1` | `SM-A366W` | `6.6.46` | Device-tested |
| `a53x-A536EXXSNGZG3` | `SM-A536E` | `5.10.237` | Device-tested |
| `dm3q-S9180ZHS8FZF5` | `SM-S9180` | `5.15.189` | Testing in progress |
| `q4q-F9360ZCSAIZF1` | `SM-F9360` | `5.10.236` | Device-tested |
| `dm3q-S918BXXSAFZF5` | `SM-S918B` | `5.15.189` | App flow verified through Shizuku |

S918B FZF5 has completed the exploit, KernelSU late-load and granted `su`
through the app's Shizuku mode with SELinux enforcing. Its direct app-domain
fallback has reached the physical-P0 route, but successful root without
Shizuku has not been confirmed. See the
[S918B validation record](docs/SM-S918B-S918BXXSAFZF5.md).

## Experimental profiles

| Profile | Model | Kernel | Status |
| --- | --- | --- | --- |
| `dm2q-S916BXXSAFZG1` | `SM-S916B` | `5.15.189` | Root verified from ADB shell; excluded from the app feed |
| `q6q-F956BXXS4DZH5` | `SM-F956B` | `6.1.145` | Existing shell contribution; excluded from the app feed |
| `q6q-F956BXXS4DZI1` | `SM-F956B` | `6.1.145` | Existing shell contribution; runtime compatibility needs device validation |
| `q6q-F956BXXU4EZIH` | `SM-F956B` | `6.1.162` | Firmware offsets and builds checked offline; hardware root untested |

The S916B route needs a shell bridge for app use. Its
[artifact notes](artifacts/dm2q-S916BXXSAFZG1/README.md) describe the tested
execution context.

The [Fold6 EZIH record](docs/SM-F956B-F956BXXU4EZIH.md) contains the exact
kernel identity, firmware evidence and dedicated runtime audit. Its PomPomd
payload is a standalone shell executable. It is excluded from the app feed
because Root My Galaxy expects a shared-library payload. Installing the
matching APK does not verify a root attempt. The older DZH5 and DZI1
artifacts also use the shell executable format despite their historical
`.so` names, so their misleading app feed entries have been removed. All
three remain in the [experimental catalog](support/experimental.json).

## Support feed

`support/targets-v3.json` lists the payloads available to the app. Selection
matches the model and three-part kernel version; firmware suffixes and patch
dates are not part of schema 3. The
[schema notes](support/README.md) explain the matching rules and
`requiresFreshP0Session` option.

The app resolves the repository's current commit, then downloads the manifest
and artifacts from that revision. Schema 3 does not include artifact hashes
or manifest signatures. `targets-v2.json` remains available for released
0.2.3 clients.

## Building

Set `TARGET` to a profile under `src/targets` and supply an Android NDK:

```sh
make TARGET=e3q-S928USQS6DZF2 ANDROID_NDK_HOME=/path/to/android-ndk
```

The build writes these files under `build/<profile>/`:

```text
cve-2026-43499
cve-2026-43499-app.so
cve-2026-43499-root
```

Use the `release` target for the size-limited app artifact:

```sh
make TARGET=essi-S721NKSSCDZF3 ANDROID_NDK_HOME=/path/to/android-ndk release
```

Experimental shell profiles use the build instructions in their firmware
records. The generic app build command does not apply to the EZIH PomPomd
executable.

The [porting guide](docs/PORTING.md) covers firmware extraction and profile
creation. [KernelSU notes](kernelsu/README.md) describe Samsung-specific
changes and runtime builds. Device evidence is kept under `docs/`, including
the [DZH5](docs/SM-F956B-F956BXXS4DZH5.md) and
[DZI1](docs/SM-F956B-F956BXXS4DZI1.md) Fold6 contributions.

The exploit is derived from
[NebuSec's IonStack source](https://github.com/NebuSec/CyberMeowfia/tree/main/IonStack/CVE-2026-43499/exploit).
Use it on devices you own or have permission to test.
