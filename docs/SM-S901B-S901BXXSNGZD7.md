# SM-S901B / S901BXXSNGZD7 port record

## Status

```text
model: SM-S901B (SM-S901B/DS)
device: r0s
region/CSC: EUX / OXM
AP/PDA: S901BXXSNGZD7
CSC: S901BOXMNGZD7
CP: S901BXXSNGZD7
display build: BP2A.250605.031.A3.S901BXXSNGZD7
system fingerprint: samsung/r0sxxx/essi:16/BP2A.250605.031.A3/S901BXXSNGZD7:user/release-keys
Android SDK: 36 (One UI on Android 16; kernel stays on the launch android12-5.10 branch)
ABI: arm64-v8a
page size: 4096
kernel release: 5.10.237-android12-9-31999025-abS901BXXSNGZD7
```

The exploit profile, app payload, Samsung-compatible KernelSU module, and
late-load binary are static-analysis and build verified. There is no target
handset in this environment, so execution remains device-untested.

No values in this profile were copied from another device. Every symbol
offset and layout constant was recovered from the exact GZD7 kernel Image or
its disassembly; the P0 fingerprint table was generated from the same Image
and read back.

## Firmware extraction

Downloaded from Samsung FUS (region `EUX`, version string
`S901BXXSNGZD7/S901BOXMNGZD7/S901BXXSNGZD7`, package
`SM-S901B_5_20260508045850_jzknv3x9a9_fac.zip.enc4`, 9,903,147,360 bytes,
build date 2026-05-08). Extracted `boot.img.lz4`, `vendor_boot.img.lz4`,
`meta-data/fota.zip` from the AP archive and `sboot.bin.lz4` from the BL
archive.

Exact retained hashes:

| Object | Size | SHA-256 |
| --- | ---: | --- |
| `boot.img` | 67,108,864 | `74C8784753E1B9B3F239D85244FD3498948C20D89876B41F93759E22E6E96E50` |
| raw ARM64 `Image` | 34,779,648 | `3A5C445B896F9C23130A393569DBDFAFE0C9A15212788B7562FE00964435096C` |
| `sboot.bin` | 8,388,608 | `D58D509AAA3986A5E7BE3E0D04088BC1F0F9BD96FA32B9F8D43DF483C471FC0A` |

The boot image is header version 4 with a 4096-byte page; the kernel is
stored uncompressed (`kernel_size = 34779648` at header offset 0x08).

The raw Image header reports `text_offset=0`, `image_size=0x2410000`, and
flags `0xa`. The embedded banner is:

```text
Linux version 5.10.237-android12-9-31999025-abS901BXXSNGZD7 (dpi@21DODB17) (Android (7211189, based on r416183b) clang version 12.0.4 ..., LLD 12.0.4 ...) #1 SMP PREEMPT Thu May 7 21:36:19 KST 2026
```

The Image contains IKCONFIG (extracted, 237,868 bytes) and no BTF, as
expected for a Samsung 5.10 build.

## Symbol recovery

`vmlinux-to-elf` recovered the symbolized ELF at base `0xffffffc008000000`
(0 % negative offsets, 0 % null addresses). All exploit symbols were then
taken from `llvm-nm --numeric-sort` on that ELF; offsets below are relative
to the recovered base.

| Macro/use | Symbol | Offset |
| --- | --- | ---: |
| UMH callback | `call_usermodehelper_exec_work` | `0x000f85ac` |
| `SLIDE_TRACEFS_WORKER_CALLER_OFF` | instruction after the blocking `worker_thread -> schedule` call | `0x000ffcc0` |
| `NOOP_LLSEEK_OFF` | `noop_llseek` | `0x0037ae34` |
| `COPY_SPLICE_READ_OFF` | `generic_file_splice_read` | `0x003c5f38` |
| configfs read | `configfs_read_file` | `0x0044b5c0` |
| configfs write | `configfs_write_bin_file` | `0x0044ba20` |
| ashmem ioctl | `ashmem_ioctl` | `0x00c6818c` |
| ashmem compat ioctl | `compat_ashmem_ioctl` | `0x00c68ae0` |
| ashmem mmap | `ashmem_mmap` | `0x00c68b38` |
| ashmem open | `ashmem_open` | `0x00c68d68` |
| ashmem release | `ashmem_release` | `0x00c68dec` |
| ashmem fdinfo | `ashmem_show_fdinfo` | `0x00c68f0c` |
| pipe ops | `anon_pipe_buf_ops` | `0x01a25c28` |
| ashmem fops | `ashmem_fops` | `0x01bb0d10` |
| kmalloc table | `kmalloc_caches` | `0x01bf6700` |
| system workqueue | `system_unbound_wq` | `0x01ea9e10` |
| `SLIDE_NFULNL_LOGGER_OBJECT_OFF` | `nfulnl_logger` object | `0x01eb1380` |
| init task | `init_task` | `0x01ebdd00` |
| ashmem misc fops | `ashmem_misc + 0x10` | `0x020ae060` |
| `SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_OFF` | `.data` slot of the `random_table[]` `boot_id` entry | `0x0206e930` |
| root task group | `root_task_group` | `0x02135080` |
| SELinux enforcing | `selinux_state.enforcing` | `0x02288d58` |
| `SLIDE_SYSCTL_BOOTID_OFF` | actual `sysctl_bootid` UUID storage | `0x0232d391` |
| `SLIDE_NFULNL_LOGGER_NAME_OFF` | `"nfnetlink_log"` string referenced by `nfulnl_logger.name` | `0x0192fbb3` |

Binary cross-checks performed on the Image itself:

- `nfulnl_logger + 0x00` = `0xffffffc00992fbb3` → the string at
  `0x0192fbb3` is `nfnetlink_log`; `+0x08 = 1`.
- `ashmem_misc + 0x10` contains exactly the `ashmem_fops` address.
- the `random_table[]` entry whose `.procname` points at the single
  `boot_id` string is at `random_table + 0x100`; its `.data` pointer slot at
  `+0x108` (`0x0206e930`) holds `sysctl_bootid` (`0x0232d391`).
- `selinux_state` layout: `enforcing` is the first field
  (`CONFIG_SECURITY_SELINUX_DEVELOP=y`, `CONFIG_SECURITY_SELINUX_DISABLE` not
  set); `status_page` at `+0x10` and `status_lock` at `+0x18` were confirmed
  in `selinux_status_update_setenforce`, so the struct is not randomized
  (`CONFIG_RANDSTRUCT` not set).

## Layout values

`file_operations` (measured from `ashmem_fops` and
`configfs_bin_file_operations` in the Image): `llseek 0x08, read 0x10,
write 0x18, read_iter 0x20, write_iter 0x28, ioctl 0x50, compat_ioctl 0x58,
mmap 0x60, open 0x70, release 0x80, splice_read 0xc8, show_fdinfo 0xe0`.

`struct configfs_buffer` (measured from `configfs_read_file` /
`configfs_write_bin_file` disassembly; Samsung's 5.10 configfs carries extra
members versus the vanilla struct, so this was read from the binary, not
from source): `page 0x10, needs_read_fill 0x50, bin_buffer 0x58,
bin_buffer_size 0x60, cb_max_size 0x64`. The mutex sits at `0x20`
(`CONFIG_DEBUG_MUTEXES` not set).

`task_struct` (measured from `rt_mutex_setprio`,
`rt_mutex_adjust_prio_chain`, `sched_move_task`, `dup_task_struct`):
`usage 0x40, policy 0x80, prio 0x84, normal_prio 0x8c, sched_task_group
0x310, pi_lock 0x86c, pi_waiters 0x880, pi_top_task 0x890, pi_blocked_on
0x898`. `sizeof(task_struct) = 0x1280` (from `fork_init` /
`dup_task_struct`).

`rt_mutex_waiter`: legacy 5.10 layout (`CONFIG_DEBUG_RT_MUTEXES` not set):
`tree_entry 0x00, pi_tree_entry 0x18, task 0x30, lock 0x38, prio 0x40,
deadline 0x48, sizeof 0x50` (`LEGACY_RT_MUTEX_WAITER=1`).

`mm_struct`: `sizeof = 0x3c0` (from the `kmem_cache_create_usercopy` size
argument in `mm_init`). SLUB `calculate_order` reproduced for size 0x3c0
with 8 CPUs (`min_objects = 4*(fls(8)+1) = 20`, `slub_max_order = 3`)
selects order 3 → `MM_ORDER 3`.

kmalloc caches: the target sets neither `CONFIG_ZONE_DMA` nor a memcg
kmalloc row; `enum kmalloc_cache_type` in android12-5.10 is
`KMALLOC_NORMAL=0, KMALLOC_RECLAIM=1`, and `kmalloc_type()` routes
`GFP_KERNEL_ACCOUNT` (the pipe-buffer flag, confirmed in this tree's
`fs/pipe.c`) to the NORMAL row. Hence `KMALLOC_CGROUP_TYPE 0` and
`KMALLOC_CACHE_TYPES 2`. Pipe buffers land in `kmalloc-2k` (index 11,
`PIPE_OBJECT_SIZE 0x800`, 32 slots).

`struct page`: `sizeof 0x40` (consistent with the derived `VMEMMAP_START`),
`compound_head 0x08, slab_cache 0x18, page_type 0x30`.

Workqueue (from `alloc_workqueue` disassembly and the branch-invariant
Samsung 5.10 layout): `wq->dfl_pwq 0xb0`, `pwq->pool 0x00, pwq->wq 0x08,
work_color 0x10, refcnt 0x18, nr_in_flight 0x1c, nr_active 0x58, max_active
0x5c`, `pool->worklist 0x20, nr_idle 0x34`.

## Physical map

`sboot.bin` boots the kernel through the sequence at file offset
`0x14456c`:

```text
adrp x0, <"Starting kernel ...">
add  x0, x0, #0x338
bl   <print>
adrp x8, <load-offset slot>
ldr  w8, [x8, #0xcec]
mov  w9, #-0x80000000
add  x8, x8, x9
blr  x8
```

With the Image `text_offset = 0`, the entry address is `0x80000000`. The
DRAM layout is corroborated by the vendor DTB: the low bank occupies
`0x80000000..0xffffffff` (reserved-memory regions span
`0x8ff00000..0xfffe0000`) and a second bank starts at `0x880000000` (the
`alloc_workqueue`-adjacent bank table and `PARAMs r0s_eur_open` in sboot
agree). Therefore:

```c
#define P0_PHYS_OFFSET 0x80000000ULL
#define P0_KERNEL_PHYS_LOAD 0x80000000ULL
#define P0_PAGE_OFFSET 0xffffff8000000000ULL
#define DIRECT_MAP_BASE 0xffffff8000000000ULL
#define DIRECT_MAP_END 0xffffffc000000000ULL
#define VMEMMAP_START 0xfffffffeffe00000ULL
```

(CONFIG_ARM64_VA_BITS=39, 4K pages; `_PAGE_END(39) = -(1<<38)`;
`VMEMMAP_SIZE = ((_PAGE_END - PAGE_OFFSET) >> 12) * 0x40 = 0x100000000`.)

## Slide data and P0 fingerprints

`__TRACE_LAST_TYPE` is 17 on this branch (`register_trace_event` loads the
initial `next_event_type` as `mov w8, #0x11`). The `_ftrace_events` pointer
array gives `sched_blocked_reason` index
`(0x9e70080 - 0x9e6fdc8) / 8 = 87` (the entry was verified to point at the
event call whose tracepoint name is `sched_blocked_reason`; neighbours are
`sched_stat_blocked` and `sched_stat_runtime`). Hence
`SLIDE_TRACEFS_EVENT_ID = 17 + 87 = 104`.

The idle kworker blocks at the single `bl schedule` in `worker_thread`
(`0x0080ffcbc`); the saved return PC is the next instruction,
`SLIDE_TRACEFS_WORKER_CALLER_OFF = 0x000ffcc0`.

`SLIDE_PSELECT_WORD_SHIFT = 0`: `core_sys_select` copies the fd-sets into a
`kvmalloc_node` buffer exactly like the other android12-5.10 ports (verified
in this binary), so the logical fd-set word zero overlaps waiter qword zero.

`SKB_DATA_DELTA` stays at the android12-5.10 default `-0xe80`.

`src/targets/r0s-S901BXXSNGZD7/p0_fingerprint.h` contains 32 slide rows
generated from this exact Image with
`tools/generate_p0_fingerprint.pl <Image> 0x1f0000`; all 256 source qwords
were read back and matched.

## Build

```sh
make TARGET=r0s-S901BXXSNGZD7 ANDROID_NDK_HOME=<android-ndk-r29> all release
```

with Android NDK r29, API 35, AArch64. Outputs copied to
`artifacts/r0s-S901BXXSNGZD7/cve-2026-43499-app.so` (fixed size 104,128).

| File | Size | SHA-256 |
| --- | ---: | --- |
| `artifacts/r0s-S901BXXSNGZD7/cve-2026-43499-app.so` | 104,128 | `7e4f1aeec2c3f86a44b9853913c79693417abea5c2fd18217bf21ffe9bbeb6d3` |

## KernelSU 5.10 port

The source base is Samsung's OSRC release `SM-S901B_16_Opensource.zip`
(kernel tree reports `5.10.237`, defconfig `s5e9925-r0sxxx_defconfig`,
toolchain clang r416183b per the OSRC README), overlaid with the published
`S901BXXSMGZB2` delta files. Samsung has not published a source package
tagged `GZD7`; the tree's SUBLEVEL and branch (`5.10.237-android12-9`)
match the running kernel exactly, and the module was audited against the
recovered GZD7 `vmlinux` as below.

Build procedure (the target's own extracted IKCONFIG is used as `.config`):

```sh
cp <GZD7 ikconfig> out/.config
scripts/config --file out/.config \
  --set-str UNUSED_KSYMS_WHITELIST <tree>/gki/abi_symbollist.raw \
  --disable LOCALVERSION_AUTO \
  --set-str LOCALVERSION "-android12-9-31999025-abS901BXXSNGZD7"
make O=out ARCH=arm64 LLVM=1 LLVM_IAS=1 olddefconfig prepare modules_prepare
out/scripts/selinux/genheaders/genheaders \
  out/security/selinux/flask.h out/security/selinux/av_permissions.h
make -C out M=<KernelSU>/kernel src=<KernelSU>/kernel \
  ARCH=arm64 LLVM=1 LLVM_IAS=1 \
  CONFIG_KSU=m CONFIG_KSU_SAMSUNG_KDP=y CONFIG_KSU_SAMSUNG_RKP=y \
  CONFIG_KSU_SAMSUNG_DEFEX=y CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y \
  KBUILD_MODPOST_WARN=1 modules
```

`CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y` follows every device-tested Exynos/KDP
build in this repository: live text patching panics in Samsung EL2, so the
module uses the kprobe-based Samsung fallback hooks.

Audit results (`kernelsu/tools/audit_module_against_target.py
--manual-relocation`, CRC table extracted from the recovered vmlinux, 7,206
exports):

```text
undefined symbols: 201
module version entries: 0
missing from target symbol table: 0
symbols resolved from kallsyms rather than target exports: 68
target CRC mismatches: 0
```

All runtime-resolved Samsung helpers are present in the target kallsyms:
`prepare_ro_creds`, `kdp_assign_pgd`, `kdp_usecount_dec_and_test`,
`kdp_usecount_inc_not_zero`, `get_task_creds`, `set_task_creds`,
`task_defex_enforce`, `sys_call_table`, `__arm64_sys_ni_syscall`,
`__arm64_sys_setresuid`. The module does not import `stop_machine`.

The stripped module reports exactly:

```text
vermagic: 5.10.237-android12-9-31999025-abS901BXXSNGZD7 SMP preempt mod_unload modversions aarch64
__versions size: 0
```

The module is embedded as `android12-5.10_kernelsu.ko` in the late-load
binary built with `cargo --target aarch64-linux-android` against NDK r29.

| File | Size | SHA-256 |
| --- | ---: | --- |
| `kernelsu/android12-5.10_kernelsu-r0s-S901BXXSNGZD7-kdp.ko` | 323,168 | `47a66801c8a1e94a757924fd30099065cea62edc80e14b83b0879cca22fef568` |
| `kernelsu/ksud-r0s-S901BXXSNGZD7-kdp` | 4,621,280 | `fc0097be827dab2078ba23e7af223905cc8ab38c763a602982071439ae7ed962` |

## Feed entry

`support/targets-v3.json` gained payload `r0s-S901BXXSNGZD7` with models
`["SM-S901B"]` and kernelVersions `["5.10.237"]` (the app matches on the
leading numeric `uname -r` component).

## Residual risk / device validation

Everything above is static or build verified. Two residual risks remain
until hardware execution:

1. The OSRC tree matches the running kernel in version and branch but is
   not the exact GZD7 source drop. Struct layouts used by the module are
   core credential/task/selinux structures; the audit shows zero missing
   symbols and zero CRC mismatches against the real GZD7 vmlinux, but a
   security patch between MGZB2 and GZD7 could in principle have changed
   something the module touches.
2. The S22 firmware ships `androidboot.selinux=permissive` on the vendor
   cmdline in this build's fota metadata; on-handset enforce state should
   still be confirmed at run time by the payload logs.

Hardware execution on an SM-S901B running S901BXXSNGZD7 remains the final
validation step.
