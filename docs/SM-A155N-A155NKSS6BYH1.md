# SM-A155N / A155NKSS6BYH1 port record

## Status

```text
model: SM-A155N
device: a15
region/CSC: SKC / OKR
AP/PDA: A155NKSS6BYH1
display build: AP3A.240905.015.A2.A155NKSS6BYH1
system fingerprint: samsung/a15ks/a15:15/AP3A.240905.015.A2/A155NKSS6BYH1:user/test-keys
Android SDK: 35
ABI: arm64-v8a
page size: 4096
kernel release: 5.10.226-android12-9-31117096
```

The exploit profile, app payload, Samsung-compatible KernelSU module, and
late-load binary are static-analysis and build verified. There was no target
handset, so execution remains device-untested.

No values in this profile were copied from the 6.1 or 6.6 targets.

## Firmware extraction

The local firmware directory contained:

```text
AP_A155NKSS6BYH1_A155NKSS6BYH1_MQB99251917_REV00_user_low_ship_MULTI_CERT_meta_OS15.tar
BL_A155NKSS6BYH1_A155NKSS6BYH1_MQB99251917_REV00_user_low_ship_MULTI_CERT.tar
CP_A155NKSS6BYH1_CP31143383_MQB99251917_REV00_user_low_ship_MULTI_CERT.tar
CSC_OKR_A155NOKR6BYH1_MQB99251917_REV00_user_low_ship_MULTI_CERT.tar
HOME_CSC_OKR_A155NOKR6BYH1_MQB99251917_REV00_user_low_ship_MULTI_CERT.tar
USERDATA_SKC_A155NKSS6BYH1_A155NKSS6BYH1_MQB99251917_REV00_user_low_ship_MULTI_CERT.tar
```

Only `boot.img.lz4`, `vendor_boot.img.lz4`, `dtbo.img.lz4`,
`meta-data/fota.zip`, `lk-verified.img.lz4`, and the minimum bootloader images
needed to verify the load address were extracted. Samsung LZ4 frames were
decompressed without modifying their contents.

The Android boot image uses header version 4 and a 4096-byte page. Its kernel
starts at `0x1000`, and the header reports `kernel_size = 18833170`. The kernel
payload is a gzip stream; decompressing it produces the raw ARM64 `Image`.

Exact retained and intermediate hashes:

| Object | Size | SHA-256 |
| --- | ---: | --- |
| `boot.img` | 67,108,864 | `B56B03B4ED880CE3174F4C85FC30EE5392313BD6ED66D6360FB42480D0F92BDE` |
| compressed kernel payload | 18,833,170 | `88B633D14396EDF4B8BB39BEABE351EFE6724844C88BDC3FF4F7BA56634972EC` |
| raw ARM64 `Image` | 41,544,192 | `6ED73130C608EFBB0451590B853F2B8BE0A52B57B20D324EAAF094911333A02F` |
| recovered `vmlinux.elf` | 47,034,127 | `D1601F24E44D46ECDAC08004137BAE6A33FCC4C514EDF9094A2318CF9316CFB7` |

The raw Image header reports `text_offset=0`, `image_size=0x2a60000`, and flags
`0xa`. The embedded banner is:

```text
Linux version 5.10.226-android12-9-31117096 (build-user@build-host) (Android (7284624, based on r416183b) clang version 12.0.5 (https://android.googlesource.com/toolchain/llvm-project c935d99d7cf2016289302412d708641d52d2f7ee), LLD 12.0.5 (/buildbot/src/android/llvm-toolchain/out/llvm-project/lld c935d99d7cf2016289302412d708641d52d2f7ee)) #1 SMP PREEMPT Thu Jul 31 00:05:14 UTC 2025
```

## Symbol recovery

`vmlinux-to-elf` recovered 110,886 defined symbols from the target Image at
base `0xffffffc008000000`. The target contains IKCONFIG but no BTF. Structure
layouts were recovered from the Samsung A15 source mirror at commit
`5074ff414f1b835fba113b71175d4f217b1cdc39`, then every exploited field was
checked against the target disassembly. The mirror is a layout reference, not
a substitute firmware image.

Required target offsets:

| Use | Target symbol/derivation | Offset |
| --- | --- | ---: |
| UMH callback | `call_usermodehelper_exec_work` | `0x00107fb0` |
| `SLIDE_TRACEFS_WORKER_CALLER_OFF` | instruction after the blocking `worker_thread -> schedule` call | `0x001123c4` |
| `NOOP_LLSEEK_OFF` | `noop_llseek` | `0x004c2234` |
| `COPY_SPLICE_READ_OFF` | `generic_file_splice_read` | `0x005369cc` |
| configfs read | `configfs_read_file` | `0x005f86fc` |
| configfs write | `configfs_write_bin_file` | `0x005f908c` |
| ashmem ioctl | `ashmem_ioctl` | `0x010db6e0` |
| ashmem compat ioctl | `compat_ashmem_ioctl` | `0x010dc1ac` |
| ashmem mmap | `ashmem_mmap` | `0x010dc204` |
| ashmem open | `ashmem_open` | `0x010dc434` |
| ashmem release | `ashmem_release` | `0x010dc4cc` |
| ashmem fdinfo | `ashmem_show_fdinfo` | `0x010dc5e8` |
| `SLIDE_NFULNL_LOGGER_NAME_OFF` | `"nfnetlink_log"` string referenced by `nfulnl_logger.name` | `0x01e4a980` |
| pipe ops | `anon_pipe_buf_ops` | `0x01f491a8` |
| ashmem fops | `ashmem_fops` | `0x020a4688` |
| kmalloc table | `kmalloc_caches` | `0x020e6580` |
| trace event start | `__start_ftrace_events` | `0x0255fd18` |
| blocked event | `__event_sched_blocked_reason` | `0x0255ff30` |
| system workqueue | `system_unbound_wq` | `0x02599e08` |
| `SLIDE_NFULNL_LOGGER_OBJECT_OFF` | `nfulnl_logger` object | `0x025a1340` |
| init task | `init_task` | `0x025ac000` |
| `SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_OFF` | `.data` pointer slot in the `random_table[]` entry named `boot_id` | `0x026ba828` |
| ashmem misc fops | `ashmem_misc + 0x10` | `0x026fa328` |
| root task group | `root_task_group` | `0x027a8040` |
| SELinux enforcing | `selinux_state.enforcing` | `0x028d9770` |
| `SLIDE_SYSCTL_BOOTID_OFF` | actual `sysctl_bootid` UUID storage | `0x0297d8c5` |

The logger object and boot-id reference were verified directly in the Image:

```text
nfulnl_logger + 0x00 = 0xffffffc009e4a980
nfulnl_logger + 0x08 = 1
nfulnl_logger + 0x10 = 0xffffffc0092fe4b8
random_table boot_id .data pointer slot = Image + 0x026ba828
```

The first qword of `nfulnl_logger` points to the name string, so the object and
name offsets are intentionally different. The `random_table[]` slot is the
pointer temporarily redirected by the oracle; `sysctl_bootid` is the UUID
storage restored into that slot.

`ashmem_misc + offsetof(miscdevice, fops)` contains the exact `ashmem_fops`
address. `__TRACE_LAST_TYPE` is 17 on this branch.
`sched_blocked_reason` has zero-based linker registration index 67, so the
target event ID is `17 + 67 = 84`. `SLIDE_PSELECT_WORD_SHIFT` is zero because
waiter qword zero overlaps the first qword in the logical read/write/exception
fd-set sequence; the macro counts qwords, not bytes.

## Physical map

The target `vendor_boot` header is version 4:

```text
page_size: 4096
kernel_addr: 0x40000000
ramdisk_addr: 0x66f00000
tags_addr: 0x47c80000
dtb_addr: 0x47c80000
board: SRPWJ19A006
```

The embedded DTB memory node starts at physical `0x40000000`. The Mediatek LK
path was also checked to confirm that vendor boot v4 uses the header's
`kernel_addr`. With `text_offset=0`:

```c
#define P0_PHYS_OFFSET 0x40000000ULL
#define P0_KERNEL_PHYS_LOAD 0x40000000ULL
#define DIRECT_MAP_BASE 0xffffff8000000000ULL
#define DIRECT_MAP_END  0xffffffc000000000ULL
#define VMEMMAP_START    0xfffffffeffe00000ULL
```

## 5.10 layout changes

The target cannot use the 6.1/6.6 waiter layout. Its exact values are:

```text
mm_struct cache object: 0x3c0
mm_struct slab order: 3

rt_mutex_waiter.pi_tree_entry: 0x18
rt_mutex_waiter.task: 0x30
rt_mutex_waiter.lock: 0x38
rt_mutex_waiter.prio: 0x40
rt_mutex_waiter.deadline: 0x48
sizeof(rt_mutex_waiter): 0x50

task_struct.usage: 0x40
task_struct.prio: 0x84
task_struct.normal_prio: 0x8c
task_struct.sched_task_group: 0x310
task_struct.pi_lock: 0x86c
task_struct.pi_waiters: 0x880
task_struct.pi_top_task: 0x890
task_struct.pi_blocked_on: 0x898

workqueue_struct.dfl_pwq: 0xb0
pool_workqueue.nr_active: 0x58
pool_workqueue.max_active: 0x5c
worker_pool.worklist: 0x20
worker_pool.nr_idle: 0x34
```

The 5.10 kernel has only normal and reclaim entries in the static
`kmalloc_caches` array. Accounted pipe-buffer allocations use the normal
`kmalloc-2k` cache under this object-cgroup implementation, so the target
selects cache type 0 and two cache types total.

Shared source selects the legacy 0x50-byte waiter only when
`LEGACY_RT_MUTEX_WAITER=1`; existing 6.1/6.6 targets keep their original
0x70-byte layout.

## P0 table and build

`src/targets/a15-A155NKSS6BYH1/p0_fingerprint.h` contains 32 slide rows. For
every candidate from `0x000000` through `0x1f0000`, the eight qwords at page
offsets `0x000, 0x200, ..., 0xe00` were generated from this target Image. All
256 qwords were read back and matched.

The release payload was built with Android NDK r29 for API 35 and AArch64. Its
fixed artifact size is 104,128 bytes.

## KernelSU 5.10 port

The generic 6.1 artifact was not renamed or reused. The compatibility baseline
was the official KernelSU v3.2.5 `android12-5.10_kernelsu.ko`:

```text
size: 344,504
SHA-256: 14DBD86A9ED20E979F332AD3EE6643B3E927988D4B073437297424EBBF3F26B3
upstream vermagic: 5.10.252-dirty SMP preempt mod_unload modversions aarch64
```

The module was rebuilt from KernelSU commit
`b0bc817b4e966aa6aa830834eaf6ef765d821d40` and the Samsung KDP/RKP/DEFEX
patch already used by the 6.1 and 6.6 builds. The A15 source reference was
Samsung commit `5074ff414f1b835fba113b71175d4f217b1cdc39`.

Samsung 5.10 uses `cred->user->processes`, not the later `cred->ucounts`
RLIMIT counter. The KDP commit worker therefore follows this target's native
`commit_creds()` sequence and increments/decrements `user->processes` when the
credential user changes. The protected credential allocation, KDP use count,
native `kdp_assign_pgd()`, RKP-safe kprobe fallback, and DEFEX synchronization
remain identical to the previously deployed Samsung path.

The target has `CONFIG_TRIM_UNUSED_KSYMS=y`. `ksuinit::load_module()` resolves
all undefined module symbols from `/proc/kallsyms` before `init_module`, so the
KO intentionally retains a zero-length `__versions` section. This is the same
late-load mechanism used by the existing Samsung artifacts; it is not a
renamed stock KO. The module's 208 undefined symbols were all found in the
recovered target `vmlinux`, and the runtime-only Samsung helpers were verified
separately:

```text
prepare_ro_creds
kdp_assign_pgd
kdp_usecount_dec_and_test
get_task_creds
set_task_creds
task_defex_enforce
sys_call_table
__arm64_sys_ni_syscall
__arm64_sys_setresuid
```

The release outputs are:

| File | Size | SHA-256 |
| --- | ---: | --- |
| `kernelsu/android12-5.10_kernelsu-samsung-kdp.ko` | 361,328 | `23CA2A7C3F9BB36133003348AE15099E7BC4DCAE02C96604988359FFA6107D6D` |
| `kernelsu/ksud-samsung-android12-5.10-kdp` | 6,649,032 | `727EECC25BB03D4FEF3301DE137EC13391DA51BCA5E701C842033CD270B781BA` |

The standalone KO reports exactly:

```text
5.10.226-android12-9-31117096 SMP preempt mod_unload modversions aarch64
```

The `ksud` binary embeds that KO as `android12-5.10_kernelsu.ko`. This exact
profile is included in the support feed, but hardware execution remains
unverified until an `SM-A155N` running `A155NKSS6BYH1` is available.
