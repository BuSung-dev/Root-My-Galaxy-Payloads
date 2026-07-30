# SM-S938U1 / S938U1UESBCZF5 hardware validation plan

**Do not execute this plan until offline gates are accepted and the device
owner explicitly authorizes each stage.** This document is a checklist only.

Profile: `pa3q-S938USQSBCZF5`  
Device: owner-authorized `SM-S938U1` only  
Payload repo status at plan write: offline build-verified, **not** device-tested.

## Preconditions (must all pass)

1. Live device matches the frozen identity exactly:
   - model `SM-S938U1`
   - `uname -r` =
     `6.6.98-android15-8-pd6ff1cd-abogkiS938USQSBCZF5-4k`
   - full `/proc/version` matches the feed `kernelVersion` string
   - build fingerprint =
     `samsung/pa3quew/pa3q:16/BP4A.251205.006/S938U1UESBCZF5_OYMBCZF5:user/release-keys`
     (or the same AP/CSC pair used to derive the profile)
2. App resolves profile `pa3q-S938USQSBCZF5` without Advanced Mode overrides.
3. Downloaded `cve-2026-43499-app.so` size is exactly `104128`.
4. Backup / recovery plan ready (stock firmware package already retained).
5. USB debugging only as needed for read-only identity checks; no forced
   mismatched profiles.

## Stage A — read-only identity (safe)

- [ ] Confirm model, fingerprint, SDK, ABI, page size via the app snapshot or
      `getprop` / `uname`.
- [ ] Confirm
      `cat /sys/kernel/tracing/events/sched/sched_blocked_reason/id` → `109`.
- [ ] Confirm Verified Boot state remains expected before any install attempt.
- [ ] **Stop here if any field differs.** Do not continue on a different build.

## Stage B — install path dry-run (no root claim)

- [ ] Install the app build that consumes the updated feed (or local payload
      override if the app supports offline testing).
- [ ] Confirm profile selection UI shows `pa3q-S938USQSBCZF5` / S938U1 CZF5.
- [ ] Confirm artifact fetch size and that KernelSU package URL resolves.
- [ ] Do **not** start the exploit until Stage C is planned with owner consent.

## Stage C — exploit / late-load (high risk; owner-authorized only)

Run only on the exact frozen firmware. Expect possible reboot or crash.

Suggested diagnostic order (abort on unexpected panic without logs):

1. **Slide / oracle path only** if the app or payload exposes intermediate
   success markers (KASLR slide, P0 match). Record whether:
   - event ID `109` matches blocked-worker callers
   - P0 fingerprint hits any of the 32 candidates
   - `P0_KERNEL_PHYS_LOAD` / `SKB_DATA_DELTA` assumptions hold
2. If slide fails with wrong lock / null pointer:
   - both `SLIDE_PSELECT_WORD_SHIFT = 0` and
     `P0_KERNEL_PHYS_LOAD = 0x80080000` are now statically derived from the
     exact CZF5 syscall/ABL inputs; re-check their runtime observations before
     revisiting other constants
   - **not** the bulk symbol table (already ELF/BTF verified offline)
3. KernelSU late-load only after a confirmed root/helper path, and only with
   the `android15-6.6` artifact intended for this profile.
   - Existing `ksud-s25u-kdp` is shared with other PA3Q builds; vermagic is
     **not** the exact CZF5 release string. Prefer an exact rebuild if load
     fails.

## Stage D — post-condition checks

- [ ] If root succeeds: verify UID 0 helper, SELinux context expectations, and
      that the device still boots after a deliberate reboot.
- [ ] If it fails: capture panic logs / last kmsg if available; do not flip
      unrelated offsets “to try again.”
- [ ] Update profile status from “offline build-verified” only after a
      successful, repeatable owner-authorized run.

## Explicit non-goals for first session

- Do not test on a different CSC or a different security patch level.
- Do not force another PA3Q profile (`S938N` / `S9380` CZF1) onto this phone.
- Do not treat a single lucky boot as “device-tested” without recording the
  exact app/payload commit and feed commit used.
