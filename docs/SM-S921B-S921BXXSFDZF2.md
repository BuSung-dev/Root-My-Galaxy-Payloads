# Galaxy S24 SM-S921B / S921BXXSFDZF2 port record

This record contains the exact inputs and derived values for the hardware-
untested Galaxy S24 profile `e1s-S921BXXSFDZF2`. The kernel has the same
release string as the S24 FE profile, but the raw Images are not identical and
their firmware-dependent offsets must not be mixed.

## Firmware identity and acquisition

Samsung FUS was queried with `samloader-rs` for model `SM-S921B`, region `XSP`.
The returned four-part version was:

```text
S921BXXSFDZF2/S921BOXMFDZF2/S921BXXSFDZF1/S921BXXSFDZF2
```

The encrypted source object and extracted archives were:

```text
SM-S921B_4_20260609201941_vz9pr3ds13_fac.zip.enc4
S921BXXSFDZF2_XSP.zip
AP_S921BXXSFDZF2_S921BXXSFDZF2_MQB110699048_REV00_user_low_ship_MULTI_CERT_meta_OS16.tar.md5
BL_S921BXXSFDZF2_S921BXXSFDZF2_MQB110699048_REV00_user_low_ship_MULTI_CERT.tar.md5
```

The extracted firmware properties are:

```text
model: SM-S921B
device: e1s
display build: BP4A.251205.006.S921BXXSFDZF2
fingerprint: samsung/e1sxxx/essi:16/BP4A.251205.006/S921BXXSFDZF2:user/release-keys
SDK: 36
ABI: arm64-v8a
page size: 4096
kernel release: 6.1.157-android14-11
kernel build: #1 SMP PREEMPT Tue Jun 9 07:16:56 UTC 2026
```

The system partition reports device `essi`, while Android's configured product
property source order selects the vendor value `e1s`; the support feed therefore
uses `e1s` as `Build.DEVICE` metadata and preserves the literal system build
fingerprint.

## Extracted image hashes

```text
boot.img size: 67108864
boot.img SHA-256: 27FD306FDB7E624C33F886AD5FAD27E0D6C80DF5EC8D126A1024350D15AF08AD
kernel size: 38832640
kernel SHA-256: F89829E4A7F6C833F1D60F59085F29AC16190245125831BF432771A4D5A11C97
vendor_boot.img SHA-256: 6A06B2D384686D8908E69B2594E0B81E75475366B433A4B3DBEEB25BA9EFFCF6
sboot.bin SHA-256: 5C4CA4C087862647AAF2733662EBA813941E54060BBD2263CDC2B85BF87C748E
```

The S24 FE DZF3 raw kernel hash is
`849AB87AC89552DC9624647B953491B35060517B05A68DBCBB0E76A02E59B30A`.
The differing S921B hash is why this port has a dedicated target header and P0
fingerprint table despite the matching release string.

## Symbol and BTF recovery

`vmlinux-to-elf` recovered 115630 symbols at image base
`0xffffffc008000000`. Raw BTF was extracted from kernel file interval
`[0x188c248, 0x1e439a9)`. Its little-endian header starts with
`9f eb 01 00 18 00 00 00`; extraction and complete header validation follow
the procedure in [`PORTING.md`](PORTING.md). Exact member offsets below came
from `bpftool ... format raw`, not the C declaration view.

Required offsets from the recovered S921B ELF are:

| Macro/use | Symbol or derivation | Offset |
| --- | --- | ---: |
| `CALL_USERMODEHELPER_EXEC_WORK_OFF` | `call_usermodehelper_exec_work` | `0x000d4468` |
| `SLIDE_TRACEFS_WORKER_CALLER_OFF` | instruction after the blocking `worker_thread -> schedule` call | `0x000dbd9c` |
| `NOOP_LLSEEK_OFF` | `noop_llseek` | `0x003a1414` |
| `COPY_SPLICE_READ_OFF` | `generic_file_splice_read` | `0x003ef02c` |
| `CONFIGFS_READ_ITER_OFF` | `configfs_read_iter` | `0x00470d44` |
| `CONFIGFS_BIN_WRITE_ITER_OFF` | `configfs_bin_write_iter` | `0x00471274` |
| `ASHMEM_IOCTL_OFF` | `ashmem_ioctl` | `0x00d38cfc` |
| `ASHMEM_COMPAT_IOCTL_OFF` | `compat_ashmem_ioctl` | `0x00d39634` |
| `ASHMEM_MMAP_OFF` | `ashmem_mmap` | `0x00d3968c` |
| `ASHMEM_OPEN_OFF` | `ashmem_open` | `0x00d398b8` |
| `ASHMEM_RELEASE_OFF` | `ashmem_release` | `0x00d39940` |
| `ASHMEM_SHOW_FDINFO_OFF` | `ashmem_show_fdinfo` | `0x00d39a60` |
| `ANON_PIPE_BUF_OPS_OFF` | `anon_pipe_buf_ops` | `0x0121dd10` |
| `ASHMEM_FOPS_OFF` | `ashmem_fops` | `0x013d9ec8` |
| `SLIDE_NFULNL_LOGGER_NAME_OFF` | `"nfnetlink_log"` string referenced by `nfulnl_logger.name` | `0x016dd6a0` |
| `KMALLOC_CACHES_OFF` | `kmalloc_caches` | `0x017a8098` |
| `SYSTEM_UNBOUND_WQ_OFF` | `system_unbound_wq` | `0x022eae58` |
| logger array | distinct `loggers[NFPROTO_NUMPROTO][NF_LOG_TYPE_MAX]` object | `0x022f2950` |
| `SLIDE_NFULNL_LOGGER_OBJECT_OFF` | `nfulnl_logger` object | `0x022f2a08` |
| `INIT_TASK_OFF` | `init_task` | `0x022ff800` |
| `SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_OFF` | `.data` pointer slot in the `random_table[]` entry named `boot_id` | `0x0243ef78` |
| `ASHMEM_MISC_FOPS_OFF` | `ashmem_miscs + offsetof(miscdevice, fops)` | `0x02484bb0` |
| `ROOT_TASK_GROUP_OFF` | `root_task_group` | `0x02515cc0` |
| `SELINUX_ENFORCING_OFF` | `selinux_state.enforcing` | `0x025ea478` |
| `SLIDE_SYSCTL_BOOTID_OFF` | actual `sysctl_bootid` UUID storage | `0x026d1b60` |

The target BTF confirms the same exploit-relevant layouts as the existing 6.1
profile: `struct file_operations` is `0x110`, `struct page` is `0x40`, and the
task, waiter, configfs, workqueue, pipe, subprocess, and miscdevice fields used
by the payload have identical offsets. The `__arm64_sys_pselect6` stack shape
also places waiter qword zero at the first qword of the logical read/write/
exception fd-set sequence, so `SLIDE_PSELECT_WORD_SHIFT` is zero. This macro is
a count of leading 64-bit words, not a byte offset. Runtime trace event ID `106`
was derived as `__TRACE_LAST_TYPE (20) +` the zero-based
`sched_blocked_reason` registration index `(86)`.

`SLIDE_NFULNL_LOGGER_NAME_OFF` and
`SLIDE_NFULNL_LOGGER_OBJECT_OFF` deliberately name different addresses: the
former is the string pointer read from qword zero of the latter. Likewise,
`SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_OFF` is the writable sysctl table pointer
slot temporarily redirected by the oracle, whereas `SLIDE_SYSCTL_BOOTID_OFF`
is the UUID storage restored into that slot.

## Physical load proof

The raw ARM64 Image has `text_offset == 0`. In `sboot.bin`, the code referencing
`Starting kernel...` is at file offset `0x248e6c`; the call path at `0x248e78`
loads the Image text offset, adds `0x80000000`, and branches to the resulting
entry point. Therefore:

```c
#define P0_PHYS_OFFSET 0x80000000ULL
#define P0_KERNEL_PHYS_LOAD 0x80000000ULL
```

## P0 table and payload build

`src/targets/e1s-S921BXXSFDZF2/p0_fingerprint.h` was generated from the target
raw Image for all 32 candidates `0x000000` through `0x1f0000`. Verification
read all 256 source qwords back at page offsets `0x000, 0x200, ..., 0xe00`.

The app-domain release payload is built with:

```sh
make TARGET=e1s-S921BXXSFDZF2 \
  ANDROID_NDK_HOME=/path/to/android-ndk-r29 release
```

The fixed-size result is published at
`artifacts/e1s-S921BXXSFDZF2/cve-2026-43499-app.so`.

## KernelSU compatibility

The Samsung-patched KernelSU 6.1 module has exact vermagic
`6.1.157-android14-11 SMP preempt mod_unload modversions aarch64`. Its complete
module symbol checker passed against this recovered S921B `vmlinux.elf`, and
the Samsung KDP/DEFEX function prototypes match the target BTF. Only after
those checks was the existing 6.1 module shared under the generic artifact
names documented in `kernelsu/README.md`.

## Cleanup and validation state

The 38,832,640-byte raw kernel and a provenance record are retained locally.
The 17.6 GB firmware ZIP, AP/BL archives, boot/vendor images, recovered ELF,
and temporary BTF files are removed after verification. The profile has not
been executed on physical SM-S921B hardware; device validation remains
explicitly pending.
