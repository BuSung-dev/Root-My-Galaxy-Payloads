# Samsung KernelSU late-load builds

The files in this directory are built from KernelSU `v3.2.5`, commit
`b0bc817b4e966aa6aa830834eaf6ef765d821d40`. They are not interchangeable
between KMIs.

## Versioned artifacts

| File | Target | KMI | Purpose |
| --- | --- | --- | --- |
| `android15-6.6_kernelsu-s25u-kdp.ko` | `SM-S938N`, `S938NKSUACZF1` | `android15-6.6` | Standalone reference module from the previously deployed S25U build |
| `ksud-s25u-kdp` | `SM-S938N`, `S938NKSUACZF1` | `android15-6.6` | Late-load binary embedding the 6.6 module |
| `android14-6.1_kernelsu-e3q-S928USQS6DZF2-kdp.ko` | `SM-S928U/SM-S928U1`, `S928USQS6DZF2` | `android14-6.1` | Exact E3Q module with target `vermagic`, audited for manual relocation |
| `ksud-e3q-S928USQS6DZF2-kdp` | Same exact E3Q build | `android14-6.1` | Late-load binary embedding the E3Q module |
| `android14-6.1_kernelsu-samsung-kdp.ko` | `SM-S721N` `S721NKSSCDZF3`; `SM-S921B` `S921BXXSFDZF2` | `android14-6.1` | Standalone Samsung KDP/RKP/DEFEX module with target `vermagic` |
| `ksud-samsung-android14-6.1-kdp` | Same verified 6.1 targets | `android14-6.1` | Late-load binary embedding the 6.1 module |
| `android12-5.10_kernelsu-samsung-kdp.ko` | `SM-A155N` `A155NKSS6BYH1` | `android12-5.10` | Standalone Samsung KDP/RKP/DEFEX module built against the exact A15 kernel |
| `ksud-samsung-android12-5.10-kdp` | `SM-A155N` `A155NKSS6BYH1` | `android12-5.10` | Late-load binary embedding the 5.10 module |

The standalone `.ko` files are retained for auditing. Root My Galaxy downloads
the corresponding `ksud-*` file because `ksud late-load` loads its embedded
`<kmi>_kernelsu.ko` asset.

The 6.1 files are build-verified but device-untested. The E3Q pair is tied to
the full S928U DZF2 release string and must not be replaced with the generic
6.1 pair. The 5.10 files are also build-verified and device-untested.

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

[`patches/KernelSU-v3.2.5-samsung-kdp-rkp-defex.patch`](patches/KernelSU-v3.2.5-samsung-kdp-rkp-defex.patch)
contains the complete source delta from the tagged v3.2.5 tree:

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

Apply the patch to a clean v3.2.5 checkout:

```sh
git checkout v3.2.5
git apply KernelSU-v3.2.5-samsung-kdp-rkp-defex.patch
```

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
