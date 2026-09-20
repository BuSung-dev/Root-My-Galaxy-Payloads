# KernelSU v3.2.5 delta for dm3q-S918WVLS6CYB3 — rationale

Target: SM-S918W, kernel `5.15.148-android13-8-29539737-abS918WVLS6CYB3`
(Samsung android13-5.15 branch).

Apply order on a clean KernelSU v3.2.5 (`b0bc817`) tree:

1. `KernelSU-v3.2.5-samsung-kdp-rkp-defex.patch` (upstream project patch:
   Samsung KDP credential helpers, RKP/DEFEX handling, guarded dispatcher
   install, kretprobe fallbacks).
2. This file (`KernelSU-v3.2.5-dm3q-5.15.148.patch`).

## Hunk 1 — `enum ucount_type`

Samsung android13-5.15 still names the ucounts limit enum `enum
ucount_type`; upstream KernelSU uses `enum rlimit_type` (renamed in 5.16).
Verified in the target BTF: type `ucount_type` present, `rlimit_type`
absent. Without this, the module does not compile against a 5.15 Samsung
tree. Same approach as the in-tree dm1q-android13-5.15 build fix; the
`>= 5.16` gate keeps it correct on newer branches too.

## Hunk 2 — resolve `sys_call_table` before the RKP early return

The dm2q-fzg1 variant returns from `ksu_syscall_hook_init()` immediately
under `CONFIG_KSU_SAMSUNG_RKP`, before `ksu_syscall_table` is resolved.
That leaves the table NULL, so `samsung_sucompat_hook_init()` bails with
`-ENOENT`: `ksud feature list` reports `su_compat [NOT_SUPPORTED]` and
app-facing `/system/bin/su` never registers (observed on the sibling
dm2q-S916U1UES6CYB3 port, fixed by rebuild).

This hunk keeps the RKP early return (live table patching stays off — RKP
pins the table read-only at EL2) but performs the table resolution first,
so the Samsung sucompat kprobes register and `su_compat` reports ENABLED.
Expected dmesg markers after load:

```text
KernelSU: sys_call_table=0xffffffc009d1ed10
KernelSU: hook_manager: Samsung sucompat kprobes registered
```

## Build (once a kernel tree is available)

```sh
CONFIG_KSU=m \
CONFIG_KSU_SAMSUNG_KDP=y \
CONFIG_KSU_SAMSUNG_RKP=y \
CONFIG_KSU_SAMSUNG_DEFEX=y \
CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y \
  <kernel-tree build, ARCH=arm64 LLVM=1, exact release
   5.15.148-android13-8-29539737-abS918WVLS6CYB3>
```

Then: `check_symbol` vs the recovered `vmlinux.elf`, `modinfo` vermagic,
strip debug sections, `audit_module_against_target.py --manual-relocation`
(0 missing, 0 CRC mismatches, empty `__versions`), embed as
`android13-5.15_kernelsu.ko` in `ksud`, publish the pair.
