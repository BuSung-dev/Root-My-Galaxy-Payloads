# dm3q ksud rebuild — KernelSU v3.3.0 (KSU_VERSION 32601)

Scope: Stage 2 Worker-2B (ksud/loader side). Three `dm3q` profiles, one shared
KMI (`android13-5.15`). Companion: `kernelsu/patches/KernelSU-v3.3.0-samsung-kdp-rkp-defex.patch`
(Stage 1, rebased onto upstream `v3.3.0` / `932014a`).

> Filename note: `kernelsu/REBUILD-dm3q-v3.3.0.md` did not exist when 2B ran
> (only `kernelsu/README.md`), so 2B created this primary name directly. If
> Stage2-A later produces a same-named file, 2B's content stays authoritative
> for the ksud side and any A-side file should be renamed `-app` / merged, not
> overwritten.

## 0. FZG1 loader resolution (authoritative for this file)

**Decision: `dm3q-S9180ZHS8FZG1` reuses `kernelsu/ksud-dm3q-S918BXXSAFZF5-kdp`
byte-for-byte. No independent FZG1 ksud is minted.**

Evidence:

- Same kernel-build family `33413713`:
  FZG1 `5.15.189-android13-8-33413713-abS9180ZHS8FZG1` vs S918B
  `5.15.189-android13-8-33413713-abS918BXXSAFZF5` (docs `SM-S918B-*.md`
  "What this profile is" section:
  `.text` identical with `S916BXXSAFZG1`; `SM-S9180-…FZG1.md` §10.5: FZG1 vs
  FZF5 `do_ipv6_setsockopt` differs in 2 relocation instructions only).
- `ksud-dm3q-S918BXXSAFZF5-kdp` SHA-256
  `5da5818d…b86bd` == `ksud-dm2q-S916BXXSAFZG1-kdp` (byte-identical, 4879560 B,
  single-KMI asset `android13-5.15_kernelsu.ko`, exact-source kallsyms-aware
  manual loader, RKP syscall-table + live text patching disabled).
  Hardware-verified on S918B: module `Live`, Manager `Working <LKM>
  [Jailbreak mode]`, `su` → `u:r:ksu:s0` enforcing.
- `docs/SM-S9180-S9180ZHS8FZG1.md` §11.3 string `ksud-s25u-kdp` is the
  **on-device pathname** (the app/helper staging name, bind-mounted over
  `/system/bin/logcat` to dodge Knox DEFEX Safeplace), NOT a provenance
  claim. Repo
  `kernelsu/ksud-s25u-kdp` is `android15-6.6` (minSdk 35, single asset
  `android15-6.6_kernelsu.ko`) — a 6.6 module cannot load on a 5.15 kernel
  (vermagic + ~200 undefined symbols). The §11.3 success log (`ksud 3.2.5`,
  `u:r:ksu:s0` enforcing) is only consistent with the pushed file's
  **content** being the 5.15 S918B loader renamed on-device. Doc wording
  should be fixed accordingly; `KSU_LOADER_PATH` keeps working because the
  app pushes whatever loader the profile selects under that name.
- `artifacts/dm3q-S9180ZHS8FZG1/README.md` L22 already declares the S918B
  ksud SHA (`5da5818d…`) — reuse is the recorded hardware truth.
- Residual vermagic-suffix difference (`abS9180ZHS8FZG1` vs `abS916BXXSAFZG1`)
  is covered by the same mechanism that already lets the `abS916BXXSAFZG1`
  module load on `abS918BXXSAFZF5` hardware (zero-length `__versions`,
  kallsyms-aware manual relocation, no plain `insmod`).
- Minting `ksud-dm3q-S9180ZHS8FZG1-kdp` would be a byte-copy with zero
  technical delta and would split the feed. Do not create it.

## 1. Per-profile asset map

| Profile | Asset `.ko` (repo) | vermagic suffix | ksud publish name (v3.3.0) |
| --- | --- | --- | --- |
| `dm3q-S9180ZHS8FZF5` | `kernelsu/android13-5.15.189_kernelsu-dm3q-S9180ZHS8FZF5.ko` (227224 B, stripped) | `abS9180ZHS8FZF5` | `kernelsu/ksud-dm3q-S9180ZHS8FZF5-kdp` (rebuild; replaces APK-extracted multi-KMI 4556352 B file with exact-source single-KMI build) |
| `dm3q-S918BXXSAFZF5` | `kernelsu/android13-5.15.189_kernelsu-dm2q-S916BXXSAFZG1.ko` (356928 B) | `abS916BXXSAFZG1` | `kernelsu/ksud-dm3q-S918BXXSAFZF5-kdp` (rebuild; current 4879560 B file is v3.2.5) |
| `dm3q-S9180ZHS8FZG1` | SAME as S918B row (no independent KO) | `abS9180ZHS8FZG1` via kallsyms loader | REUSE `ksud-dm3q-S918BXXSAFZF5-kdp`; no new binary |

## 2. Prerequisites

- Upstream source: `https://github.com/tiann/KernelSU.git`, tag `v3.3.0`
  (`932014ab5b2c9b74a3d11e2ec4d17dd10fc9442e`).
- This repo's Samsung patch:
  `kernelsu/patches/KernelSU-v3.3.0-samsung-kdp-rkp-defex.patch` (applies
  cleanly at `v3.3.0`; kernel `Kbuild` KSU_VERSION fallback `?= 32601`,
  `compat/samsung_{kdp,defex}`, RKP guard, execveat 6-piece kprobe fallback,
  `NO_PATCH_TEXT` fail-closed, KDP/DEFEX init order).
- `cargo 1.98.x` + `aarch64-linux-android` target + Android NDK **r29**
  (`29.0.14206865`; `file` on existing binaries confirms built-by NDK r29).
- `cargo-ndk` helper (or manual `--target aarch64-linux-android` + NDK clang
  in `PATH`). This env: `cargo-ndk` NOT installed (see §5 blocked).
- `.ko` rebuild (only if the module itself changes): Samsung opensource
  `msm-kernel` (Kalama 5.15) tree + target defconfig + clang `r450784e`,
  exact-release vermagic override, `Module.symvers` reconstructed from target
  `vmlinux.elf`. `.ko` files are NOT rebuilt for a pure ksud userspace bump.

## 3. v3.3.0 userspace semantics that constrain the rebuild

(from Stage 1 patch + upstream `v3.3.0` tree; Stage 1 already merged the
staging-rename flow)

- `late_load.rs`: `utils::install(None, None)` → split into
  `stage_daemon()` / `finish_install(None, None)`; late-load path calls
  `stage_daemon_from("/data/local/tmp/.ksud-stage")` BEFORE the module load
  changes the process security context, then `finish_install(None, None)`
  after; the `am force-stop/start Manager` tail is dropped.
- `utils.rs`: new `stage_daemon()` (`/proc/self/exe` → `/data/adb/ksud`,
  0755, self-copy-race guard) and `stage_daemon_from()` (rename + root:root
  chown + 0755).
- Upstream `v3.3.0` surface (unchanged by Samsung patch, must be preserved):
  `defs.rs` `FULL_VERSION = "{VERSION_NAME} (uapi: {UAPI})"` shown by
  `ksud -V`; `getprop` direct call (`utils::getprop`, no shell-out);
  `su --context=` passthrough (`su.rs` accepts `--context=`); backup
  staging under `/data/adb` during install.
- Manager pairing: KernelSU Manager **v3.3.0 / versionCode 32601**.
  A 3.2.5 (`32525`) Manager against a 32601 kernel reports version skew;
  always upgrade Manager together with the `.ko`+`ksud` pair.

## 4. Rebuild steps (per profile: FZF5, then S918B)

```sh
# 0. fetch + patch (once)
git clone --branch v3.3.0 --depth 1 https://github.com/tiann/KernelSU.git /tmp/ksu-v3.3.0
cd /tmp/ksu-v3.3.0
patch -p1 < <repo>/kernelsu/patches/KernelSU-v3.3.0-samsung-kdp-rkp-defex.patch

# 1. stage the per-profile module asset (exact filename the build embeds)
# ORDERING GUARD (Stage 2 review): rebuild the v3.3.0 (32601) .ko per §7
# FIRST, then stage the NEW ko. Do NOT stage the repo's existing v3.2.5
# (32525) ko into a v3.3.0 ksud — that ko/ioctl vs ksud skew (32525 vs
# 32601) will trip gate §4b-1.
# FZF5 (new 32601 ko from §7-A):
cp <repo>/kernelsu/android13-5.15.189_kernelsu-dm3q-S9180ZHS8FZF5.ko \
   userspace/ksud/bin/aarch64/android13-5.15_kernelsu.ko
# S918B (and, by §0, FZG1):
cp <repo>/kernelsu/android13-5.15.189_kernelsu-dm2q-S916BXXSAFZG1.ko \
   userspace/ksud/bin/aarch64/android13-5.15_kernelsu.ko

# 2. toolchain
export ANDROID_NDK_HOME=$HOME/Android/Sdk/ndk/29.0.14206865
export KSU_VERSION=32601 VERSION_CODE=32601 VERSION_NAME=3.3.0
export PATH=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH

# 3. build (repeat per asset staging; clean bin/ between profiles)
cargo ndk -t arm64-v8a --platform 24 build --release
# output: target/aarch64-linux-android/release/ksud

# 4. publish (do NOT overwrite existing binaries in place; stage aside first)
cp target/aarch64-linux-android/release/ksud \
   <repo>/kernelsu/ksud-dm3q-<PROFILE>-kdp.new
```

FZG1 has no step of its own: after the S918B rebuild verifies (§4 checks),
record `ksud-dm3q-S918BXXSAFZF5-kdp` as the FZG1 loader in
`artifacts/dm3q-S9180ZHS8FZG1/README.md` (already the case at v3.2.5).

## 4b. Verification gates (each rebuilt ksud)

1. `ksud -V` → `3.3.0` (FULL_VERSION `3.3.0 (uapi: …)`); kernel ioctl
   `ksu_get_info` version field → `32601`.
2. Rebuild gates (per `kernelsu/README.md` + `fa7313a`): exact vermagic on
   the staged `.ko`, `check_symbol` clean, manual-relocation audit
   (0 missing / 0 CRC-mismatch-blocking), paired `.ko`+`ksud` publish, then
   hardware `late-load [--ephemeral] --package-name me.weishu.kernelsu` +
   `su -c id` → `u:r:ksu:s0` enforcing + Manager `Working <LKM>`.
3. `--ephemeral` note: some v3.2.5 Samsung ksud builds exit 0 yet ignore
   `--ephemeral` (the app/helper falls back via control check). v3.3.0
   honors it through the staging-rename path — verify on hardware
   (expected control line; exact string to be confirmed on device),
   not the exit code.

## 5. This environment: build-readiness (honest, 2026-10-01)

- `cargo 1.98.1` + `rustc 1.98.1`: PRESENT; `aarch64-linux-android` target:
  installed.
- NDK r29 (`29.0.14206865`): PRESENT at
  `~/Android/Sdk/ndk/29.0.14206865` (task premise "NDK 無" is stale for this
  host; r27/r28 also present; existing binaries are NDK-r29-built).
- Upstream `v3.3.0` clone: OK (`932014a` Farben, `/tmp/opencode/ksud-check/`);
  `bin/aarch64/` holds only `bootctl`+`busybox` — confirms the per-profile
  `.ko` staging step is mandatory.
- BLOCKED: `cargo check --target aarch64-linux-android` in
  `userspace/ksud` fails at dependency fetch, before any NDK compile:
  `ksu_props` git dep `rev 6f57231…` → auth/404
  (`failed to fetch … not found … attempted credential.helper`).
  `cargo-ndk` also not installed. No vendored/offline deps in repo.
  → ksud userspace compile is **not runnable in this sandbox as-is**;
  needs network-auth fix (or vendored deps) + `cargo-ndk`.
- BLOCKED (out of scope for ksud-only rebuild but gating any `.ko` change):
  Samsung `msm-kernel` Kalama tree + target `vmlinux.elf`/configs are
  host-side evidence dirs, not in repo; FZG1 needs no `.ko` change per §0.
- No repo files were modified by the readiness probe except this new doc;
  no existing `.ko`/`ksud` binaries touched (clone + check ran in `/tmp`).

---

# Stage 2 Worker-2A appendix — kernel side (2026-10-01)

Scope: `S23U` `dm3q` `5.15.189` kernel side. Stage 1 committed
`kernelsu/patches/KernelSU-v3.3.0-samsung-kdp-rkp-defex.patch`
(`KSU_VERSION 32601`). No existing `.ko`/`ksud` binaries touched; one new
patch file added (§6.3). 2B content above (§0–§5, ksud/loader side) is
preserved verbatim; what follows is the 2A kernel-side record.

## 6. Build-fix verdict for dm3q 5.15 on v3.3.0

### 6.1 Task-1 verdict: YES, the build-fix is still needed

- After applying the Stage-1 main patch to a clean upstream `v3.3.0`
  (`932014a`) tree, `kernel/compat/samsung_kdp.c:30-31` still declares the
  ucounts helpers with an **ungated** `enum rlimit_type` (only the outer
  `#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)` guard) — byte for
  byte the same defect as the old `v3.2.5` main patch
  (`KernelSU-v3.2.5-samsung-kdp-rkp-defex.patch:210-211`). Verdict:
  **與舊一致 (consistent with old)**.
- Samsung `android13-5.15` (incl. all three dm3q profiles, KMI
  `android13-5.15.189`, changelist `33413713`) keeps the **pre-5.16** enum
  name `enum ucount_type` in `include/linux/user_namespace.h` (upstream
  rename to `enum rlimit_type` landed in 5.16). The `dm1q` version-gated
  fix is the canonical precedent; `dm2q` is its subset + RKP early-return.
  Without the fix the module does not compile against the Samsung 5.15
  tree (unknown type name `rlimit_type` at 5.15).

### 6.2 /tmp verification (fresh clone, real `patch -p1` sequence)

```text
git clone --branch v3.3.0 --depth 1 https://github.com/tiann/KernelSU.git
# → 932014a, `git describe --tags` = v3.3.0
patch -p1 < kernelsu/patches/KernelSU-v3.3.0-samsung-kdp-rkp-defex.patch
# → MAIN-APPLY-OK (all 16 files)
patch -p1 --dry-run < kernelsu/patches/KernelSU-v3.2.5-dm1q-android13-5.15-build-fix.patch
# → Hunk #1 succeeded at 27 with fuzz 2 (offset 5 lines)
#   (v3.3.0 added the 6.12 usecount block, shifting context by 5 lines;
#    semantically correct, but fuzz-dependent)
patch -p1 < kernelsu/patches/KernelSU-v3.3.0-dm3q-5.15-build-fix.patch
# → patching file kernel/compat/samsung_kdp.c (NO fuzz, clean)
# Result: version-gated typedefs at samsung_kdp.c:29-38, verified by re-read.
```

The full chain was re-verified on a second fresh clone straight from the
repo file (`MAIN-OK` → `REPO-FIX-OK`, byte-identical to the verified
diff).

### 6.3 Deliverable decision (dedup rule)

New file added: `kernelsu/patches/KernelSU-v3.3.0-dm3q-5.15-build-fix.patch`.
The dedup clause ("若與 dm1q 內容完全相同則只加說明文件") does **not**
trigger: the 6 inserted lines are semantically identical to the `dm1q`
canonical fix (same `>=5.16 rlimit_type / else ucount_type` gate, same
comment), but the file is **not byte-identical** — hunk header
`@@ -27,8 +27,14 @@` vs dm1q's `@@ -22,8 +22,14 @@`, and the leading
context covers the v3.3.0 `6.12` usecount block. The old file needs
fuzz 2 to apply; the new file applies fuzz-free. Keeping both is a
context rebase, not a duplication.

### 6.4 RKP early-return carry-over (S918B route)

The second hunk of `KernelSU-v3.2.5-dm2q-fzg1.patch` (RKP early-return in
`ksu_syscall_hook_init`, "syscall table patch is off") is **not** part of
the main patch (v3.3.0 main has `NO_PATCH_TEXT` stub + dispatcher-failure
fallback instead). Extracted standalone, it dry-runs cleanly on
`v3.3.0 + main + dm3q-fix` (`Hunk #1 succeeded at 221`, no fuzz), so the
S918B/FZG1 no-patch-text route can carry it forward verbatim if exact
fidelity is wanted. It is intentionally **not** folded into the dm3q
build-fix (which stays a pure ucount gate like dm1q); see §7 profile B
for when it applies.

## 7. Per-profile v3.3.0 kernel rebuild steps

### 7.0 Shared constants (all three dm3q profiles)

| Item | Value |
| --- | --- |
| KMI | `android13-5.15` (kernel `5.15.189-android13-8-33413713-ab<BUILD>`) |
| Changelist | `33413713` (shared by FZF5 / S918B-FZF5 / FZG1 / dm2q-FZG1) |
| SoC | Snapdragon 8 Gen 2 (Kalama / SM8550), codename `dm3q` |
| Target compiler (module) | Android clang `14.0.7 (r450784e)` per device IKCONFIG; NDK r25c ships the same-llvm-commit clang (`r450784d1` wrapper) — mandatory on CFI/LTO kernels (dm1q lesson) |
| App/ksud userspace | NDK r29 (existing `.so`/`ksud` are r29-built) |
| KSU Kconfig (all) | `CONFIG_KSU=m CONFIG_KSU_SAMSUNG_KDP=y CONFIG_KSU_SAMSUNG_RKP=y CONFIG_KSU_SAMSUNG_DEFEX=y` + `KBUILD_MODPOST_WARN=1`; `NO_PATCH_TEXT` per route below |
| Hardening CONFIGs (exact IKCONFIG from device `/proc/config.gz`) | `TRIM_UNUSED_KSYMS`, `LTO_CLANG_FULL`, `CFI_CLANG`, `SHADOW_CALL_STACK`, `MODVERSIONS`, module-sig enforcement, `DEBUG_INFO_BTF_MODULES` (dm1q lesson: add `KCFLAGS=-DCONFIG_DEBUG_INFO_BTF_MODULES=1` so `init_module`/`cleanup_module` relocs land at `0x178`/`0x378`) |
| `Module.symvers` | **Never** copy another build's; reconstruct from the target `vmlinux.elf` (`kernelsu/tools/extract_target_symvers.py`), and for manual-relocation builds keep `__versions` empty (standard modpost against an empty symvers) |
| Gates (before strip/publish) | `modinfo` vermagic = full exact release; `readelf -SW \| grep __versions`; `kernel/check_symbol <ko> <vmlinux.elf>` (v3.3.0 tool **requires `__versions` size 0**); `kernelsu/tools/audit_module_against_target.py [--manual-relocation]`; `.symtab`/`.strtab` retained; strip debug only (`llvm-strip -d`) |

Generic module recipe (adapted from `kernelsu/README.md` A155N pattern):

```sh
# Samsung OSS tree + exact IKCONFIG as out/.config, empty UNUSED_KSYMS_WHITELIST
make O=out ARCH=arm64 LLVM=1 LLVM_IAS=1 CROSS_COMPILE=aarch64-linux-gnu- \
  CLANG_TRIPLE=aarch64-linux-gnu- olddefconfig modules_prepare
# literal target release into out/include/config/kernel.release +
# out/include/generated/utsrelease.h; SELinux genheaders for external module
make -C out M="$PWD/KernelSU/kernel" ARCH=arm64 LLVM=1 LLVM_IAS=1 \
  CROSS_COMPILE=aarch64-linux-gnu- CLANG_TRIPLE=aarch64-linux-gnu- \
  CONFIG_KSU=m CONFIG_KSU_SAMSUNG_KDP=y CONFIG_KSU_SAMSUNG_RKP=y \
  CONFIG_KSU_SAMSUNG_DEFEX=y [CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y] \
  KCFLAGS=-DCONFIG_DEBUG_INFO_BTF_MODULES=1 KBUILD_MODPOST_WARN=1 modules
```

### Profile A — `dm3q-S9180ZHS8FZF5` (legacy `__versions` route)

- Firmware: `SM-S9180`, region `BRI`, `BP4A.251205.006.S9180ZHS8FZF5`,
  kernel `5.15.189-android13-8-33413713-abS9180ZHS8FZF5`
  (`#1 SMP PREEMPT Mon Jun 15 08:10:25 UTC 2026`).
- Existing artifacts (v3.2.5, **kept as-is**): `.ko` 227224 B, vermagic
  `5.15.189-android13-8-33413713-abS9180ZHS8FZF5 SMP preempt mod_unload
  modversions aarch64`; `ksud` 4556352 B = `libksud.so` extracted from
  the official Manager APK (multi-KMI, not exact-source).
- Route: **opposite of S918B** — retains `__versions` + runtime-patches
  `check_version` at `insmod` via the CVE primitive. Re-measured in this
  env (2026-10-01, `extract_target_symvers.py` → 8543 CRCs):
  `undefined 205 / version entries 135 / missing-from-symtab 0`
  (matches the doc's `205/135`; the residual unexported core symbols are
  the runtime-patched set).
- v3.3.0 rebuild: main patch + §6.3 dm3q build-fix, **without**
  `NO_PATCH_TEXT` (keep the `stop_machine` patch path), `Module.symvers`
  reconstructed from `/mnt/240G_SSD/s9180-fzf5/vmlinux-fzf5.elf`
  (host evidence dir, not in repo). Note: v3.3.0 `check_symbol` enforces
  `__versions` size 0, so this legacy route is checked with the
  non-`--manual-relocation` audit only; migrating FZF5 to the
  manual-relocation route is recommended but **hardware-unproven**
  (profile status: test in progress).

### Profile B — `dm3q-S918BXXSAFZF5` (exact-source, no-patch-text route)

- Firmware: `SM-S918B` (EU), `BP4A.251205.006.S918BXXSAFZF5` (June 2026
  patch), kernel `5.15.189-android13-8-33413713-abS918BXXSAFZF5`
  (`.text` identical to `S916BXXSAFZG1`; only `.data`/`P0`/slide differ).
- Artifacts (v3.2.5, **kept as-is**): **no independent `.ko`** — borrows
  `android13-5.15.189_kernelsu-dm2q-S916BXXSAFZG1.ko` (356928 B, vermagic
  `...-abS916BXXSAFZG1 ...`); `ksud-dm3q-S918BXXSAFZF5-kdp` 4879560 B,
  SHA-256 `5da5818d…b86bd`, byte-identical to `ksud-dm2q-S916BXXSAFZG1-kdp`
  (verified `cmp` in this env) = exact-source single-KMI
  (`android13-5.15_kernelsu.ko`) kallsyms-aware manual loader, RKP
  syscall-table + live text patching disabled. Hardware-verified:
  `Live`, Manager `Working <LKM> [Jailbreak mode]`, `su → u:r:ksu:s0`
  enforcing.
- v3.3.0 rebuild: main patch + §6.3 dm3q build-fix, source Samsung
  `SM-S916B_16_Opensource` + live FZG1 config + clang `r450784e`,
  `CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y` (+ optionally the §6.4 RKP
  early-return hunk, applies cleanly). Gates: `check_symbol` clean,
  `--manual-relocation` audit `200 undef / 0 version entries / 0 missing /
  0 CRC mismatches` (doc baseline; §8 re-confirms the tooling chain),
  then 2B §4/§4b ksud rebuild + hardware late-load.
- Audit指標 for the record: manual-relocation **零 `__versions`**
  (`readelf`: `__versions` size `0x0`; cf. FZF5 `0x21c0`).

### Profile C — `dm3q-S9180ZHS8FZG1` (reuse, no kernel build)

- Firmware: `SM-S9180` (`dm3q`/`dm3qzhx`, BRI),
  `BP4A.251205.006.S9180ZHS8FZG1`, kernel
  `5.15.189-android13-8-33413713-abS9180ZHS8FZG1`; AP tar
  `AP_S9180ZHS8FZG1_..._MQB112101698_...tar.md5` in
  `SM-S9180_5_20260715075451_uegztj2s7l_fac.zip` (host evidence
  `/mnt/240G_SSD/s9180-fzg1/`, incl. `vmlinux.elf`).
- **No independent KO/ksud; borrows the S918B ksud** (2B §0
  authoritative; `artifacts/dm3q-S9180ZHS8FZG1/README.md:22` records the
  same `5da5818d…` SHA; hardware-verified `u:r:ksu:s0` enforcing).
  Residual `abS9180ZHS8FZG1` vs `abS916BXXSAFZG1` vermagic-suffix delta
  is covered by the zero-`__versions` kallsyms-aware loader (no plain
  `insmod`).
- Static proof re-run in this env against the **actual FZG1 vmlinux**
  (`/mnt/240G_SSD/s9180-fzg1/vmlinux.elf`, v3.3.0 `check_symbol` built
  with host gcc): `check_symbol dm2q.ko == exit 0`; `--manual-relocation`
  audit `200 undef / 0 version entries / 0 missing / 64 kallsyms-resolved
  / 0 CRC mismatches, exit 0`. (Doc baseline vs the S916B FZG1 vmlinux
  records 200 undef / 0 missing / empty `__versions` with no
  kallsyms-resolved count; the 64 here is measured against the S9180
  FZG1 vmlinux — same pass, per-target delta expected.)
  → No v3.3.0 kernel rebuild needed for FZG1; it tracks profile B's
  S918B rebuild.

## 8. Can this environment actually compile the `.ko`? (honest assessment)

Attempted in this env (2026-10-01) — attempt counts, no `.ko` forced:

| Attempt | Result |
| --- | --- |
| Fresh `v3.3.0` clone (`932014a`) + main patch + dm3q build-fix `patch -p1` | PASS (§6.2) |
| Build v3.3.0 `kernel/tools/check_symbol` with host gcc; run vs FZF5/FZG1 `vmlinux.elf` | PASS (FZF5 legacy ko → expected `__versions size 0` refusal, confirming the route split; dm2q ko → `exit 0` vs **both** FZF5 and FZG1 ELFs) |
| `extract_target_symvers.py` (8543 CRCs each) + `audit_module_against_target.py` for FZF5 (205/135, plain audit for bookkeeping only — expected exit 1 with 71 MISSING_EXPORT/CRC entries, NOT a manual-relocation PASS) and FZG1-reuse (200/0/64, `--manual-relocation` exit 0) | PASS |
| Real `make M=... modules` for a 5.15.189 target | **BLOCKED — no Samsung OSS kernel source tree on this host** (`msm-kernel`/`SM-S91*Opensource*` search negative). Without it there is nothing to run `olddefconfig`/`modules_prepare` against, so no `.ko` was (or could be) produced here. |

Present (premise "無 NDK/無 vmlinux" is stale for this host): NDK
r27/r28/r29 under `~/Android/Sdk/ndk/`; `cargo/rustc 1.98.1`; upstream
`v3.3.0` clone; FZF5 `vmlinux-fzf5.elf` + FZG1 `vmlinux.elf` under
`/mnt/240G_SSD/` (host-side evidence, not repo).

Missing inputs to unblock a real `.ko` build, and how to get them:

1. Samsung OSS source: Kalama 5.15 tree for the 33413713 family —
   `SM-S916B_16_Opensource` (profile B/C) and the S9180 FZF5 Kalama
   5.15 base + per-build delta (profile A) — from Samsung Open Source
   Release Center; verify the tree reports `5.15.189` + changelist
   `33413713` before building.
2. Exact device IKCONFIG (`/proc/config.gz` over ADB, per profile) —
   mandatory (`TRIM_UNUSED_KSYMS`, CFI/LTO/SCS, `MODVERSIONS`,
   `DEBUG_INFO_BTF_MODULES` all change the build).
3. Module compiler clang `r450784e` (NDK r25c) for the `.ko`; NDK r29
   stays for app payloads/ksud userspace.
4. S918B-suffix `vmlinux` (only S9180 FZF5/FZG1 ELFs are on this host;
   S918B reuses the dm2q/S916B recovery per docs — reacquire if a
   S918B-exact audit is required).

## 9. 2A output list

- `kernelsu/patches/KernelSU-v3.3.0-dm3q-5.15-build-fix.patch` (new;
  §6.3; verified `patch -p1` after the main patch, fuzz-free).
- This appendix (§6–§9) merged into `kernelsu/REBUILD-dm3q-v3.3.0.md`
  (2B §0–§5 untouched).
- No existing `.ko`/`ksud` binaries modified; no old files deleted.
