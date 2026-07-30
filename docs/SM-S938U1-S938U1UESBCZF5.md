# SM-S938U1 / S938U1UESBCZF5 offline derivation

Date: 2026-07-30  
Profile candidate: `pa3q-S938USQSBCZF5`  
Scope: offline firmware and kernel analysis only. No exploit, KernelSU module,
or payload was executed, installed, or transferred to the device.

## Identity

| Field | Value |
| --- | --- |
| Model | `SM-S938U1` |
| Codename | `pa3q` |
| Product | `pa3quew` |
| AP/PDA | `S938U1UESBCZF5` |
| Internal kernel build | `S938USQSBCZF5` |
| CSC (live) | `S938U1OYMBCZF5` / CHA |
| Display build | `BP4A.251205.006.S938U1UESBCZF5` |
| Live fingerprint | `samsung/pa3quew/pa3q:16/BP4A.251205.006/S938U1UESBCZF5_OYMBCZF5:user/release-keys` |
| Kernel release | `6.6.98-android15-8-pd6ff1cd-abogkiS938USQSBCZF5-4k` |
| SDK / ABI / page size | `36` / `arm64-v8a` / `4096` |
| Security patch | `2026-07-05` |
| Live `sched_blocked_reason` ID | `109` |

FUS four-part version:

```text
S938U1UESBCZF5/S938U1OYMBCZF5/S938U1UESBCZF5/S938U1UESBCZF5
```

## Provenance hashes (SHA-256)

| Artifact | SHA-256 |
| --- | --- |
| `S938U1UESBCZF5_CHA.zip` | `9a34b75dc5d51e6f29858f37e0fbd7e1097f9f4cf42b09807d0a7efbe3133129` |
| AP tar.md5 | `0df6412de2966ca5d7bb69bc64338cf103e2350a166412bf21193e950c645c74` |
| BL tar.md5 | `760aa64d454b36bc26147952f176647385389a7bb27d53067c3ff7faf738d218` |
| `boot.img.lz4` | `a2654342c9eaf1451518bd32e597c3289ebc836c3e07c01748a9ea947c2023a1` |
| `boot.img` | `5f0772529bbaab7be71a519280a1f417252ac6ad6c8bfe00f8eaa9acea845cde` |
| raw `kernel` | `bd46d31ee26f9cd4e4dd8ea64d3b534eec17fec613022e45f9498ff8dd915c0a` |
| `abl.elf.lz4` | `8e6e229edb238e9db4a28255d8fe029835fc21e976813c06ec082c022cc05cd3` |
| `abl.elf` | `c5c50aeb26ac49ea4816208043693568a7425b3c28bada4bae803ef21cfdf749` |
| `vmlinux.elf` | `150edc3d10dbfa072d623ab380842fd4ab411afcf577b7b157fbd42abefdcd7a` |
| `vmlinux.btf` | `c3a0fbfeff1410502ab4624c906908a8d176a763c8835d5dfaffe33a3ece3e36` |

Kernel image header: ARM64 Image, `text_offset=0`, `image_size=0x27b0000`,
flags `0xa`. Boot image magic `ANDROID!`, `kernel_size=38849024`.

Recovered ELF base: `_text = 0xffffffc080000000` (114,227 kallsyms entries).

Raw BTF: single validated little-endian blob extracted from the Image
(`tools/extract_btf.py`).

## Required BTF layouts

Source: `recovered/required-btf-layouts.json` via `tools/inspect_btf.py`.

Modern kernels place `slab_cache` on `struct slab` (overlay), not on
`struct page`. Derived values used by the payload:

```text
sizeof(file_operations) = 264 (0x108)
  unlocked_ioctl=0x48 compat_ioctl=0x50 mmap=0x58 open=0x68
  release=0x78 splice_read=0xb8 show_fdinfo=0xd8

task_struct: usage=0x40 prio=0x84 normal_prio=0x8c
  sched_task_group=0x348 pi_lock=0x90c pi_waiters=0x920
  pi_top_task=0x930 pi_blocked_on=0x938

rt_mutex_waiter size=112: tree=0 pi_tree=0x28 task=0x50 lock=0x58
  wake_state=0x60 ww_ctx=0x68
rt_waiter_node: entry=0 prio=0x18 deadline=0x20

configfs_buffer: page=16 needs_read_fill=80 bin_buffer=88
  bin_buffer_size=96 cb_max_size=100

workqueue_struct.dfl_pwq=0xb0
pool_workqueue: pool=0 wq=8 work_color=0x10 refcnt=0x18
  nr_in_flight=0x1c nr_active=0x5c max_active=0x60
worker_pool: worklist=0x28 nr_idle=0x3c
work_struct: data=0 entry=8 func=0x18

sizeof(page)=0x40 compound_head=0x08 page_type=0x30
sizeof(slab)=0x40 slab_cache=0x08
miscdevice.fops=0x10
```

These member offsets match the existing June `pa3q-S938NKSUACZF1` /
`pa3q-S9380ZHUBCZF1` headers.

## Symbol offsets (`KIMAGE_TEXT_BASE + OFF`)

| Macro | Derivation | Offset |
| --- | --- | ---: |
| `CALL_USERMODEHELPER_EXEC_WORK_OFF` | `call_usermodehelper_exec_work` | `0x000d0eac` |
| `SLIDE_TRACEFS_WORKER_CALLER_OFF` | insn after `bl schedule` in `worker_thread` | `0x000d97ec` |
| `NOOP_LLSEEK_OFF` | `noop_llseek` | `0x003c9450` |
| `COPY_SPLICE_READ_OFF` | `copy_splice_read` | `0x00416970` |
| `CONFIGFS_READ_ITER_OFF` | `configfs_read_iter` | `0x004954b8` |
| `CONFIGFS_BIN_WRITE_ITER_OFF` | `configfs_bin_write_iter` | `0x004959e4` |
| `ASHMEM_IOCTL_OFF` | `ashmem_ioctl` | `0x00d70dfc` |
| `ASHMEM_COMPAT_IOCTL_OFF` | `compat_ashmem_ioctl` | `0x00d714b8` |
| `ASHMEM_MMAP_OFF` | `ashmem_mmap` | `0x00d7150c` |
| `ASHMEM_OPEN_OFF` | `ashmem_open` | `0x00d7172c` |
| `ASHMEM_RELEASE_OFF` | `ashmem_release` | `0x00d717b4` |
| `ASHMEM_SHOW_FDINFO_OFF` | `ashmem_show_fdinfo` | `0x00d71840` |
| `ANON_PIPE_BUF_OPS_OFF` | `anon_pipe_buf_ops` | `0x0124cdc8` |
| `ASHMEM_FOPS_OFF` | `ashmem_fops` | `0x0140b440` |
| `KMALLOC_CACHES_OFF` | `kmalloc_caches` | `0x017da710` |
| `SLIDE_NFULNL_LOGGER_NAME_OFF` | first qword of `nfulnl_logger` → `"nfnetlink_log"` | `0x0175e2af` |
| `SYSTEM_UNBOUND_WQ_OFF` | `system_unbound_wq` | `0x022fae60` |
| `SLIDE_NFULNL_LOGGER_OBJECT_OFF` | `nfulnl_logger` | `0x02302278` |
| `INIT_TASK_OFF` | `init_task` | `0x0230e4c0` |
| `SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_OFF` | sole Image qword equal to `sysctl_bootid` KVA | `0x02439490` |
| `ASHMEM_MISC_FOPS_OFF` | `ashmem_misc` + `offsetof(miscdevice,fops)` | `0x0247d7f0` |
| `ROOT_TASK_GROUP_OFF` | `root_task_group` | `0x0251cd80` |
| `SELINUX_ENFORCING_OFF` | `selinux_state.enforcing` at +0 | `0x0255f5c0` |
| `SLIDE_SYSCTL_BOOTID_OFF` | `sysctl_bootid` | `0x026426d8` |

### Diff vs `pa3q-S938NKSUACZF1`

Almost every symbol and layout offset is identical. Confirmed differences:

1. **`SLIDE_NFULNL_LOGGER_NAME_OFF`**: `0x0175e2a1` → **`0x0175e2af`**
   (logger object still at `0x02302278`; its name pointer retargeted by 14 bytes).
2. **All 32 P0 fingerprint rows** differ (image text content at candidate
   slides is not the same).
3. Build identity strings, kernel release, and CSC.

## Slide helpers

### Trace event ID

Live: `109`.

Offline:

```text
event_index = (__event_sched_blocked_reason - __start_ftrace_events) / 8
            = (0x22b1be8 - 0x22b1920) / 8 = 89
event_id    = 20 + 89 = 109   # __TRACE_LAST_TYPE = 20 on this branch
```

Matches the live device.

### Worker caller

```text
worker_thread @ 0x000d9750
bl schedule   @ 0x000d97e8
next insn     @ 0x000d97ec  → SLIDE_TRACEFS_WORKER_CALLER_OFF
```

### P0 fingerprints

Generated from the raw kernel Image: for each slide
`0x000000 … 0x1f0000` step `0x10000`, eight LE qwords at page offsets
`0x000,0x200,…,0xe00`. All **256** source qwords were read back and verified.

Draft header: `recovered/p0_fingerprint.h`.

### Physical load address

The outer ABL's compressed UEFI firmware volume contains an AArch64
`LinuxLoader` PE image. In that image, PE RVA `0x0130d8` enumerates EFI RAM
partition descriptors, selects the lowest descriptor base, and stores it as
the memory base. Its diagnostic string is `Memory Base Address: 0x%x` at PE
RVA `0x0d57e7`.

The exact CZF5 kernel has a valid ARM64 Image header (`ARMd` at offset `0x38`),
`text_offset = 0`, `image_size = 0x027b0000`, and flags `0x0a`. The ARM64 path
at PE RVA `0x0185c8` selects the `0x00080000` load offset and combines it with
the memory base. The matching platform DTBs independently anchor the first
reserved DDR region at `0x80000000`, fixing the lowest RAM base used by that
calculation.

Therefore:

```c
#define P0_PHYS_OFFSET      0x80000000ULL
#define P0_KERNEL_PHYS_LOAD 0x80080000ULL
```

This is derived from the exact CZF5 BL/AP inputs and is not copied from the
older PA3Q profiles.

### Pselect word shift

The exact CZF5 disassembly fixes both sides of the stack overlap relative to
the syscall-entry stack pointer `E`:

- `__arm64_sys_futex` reserves `0x70`, `do_futex` reserves `0x60`, and
  `futex_wait_requeue_pi` places its `rt_mutex_waiter` at frame offset `0x90`
  in a `0x1c0` frame: `E - 0x70 - 0x60 - 0x1c0 + 0x90 = E - 0x200`.
- `__arm64_sys_pselect6` reserves `0x90`, and `core_sys_select` places
  `stack_fds` at frame offset `0x80` in a `0x1f0` frame:
  `E - 0x90 - 0x1f0 + 0x80 = E - 0x200`.

Waiter qword zero therefore coincides with the first logical fd-set qword:

```c
#define SLIDE_PSELECT_WORD_SHIFT 0
```

## Draft artifacts (analysis tree only)

```text
recovered/required-btf-layouts.json
recovered/derived-offsets.json
recovered/target.h
recovered/p0_fingerprint.h
recovered/vmlinux.elf
recovered/vmlinux.btf
recovered/vmlinux.nm
```

These have **not** been copied into `Root-My-Galaxy-Payloads` yet. Remaining
upstream work:

1. Copy drafts to `src/targets/pa3q-S938USQSBCZF5/`.
2. Build app payload (NDK r29) and size-check.
3. Build KernelSU module for exact vermagic + audit imports.
4. Rebuild `ksud`, add support-feed entry, provenance in payloads docs.
5. Separate hardware-validation plan before any device run.

## Payload build (completed offline)

```sh
export ANDROID_NDK_HOME=/path/to/android-ndk-29.0.14206865
make TARGET=pa3q-S938USQSBCZF5 API=35 all release
cp build/pa3q-S938USQSBCZF5/cve-2026-43499-app.release.so \
   artifacts/pa3q-S938USQSBCZF5/cve-2026-43499-app.so
```

| Artifact | Size | SHA-256 |
| --- | ---: | --- |
| `artifacts/pa3q-S938USQSBCZF5/cve-2026-43499-app.so` | 104128 | `9cc661f39293b82474cfc1a5a47d6d41cc530df830b5654d4386007f1291f588` |

Built with NDK r29 (`29.0.14206865`), API 35. Release size gate enforced.

## KernelSU status

| Item | Result |
| --- | --- |
| Feed binary | `kernelsu/ksud-pa3q-S938USQSBCZF5-kdp` (4622960 bytes; SHA-256 `3559dfbe44834ee1bd45478524bd980307312bdea868b18773af3bf0c12037a2`) |
| Standalone module | `kernelsu/android15-6.6_kernelsu-pa3q-S938USQSBCZF5-kdp.ko` (327528 bytes; SHA-256 `3c9aaf33fab3bdacc7fd8c26d2dd5ecb19f6bfe38f0c5332044fe017544b4ef6`) |
| Standalone module vermagic | `6.6.98-android15-8-pd6ff1cd-abogkiS938USQSBCZF5-4k SMP preempt mod_unload modversions aarch64` |
| Target release | `6.6.98-android15-8-pd6ff1cd-abogkiS938USQSBCZF5-4k` |
| Exact vermagic rebuild | Android 15/6.6 DDK image `ghcr.io/ylarod/ddk-min:android15-6.6-20260313` |
| Module.symvers recovered | 8873 CRCs from CZF5 `vmlinux.elf` |
| Exact module audit | 221 undefined imports; 0 missing target symbols; empty `__versions`; 67 resolved through kallsyms; 0 CRC mismatches |

Hardware validation plan:
[`SM-S938U1-S938U1UESBCZF5-HARDWARE-PLAN.md`](SM-S938U1-S938U1UESBCZF5-HARDWARE-PLAN.md).
