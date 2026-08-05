# Galaxy S24 SM-S921N / S921NKSSFDZF3 port record

This record contains the exact offline inputs and derived values for profile
`e1s-S921NKSSFDZF3`. Values were recovered from the exact Korean KSS firmware
and validated on the connected SM-S921N. The S921N kernel shares the
`6.1.157-android14-11` release string with the S921B/S721N builds, but the raw
Images are not identical: four firmware-dependent offsets differ from the e2s
(S926B) build by exactly `0x40` and must not be mixed.

## Firmware identity

```text
model: SM-S921N
device: e1s
AP/PDA: S921NKSSFDZF3
display build: BP4A.251205.006
fingerprint: samsung/e1sksx/e1s:16/BP4A.251205.006/S921NKSSFDZF3:user/release-keys
SDK: 36
ABI: arm64-v8a
page size: 4096
SoC: Exynos 2400 (s5e9945)
kernel release: 6.1.157-android14-11
kernel build: #1 SMP PREEMPT Tue Jun 9 04:25:46 UTC 2026
```

## Extracted image hashes

```text
boot.img size: 67108864
boot.img SHA-256: B91B270FF11BCCDE21F2B337B5760EFE1A6E9B87C4303E42B84E57DB837FEC42
kernel size: 38832640
kernel SHA-256: C2BC0495197B98F461F8213023A8AA412938BC82BA7F192F85D6D24FC079C20D
ARM64 Image text_offset: 0x0
ARM64 Image size: 0x27c0000
ARM64 Image flags: 0xa
```

## Symbol and BTF recovery

Symbols were recovered with `vmlinux-to-elf` at image base
`0xffffffc008000000`. Raw BTF was validated at
`[0x188c248, 0x1e439a9)`.

Target offsets (validated against the recovered ELF):

| Macro/use | Offset |
| --- | ---: |
| `CALL_USERMODEHELPER_EXEC_WORK_OFF` | `0x000d4468` |
| `SLIDE_TRACEFS_WORKER_CALLER_OFF` | `0x000dbd9c` |
| `NOOP_LLSEEK_OFF` | `0x003a1414` |
| `COPY_SPLICE_READ_OFF` | `0x003ef02c` |
| `CONFIGFS_READ_ITER_OFF` | `0x00470d44` |
| `CONFIGFS_BIN_WRITE_ITER_OFF` | `0x00471274` |
| `ASHMEM_IOCTL_OFF` | `0x00d38cfc` |
| `ASHMEM_COMPAT_IOCTL_OFF` | `0x00d39634` |
| `ASHMEM_MMAP_OFF` | `0x00d3968c` |
| `ASHMEM_OPEN_OFF` | `0x00d398b8` |
| `ASHMEM_RELEASE_OFF` | `0x00d39940` |
| `ASHMEM_SHOW_FDINFO_OFF` | `0x00d39a60` |
| `ANON_PIPE_BUF_OPS_OFF` | `0x0121dd50` |
| `ASHMEM_FOPS_OFF` | `0x013d9f08` |
| `SLIDE_NFULNL_LOGGER_NAME_OFF` | `0x016dd6e0` |
| `KMALLOC_CACHES_OFF` | `0x017a80d8` |
| `SYSTEM_UNBOUND_WQ_OFF` | `0x022eae58` |
| `SLIDE_NFULNL_LOGGER_OBJECT_OFF` | `0x022f2a08` |
| `INIT_TASK_OFF` | `0x022ff800` |
| `SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_OFF` | `0x0243ef78` |
| `ASHMEM_MISC_FOPS_OFF` | `ashmem_miscs + 0x10` = `0x02484bb0` |
| `ROOT_TASK_GROUP_OFF` | `0x02515cc0` |
| `SELINUX_ENFORCING_OFF` | `0x025ea478` |
| `SLIDE_SYSCTL_BOOTID_OFF` | `0x026d1b60` |

Four offsets differ from the e2s `S926BXXUEDZDR` build by exactly `0x40`:
`ashmem_fops` (`0x013d9ec8` -> `0x013d9f08`), `kmalloc_caches`
(`0x017a8098` -> `0x017a80d8`), `anon_pipe_buf_ops` (`0x0121dd10` ->
`0x0121dd50`), and `nfulnl_logger_name` (`0x016dd6a0` -> `0x016dd6e0`).
All other offsets match the e2s build. The profile was created by copying the
hardware-validated e2s target and applying only these four offset deltas plus
the S921N fingerprint table.

Target BTF layout notes (same family as e2s):

```text
sizeof(struct rt_mutex_waiter) = 0x58  (COMPACT_RT_MUTEX_WAITER)
sizeof(struct mm_struct)       = 0x3c0, SLUB stride 0x400  (MM_STRUCT_SZ 0x400)
sizeof(struct page)            = 0x40
enum kmalloc_cache_type { NORMAL=0, DMA=0, CGROUP=1, RECLAIM=2, NR=3 }
kmalloc_caches = [3][14] (42 qwords)
```

The cgroup kmalloc row is index 1 on this kernel, so the target sets
`KMALLOC_CGROUP_TYPE 1`; the shared default of 2 would read the reclaim row and
the pipe-buffer cache gate would fail.

## Physical load proof

The ARM64 Image has `text_offset == 0` and the sboot branch path loads
`0x80000000`, so:

```c
#define P0_PHYS_OFFSET 0x80000000ULL
#define P0_KERNEL_PHYS_LOAD 0x80000000ULL
```

## KernelSU module

Exact-vermagic `android14-6.1` no-patch-text build (KDP/RKP/DEFEX enabled,
`CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT`):

```text
vermagic: 6.1.157-android14-11 SMP preempt mod_unload modversions aarch64
undefined imports: 202
missing from target symbol table: 0
target CRC mismatches: 0
__versions: empty
```

Live text patching is disabled because the RKP-protected syscall-table write
faults at EL2 on this firmware; the module uses the kprobe fallback path.

## Validation

- Exploit chain (p0 slide -> fops data alias -> CFI -> pipe physrw ->
  workqueue usermode-helper root) completed in one attempt on hardware:
  `uid=2000 -> 0`, kernel context `u:r:kernel:s0`.
- KernelSU module loads via `ksud late-load` without panic (kprobe fallback).
- App-domain install (Root My Galaxy) verified on the connected SM-S921N.
