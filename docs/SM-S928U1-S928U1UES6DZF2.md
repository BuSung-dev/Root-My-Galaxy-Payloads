# SM-S928U1 / S928U1UES6DZF2 porting record

This file records the evidence-driven port of the Galaxy S24 Ultra US unlocked
firmware. Values from another device or firmware must not be reused. Each stage
is added only after its inputs and results have been verified.

## Stage 1: freeze and verify the input evidence

Status: **COMPLETE**

### Firmware identity

| Field | Verified value |
| --- | --- |
| Package model | `SM-S928U1` |
| AP/PDA package | `S928U1UES6DZF2` |
| Internal AP/kernel build | `S928USQS6DZF2` |
| Device codename | `e3q` |
| Product name | `e3quew` |
| Android build ID | `BP4A.251205.006` |
| Build fingerprint | `samsung/e3quew/e3q:16/BP4A.251205.006/S928U1UES6DZF2:user/release-keys` |
| Kernel release | `6.1.145-android14-11-33419968-abS928USQS6DZF2` |

The outer firmware filenames use `SM-S928U1` and `S928U1UES6DZF2`, while the
boot metadata and kernel banner use `SM-S928U` and `S928USQS6DZF2`. Both forms
are retained so that the package and its internal build can be traced exactly.

### Verified AP/kernel chain

| Object | Size (bytes) | SHA-256 |
| --- | ---: | --- |
| AP `boot.img.lz4` | — | `bf212e8c0e3b39743f76f5e0802fde8ba6ca07a048135017c079207708cbbc88` |
| Decompressed `boot.img` | 100,663,296 | `2d7041a156862981ca5af5529c8fca4e8309c8e9fa5f1557491002336d3d7e55` |
| Raw kernel payload | 38,005,248 | `875b5017e46cb657510a1966d07f897616b448798746d9419abc0e97d080c7c5` |
| Recovered `vmlinux.elf` | 43,070,883 | `f99b81dca0b44c86e4aad58664497305d796c220ef4a895d2f6680138fc88b06` |
| Extracted `vmlinux.btf` | 5,981,643 | `8415104c012e18942b18bcb52f401075cb6b92df837b9552a8c11070d65efe56` |

The decompressed AP image hash matches the extracted `boot.img`. The raw
kernel is recognized as a little-endian ARM64 Linux boot Image with 4 KiB
pages. The kernel payload extracted from `recovery.img` is byte-for-byte
identical to the payload from `boot.img`.

`vmlinux.elf` was recovered directly from this raw kernel with
`vmlinux-to-elf`; it is a symbolized, non-stripped AArch64 ELF. Therefore its
symbols are tied to this exact DZF2 kernel payload rather than to a rebuilt or
sibling-device kernel.

### Verified bootloader chain

| Object | Size (bytes) | SHA-256 |
| --- | ---: | --- |
| BL `abl.elf.lz4` | 763,279 | `858098f9c9ca84ab2fc529456c8964dcc6c15b97c07352b1401481ba60776a65` |
| Decompressed `abl.elf` | 2,441,528 | `61021eab404516243207939debaefd8448f833ef161254a1819f0de8e534bd3e` |

Streaming decompression of the original BL archive member produces exactly
the same SHA-256 as the recovered `abl.elf`. This proves that the bootloader
analysis input comes from the matching
`BL_S928U1UES6DZF2_S928USQS6DZF2_...` package.

### Local evidence paths

```text
analysis-s928-dzf/firmware/AP/boot.img
analysis-s928-dzf/firmware/AP/boot.img.kernel_payload
analysis-s928-dzf/stage5a/recovered/vmlinux.elf
analysis-s928-dzf/stage5a/recovered/vmlinux.nm
analysis-s928-dzf/stage5a/recovered/vmlinux.btf
analysis-s928-dzf/stage5a/recovered/bl/abl.elf
```

### Stage 1 conclusion

The kernel, recovered symbol ELF, extracted BTF, and ABL inputs have an exact,
hash-verified provenance chain to the S928U1 DZF2 AP/BL packages. No value from
an S921B, S721N, S928N, or S938N build was used.

No symbol offsets, structure-layout constants, physical load addresses, trace
constants, or P0 fingerprints are asserted by this stage.

## ABL kernel handoff analysis

Status: **COMPLETE**

The outer `abl.elf` is an ELF32 ARM container whose load segment contains a
compressed UEFI firmware volume.  Decompressing that volume exposes the
`LinuxLoader` application as a stripped AArch64 PE32+ image (FFS GUID
`f536d559-459f-48fa-8bbc-43b554ecae8d`).  All RVAs below refer to that
`LinuxLoader` PE image, not to file offsets in the outer ELF container.

There is no literal `Starting kernel` string in either the outer ABL or the
extracted `LinuxLoader`.  The closest string, `Now, Start Booting...`, is at
PE RVA `0x0b6e3e`; its reference at RVA `0x046c08` belongs to boot-state timeout
reporting and is not the kernel handoff.  The actual path was identified from
the `BootLinux`, `Kernel Load Address: 0x%x`, and shutdown-services code.

### ARM64 entry calculation

The entry calculation and final transfer are:

1. RVA `0x012a44` enumerates the EFI RAM partition descriptors and stores the
   lowest descriptor base as the memory base.  Its diagnostic string is
   `Memory Base Address: 0x%x` at RVA `0x0d364b`.
2. RVA `0x0176fc` through `0x017740` selects the ARM64 load constants.  For an
   ARM64 Image it reads `0x00080000` from RVA `0x0d7f18`, combines it with the
   memory base, and retains a `0x05600000` kernel region size.  The alternative
   `0x00800000`/`0x03c00000` pair is selected for the ARM32 path.
3. RVA `0x0178cc` through `0x0178f0` reads the ARM64 Image header.  When the
   header's `image_size` field is nonzero, the final entry is computed as
   `kernel_load_base + text_offset` and saved for the handoff.
4. RVA `0x017db0` loads the computed address for the
   `Kernel Load Address: 0x%x` diagnostic.  After UEFI shutdown, RVA
   `0x018d48` loads that same value into `x20`; RVA `0x018d94` loads the DTB in
   `x0`, and RVA `0x018da4` executes `blr x20`.  The separate `blr x20` at RVA
   `0x018d60` uses the ARM32 `r0/r1/r2` calling convention and is not the path
   taken by this kernel.

The exact DZF2 raw kernel has the following little-endian ARM64 Image header
fields:

```text
text_offset = 0x0000000000000000
image_size  = 0x00000000026f0000
flags       = 0x000000000000000a
magic       = ARMd
```

ABL obtains the memory base dynamically rather than embedding it in the
handoff instruction.  The matching DZF2 `vendor_boot.img` contains 22 DTBs;
their `/memory` node is intentionally filled at boot, while the selected
platform layout anchors the first reserved DDR region as
`gunyah_hyp_region@80000000` with `reg = <0 0x80000000 0 0x00e00000>`.
This independently fixes the lowest RAM base used by the ABL calculation at
`0x80000000`.

Therefore the values for this target are:

```c
#define P0_PHYS_OFFSET      0x80000000ULL
#define P0_KERNEL_PHYS_LOAD 0x80080000ULL
```

The second value is not copied from an Exynos `sboot.bin` record: it is the
S928U1 Qualcomm ABL result
`0x80000000 + 0x00080000 + text_offset(0)`.  It also differs from both the
Exynos sibling value `0x80000000` and the S25U Qualcomm value `0xa8000000`.

## Symbol/BTF compatibility checkpoint

The non-deployable symbol/BTF compatibility audit is recorded in
`analysis-s928-dzf/docs/E3Q_DZF2_COMPATIBILITY_AUDIT.md`. Required symbol names
are present and unique, but the target's compact `rt_mutex_waiter` ABI is not
compatible with the expanded waiter layout assumed by the existing Android
6.1 target headers. A sibling `target.h` must not be copied for this device.

## Target symbol and BTF extraction

Status: **COMPLETE for named symbols and required structure layouts**

All offsets in this section were extracted from the hash-frozen DZF2
`vmlinux.elf` and detached BTF listed in Stage 1. The recovered image base is
`0xffffffc008000000`.

| Use | Target derivation | Offset |
| --- | --- | ---: |
| usermode-helper worker | `call_usermodehelper_exec_work` | `0x000d39cc` |
| no-op seek | `noop_llseek` | `0x003a14e4` |
| splice read | `generic_file_splice_read` | `0x003ef340` |
| configfs read | `configfs_read_iter` | `0x004712a4` |
| configfs binary write | `configfs_bin_write_iter` | `0x004717d4` |
| ashmem ioctl | `ashmem_ioctl` | `0x00d3a314` |
| ashmem compat ioctl | `compat_ashmem_ioctl` | `0x00d3ac4c` |
| ashmem mmap | `ashmem_mmap` | `0x00d3aca4` |
| ashmem open | `ashmem_open` | `0x00d3aed0` |
| ashmem release | `ashmem_release` | `0x00d3af58` |
| ashmem fdinfo | `ashmem_show_fdinfo` | `0x00d3b078` |
| anonymous pipe operations | `anon_pipe_buf_ops` | `0x01219d90` |
| ashmem operations | `ashmem_fops` | `0x013d1140` |
| allocator caches | `kmalloc_caches` | `0x0176c6f8` |
| unbound workqueue | `system_unbound_wq` | `0x0223ae60` |
| logger array | `loggers` | `0x02242968` |
| netfilter logger object | `nfulnl_logger` | `0x02242a20` |
| initial task | `init_task` | `0x0224f8c0` |
| ashmem misc fops | `ashmem_miscs + 0x10` | `0x023bb5b0` |
| root task group | `root_task_group` | `0x0244cd80` |
| SELinux enforcing | `selinux_state.enforcing` | `0x02521588` |
| boot-ID sysctl object | `sysctl_bootid` | `0x026046e8` |

The ashmem misc symbol is plural on this kernel. BTF reports
`miscdevice.fops == 0x10`, so the derived fops location is
`ashmem_miscs + 0x10`; it is not an independently named symbol.

Required target layouts are:

```text
file_operations: size 0x110; ioctl 0x50; compat_ioctl 0x58; mmap 0x60;
  open 0x70; release 0x80; splice_read 0xc8; show_fdinfo 0xe0
task_struct: size 0x12c0; usage 0x40; prio 0x84; normal_prio 0x8c;
  sched_task_group 0x348; pi_lock 0x924; pi_waiters 0x938;
  pi_top_task 0x948; pi_blocked_on 0x950
rt_mutex_waiter: size 0x58; tree_entry 0x00; pi_tree_entry 0x18;
  task 0x30; lock 0x38; wake_state 0x40; prio 0x44;
  deadline 0x48; ww_ctx 0x50
configfs_buffer: page 0x10; needs_read_fill 0x50; bin_buffer 0x58;
  bin_buffer_size 0x60; cb_max_size 0x64
workqueue_struct.dfl_pwq 0xb0
pool_workqueue: pool 0x00; wq 0x08; work_color 0x10; refcnt 0x18;
  nr_active 0x5c; max_active 0x60
worker_pool: worklist 0x28; nr_idle 0x3c
work_struct: data 0x00; entry 0x08; func 0x18
page/slab: size 0x40; compound_head 0x08; slab_cache 0x18;
  page_type 0x30
```

The shared payload now has an explicit `COMPACT_RT_MUTEX_WAITER` selection.
It initializes the E3Q-only packed `wake_state`/`prio` word and the trailing
`ww_ctx` pointer without changing the older 5.10 legacy path. Host Clang
syntax checks pass for both the existing legacy selection and the new compact
selection.

## Slide and P0 derivation

Status: **COMPLETE for static derivation and source readback**

The DZF2 ftrace-event section begins at `0x021ff2b0` and
`__event_sched_blocked_reason` is at `0x021ff560`. The difference is `0x2b0`,
or 86 eight-byte section entries. The Android 6.1 dynamic event base is 20,
giving runtime event ID `106`.

Disassembly of the target `worker_thread` shows the idle-worker call to
`schedule` at `0x000db19c`; the saved return/wchan address is therefore:

```c
#define SLIDE_TRACEFS_EVENT_ID 106
#define SLIDE_TRACEFS_WORKER_CALLER_OFF 0x000db1a0ULL
```

The target requires a three-qword pselect shift. From the syscall-entry stack
pointer `E`, the stale waiter is at `E - 0x1e8`, while the pselect `stack_fds`
array begins at `E - 0x200`. Hardware panic readback confirmed that shift
three places the intended fake-lock address in the actual `waiter->lock` slot;
shifts zero and one reached null or incorrect lock paths. Therefore:

```c
#define SLIDE_PSELECT_WORD_SHIFT 3
```

The skb fragment bias also required a runtime correction. BTF gives
`sizeof(struct skb_shared_info) == 344`, which makes `SKB_MAX_HEAD(0)` equal
to `0xe80`, but `unix_stream_sendmsg` subsequently page-aligns the fragment
data length. For a 64 KiB send, the first order-3 fragment begins at source
offset `0x1000`, not `0xe80`:

```c
#define SKB_DATA_DELTA (-0x1000LL)
```

## Hardware debugging status (2026-07-21)

Authorized device testing has reached the PI-chain path with the expected
fake-lock address and has also produced confirmed pselect write windows. The
complete 240-pipe oracle nevertheless remained unchanged, separating working
scheduler timing from the unresolved allocator-placement problem.

Reclaim sprays of 28, 30, and 32 sends and adjacent drain counts from 28 to 32
were tested. The 32-send and 28-drain boundaries produced stale/null fake-lock
panics; stable configurations produced reclaim misses rather than an oracle
hit. The retained conservative baseline is 28 sends with two synchronous
post-target drain triggers. Experimental multi-trigger and boundary settings
were reverted.

Review of the release path found that closing `/proc/PID/mem` before reaping
the child could leave the final `mm_struct` reference to deferred task-exit
cleanup. The retained order kills and waits while the descriptor pins the mm,
then closes the last reference on the exploit CPU. This makes the final release
synchronous, but has not yet demonstrated target-page reuse. The next useful
diagnostic is to trace whether the leaked order-3 slab reaches the buddy
allocator and which skb fragment receives that physical block.

The raw Image contains exactly one qword equal to the target address of
`sysctl_bootid`, at Image offset `0x023762f0`. Inspection of the enclosing
`random_table` confirms this is the `boot_id` ctl-table entry's `data` field.
The `nfulnl_logger` object at `0x02242a20` contains:

```text
+0x00 = 0xffffffc0096a61b8  (Image offset 0x016a61b8)
+0x08 = 1
+0x10 = 0xffffffc008f0e9e4
```

The target slide constants are consequently:

```c
#define SLIDE_NFULNL_LOGGER_OFF 0x016a61b8ULL
#define SLIDE_LOGGERS_0_1_OFF 0x02242a20ULL
#define SLIDE_RANDOM_BOOT_ID_DATA_OFF 0x023762f0ULL
#define SLIDE_SYSCTL_BOOTID_OFF 0x026046e8ULL
```

`stage5a/tools/generate_p0_fingerprint.pl` generated all 32 candidate rows
from the exact DZF2 raw Image. It then reopened the source and independently
read back all 256 qwords at offsets `0x000, 0x200, ..., 0xe00`. Regenerating
the file produces a byte-identical result:

```text
src/targets/e3q-S928USQS6DZF2/p0_fingerprint.h
SHA-256 ed0540d293e9b39cb19470ff4d382a9b7195c6ea61bb3afc0d34072bb422f859
```

The target header and P0 table are now present. Host Clang accepts all app
payload sources with this target selected.

## Payload build

Status: **COMPLETE for reproducible offline build**

The official Android NDK r29 (`29.0.14206865`) Linux archive was checked
against its published SHA-1 before use:

```text
android-ndk-r29-linux.zip
SHA-1 87e2bb7e9be5d6a1c6cdf5ec40dd4e0c6d07c30b
```

The release command was:

```sh
make TARGET=e3q-S928USQS6DZF2 \
  ANDROID_NDK_HOME=/home/steve/.local/android-ndk/android-ndk-r29 release
```

The unpadded stripped AArch64 shared object is 88,856 bytes. The Makefile
fixed-size result is 104,128 bytes. Two forced release builds produced the
same SHA-256, and the published local artifact is byte-identical:

```text
artifacts/e3q-S928USQS6DZF2/cve-2026-43499-app.so
size 104128
SHA-256 693d86f889f3137cfc7d455b1ff9aa4673bd112c98a9ff68382e98b5adc10dbd
```

`make all` also completed for the root-domain shared object, app-domain
shared object, and PIE helper. This verifies compilation only. S921B is an
Exynos 2400 target; none of its kernel constants, load-address proof, or
bootloader assumptions are used as Qualcomm E3Q evidence.

## KernelSU late-load artifacts

Status: **COMPLETE for target-specific build and static compatibility audit**

KernelSU tag `v3.2.5`, commit
`b0bc817b4e966aa6aa830834eaf6ef765d821d40`, was patched with the repository's
Samsung KDP/RKP/DEFEX patch. The module was built with the Android 14 6.1 DDK
image `ghcr.io/ylarod/ddk-min:android14-6.1-20260313` (digest
`sha256:1dd6ac340b627a90a4031d6d0df6b129d8cc949b139fb56032c898f023d3d5d3`)
and its Clang r487747c 17.0.2 toolchain.

The generated release and module metadata match the exact target:

```text
vermagic: 6.1.145-android14-11-33419968-abS928USQS6DZF2 SMP preempt mod_unload modversions aarch64
```

KernelSU's `check_symbol` passed against the recovered DZF2 `vmlinux.elf`.
The repository audit tool additionally checked all 209 undefined module
symbols against that same ELF. All were present. The target enables symbol
trimming, so the module deliberately has an empty `__versions` section and is
loaded through KernelSU's kallsyms-based manual relocation path; 50 imports
are resolved by name rather than conventional target exports.

The stripped KO was placed in the `android14-6.1_kernelsu.ko` asset slot and
`ksud` was rebuilt from a clean asset input for Android/AArch64 with NDK r29.
The resulting binary names the embedded KMI asset. Published artifacts are:

| File | Size (bytes) | SHA-256 |
| --- | ---: | --- |
| `kernelsu/android14-6.1_kernelsu-e3q-S928USQS6DZF2-kdp.ko` | 400,152 | `ed7afea6cd221d5698739d3a1633264c084ffb77f2df730e5808941e0a555de5` |
| `kernelsu/ksud-e3q-S928USQS6DZF2-kdp` | 4,726,416 | `50c10e5110048b901287c311e6cff12d4f59fff888b1e6023db7bb6f7ddbed88` |

SM-S921B is Exynos 2400 and its 6.1 module is not used or shared with this
Snapdragon E3Q build.

## Completion boundary

All offline porting gates are complete: exact firmware provenance, ABL load
address proof, symbols and BTF layouts, slide constants, P0 readback, payload
reproducibility, exact KernelSU vermagic, target-symbol audit, and embedded
late-load build. Hardware diagnostics have validated the corrected pselect
placement and skb fragment boundary, but target-page reuse and a complete
oracle hit remain unverified. The profile stays experimental while debugging
continues.
