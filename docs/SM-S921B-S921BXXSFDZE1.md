# Galaxy S24 SM-S921B / S921BXXSFDZE1 port record

This record contains the exact inputs and derived values for the hardware-
untested Galaxy S24 profile `e1s-S921BXXSFDZE1`. The DZE1 raw kernel differs
from the earlier S921B DZF2 image, so every value below was derived from the
DZE1 image and then confirmed to coincide with the DZF2 record; the two builds
are content-identical apart from the build banner.

## Firmware identity and acquisition

Samsung FUS was queried with `samloader` for model `SM-S921B`, region `EUX`.
The returned four-part version was:

```text
S921BXXSFDZE1/S921BOXMFDZE1/S921BXXSFDZE1/S921BXXSFDZE1
```

The encrypted source object and extracted archives were:

```text
SM-S921B_4_20260518172251_dgie00jpjb_fac.zip.enc4
S921BXXSFDZE1_EUX.zip
AP_S921BXXSFDZE1_S921BXXSFDZE1_MQB109843140_REV00_user_low_ship_MULTI_CERT_meta_OS16.tar.md5
BL_S921BXXSFDZE1_S921BXXSFDZE1_MQB109843140_REV00_user_low_ship_MULTI_CERT.tar.md5
```

The device-reported properties are:

```text
model: SM-S921B
device: e1s
display build: BP4A.251205.006.S921BXXSFDZE1
fingerprint: samsung/e1sxxx/essi:16/BP4A.251205.006/S921BXXSFDZE1:user/release-keys
SDK: 36
ABI: arm64-v8a
page size: 4096
kernel release: 6.1.157-android14-11
kernel build: #1 SMP PREEMPT Mon May 18 05:19:31 UTC 2026
```

The system partition reports device `essi`, while Android's configured product
property source order selects the vendor value `e1s`; the support feed therefore
uses `e1s` as `Build.DEVICE` metadata and preserves the literal system build
fingerprint.

## Extracted image hashes

```text
boot.img size: 67108864
boot.img SHA-256: 0A740C105AC839185A73BA6C54989229B140083EF60E1BC5462BD89489D6CACF
kernel size: 38832640
kernel SHA-256: 6B616D3A227AC30DF4B4633F3D7CCFEFC5E38F04EBE922FBDBF9B210DB953DDD
vendor_boot.img SHA-256: 81354C8920785348AC4CDC93C32D1B2E6BADEA0613625B270368EE75FE7B1098
sboot.bin SHA-256: 89F3A6CC64344F1BCA97B594C240E3C1A1125BB15E96FDB669B7C2386D57C525```

The earlier S921B DZF2 raw kernel hash is
`F89829E4A7F6C833F1D60F59085F29AC16190245125831BF432771A4D5A11C97`. DZE1 and
DZF2 have the same release string but different raw Images; the DZE1 Image is
dated May 18 2026 and the DZF2 Image June 9 2026.

## Symbol and BTF recovery

`vmlinux-to-elf` recovered 115629 symbols at image base
`0xffffffc008000000`. Raw BTF was extracted from kernel file interval
`[0x188c248, 0x1e439a9)`. Its little-endian header starts with
`9f eb 01 00 18 00 00 00`; extraction and complete header validation follow
the procedure in [`PORTING.md`](PORTING.md). Exact member offsets below came
from the raw BTF, not the C declaration view.

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

`src/targets/e1s-S921BXXSFDZE1/p0_fingerprint.h` was generated from the target
raw Image for all 32 candidates `0x000000` through `0x1f0000`. Verification
read all 256 source qwords back at page offsets `0x000, 0x200, ..., 0xe00`;
all 256 qwords match the DZF2 table, confirming the two Images are
content-identical apart from the build banner.

The app-domain release payload is built with:

```sh
make TARGET=e1s-S921BXXSFDZE1 \
  ANDROID_NDK_HOME=/path/to/android-ndk release
```

On macOS, point `TARGET_CC` at the darwin prebuilt clang and shim `stat -c %s`
for the release size gate. The fixed-size result is published at
`artifacts/e1s-S921BXXSFDZE1/cve-2026-43499-app.so`.

## KernelSU compatibility

The Samsung-patched KernelSU 6.1 module has exact vermagic
`6.1.157-android14-11 SMP preempt mod_unload modversions aarch64`. On hardware,
the generic 6.1 module (`android14-6.1_kernelsu-samsung-kdp.ko`) panics the
device in Samsung/Exynos EL2 during KernelSU's live text patching (it imports
`stop_machine`), so this target uses a dedicated no-patch-text build,
`android14-6.1_kernelsu-e1s-S921BXXSFDZE1-kdp.ko` with the matching
`ksud-e1s-S921BXXSFDZE1-kdp` late-load binary, documented in
`kernelsu/README.md`. The module is built from the Samsung no-patch-text source
with `CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y` for the exact DZE1 release. Its
manual-relocation audit against this recovered S921B `vmlinux.elf` reports 202
undefined imports, zero missing from the target symbol table, an empty
`__versions` section, zero target CRC mismatches, and no `stop_machine` import.
On device the `ksud-e1s-S921BXXSFDZE1-kdp` late-load reports KernelSU active
without an EL2 panic.

## Cleanup and validation state

The 38,832,640-byte raw kernel and recovered `vmlinux.elf` are retained locally
for debugging and retries, together with a provenance record. The 17.6 GB
firmware ZIP and AP/BL archives are removed after verification. Device
validation is complete: the e1s exploit reached `done=1 root=1` and the
no-patch-text KernelSU module late-loaded and reported active on SM-S921B
hardware. Root is RAM-only; a reboot removes it, and force-stopping the app
after the exploit can panic the device by releasing retained kernel pages.
