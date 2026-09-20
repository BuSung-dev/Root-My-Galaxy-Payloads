# SM-S918W / S918WVLS6CYB3 port — derivation + validation record

Status: **device-tested end-to-end on hardware, 2026-09-18.**

## Environment (2026-09-19)

- Workspace: `E:\s918w-port\` (profile, payload, kernel, tools, notes, evidence).
- WSL2 Ubuntu 26.04 moved to `E:\wsl\Ubuntu` (was C:, freed ~13 GB);
  backup tar `E:\s918w-port\wsl-ubuntu-backup.tar` (12.34 GB). In-WSL paths
  (`/root/...`) unchanged; verified post-move (same `.ko` hash).
- ADB: `C:\Users\Aows\AppData\Local\Temp\opencode\platform-tools\` (kept, ~24 MB).

Canadian Galaxy S23 Ultra (`dm3q`, product `dm3qcsx`) on firmware
`S918WVLS6CYB3` (`UP1A.231005.007.S918WVLS6CYB3`), kernel
`5.15.148-android13-8-29539737-abS918WVLS6CYB3`.

Status: **offline-built, NOT device-tested** (2026-09-18).

## Firmware identity and acquisition

Samsung FUS, model `SM-S918W`, region `XAC`, via samloader-rs 2.1.0:

```text
S918WVLS6CYB3/S918WOYV6CYB3/S918WVLS6CYB3/S918WVLS6CYB3
```

Device-side ground truth (adb, read-only) matches: `ro.build.PDA`
`S918WVLS6CYB3`, `ril.official_cscver` `S918WOYV6CYB3`, baseband
`S918WVLS6CYB3`, fingerprint
`samsung/dm3qcsx/dm3q:14/UP1A.231005.007/S918WVLS6CYB3:user/release-keys`,
`uname -r` `5.15.148-android13-8-29539737-abS918WVLS6CYB3`.
Bootloader locked, warranty bit 0, verified-boot green.

## Kernel extraction and hashes

AP tar → `boot.img.lz4` → boot image header v4, `kernel_size` u32 at 0x08,
kernel blob at 0x1000:

```text
boot.img size: 100663296
boot.img SHA-256: 0418bd2671ec5d7360a93607b93670e5e51ec248c24a0f4b1ef3355154f9b6ab
kernel size: 45017600
kernel SHA-256: 30fa611c0f878915bfe8556401d901793d0c4543b98d1be321dc0960b1f198a3
```

(Same boot/kernel sizes as the sibling `S916U1UES6CYB3` build; different
hash — different variant, same SoC/base date.)

## Symbol and BTF recovery

`vmlinux-to-elf` 1.3.6 recovered the symbolized ELF at image base
`0xffffffc008000000` (121,160 symbols). A raw-BTF scan found one validated
blob at `[0x20962c4, 0x2629962)` (5,846,686 bytes). A from-scratch Python BTF
parser (full walk: 134,390 types, no desync) supplied all structure layouts.

Notable findings:

- All `.text` symbol offsets are identical to the sibling S916U1 CYB3 build
  (same kernel source/compiler/config for core code).
- Variant-specific `.data` offsets differ and were re-derived:
  `anon_pipe_buf_ops` 0x01d496e0, `ashmem_fops` 0x01ec6a80,
  `kmalloc_caches` 0x01f1d300, `ashmem_misc` 0x02a408b8,
  `"nfnetlink_log"` string 0x01c3dd8e.
- `file_operations` is 0x120 bytes here (36 members: standard 5.15 ops +
  `copy_file_range`/`remap_file_range`/`fadvise` + 4 `android_kabi_reserved`).
  The exploit only uses members through `show_fdinfo` (0xe0), so the shared
  0x110 mirror geometry is unchanged.
- `mm_struct` BTF body is 0x3e0; with `SLAB_HWCACHE_ALIGN` the slab object is
  `ALIGN(0x3e0, 64) = 0x400`, hence `MM_STRUCT_SZ 0x400` (unchanged).
- `P0_KERNEL_PHYS_LOAD 0x80080000` adopted from the same-SoC (SM8550) S23
  family, fail-closed via fingerprint match (Qualcomm BL has no
  load-address literal) — same approach as the sibling port.

## Tracefs slide anchors

- `SLIDE_TRACEFS_EVENT_ID` = **108**: read live on-device from
  `/sys/kernel/tracing/events/sched/sched_blocked_reason/id`, and confirmed
  offline: `(0x28977c8 − 0x2897508) / 8 = 88`, `20 + 88 = 108`.
- `SLIDE_TRACEFS_WORKER_CALLER_OFF` = `0x0010c9c4` (in `worker_thread`, the
  instruction after the blocking `bl schedule` at `...c9c0`).
- `SLIDE_TRACEFS_VFORK_CALLER_OFF` = `0x000c87a0` (in `wait_for_vfork_done`,
  the return of `bl wait_for_common` at `...879c`).
- Logger oracle triple validated against the image: logger object
  `0x028e1e18` → name `0x01c3dd8e` (`"nfnetlink_log"`); `boot_id` data pointer
  `0x029fe728` → `sysctl_bootid` `0x02c6d429` (pointer value verified equal).

## P0 fingerprint table

`p0_fingerprint.h` generated from the exact raw Image at probe `0x1f0000`
(32 slide rows, 256 source qwords, readback-verified). Row 0 is identical to
the sibling build (shared `.text`); the table was still generated fresh and
is bound to this image's SHA-256 above.

## Audit

`tools/audit_target.py`: all 55 `target.h` offsets (23 symbols vs ELF
symtab, 31 layouts vs BTF, 1 composite) match — 0 mismatches.

## Build

Android NDK r28c, `aarch64-linux-android35`, Makefile `release` flags +
`-DSLIDE_STACK_WRITER=1`: `cve-2026-43499-app.so`, 97,960 bytes built,
padded to the fixed 104,128-byte release size:

```text
SHA-256: 76378da69381a6c55803f9ed8ddbb640efcc0824701c2f49a2b31858d2eb2508
```

ELF audit: AArch64 shared object, `NEEDED` only `libdl.so`/`libc.so` (no
`ld-linux-aarch64.so.1` dependency — the issue-#151 bug class is absent),
variant label embedded.

## Kernel source status (2026-09-18, updated)

The exact CYB3 opensource drop is no longer listed: the Samsung portal
keeps only the newest unified S918-family drop (FZE/FZF/FZG-era,
5.15.189; portal id 14018, all S918 variants bundled). Portal file
downloads sit behind hCaptcha, so automated fetching stops here.

Fallback plan (gated, all offline): build the module from the newest
unified S918 tree with the exact dm3q CYB3 `config.gz` and the exact
`5.15.148-android13-8-29539737-abS918WVLS6CYB3` release string. This is
defensible because 5.15.148→5.15.189 are both 5.15.y stable (no API/
prototype churn → export CRCs stable) and Samsung's `android_kabi_reserved`
slots keep existing field offsets stable. Gates before any device contact:

1. Compile-time offset check: `offsetof()` for ~40 critical fields
   (task_struct pi/cred, file_operations ops, page, waiter, mm) from the
   newer headers must equal our BTF values exactly.
2. `audit_module_against_target.py --manual-relocation`: 0 missing, 0 CRC
   mismatches, empty `__versions`.
3. Only then: `ksud` packaging and the Shizuku device test.

KernelSU source prep is DONE: v3.2.5 (`b0bc817`) + samsung patch +
`kernelsu/patches/KernelSU-v3.2.5-dm3q-5.15.148.patch` (ucount_type +
table-before-RKP-return sucompat fix), applied cleanly in
`/root/KernelSU-dm3q` (Ubuntu 26.04 WSL2). NDK r28c Linux toolchain ready
at `/root/ndk/android-ndk-r28c`. Missing input: the Samsung kernel tree
(`E:\s918w-port\oss\`, needs a manual captcha download).

## KernelSU pair (built 2026-09-18, offline, NOT device-tested)

- Tree: Samsung unified S918 FZH3-era source (5.15.189) + exact dm3q CYB3
  `config.gz` + exact release override. Layout gate vs target BTF passed
  (32 member + 5 sizeof checks, all byte-identical).
- Compiler: NDK r25c clang 14.0.7 (matches IKCONFIG `r450784e`), LLVM=1,
  `CONFIG_KSU=m` + KDP/RKP/DEFEX + `NO_PATCH_TEXT=y`,
  `KCFLAGS=-DCONFIG_DEBUG_INFO_BTF_MODULES=1`, `KBUILD_MODPOST_WARN=1`.
- Patches: samsung-kdp-rkp-defex + dm3q-5.15.148 (ucount_type; table
  resolved before RKP return so sucompat registers).
- Rebuilt ksud note: upstream Kernel-SU org git deps were deleted
  (adb_client, java-properties, ksu_props, rustix fork). Manifest rewired
  to same-commit homes (5ec1cff/adb_client@d97a9664, ReSukiSU mirrors for
  ksu_props@699849f3 and rustix@4a53fbc, crates.io java-properties 2.0.0);
  cargo git now uses the CLI backend. ksud embeds the KO below as
  `android13-5.15_kernelsu.ko`, version 32525 / 3.2.5.

```text
android13-5.15.148_kernelsu-dm3q-S918WVLS6CYB3-kdp.ko
  size: 357048
  SHA-256: eb7e4b4a4518d130faec24e8203e4a8469096e4ac1d4c0432cdf4f355d8189bf
  vermagic: 5.15.148-android13-8-29539737-abS918WVLS6CYB3 SMP preempt mod_unload modversions aarch64
  audit: 200 undefined imports, 0 missing, 65 via kallsyms, 0 CRC mismatches, __versions empty

ksud-dm3q-S918WVLS6CYB3-kdp
  size: 4629200
  SHA-256: 3180a210a84876b16ef34a148889524ac03e301b002ac8d63d62f381fe76aa2b
```

## Device validation (2026-09-18, SM-S918W S918WVLS6CYB3, first hardware run)

- Exploit (4x pile-up build `cb14959b`): slide/KASLR OK every attempt;
  full 32-object group on attempt 2; `uid=2000->0`, `root=1`, no panic,
  Knox warranty bit 0. Log: `test-attempt2.log` (prior run), phone-side
  `dm3q-attempt3.log`.
- KernelSU late-load: `kernelsu ... Live` in `/proc/modules`, no panic.
  ksud stages complete (`ephemeral: true`, KMI `android13-5.15` detected,
  entered `u:r:ksu:s0`). Defeated two integration bugs found live: ksud
  needed a new `--ephemeral` flag (added), and DEFEX Safeplace kills any
  ksud exec parented by `sh` (guarded logcat bind-mount path required;
  direct exec gets SIGKILL + Safeplace violation in dmesg).
- Manager v3.2.5 (32525-2): `Working <LKM> [Jailbreak mode]`, kernel
  `5.15.148-android13-8-29539737-abS918WVLS6CYB3`, fingerprint
  `samsung/dm3qcsx/...`, SELinux Enforcing. Screenshot:
  `SM-S918W-S918WVLS6CYB3-KernelSU-manager.png`.
- Control ioctl from an unprivileged probe: version=32525 flags=0x5
  features=0x5 uapi=2, **`su_compat=ENABLED`** — the table-before-RKP
  fix verified live.
- Pending: app-facing `su` grant (Termux shows no su until Manager
  allowlists it; Superuser count 0 at validation time).

## Validation evidence (all 2026-09-18, same boot, no reboot since)

- `test-attempt2.log`: prior run (256-pile-up build) — slide 3/3, mm-search
  0-3 collisions, fail-clean, no panic.
- `dm3q-attempt3.log` (phone + workspace copy pending): 4x pile-up build
  `cb14959b` — slide OK all 8 attempts, 32/32 group on attempt 2,
  `uid=2000->0 root=1`, no panic, Knox 0.
- `SM-S918W-S918WVLS6CYB3-KernelSU-manager.png`: Manager `Working <LKM>
  [Jailbreak mode]`, `32525-2`, exact kernel/fingerprint, Enforcing.
- `SM-S918W-S918WVLS6CYB3-Termux-su.png`: Termux `su` → `#`, `id` →
  `uid=0(root) gid=0(root) groups=0(root) context=u:r:ksu:s0` (after
  Manager grant; pre-grant `su` correctly hidden).
- Unprivileged ioctl probe: version=32525 flags=0x5 features=0x5 uapi=2,
  `su_compat=ENABLED`.
- Third-party apps: Termux granted via Manager prompt, `su` → `uid=0`
  `u:r:ksu:s0` (screenshot). AdAway 6.1.4 granted via Manager Superuser
  profile toggle (its stat-only root check never fires a prompt by itself);
  with grant: 80,173 blocked, 3 sources up-to-date, blocking active
  (screenshot). AFWall+ installs but reports missing iptables targets/
  chains on this kernel and was not pursued (iptables core itself works:
  `iptables -L` and xt_owner confirmed present via root shell).
- Manager downgraded 32601→32525 afterward (matched pair; grants live
  kernel-side and survived).
- Final ksud: 4,629,456 bytes,
  `d972cd679209c864433c85f4c13dde80c36e9bdd15b8826d360a45c148f76a78`
  (adds `--ephemeral`; manifest dep homes rewired with same pins).
- Root helper `30a1ef2698b14f4e6e9c0bd3dae1a1f646e349dd884e22dfe57f7f35a69d47ba`
  (guarded late-load, `--ephemeral` intact).

## Remaining work (not done)

1. App-facing `su` grant via Manager Superuser tab, then Termux
   `su -c id` (expect `uid=0 ... context=u:r:ksu:s0`).
2. Only then: feed entry, screenshots, upstream PR.
