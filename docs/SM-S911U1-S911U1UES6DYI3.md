# Galaxy S23 SM-S911U1 / S911U1UES6DYI3 port record

This record contains the exact inputs and derived values for the base Galaxy
S23 (Snapdragon 8 Gen 2 / Kalama, codename `dm1q`) profile
`dm1q-S911U1UES6DYI3`. The kernel is Samsung's `android13-5.15` branch at
5.15.153 with KDP, RKP, and DEFEX hardening. It is 36 Android sublevels older
than the S23 Ultra `dm3q-S9180ZHS8FZF5` profile and — importantly — Samsung
restructured `task_struct` and `worker_pool` between the two builds, so no
layout value was carried over from that sibling without re-derivation.

## Firmware identity and acquisition

Samsung FUS was queried with samloader 2.0.0 for model `SM-S911U1`, region
`XAA`. The four-part DYI3 version was still served as the previous-stable
binary of its line:

```text
S911U1UES6DYI3/S911U1OYM6DYI3/S911U1UES6DYI3/S911U1UES6DYI3
```

Device-side ground truth (read over ADB from the target phone) confirms the
build:

```text
model: SM-S911U1
device: dm1q (product dm1quew)
display build: AP3A.240905.015.A2.S911U1UES6DYI3
fingerprint: samsung/dm1quew/dm1q:15/AP3A.240905.015.A2/S911U1UES6DYI3:user/release-keys
SDK: 35 (Android 15, One UI 7)
CSC: XAA
security patch: 2025-09-01
kernel release: 5.15.153-android13-8-30958972-abS911U1UES6DYI3
kernel build: #1 SMP PREEMPT Wed Sep 3 06:21:36 UTC 2025
```

## Kernel extraction and hashes

AP tar → `boot.img.lz4` (LZ4-frame) → boot image header v4, 4096-aligned,
`kernel_size` u32 at 0x08, kernel blob at 0x1000:

```text
boot.img size: 100663296
boot.img SHA-256: 05D52B10E84FBAFB8B09E696D7AAD6BFDB757706EF0168C611B3DE8644FC70F6
kernel size: 45550080
kernel SHA-256: A82D979745F3821816A3F870F5DC6A1A595B7C4818D748EF3E620AD823EF30D2
ARM64 Image text_offset: 0x0
ARM64 Image flags: 0xa
```

## Symbol and BTF recovery

`vmlinux-to-elf` recovered 122,985 symbols at image base
`0xffffffc008000000`. The raw-BTF scan found exactly one validated blob:

```text
vmlinux.btf: [0x20f5f6c, 0x26a24ae), 5948738 bytes
```

All exploit-relevant layouts were derived from this BTF. **The task_struct
layout differs materially from the 5.15.189 S23 profile**: `cred` moved from
`0x5e0` to `0x798`, `real_cred` to `0x790`, `sched_task_group` sits at
`0x400`, and the pi block (`pi_lock` `0x884`, `pi_waiters` `0x898`,
`pi_top_task` `0x8a8`, `pi_blocked_on` `0x8b0`) shifted −0xa0 relative to
dm3q. `worker_pool.worklist` moved to `0x20` (dm3q 0x28) and `nr_idle` to
`0x34`. Conversely `rt_mutex_waiter` (`0x58`, task `0x30`, lock `0x38`,
wake_state+prio `0x40/0x44`), the `file_operations` member offsets
(llseek `0x08` … splice_read `0xc8`, show_fdinfo `0xe0`; sizeof is `0x120`
with kabi reserved words), `struct page` (`0x40`, compound_head `0x08`,
slab_cache `0x18`, page_type `0x30`), `miscdevice.fops` `0x10`, the
pool_workqueue/workqueue/work_struct fields, and the configfs buffer fields
are identical to the 5.15.189 GKI layout. `selinux_state.enforcing` is the
first member (offset `0x0`) in this build.

| Macro/use | Symbol or derivation | Offset |
| --- | --- | ---: |
| `INIT_TASK_OFF` | `init_task` | `0x02ac9bc0` |
| `PREPARE_KERNEL_CRED_OFF` | `prepare_kernel_cred` | `0x0011db04` |
| `COMMIT_CREDS_OFF` | `commit_creds` | `0x0011f840` |
| `OVERRIDE_CREDS_OFF` | `override_creds` | `0x0011e918` |
| `ROOT_TASK_GROUP_OFF` | `root_task_group` | `0x02b79ac0` |
| `SELINUX_ENFORCING_OFF` | `selinux_state` + 0 (`enforcing` first member) | `0x02c4e438` |
| `KMALLOC_CACHES_OFF` | `kmalloc_caches` | `0x01f77a90` |
| `ANON_PIPE_BUF_OPS_OFF` | `anon_pipe_buf_ops` | `0x01da30a0` |
| `SYSTEM_UNBOUND_WQ_OFF` | `system_unbound_wq` | `0x0295e480` |
| `CALL_USERMODEHELPER_EXEC_WORK_OFF` | `call_usermodehelper_exec_work` | `0x00103e50` |
| `ASHMEM_FOPS_OFF` | `ashmem_fops` | `0x01f211d0` |
| `ASHMEM_MISC_FOPS_OFF` | `ashmem_misc + 0x10` | `0x02ac1b88` |
| `ASHMEM_IOCTL_OFF` | `ashmem_ioctl` | `0x010b5de0` |
| `ASHMEM_COMPAT_IOCTL_OFF` | `compat_ashmem_ioctl` | `0x010b643c` |
| `ASHMEM_MMAP_OFF` | `ashmem_mmap` | `0x010b6494` |
| `ASHMEM_OPEN_OFF` | `ashmem_open` | `0x010b6774` |
| `ASHMEM_RELEASE_OFF` | `ashmem_release` | `0x010b680c` |
| `ASHMEM_SHOW_FDINFO_OFF` | `ashmem_show_fdinfo` | `0x010b6928` |
| `CONFIGFS_READ_ITER_OFF` | `configfs_read_iter` | `0x005d30a8` |
| `CONFIGFS_BIN_WRITE_ITER_OFF` | `configfs_bin_write_iter` | `0x005d3ad0` |
| `COPY_SPLICE_READ_OFF` | `generic_file_splice_read` (5.15.153 predates the `copy_splice_read` rename) | `0x00523f70` |
| `NOOP_LLSEEK_OFF` | `noop_llseek` | `0x004b6da4` |
| `SLIDE_NFULNL_LOGGER_NAME_OFF` | `"nfnetlink_log"` string, read from qword 0 of `nfulnl_logger` | `0x01c96c94` |
| `SLIDE_NFULNL_LOGGER_OBJECT_OFF` | `nfulnl_logger` | `0x02961dc0` |
| `SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_OFF` | `.data` slot of the `boot_id` entry (index 4) in `random_table[]` | `0x02a7f960` |
| `SLIDE_SYSCTL_BOOTID_OFF` | `sysctl_bootid` | `0x02ceaf29` |

Cross-check: the `boot_id` entry's `.data` field in the raw Image contains
exactly `sysctl_bootid`, confirming both boot-id derivations.

## Route update: PR 231 cross-validation and the tracefs/MCAST route

After this profile's first build, PR #231 added a device-tested
`dm1q-S911BXXSAFZE1` port (SM-S911B, 5.15.189, One UI 8) whose full chain
succeeded on hardware. Its values independently confirm every layout this
profile derived from BTF: `cred` 0x798, `real_cred` 0x790,
`sched_task_group` 0x400, the whole fake-task pi block (0x38/0x7c/0x84/
0x884/0x898/0x8a8/0x8b0), `worker_pool` 0x20/0x34, event ID 108, and
`P0_KERNEL_PHYS_LOAD` 0x80080000 — identical despite the 36-sublevel gap.

That cross-check also exposes a defect in the older `dm3q-S9180ZHS8FZF5`
profile: it ships `cred` 0x5e0, `pi_lock` 0x924, `worklist` 0x28 for the
same `33413713` kernel build family, contradicting the device-proven values
— which plausibly explains why that profile never progressed past
"test in progress".

The profile was therefore rebuilt on the PR 231 exploit generation:
tracefs KASLR slide (canonical data mode; never force `SLIDE_P0_OFFSET`),
controlled mm_struct slab, SKB reclaim, MCAST stale-waiter write
(`SLIDE_STACK_WRITER=1`), closed fops route, configfs, pipe, UMH root,
`ksud --allow-shell` late-load. The pselect/PI-stack-reclaim machinery —
and with it `SLIDE_PSELECT_WORD_SHIFT`, this port's one unresolved
constant — is not used by this route; the earlier analysis is retained
below for the record.

New firmware-specific derivation required by the tracefs slide:
`SLIDE_TRACEFS_VFORK_CALLER_OFF` — for a vfork parent blocked in
`wait_for_vfork_done`, the wchan unwind (`in_sched_functions` skips
`__sched` frames) lands at the return of `bl wait_for_common` inside
`wait_for_vfork_done`: `0xffffffc0080c8fb4` → `0x000c8fb4`
(FZE1 uses 0x000c8fe4 for the same structure). The worker anchor stays
`0x0010d370` as derived earlier. The `p0_fingerprint.h` probe offset
(0x1f0000) matches the PR 231 convention.

Updated artifacts:

```text
artifacts/dm1q-S911U1UES6DYI3/cve-2026-43499-app.so
size: 130464
SHA-256 35d03089ef6192d14d4b6f1b0472d0f77f2529d8afda3947e9aa2351a3a586e3
```

(The 104128-byte fixed-size `release` target predates the PR 231 route,
whose payloads are ~130 KB; like the FZE1 artifacts, the plain app build
is published with its true size in the feed.)

## Slide parameters (original pselect-route build, superseded)

- `SLIDE_TRACEFS_EVENT_ID` **108**, read authoritatively on-device from
  `/sys/kernel/tracing/events/sched/sched_blocked_reason/id`. The offline
  computation agrees: `__event_sched_blocked_reason (0xffffffc00a9177d8) -
  __start_ftrace_events (0xffffffc00a917518)) / 8 = 88` zero-based index, and
  `__TRACE_LAST_TYPE == 20` ⇒ 108.
- `SLIDE_TRACEFS_WORKER_CALLER_OFF` `0x0010d370`: in `worker_thread`, the
  blocking `bl schedule` is at `0xffffffc00810d36c`; the following
  instruction (`nop` at `+0x78`) is the saved return PC.
- `SLIDE_PSELECT_WORD_SHIFT` **3** — the android13-5.15 family value. The
  static frame-chain analysis on this build is inconclusive: measuring from
  the syscall entry SP, the `core_sys_select` fd-set copy base
  (`__arm64_sys_pselect6` 0xa0 frame → `core_sys_select` 0x1c0 frame, bits at
  SP+0x50) sits at −0x210, while the `rt_waiter` local of the
  `FUTEX_WAIT_REQUEUE_PI` path (`__arm64_sys_futex` 0x80 → `do_futex` 0x140 →
  `futex_wait_requeue_pi` 0x1b0 frame, waiter at SP+0x98, confirmed via the
  `try_to_take_rt_mutex`/`rt_mutex_slowlock_block` argument registers) sits
  at −0x2d8, i.e. 25 qwords below the copy base, outside the 15-qword copied
  region. The S926B record documents the same class of static-vs-hardware
  divergence (static said 0, hardware proved 3 and was corrected by panic
  readback), so the family default is used until a device run says otherwise.
  If the first device run faults or reports waiter misalignment in
  `rt_mutex_adjust_prio_chain`, read the qword displacement from the
  diagnostic/panic output and adjust this single constant.

## Physical load proof

The BL archive of this Qualcomm target contains `abl.elf` (ARM32 EFI
application, stripped release build) rather than `sboot.bin`; it carries no
 analyzable load-address literals. `P0_PHYS_OFFSET 0x80000000` /
`P0_KERNEL_PHYS_LOAD 0x80080000` are adopted from the same-SoC (SM8550)
`dm3q-S9180ZHS8FZF5` record and the Qualcomm ABL convention for ARM64 boot
images. This choice is fail-closed: the physical P0 oracle must
fingerprint-match the probed page against the table below or the exploit
aborts before any write.

## P0 table and payload build

`src/targets/dm1q-S911U1UES6DYI3/p0_fingerprint.h` was generated with
`tools/generate_p0_fingerprint.pl` at probe offset `0x1f0000`
(= `P0_ORACLE_PROBE_OFFSET`, the runtime probe address derived from
`P0_DATA_ALIAS_CONST(KIMAGE_TEXT_BASE) + P0_ORACLE_PROBE_OFFSET`); all 32
slide candidates and 256 source qwords verified by readback.

```sh
make TARGET=dm1q-S911U1UES6DYI3 ANDROID_NDK_HOME=... release
```

The fixed-size result (104128 bytes) is published at
`artifacts/dm1q-S911U1UES6DYI3/cve-2026-43499-app.so`:

```text
SHA-256 12b43d1d05f17be4f304796dac7658970adf5cac5af29937146a379ccf1ad591
```

Every symbol offset in `target.h` was re-validated programmatically against
`vmlinux.nm` after the initial build (this caught six ashmem offsets that
would otherwise have shipped wrong).

## dm3q P0 fingerprint discrepancy

While surveying sibling profiles, `src/targets/dm3q-S9180ZHS8FZF5/p0_fingerprint.h`
was found generated at probe `0x400000`, while every other profile and the
runtime oracle math (`P0_DATA_ALIAS_CONST(KIMAGE_TEXT_BASE) +
P0_ORACLE_PROBE_OFFSET` with `P0_ORACLE_PROBE_OFFSET == 0x1f0000`) use
`0x1f0000`. If the dm3q table content (not just its comment) really was
generated at 0x400000, that profile's physical oracle cannot match on
hardware and should be regenerated. This port used 0x1f0000.

## KernelSU compatibility

Built from KernelSU `v3.2.5` (commit `b0bc817`) with
`kernelsu/patches/KernelSU-v3.2.5-samsung-kdp-rkp-defex.patch` plus the
incremental
`kernelsu/patches/KernelSU-v3.2.5-dm1q-android13-5.15-build-fix.patch`
(Samsung `android13-5.15` still names the ucounts parameter type
`enum ucount_type`; the 5.16+ `enum rlimit_type` rename is guarded by
version).

Source tree: Samsung OSS `SM-S911B_15_Opensource.zip` (Kalama 5.15,
DYF1-era base) overlaid with the per-build
`SM-S911B_15_Opensource_S911BXXS8DYI3.zip` delta; the tree's Makefile then
reports exactly 5.15.153. The build used the **exact device IKCONFIG** read
over ADB from `/proc/config.gz` — critical because this kernel enables
`CONFIG_TRIM_UNUSED_KSYMS`, `CONFIG_LTO_CLANG_FULL`, `CONFIG_CFI_CLANG`,
`CONFIG_SHADOW_CALL_STACK`, `CONFIG_MODVERSIONS`, and signature enforcement.
Procedure: `olddefconfig` + `modules_prepare` with `LLVM=1 LLVM_IAS=1`
(host clang 22 plus `-Wno-default-const-init-var-unsafe` for one new
clang-22 warning tripping 5.15's `-Werror`), empty
`UNUSED_KSYMS_WHITELIST`, literal target release written into
`include/config/kernel.release` and `include/generated/utsrelease.h`,
SELinux `genheaders` run for the external module, and the module built with
`CONFIG_KSU=m CONFIG_KSU_SAMSUNG_KDP=y CONFIG_KSU_SAMSUNG_RKP=y
CONFIG_KSU_SAMSUNG_DEFEX=y KBUILD_MODPOST_WARN=1`.

Metadata and audits:

```text
vermagic: 5.15.153-android13-8-30958972-abS911U1UES6DYI3 SMP preempt mod_unload modversions aarch64
__versions size: 0            (manual-relocation late load)
.symtab/.strtab: retained

check_symbol vs recovered vmlinux.elf: pass
audit_module_against_target.py --manual-relocation:
  undefined symbols: 211
  module version entries: 0
  missing from target symbol table: 0
  symbols resolved from kallsyms rather than target exports: 70
  target CRC mismatches: 0
```

Published artifacts (stripped with `llvm-strip --strip-debug`):

```text
kernelsu/android13-5.15.153_kernelsu-dm1q-S911U1UES6DYI3-kdp.ko
size: 341000
SHA-256 08a0fe7b752020bd46b9d4bcda98fd0f4e62fdb2cc8822bec713b3ee542c304b

kernelsu/ksud-dm1q-S911U1UES6DYI3-kdp
size: 4627128
SHA-256 6cac70d110e9e6312f40fa40143bd24f6453b280d0473979209f8c1dcf38778a
```

The `ksud` embeds the module as `bin/aarch64/android13-5.15_kernelsu.ko`
(rust-embed with compression). Verified on the target SM-S911U1 over ADB:
the binary executes, `boot-info supported-kmis` reports `android13-5.15`
and matches `boot-info current-kmi`, so the late-load asset selection
resolves to this exact module.

## Validation state

The full chain has been executed on an SM-S911U1 running S911U1UES6DYI3:
root via CVE-2026-43499, `--late-load`, and a live `kernelsu` module in
`/proc/modules` with no panic (verified 2026-08-18; see Postmortem 4 for the
final artifact set). The pselect-route constant `SLIDE_PSELECT_WORD_SHIFT`
was never exercised on hardware: the shipped build uses the PR 231
tracefs/MCAST route, which does not read it.

## Postmortem: first module load panicked; rebuilt with the kernel's exact clang

The first clang-22-built module crashed the phone during insertion:
`mod_sysfs_setup+0x25c` walked a garbage pointer (`0x000b800090000150`,
level-0 translation fault) before any module code ran — the manual
relocation loader mis-parsed the module's ELF. Root cause: this kernel is
`CONFIG_CFI_CLANG` + `CONFIG_LTO_CLANG_FULL` + SCS, and the clang-22
module build silently received **no CFI/LTO instrumentation** (the 5.15
kbuild's compiler probes fail on modern clang), producing a section layout
the kernel's loader mishandles. The device-proven FZE1 module by contrast
contains `.text.__cfi_check_fail` and hash-named LTO data sections.

The device IKCONFIG records the kernel's compiler: `Android clang 14.0.7
(r450784e)`. NDK r25c ships clang 14.0.7 from the **same llvm-project
commit** (`4c603efb…`, build wrapper r450784d1). The module was rebuilt
with it (modules_prepare re-run under clang 14, same vermagic override and
whitelist steps): the resulting `.ko` now carries `.rela.text.__cfi_check_fail`
and `.rela.data..Lanon.e21dd8106d8b9de6595c3ab7b6647bac.1` — **the same
anonymous-section hash as the device-proven FZE1 module**, confirming
toolchain fidelity. Audits unchanged (211 imports, 0 missing, 0 CRC
mismatches, `__versions` still empty).

Updated artifacts:

```text
kernelsu/android13-5.15.153_kernelsu-dm1q-S911U1UES6DYI3-kdp.ko
size: 372480
SHA-256 a7f375b4da0e4ebc…

kernelsu/ksud-dm1q-S911U1UES6DYI3-kdp
size: 4639088
SHA-256 4253a29ab1c6d2e1…
```

Lesson recorded for future ports: on CFI/LTO Samsung kernels, build the
module with the compiler named in the target IKCONFIG
(`CONFIG_CC_VERSION_TEXT`), not a host toolchain, and diff the module's
section list against a known-good KMI sibling before shipping.

## Postmortem 2: all three module panics were zeroed-kallsyms symbol resolution

Crash #3 (with `kptr_restrict` manually cleared before the load) still died
at `mod_sysfs_setup+0x25c`, proving the manual sysctl change never reached
the resolver. Root cause found in `ksuinit`: its `Kptr` guard **writes
`kptr_restrict = 1`** before reading `/proc/kallsyms`. On this kernel
(Samsung default 2; SELinux denies the CAP_SYSLOG check under 1 for the
loader's context) every address reads as zero, so the module was loaded
with every external reference zero-based — the `add_usage_links` walk then
dereferenced relocation addends as pointers.

Fix (all in the loader, verified by offline dry-run against live
kallsyms — 211/211 imports resolved, embedded addresses matched the
running kernel exactly at slide 0x30000):

1. `Kptr` now writes `0`, never `1`;
2. resolution prefers live kallsyms, falls back to an **embedded static
   table** (211 imports, offsets from the DYI3 `vmlinux.nm`) plus the
   runtime slide recovered from the `_text` entry;
3. **fail-closed**: if any import cannot be resolved to a non-zero address
   — or the embedded fallback is needed but no slide was recovered — the
   loader refuses with a diagnostic instead of calling `init_module`.

Artifacts updated: `kernelsu/ksud-dm1q-S911U1UES6DYI3-kdp` (4663256
bytes). The loader source delta lives in the local KernelSU tree
(`userspace/ksuinit/src/lib.rs`, `static_syms.rs`).

## Postmortem 3: true root cause was the module's `this_module` relocation offset

Despite the Postmortem 2 fixes, the DYI3 module still panicked
deterministically at `mod_sysfs_setup+0x25c` (VA `000b800090000150`,
5/5 loads). The offline dry-run had passed because it only rewrites
`.symtab` st_value and refused unresolved symbols — it never checked the
`.rela.gnu.linkonce.this_module` relocations, which the kernel applies to
the live `struct module` during `load_module`.

Root cause (register math + vmlinux disasm + device BTF, all confirmed):

- DYI3 `.ko` relocates `init_module` at module+0x178 and `cleanup_module`
  at module+0x368.
- The device BTF `struct module` has `target_list` at 0x368 and `exit` at
  0x378 (`CONFIG_DEBUG_INFO_BTF_MODULES` adds the `btf_data_size` +
  `btf_data` pair before `target_list`).
- `apply_relocations` (module.c:4206) therefore writes `&cleanup_module`
  into the already-INIT_LIST_HEAD'd `target_list`; `add_usage_links`
  (module.c:1752) then walks it as fake `module_use` entries and faults.
- The device-proven FZE1 ko (S911B, 5.15.189) relocates at 0x178/0x378 —
  the layout only the BTF-modules build produces.

Fix (rebuild, not loader): rebuild `kernelsu.ko` with the exact device
compiler (NDK r25c clang 14.0.7 `r450784d1`, matching the IKCONFIG
`CONFIG_CC_VERSION_TEXT`) plus `KCFLAGS=-DCONFIG_DEBUG_INFO_BTF_MODULES=1`
so the module's own `struct module` layout matches the device BTF
(relocations land at 0x178/0x378). The DYI3 `out/.config` lacked
`CONFIG_DEBUG_INFO_BTF_MODULES` even though the shipped firmware has it —
the object-level define reproduces the shipped layout without
pahole/BTF-gen side effects. `CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT` must NOT
be set (DYI3 ships the real `stop_machine` patch path; the earlier
`NO_PATCH_TEXT=y` build dropped 11 imports). Verified rebuilt ko:

```text
kernelsu/android13-5.15.153_kernelsu-dm1q-S911U1UES6DYI3-kdp.ko (fixed)
size: 340336
relocs: init_module@0x178 cleanup_module@0x378
undefined: 211 (exact match with shipped bad ko, incl. stop_machine path)
audit: 0 missing, 0 CRC mismatches, __versions empty
vermagic: 5.15.153-android13-8-30958972-abS911U1UES6DYI3
```

The rebuilt ksud (`ksud-dm1q-S911U1UES6DYI3-kdp`, embedded fixed ko SHA
`49c627fa…`) replaces the crashing one. Bad ko kept as
`android13-5.15.153_kernelsu-dm1q-S911U1UES6DYI3-kdp.ko.bad-backup`.

## Postmortem 4: RKP protects `sys_call_table`; `NO_PATCH_TEXT=y` is required

With the Postmortem 3 fix, the module loaded and `init` ran fully —
`mod_sysfs_setup+0x25c` was gone. A **second, different panic** appeared
at ~302s uptime while hooking syscalls:

```text
Internal error: synchronous external abort [#1] PREEMPT SMP
PC: copy_to_kernel_nofault+0x28/0x16c   (str x10, [x0])
LR: ksu_patch_text_cb+0x170/0x22c [kernelsu]
map: multi_cpu_stop <- stop_machine_cpuslocked  (ksu_patch_text)
Modules linked in: kernelsu(OE+)
KernelSU: sys_call_table=0xffffffc009df8958; patch syscall 42
```

The store targets `0xfffffffdfdbfeaa8` (garbage — intended
`sys_call_table[42]` at `0xffffffc009df8aa8`); the faulting write is a
stage-2 (hypervisor) fault on the `sys_call_table` page: Samsung's RKP
pins it read-only at EL2. Direct `ksu_patch_text` → `stop_machine` →
`copy_to_kernel_nofault` writes always abort, and no fixmap/bounce path
exists on this target.

Fix: **do set `CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y`** — this corrects the
Postmortem 3 note. With it, `ksu_patch_text` is a stub returning
`-EOPNOTSUPP`, the `sys_call_table` hook fails cleanly, and KernelSU falls
back to the RKP-compatible kretprobe hooks (setresuid/sucompat) — the
same path the device-proven FZE1 build (S911B, 5.15.189) uses. The 11
dropped imports (`stop_machine` etc.) are benign: they only feed the
disabled patch path.

Final working build (deployed, verified on-device):

```text
kernelsu/android13-5.15.153_kernelsu-dm1q-S911U1UES6DYI3-kdp.ko
size: 327120
config: CONFIG_KSU=m CONFIG_KSU_SAMSUNG_KDP=y CONFIG_KSU_SAMSUNG_RKP=y
        CONFIG_KSU_SAMSUNG_DEFEX=y CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y
        KCFLAGS=-DCONFIG_DEBUG_INFO_BTF_MODULES=1
ksud-dm1q-S911U1UES6DYI3-kdp: 4761608 B, embeds fixed ko
Result: kernelsu module loads + init runs; no panic on late-load
        (verified 2026-08-18: `kernelsu ... - Live (OE)` after --late-load)
```

Full chain now works: root via CVE-2026-43499 → `--late-load` → `kernelsu`
live in `/proc/modules`, no panic.
