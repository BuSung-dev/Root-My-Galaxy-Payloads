# Samsung KernelSU late-load builds

The Samsung patches in this directory target KernelSU `v3.3.0`, commit
`932014ab5b2c9b74a3d11e2ec4d17dd10fc9442e` (released 2026-08-28). They are
not interchangeable between KMIs.

> Upgrade note (update-kernelsu branch): the previous baseline was v3.2.5
> (`b0bc817b4e966aa6aa830834eaf6ef765d821d40`, version code `32525`).
> The v3.2.5 patch files are retained in `patches/` for traceability; the
> active Samsung patches are now the `KernelSU-v3.3.0-*.patch` files with
> `KSU_VERSION ?= 32601`. Published `.ko` / `ksud-*` binaries below remain
> v3.2.5 artifacts until each target is rebuilt and re-audited against the
> v3.3.0 tree — do not mix a v3.3.0 `ksud` with a v3.2.5 `.ko` or vice versa.

## Versioned artifacts

| File | Target | KMI | Purpose |
| --- | --- | --- | --- |
| `android15-6.6_kernelsu-s25u-kdp.ko` | `SM-S938N`, `S938NKSUACZF1` | `android15-6.6` | Standalone reference module from the previously deployed S25U build |
| `ksud-s25u-kdp` | `SM-S938N`, `S938NKSUACZF1` | `android15-6.6` | Late-load binary embedding the 6.6 module |
| `android15-6.6_kernelsu-A566EXXSCCZG6-kdp.ko` | `SM-A566E`, `A566EXXSCCZG6` | `android15-6.6` | Exact A56 module with target `vermagic`, audited for manual relocation; live text patching disabled for Exynos EL2 |
| `ksud-A566EXXSCCZG6-kdp` | Same exact A56 build | `android15-6.6` | Device-tested late-load binary embedding the A56 6.6 no-patch-text module |
| `android15-6.6_kernelsu-A366WVLS3AYG1-kdp.ko` | `SM-A366W`, `A366WVLS3AYG1` | `android15-6.6` | Exact A36 module with target `vermagic`, audited for manual relocation; live text patching disabled for Samsung KDP/RKP |
| `ksud-A366WVLS3AYG1-kdp` | Same exact A36 build | `android15-6.6` | Device-tested late-load binary embedding the exact A36 no-patch-text module |
| `android14-6.1_kernelsu-e3q-S928USQS6DZF2-kdp.ko` | `SM-S928U/SM-S928U1`, `S928USQS6DZF2` | `android14-6.1` | Exact E3Q module with target `vermagic`, audited for manual relocation |
| `ksud-e3q-S928USQS6DZF2-kdp` | Same exact E3Q build | `android14-6.1` | Device-tested late-load binary embedding the E3Q module |
| `android14-6.1_kernelsu-e3q-S928BXXS6DZF2-kdp.ko` | `SM-S928B`, `S928BXXS6DZF2` | `android14-6.1` | Exact S928B no-patch-text module with target `vermagic`, audited for manual relocation |
| `ksud-e3q-S928BXXS6DZF2-kdp` | Same exact S928B build | `android14-6.1` | Late-load binary embedding the S928B no-patch-text module; module-load hardware-tested |
| `android14-6.1_kernelsu-e2s-S926BXXUEDZDR-kdp.ko` | `SM-S926B`, `S926BXXUEDZDR` | `android14-6.1` | Exact E2S no-patch-text module with target `vermagic`, audited for manual relocation |
| `ksud-e2s-S926BXXUEDZDR-kdp` | Same exact E2S build | `android14-6.1` | Device-tested late-load binary embedding the E2S no-patch-text module |
| `android14-6.1_kernelsu-e1s-S921NKSSFDZF3-kdp.ko` | `SM-S921N`, `S921NKSSFDZF3` | `android14-6.1` | Exact S921N no-patch-text module with target `vermagic`, audited for manual relocation |
| `ksud-e1s-S921NKSSFDZF3-kdp` | Same exact S921N build | `android14-6.1` | Device-tested late-load binary embedding the S921N no-patch-text module |
| `android14-6.1_kernelsu-e1s-S921BXXSFDZE1-kdp.ko` | `SM-S921B`, `S921BXXSFDZE1` | `android14-6.1` | Exact E1S no-patch-text module with target `vermagic`, audited for manual relocation |
| `ksud-e1s-S921BXXSFDZE1-kdp` | Same exact E1S build | `android14-6.1` | Device-tested late-load binary embedding the E1S no-patch-text module |
| `android14-6.1_kernelsu-samsung-kdp.ko` | `SM-S721N` `S721NKSSCDZF3`; `SM-S921B` `S921BXXSFDZF2` | `android14-6.1` | Standalone Samsung KDP/RKP/DEFEX module with target `vermagic` |
| `ksud-samsung-android14-6.1-kdp` | Same verified 6.1 targets | `android14-6.1` | Late-load binary embedding the 6.1 module |
| `android12-5.10_kernelsu-samsung-kdp.ko` | `SM-A155N` `A155NKSS6BYH1` | `android12-5.10` | Standalone Samsung KDP/RKP/DEFEX module built against the exact A15 kernel |
| `ksud-samsung-android12-5.10-kdp` | `SM-A155N` `A155NKSS6BYH1` | `android12-5.10` | Late-load binary embedding the 5.10 module |
| `android12-5.10_kernelsu-F9360ZCSAIZF1-c12-nolto.ko` | `SM-F9360` `F9360ZCSAIZF1` | `android12-5.10` | Exact Z Fold4 module for auditing; no-LTO clang-12 (stock THIN-LTO function-sections layout panics on load for this kernel), 201-symbol manual relocation, loaded via `init_module` outside `ksud`; device-tested 2026-08-12 and 2026-09-01. A matching `ksud-F9360ZCSAIZF1-kdp` embedding this module is pending |
| `android13-5.15.189_kernelsu-dm2q-S916BXXSAFZG1.ko` | `SM-S916B`, `S916BXXSAFZG1` | `android13-5.15` | Exact-source FZG1 module; RKP syscall-table and live text patching disabled; hardware load untested |
| `ksud-dm2q-S916BXXSAFZG1-kdp` | Same exact S916B build | `android13-5.15` | Kallsyms-aware late-load binary embedding the exact-source FZG1 module; hardware load untested |
| `android13-5.15.189_kernelsu-gts9-X710XXS6EZF1.ko` | `SM-X710`, `X710XXS6EZF1` | `android13-5.15` | Exact-source module with target `vermagic`; RKP syscall-table and live text patching disabled; audited against the recovered X710 vmlinux (200 undefined imports, zero missing, zero CRC mismatches) |
| `ksud-gts9-X710XXS6EZF1-kdp` | Same exact X710 build | `android13-5.15` | Device-tested late-load binary embedding the exact X710 no-patch-text module; KernelSU Manager reports `Working <LKM> [Jailbreak mode]` |
| `android13-5.15.153_kernelsu-dm1q-S911U1UES6DYI3-kdp.ko` | `SM-S911U1`, `S911U1UES6DYI3` | `android13-5.15.153` | Exact DYI3 module with target `vermagic`, audited for manual relocation; no-patch-text build (RKP) with kretprobe fallback hooks |
| `ksud-dm1q-S911U1UES6DYI3-kdp` | Same exact DYI3 build | `android13-5.15.153` | Device-tested late-load binary embedding the exact DYI3 no-patch-text module |
| `android12-5.10_kernelsu-A536EXXSNGZG3-kdp.ko` | `SM-A536E`, `A536EXXSNGZG3` | `android12-5.10` | Device-tested exact A53 module with Samsung KDP/RKP/DEFEX support and live text/table patching disabled |
| `ksud-A536EXXSNGZG3-kdp` | Same exact A53 build | `android12-5.10` | Device-tested late-load binary embedding the exact A53 module |

The standalone `.ko` files are retained for auditing. Root My Galaxy downloads
the corresponding `ksud-*` file because `ksud late-load` loads its embedded
`<kmi>_kernelsu.ko` asset.

The S916B FZG1 pair is built from Samsung's released `SM-S916B_16_Opensource` tree with the live FZG1 config and Android clang `r450784e`. Its zero-length `__versions` section and retained symbol tables are intended for KernelSU's kallsyms-aware manual loader. Audit against the exact recovered FZG1 `vmlinux.elf` found all 200 undefined names. Plain `insmod` is not supported. The target patch [`KernelSU-v3.3.0-dm2q-fzg1.patch`](patches/KernelSU-v3.3.0-dm2q-fzg1.patch) selects the exact FZG1 `enum ucount_type` ABI and hard-stops RKP syscall-table writes; the build also sets `CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y`. Use the root helper's guarded `--late-load` operation so the loader's security-domain and stdio transition can complete safely. Module initialization is not yet confirmed on S916B hardware.

The generic 6.1 files remain build-verified only. The E3Q pair is
device-tested and tied to the full S928U DZF2 release string; it must not be replaced
with the generic 6.1 pair. The S928B pair is tied to the full S928B DZF2 release,
uses the no-patch-text Samsung path, is statically audited, and has now been
hardware-tested through module load: the exact `ksud` loaded `kernelsu.ko`,
entered `u:r:ksu:s0`, and survived without a reboot. KernelSU Manager and
Root Checker were then verified: Manager reported `Working <LKM> [Jailbreak
mode]`, version `32525-2`, and one superuser, while Root Checker reported root
access installed. The root remains per-boot because no boot image was
modified; reboot survival is untested.
The E2S pair is tied to the S926B DZDR release,
static-audited, and device-tested: late-load reports version code `32525`, and
the loader runs in `u:r:ksu:s0`. The E1S pair is tied to the S921B DZE1 release,
static-audited against the recovered DZE1 `vmlinux` (202 undefined symbols, zero
missing, zero CRC mismatches, no `stop_machine`), and device-tested: the
no-patch-text module late-loads cleanly and reports KernelSU active. On the same
Exynos 2400 the generic 6.1 module panics in Samsung/Exynos EL2 while attempting
live text patching, so DZE1 uses the no-patch-text build. The A56 CCZG6 pair is
exact-release,
static-audited, and
device-tested. Its first hardware late-load builds panicked in Samsung/Exynos
EL2 while KernelSU tried live text patching; the current A56 build disables
that path, uses the Samsung fallback hooks, loads successfully, and reports
KernelSU version code `32525` for manager compatibility. The A36 AYG1 pair
uses the same fail-closed Samsung path, reports the exact A36 kernel release,
passes the recovered-target symbol audit, and was loaded on hardware with
KernelSU Manager reporting `Working <LKM> [Jailbreak mode]` and version
`32525-2`. The A536E GZG3 5.10 pair was also loaded from the normal Root My
Galaxy app flow; KernelSU Manager reported `Working <LKM> [Jailbreak mode]`
and version `32525-2`. The older A15 5.10 pair remains device-untested. The exact F9360ZCSAIZF1 no-LTO module above is device-tested (full-chain root and KernelSU Manager recognition on hardware, 2026-08-12 and 2026-09-01).

## Why the stock module crashes on Samsung

The original S25U failure was captured in
`ksu_mark_running_process_locked+0x154`: a generic inline `put_cred()` wrote
directly to a KDP-protected credential reference count. Samsung's kernel uses
`kdp_usecount_inc_not_zero()` and `kdp_usecount_dec_and_test()` for those
objects; bypassing that path caused a synchronous external abort.

Three other Samsung-specific conflicts were confirmed during the 6.6 port:

1. RKP rejected KernelSU's write to an unused syscall-table slot. The generic
   code nevertheless treated the dispatcher as installed, which redirected
   marked syscalls to the unchanged `ni_syscall` entry.
2. DEFEX retained its own task credential tuple. A KernelSU UID transition
   without synchronizing that tuple triggered credential violations, while
   Safeplace/Immutable-root killed KSU-domain helpers.
3. Late-load could not write a new `/data/adb/ksud` after the module changed the
   loader's security context. The failed destination remained a zero-byte file.

## Patch contents

[`patches/KernelSU-v3.3.0-samsung-kdp-rkp-defex.patch`](patches/KernelSU-v3.3.0-samsung-kdp-rkp-defex.patch)
contains the complete source delta from the tagged v3.3.0 tree (ported from
the v3.2.5 Samsung KDP/RKP/DEFEX patch; v3.2.5 original retained alongside):

- resolve Samsung KDP credential helpers and release protected credentials with
  `kdp_usecount_dec_and_test()` plus `__put_cred()`;
- install KDP credentials through `prepare_ro_creds()` on a root workqueue and
  update the target task with the firmware-native `kdp_assign_pgd()` path;
- synchronize the DEFEX task credential record after a successful transition;
- limit the DEFEX allow path to the current UID-0 task already in `u:r:ksu:s0`;
- record a syscall-table hook only if the RKP-protected write succeeds;
- when the dispatcher is unavailable, preserve Manager FD delivery with a
  `__arm64_sys_setresuid` kretprobe and provide sucompat through address-based
  syscall kprobes without modifying the syscall table;
- mark nested sucompat calls so a handler invoking the original syscall cannot
  recursively enter the same kprobe;
- stage `ksud` at `/data/local/tmp/.ksud-stage`, rename it onto the same
  `/data` filesystem before loading the module, then finish labels/assets after
  the module is active.

Target-specific companions (v3.2.5 originals retained alongside for
traceability):

- [`patches/KernelSU-v3.3.0-dm2q-fzg1.patch`](patches/KernelSU-v3.3.0-dm2q-fzg1.patch):
  FZG1 `enum ucount_type` ABI plus RKP syscall-table hard-stop.
- [`patches/KernelSU-v3.3.0-dm1q-android13-5.15-build-fix.patch`](patches/KernelSU-v3.3.0-dm1q-android13-5.15-build-fix.patch):
  version-gated `ucount_type` (`<5.16`) vs `rlimit_type` (`>=5.16`) build fix;
  this is the correct generalization that supersedes the dm2q unconditional
  typedef.

## v3.3.0 rebase notes

The v3.3.0 patch set was verified with `git apply --check` and a real
application against a clean `v3.3.0` (`932014ab`) checkout. Adaptations from
the v3.2.5 delta:

- `kernel/Kbuild`: Samsung `KDP/RKP/DEFEX/NO_PATCH_TEXT` flags re-anchored
  around the new upstream `CONFIG_KSU_X86_PATCH_SYSCALL_DISPATCHER` block and
  the 6.18 `srcroot`/`KSU_KERNEL_DIR` guard; the `-I$(KSU_KERNEL_DIR)/..`
  addition is kept.
- `kernel/core/init.c`: include/init-order hunks re-anchored to the new
  `__x86_64__ && !defined(CONFIG_KSU_X86_PATCH_SYSCALL_DISPATCHER)` guard;
  upstream's added `ksu_app_profile_init()` call site is preserved.
- `kernel/hook/arm64/patch_memory.c`: `NO_PATCH_TEXT` early-return kept;
  the hunk now ends before upstream's new `scan_call_to()` helper.
- `userspace/ksud`: `late_load.rs` no longer calls `install()`; it stages
  first and then calls 2-argument `finish_install(None, None)`, matching the
  new upstream `install(None, None)` call shape.
  The `install()` split keeps the `data_path` boot-backup move and the
  `libadbroot` handling inside `finish_install()`; `cli.rs`
  keeps calling 2-arg `install()`, and `daemonize()` removal stays safe (only
  `late-load` called it, and that call is replaced by staging).
  Behavior change carried over from v3.2.5 and kept here: staging replaces
  upstream's `/proc/self/exe` self-copy (with its `DO NOT resolve` rule) by
  `current_exe()` read/write with an early-return when already running from
  `DAEMON_PATH`, plus `rename()` of the externally staged
  `/data/local/tmp/.ksud-stage` (staged by the payload side before invoking
  `late-load`; `chown root:root` is applied on that rename path only).
- Companions: `dm1q`/`dm2q` typedef hunks re-anchored to the `6.12`
  `kdp_usecount_sub_and_test` branch now present in `samsung_kdp.c`.
  Apply order is main patch first, then at most one companion.

Unresolved upstream deltas that still need per-target hardware validation
before any binary is rebuilt (from the Phase-0 investigation comparing
`v3.2.5...v3.3.0`, 76 commits): the `execveat` handling, tracepoint
minimum-priority hook ordering, webview-zygote `umount` semantics,
and the tightened APK signature-block verification (only v2 blocks).

## 6.1 generalization

The first 6.6 implementation invoked an S25U-specific secure monitor command to
assign the task PGD and linked directly against the 6.6 DDK's
`kdp_usecount_dec_and_test` export. The 6.1 build removes both target-specific
assumptions:

- `kdp_assign_pgd(struct task_struct *)` is resolved from the running kernel and
  used as the firmware-native PGD update entry point;
- `kdp_usecount_dec_and_test(struct cred *)` is resolved at runtime, avoiding a
  dependency on a DDK-specific exported-symbol CRC.

Both function prototypes were verified against each target's own BTF before
building. SM-S921B is an Exynos 2400 target and is not compatibility evidence
for Snapdragon E3Q. The E3Q module was therefore rebuilt with the exact
S928U DZF2 release and audited independently against its recovered
`vmlinux.elf`.

## 5.10 generalization

Samsung 5.10 predates the `cred->ucounts` RLIMIT conversion used by the 6.1
build. Its native `commit_creds()` updates `cred->user->processes` directly.
The patch selects that sequence below Linux 5.11 and keeps the existing
`inc_rlimit_ucounts()` / `dec_rlimit_ucounts()` path for later kernels.

The A15 kernel also enables `CONFIG_TRIM_UNUSED_KSYMS`. The matching `ksud`
uses KernelSU's existing manual-relocation loader: it replaces undefined ELF
symbols with absolute `/proc/kallsyms` addresses before `init_module`. The KO
therefore has a zero-length `__versions` section, as required by
`kernel/check_symbol`, while preserving `.symtab` and `.strtab`. All 208
undefined imports were checked against the recovered A15 `vmlinux`; the KDP,
DEFEX, syscall-table, and kprobe symbols resolved by name were checked
separately.

## Rebuild

Apply the patch to a clean v3.3.0 checkout:

```sh
git checkout v3.3.0  # 932014ab5b2c9b74a3d11e2ec4d17dd10fc9442e
git apply KernelSU-v3.3.0-samsung-kdp-rkp-defex.patch
```

Upstream v3.3.0 references DDK `20260828` (adds android17-6.18 and x64 LKM
builds). The pinned DDK below is unchanged: keep building each target
against its pinned DDK and exact `UTS_RELEASE`.

Rebuild status on this branch: none of the published `.ko` / `ksud-*`
binaries above has been rebuilt yet — all remain v3.2.5 artifacts. Rebuilding
is per target, in this order: exact-`vermagic` module → `check_symbol`
against the recovered target `vmlinux.elf` →
`audit_module_against_target.py --manual-relocation` (zero missing, zero
`__versions`, zero CRC mismatches, no `stop_machine` import) → embed into
`ksud` and publish the `.ko` / `ksud-*` pair together → hardware late-load
(`u:r:ksu:s0`, Manager `Working <LKM>` with version `32601`, no mismatch
banner). `support/targets-v3.json` needs no schema change; only replaced
artifacts' `size` fields get updated on rebuild. `targets-v2.json` stays
frozen.

For the Samsung 6.1 module, use DDK image
`ghcr.io/ylarod/ddk-min:android14-6.1-20260313` and set:

```sh
CONFIG_KSU=m
CONFIG_KSU_SAMSUNG_KDP=y
CONFIG_KSU_SAMSUNG_RKP=y
CONFIG_KSU_SAMSUNG_DEFEX=y
```

The DDK image defaults to `6.1.166-dirty`, but the target has
`CONFIG_MODULE_FORCE_LOAD=n`. Before building, replace the DDK's generated
`UTS_RELEASE` and `include/config/kernel.release` with the exact target release
`6.1.157-android14-11`. The resulting module must report:

```text
vermagic: 6.1.157-android14-11 SMP preempt mod_unload modversions aarch64
```

The exact container build used here was:

```sh
docker run --rm \
  -v "$PWD:/workspace" \
  -w /workspace/kernel \
  ghcr.io/ylarod/ddk-min:android14-6.1-20260313 \
  bash -lc '
    sed -i "s/6.1.166-dirty/6.1.157-android14-11/g" \
      "$KDIR/include/generated/utsrelease.h" \
      "$KDIR/include/config/kernel.release"
    make clean
    CONFIG_KSU=m \
    CONFIG_KSU_SAMSUNG_KDP=y \
    CONFIG_KSU_SAMSUNG_RKP=y \
    CONFIG_KSU_SAMSUNG_DEFEX=y \
    CC=clang make -j$(nproc)
    modinfo ./kernelsu.ko
  '
```

Run the generated symbol checker against the recovered target ELF before
stripping:

```sh
kernel/check_symbol kernel/kernelsu.ko /path/to/S721NKSSCDZF3/vmlinux.elf
kernel/check_symbol kernel/kernelsu.ko /path/to/S921BXXSFDZF2/vmlinux.elf
llvm-strip -d kernel/kernelsu.ko
```

For E3Q DZF2, replace the generated DDK release with the full exact target
release before building:

```text
6.1.145-android14-11-33419968-abS928USQS6DZF2
```

After `check_symbol`, audit the manual-relocation contract against the exact
E3Q ELF and the target-derived symbol-version list:

```sh
python3 kernelsu/tools/audit_module_against_target.py \
  kernel/kernelsu.ko \
  /path/to/S928USQS6DZF2/vmlinux.elf \
  /path/to/S928USQS6DZF2/Module.symvers \
  --manual-relocation
```

This must report zero symbols missing from the target symbol table, zero
module version entries, and zero CRC mismatches. The E3Q audit has 209
undefined imports, all present in the target ELF; 50 are intentionally
resolved through kallsyms rather than conventional exports.

The E2S module uses the same upstream Samsung no-patch-text source as the A56
build, but is rebuilt for the exact 6.1 target release. Its manual-relocation
audit reports 202 undefined imports, zero missing target symbols, an empty
`__versions` section, zero target CRC mismatches, and no `stop_machine`
imports. The published artifacts are:

```text
android14-6.1_kernelsu-e2s-S926BXXUEDZDR-kdp.ko
size: 398368
SHA-256: a6c521a2f660f595f4ea359c243e27b85142cbcd832c84340dac0994f8d12135

ksud-e2s-S926BXXUEDZDR-kdp
size: 4780056
SHA-256: dc3eb02640492a8d6f78f8515c6ae5c75ddbfa593f53cd0f3efdfc82a29c4219
```

Copy the stripped module to:

```text
userspace/ksud/bin/aarch64/android14-6.1_kernelsu.ko
```

Then rebuild `ksud` for `aarch64-linux-android`. The late-load binary and its
embedded module must always be published together.

On Windows with NDK r29, the build environment used was:

```powershell
$ndkBin = "$env:LOCALAPPDATA\Android\Sdk\ndk\29.0.14206865\toolchains\llvm\prebuilt\windows-x86_64\bin"
$env:PATH = "$ndkBin;C:\Program Files\Eclipse Adoptium\jdk-17.0.18.8-hotspot\bin;$env:PATH"
$env:LIBCLANG_PATH = $ndkBin
$env:CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER = "$ndkBin\aarch64-linux-android35-clang.cmd"
$env:CC_aarch64_linux_android = "$ndkBin\aarch64-linux-android35-clang.cmd"
$env:AR_aarch64_linux_android = "$ndkBin\llvm-ar.exe"
cargo build --release --target aarch64-linux-android -p ksud
```

## Rebuild the A155N 5.10 artifact

Use the exact `A155NKSS6BYH1` IKCONFIG and Samsung source commit
`5074ff414f1b835fba113b71175d4f217b1cdc39`. Prepare the target tree with the
same compiler target from the first configuration step:

```sh
cp target.config out/.config
scripts/config --file out/.config \
  --set-str UNUSED_KSYMS_WHITELIST /path/to/abi_symbollist.raw
make O=out ARCH=arm64 LLVM=1 LLVM_IAS=1 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  CLANG_TRIPLE=aarch64-linux-gnu- olddefconfig modules_prepare
```

Generate SELinux headers for the external module and set the literal target
release in `out/include/config/kernel.release` and
`out/include/generated/utsrelease.h`. Do not import another device's
`Module.symvers`; the late loader requires an empty `__versions` section.

```sh
mkdir -p out/security/selinux
out/scripts/selinux/genheaders/genheaders \
  out/security/selinux/flask.h \
  out/security/selinux/av_permissions.h

make -C out M="$PWD/KernelSU/kernel" src="$PWD/KernelSU/kernel" \
  ARCH=arm64 LLVM=1 LLVM_IAS=1 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  CLANG_TRIPLE=aarch64-linux-gnu- \
  CONFIG_KSU=m \
  CONFIG_KSU_SAMSUNG_KDP=y \
  CONFIG_KSU_SAMSUNG_RKP=y \
  CONFIG_KSU_SAMSUNG_DEFEX=y \
  KBUILD_MODPOST_WARN=1 modules
```

Validate and strip only debug sections:

```sh
KernelSU/kernel/check_symbol \
  KernelSU/kernel/kernelsu.ko /path/to/A155NKSS6BYH1/vmlinux
modinfo KernelSU/kernel/kernelsu.ko | grep vermagic
readelf -SW KernelSU/kernel/kernelsu.ko | grep __versions
llvm-strip -d KernelSU/kernel/kernelsu.ko
```

Expected metadata:

```text
vermagic: 5.10.226-android12-9-31117096 SMP preempt mod_unload modversions aarch64
__versions size: 0
```

Copy the stripped KO to
`userspace/ksud/bin/aarch64/android12-5.10_kernelsu.ko`, force `ksud` to
recompile after the asset changes, and publish the KO and late-load binary as
one versioned pair.
