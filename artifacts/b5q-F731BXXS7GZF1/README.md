# Galaxy Z Flip5 SM-F731B (F731BXXS7GZF1) payload

Exact firmware: `F731BXXS7GZF1`, kernel
`5.15.189-android13-8-33404244-abF731BXXS7GZF1` (`android13-5.15`).

The ADB-shell tracefs route, root bootstrap, KernelSU v3.2.5 late-load, and
Shizuku-free app route were hardware-validated on 2026-09-22 and 2026-09-23.
The app completes on-device without a PC or Shizuku, with SELinux Enforcing
and the official Manager connected. It uses fresh P0 discovery with 4K
slide-fingerprint coverage. See the
[validation record](../../docs/SM-F731B-F731BXXS7GZF1.md).

The current app library is a page-reuse reliability candidate. The experimental
`256/32/4` KernelSnitch profile produced no successful controlled-mm leaks on
the Flip5 and is now opt-in, avoiding its failed setup before every working
`256/128/8` measurement. The target's collision counts, confirmations,
exact-address search, and page grouping are unchanged. Clean P0 reclaim
misses now get a second fresh page in the same child. The
idempotent gate, probe, and both restore writes each get three independent
MCAST race windows before readback. After slide discovery, the retained P0
page is rewritten with a compact FOPS payload and verified byte-for-byte
through the retained gate view. A verified rewrite skips the second 32-object
collection. Failure or a clean FOPS miss falls back to the original FOPS page
collection with three attempts and its fine timing sweep. The prior P0
reliability build is retained as `cve-2026-43499-app-p0-reliable.so`, and the
known-good pre-optimization build remains `cve-2026-43499-app-baseline.so`.

## Files and provenance

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `cve-2026-43499-app.so` | 177800 | `323abf9881b5fb6aa45b657181808ec03634d42c4c748a99b52a97bb3dffadce` |
| `cve-2026-43499-app-p0-reliable.so` | 171680 | `3c4e6fbfe68baac56b4994963f7492963554a51c51433d504e774aec04c854d8` |
| `cve-2026-43499-app-baseline.so` | 170136 | `508af8ecdf09e33f06a9b532c6e3a9f187d3053ac5eb9cc5ef63eee17a0fe8fa` |
| `cve-2026-43499-root` | 27072 | `6a397067c4ac3841de01527d1f75219baa5ca6c4a6bc4b52c4408474e2456c82` |
| `../../kernelsu/ksud-b5q-F731BXXS7GZF1-kdp` | 4888048 | `0ba2bf39f163169319f0fe9cbb0236e572d99810d934587f8280a7e90ef5c521` |
| `../../kernelsu/android13-5.15.189_kernelsu-b5q-F731BXXS7GZF1-kdp.ko` | 381216 | `dd4a7d2cad7d45b367a93c68c2b8fbb74f3d300d7ddca1c276661115f27b8285` |

The published app payload library enables `APP_REQUIRE_FRESH_P0_SESSION` for
fresh slide discovery. The root helper was rebuilt with Android NDK r29,
API 35, and is byte-identical to the hardware-tested file. The KernelSU pair
is the newer hardware-validated revision with both
[SELinux hiding](../../docs/SM-F731B-selinux-hide.md) and
[su compatibility](../../docs/SM-F731B-sucompat.md) fixes. It replaces the
userspace frontend and overlay workaround.

Selected [validation outputs](validation/) record the final module's
firmware audit, authorized/denied `su` behavior, and SELinux hiding regression.

An earlier app library's 64K slide table omitted the validating run's
`0x178000` slide. Its logged P0 sample scores 8/8 against the exact local
kernel image with runner-up 0 in this 4K table (the old table scores 0/8).
The physical alias is therefore confirmed. An intermediate app-domain run
accumulated
the full 32-entry controlled-mm group in both stages (47 attempts for P0 and
85 for FOPS); the logged 0–3 collision messages are per-attempt misses, not the
final group size. It completed P0 discovery and prepared the FOPS page, then
stopped during the FOPS PI handoff before the phone rebooted.

The route audit found that `APP_CLOSED_FOPS_ROUTE` prepares its fake lock,
task, parent, and target for the new `PAGE_PAYLOAD_FOPS` page, but the fresh-P0
trigger then called `select_slide_payload_index(0)`. That selector still
described the earlier `PAGE_PAYLOAD_SLIDE` page and overwrote the new route
state. The fixed closed route now keeps the fields prepared for the FOPS page,
and the subsequent baseline build completed the full app-domain root and
KernelSU handoff on hardware.

## Build

```sh
make TARGET=b5q-F731BXXS7GZF1 ANDROID_NDK_HOME=/path/to/android-ndk-r29 all
```

`all` builds the standalone and app payloads, the root helper with
`--allow-shell` selected for the initial late-load.
The shared Makefile's fixed-size `release` target is still capped at 104128
bytes, which is too small for this target's expanded fingerprint table. The
published app library is the regular build with fresh-P0 enforcement and the
expanded 4K fingerprint table.

## Integration status

- Validation covers ADB-shell and direct app-domain execution on this exact
  firmware without Shizuku. The reliability candidate is build-validated but
  its timing and repeated-run reliability remain pending.
- The exact `0xa8000000` physical alias is confirmed by the latest P0 sample
  at slide `0x178000`; the baseline completed end-to-end app-domain root.
- The app defaults directly to the working conservative KernelSnitch profile.
  Set `RMG_CONTROLLED_FAST=1` only for shell-driven experiments with the
  unsuccessful `256/32/4` profile. Use
  `cve-2026-43499-app-p0-reliable.so` to disable page reuse while retaining
  the P0 reliability fixes, or `cve-2026-43499-app-baseline.so` for the
  hardware-validated binary rollback.
- Kernel `su_compat` supplies the conventional `su` path for authorized UIDs
  without a real `/system/bin/su` file or `/system/bin` overlay. Use a fresh
  boot when upgrading from the old overlay-based build and do not restore
  that overlay.
- Root and the late-loaded module are per-boot. Select `--allow-shell` on the
  initial load; the recorded unload/reload experiment hung the device.
