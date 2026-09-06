# SM-S918B — S918BXXSAFZF5

Galaxy S23 Ultra (European, `dm3q`) on firmware `S918BXXSAFZF5`
(`BP4A.251205.006.S918BXXSAFZF5`, June 2026 patch), kernel
`5.15.189-android13-8-33413713-abS918BXXSAFZF5`.

Status: **hardware-verified end-to-end through the Root My Galaxy app**
(Shizuku execution mode), including KernelSU late-load and a working
granted `su` under SELinux enforcing.

## What this profile is

- Open-source engine (this repository) with the MCAST stack writer, tracefs
  KASLR discovery, controlled `mm_struct` group reclaim, shaped order-3 SKB
  reclaim, fake ashmem fops, configfs arbitrary read/write, and pipe
  physical read/write, escalating through a root usermode helper.
- The `33413713` kernel build is shared with `SM-S916B` `S916BXXSAFZG1`
  (PR #223's profile); `.text` is identical between the two builds. Only the
  target constants differ (three `.data` symbols shifted `+0x80` in FZG1,
  the physical load address, and the P0 candidate granularity — the FZF5
  slide is 0x8000-aligned and randomized per boot; observed slides include
  `0x0`, `0x78000`, `0xf8000`, `0x118000`, `0x168000`, `0x1b0000`).
- `MCAST_WAITER_OFF 0x78` is re-derived from this kernel's disassembly, and
  the tracefs caller sites (`worker_thread+0x78`, `wait_for_vfork_done+0x44`)
  resolve identically in the FZF5 `vmlinux`.

## KernelSU

The feed's `kernelsu` artifact (`kernelsu/ksud-dm3q-S918BXXSAFZF5-kdp`) is
the `android13-5.15.189` exact-source loader from the same kernel-build
family (built from Samsung's SM-S916B FZG1 opensource tree with the
kallsyms-aware manual loader; RKP syscall-table and live text patching
disabled). Hardware-verified on this device: module `Live`, KernelSU
Manager v3.2.5 (`32525`) reports `Working <LKM> [Jailbreak mode]`, granted
`su` returns `uid=0` in `u:r:ksu:s0` with SELinux enforcing, and the
superuser grant persists across reboots.

## How to run

1. Start Shizuku (e.g. via wireless debugging — one-time pairing, then one
   tap per boot).
2. Open Root My Galaxy, enable Shizuku mode in the app settings, grant the
   Shizuku permission, and run the install for
   `Galaxy S23 Ultra SM-S918B | Kernel 5.15.189 (S918BXXSAFZF5)`.
3. Root through the app directly (adb shell, `cve-2026-43499-root
   --run-payload ...`) also works; the app flow above is what was verified.

After a successful run the KernelSU Manager can grant superuser to apps and
to the ADB shell.

## Reliability

> **⚠️ EXPECT RETRIES — DO NOT GIVE UP: it took 4 tries (one fresh reboot before each retry) with Shizuku mode until the first success.**
> **Failures along the way are normal: some attempts panic-reboot or hard-freeze the phone (hold power to recover). If the log says `stack writer ran; refusing retry on this boot`, that boot is spent — reboot and run again. Run close to boot for the best odds.**

Per-boot success is probabilistic (same as the S916B FZG1 profile):

- Run close to boot (ideally within the first ~2 minutes).
- Failures before the stack-writer stage are retried automatically by the
  supervisor.
- After the stack-writer stage a failed attempt leaves PI state behind; the
  engine detects this and refuses further in-boot retries
  (`stack writer ran; refusing retry on this boot`). Reboot and run again.
- Observed failure modes across hardware attempts: clean pre-writer failure
  (auto-retry), panic reboot at the writer, hard freeze requiring a
  power-button hold, and a clean post-writer configfs failure. None are
  persistent; everything is volatile per boot.

## Source material

- `src/targets/dm3q-S918BXXSAFZF5/target.h` — every constant verified
  against the recovered FZF5 `vmlinux` (symbol table) and on-device
  kallsyms; the P0 parameterization reproduces the closed engine's known
  fallback alias exactly.
- `src/targets/dm3q-S918BXXSAFZF5/p0_fingerprint.h` — 64 rows, 0x8000
  steps, generated from the device-verified raw Image
  (`tools/kernel`, SHA-256 `45e16fc6...`).
- Engine deltas in this PR: non-fatal tracefs failures (so the auto slide
  mode can fall back to the physical P0 oracle in unprivileged domains) and
  supervisor attempt/P0 timeout defaults raised for the physical route
  (a hardware-verified run needed 143 s for kernel page prepare alone).
