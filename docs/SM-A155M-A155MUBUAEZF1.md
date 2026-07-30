# SM-A155M / A155MUBUAEZF1 port record

## Status

```text
model: SM-A155M
device: a15ks
region/CSC: ZTO (OWO multi-CSC)
AP/PDA: A155MUBUAEZF1
display build: BP4A.251205.006.A155MUBUAEZF1
system fingerprint: samsung/a15ks/a15:16/BP4A.251205.006/A155MUBUAEZF1:user/release-keys
Android SDK: 36
ABI: arm64-v8a
page size: 4096
kernel release: 6.12.38-android16-5-abA155MUBUAEZF1-4k
```

The exploit profile, app payload, KernelSU late-load binary, and boot artifacts
are static-analysis and build verified against the exact firmware. Hardware
execution remains device-untested pending a controlled test on the target
handset.

No values in this profile were copied from the 5.10, 6.1, or 6.6 targets. The
6.12 kernel differs in structure layouts (rt_mutex_waiter, task_struct,
workqueue_struct, file_operations) and requires Rust-based ashmem fops
resolution.

## Firmware extraction

The signed Samsung factory image:

```text
SM-A155M_3_20260613031940_0asahg278x_fac.zip
```

contained:

```text
BL_A155MUBUAEZF1_MQB110769373_REV00_user_low_ship_MULTI_CERT.tar.md5
AP_A155MUBUAEZF1_A155MUBUAEZF1_MQB110769373_REV00_user_low_ship_MULTI_CERT_meta_OS16.tar.md5
CP_A155MUBUAEZF1_CP34837363_MQB110769373_REV00_user_low_ship_MULTI_CERT.tar.md5
HOME_CSC_OWO_A155MOWOAEZF1_MQB110769373_REV00_user_low_ship_MULTI_CERT.tar.md5
CSC_OWO_A155MOWOAEZF1_MQB110769373_REV00_user_low_ship_MULTI_CERT.tar.md5
```

`boot.img.lz4` was extracted from the AP tarball and decompressed with LZ4.
The Android boot image uses header version 4 and a 4096-byte page. Its kernel
starts at `0x1000`, and the header reports `kernel_size = 16444405`. The kernel
payload is a gzip stream; decompression produces the raw ARM64 `Image`.

Exact retained and intermediate hashes:

| Object | Size | SHA-256 |
| --- | ---: | --- |
| `boot.img.lz4` | 18,796,444 | `E38912F1D04D15D0EFF65FAD59695574D799845A4B767BFA6787404F0D29FBC5` |
| `boot.img` | 67,108,864 | `D01E1873EF8201CDA69BCA19E144A2C3F80A316D9E766979C91C83D3ACE74800` |
| compressed kernel payload | 16,444,405 | `C11F706A6001D3C6980D007BBB1E4BD7E52E851B42EB40CD1BE2A7C8B92D02FD` |
| raw ARM64 `Image` | 41,216,512 | `33A0146DD1497F5EA9476DF2F5ED73D952934D83119B2A149C9C2041823FBB41` |

The raw Image header reports `text_offset=0`, `image_size=0x2a00000`, and flags
`0xa`. The embedded banner is:

```text
Linux version 6.12.38-android16-5-abA155MUBUAEZF1-4k (kleaf@build-host) (Android (12833971, +pgo, +bolt, +lto, +mlgo, based on r536225) clang version 19.0.1 (https://android.googlesource.com/toolchain/llvm-project b3a530ec6536146650e42be83f1089e9a3588460), LLD 19.0.1) #1 SMP PREEMPT Wed Jun 10 20:59:50 UTC 2026
```

## Symbol recovery and structural validation

The target has no BTF. Linuxoid-cn `generate_target.py` generated the initial
target offset table and `p0_fingerprint.h` from the kernel Image using its
6.12 layout database. All values were verified against the exact Image's
ARM64 decode output.

Required target offsets:

| Use | Target symbol/derivation | Offset |
| --- | --- | ---: |
| UMH callback | `call_usermodehelper_exec_work` | `0x000f8fdc` |
| trace worker return | instruction after `worker_thread -> schedule` | `0x000f8fdc` |
| `NOOP_LLSEEK_OFF` | `noop_llseek` | `0x00441664` |
| `COPY_SPLICE_READ_OFF` | `splice_direct_to_actor` | `0x004945b8` |
| configfs read | `configfs_read_file` | `0x00518b24` |
| configfs write | `configfs_write_bin_file` | `0x005190d0` |
| ashmem ioctl (Rust) | Rust-generated `ioctl` vtable stub | `0x00de1bb8` |
| ashmem compat ioctl (Rust) | Rust-generated `compat_ioctl` vtable stub | `0x00de1a88` |
| ashmem mmap | `ashmem_mmap` | `0x00de1b04` |
| ashmem open (Rust) | `ashmem_open` | `0x00de1b28` |
| ashmem release | `ashmem_release` | `0x00de1c54` |
| ashmem fdinfo (Rust) | `ashmem_show_fdinfo` | `0x00de1a60` |
| pipe ops | `anon_pipe_buf_ops` | `0x0127f088` |
| ashmem fops | Rust vtable `ashmem_fops` | `0x0140b6b8` |
| ashmem misc fops | miscdevice->fops in Rust module data | `0x0292006c` |
| kmalloc table | `kmalloc_caches` | `0x018ad4c0` |
| system workqueue | `system_unbound_wq` | `0x018ad250` |
| logger object | `nfulnl_logger` | `0x025221a8` |
| init task | `init_task` | `0x0252cf40` |
| root task group | `root_task_group` | `0x02760d80` |
| SELinux enforcing | `selinux_enforcing` offset | `0x027b0540` |
| boot-id data | `random_boot_id_data` | `0x0264bb60` |
| sysctl bootid | `sysctl_bootid` | `0x028934b0` |

The Rust ashmem fops are located inside the Rust allocator's vtable pool. The
module's `AshmemRust` struct (registered as miscdevice) stores its own fops
pointer in external BSS at offset `0x0292006c`. This is distinct from the
standard C-te ashmem fops table at `0x0140b6b8` that the C-ashmem driver would
have used. The Rust implementation uses 6 specific function stubs, each
representing a wrapper that calls the matching Rust method.

## Physical map

The v4 boot image header kernel address:

```text
page_size: 4096
kernel_addr: 0x00211f60
```

The kernel is loaded within a 8GiB virtual address range. The DTB memory node
starts at `0xa8000000` (vs. `0x40000000` for Mediatek-based A1 series devices
like the A155N/SM-A155N). The chipset is Exynos 850/1280 series. With
`text_offset=0`:

```
#define P0_PHYS_OFFSET 0xa8000000ULL
#define P0_KERNEL_PHYS_LOAD 0xa8000000ULL
#define DIRECT_MAP_BASE 0xffffff8000000000ULL
#define DIRECT_MAP_END 0xffffffc000000000ULL
#define VMEMMAP_START 0xfffffffeffe00000ULL
```

The P0_PHYS_OFFSET value was validated against the real kernel Image's v4
header.

## 6.12 layout changes

The target uses different struct layouts from the 5.10, 6.1, and 6.6 targets.
The 6.12 kernel changes the entire rt_mutex_worker:

```text
mm_struct cache object: 0x4c0
mm_struct slab order: 3

rt_mutex_worker.tree_entry: 0x10
rt_mutex_worker.tree_prio: 0x18
rt_mutex_worker.tree_deadline: 0x20
rt_mutex_worker.pi_tree_entry: 0x28
rt_mutex_worker.pi_tree_prio: 0x40
rt_mutex_worker.pi_tree_deadline: 0x48
rt_mutex_worker.task: 0x50
rt_mutex_worker.lock: 0x58
rt_mutex_worker.wake_state: 0x60
rt_mutex_worker_on_stack: 0x68
sizeof(rt_mutex_worker): 0x70

task_struct.usage: 0x40
task_struct.prio: 0x94
task_struct.normal_prio: 0x9c
task_struct.sched_task_cgroup: 0x420
task_struct.pi_lock: 0x9ec
task_struct.pi_waiters: 0xa00
task_struct.pi_top_task: 0xa10
task_struct.pi_blocked_on: 0xa18

workqueue_struct.dfl_pwq: 0xc0
pool_workqueue.nr_active: 0x60
pool_workqueue.max_active: 0x64
worker_pool.worklist: 0x28
worker_pool.nr_idle: 0x3c
```

## P0 signature and fingerprint validation

`src/targets/a155m-A155MUBUAEZF1/p0_fingerprint.h` contains 32
search rows. Each row matches an Image page (candidates 0x000000-0x1f0000).
The 256 qwords (8 per page × 32 rows) were generated by Linuxoid-cn. All
values at that offset in the target Image match the expected values without
exception.

The KASLR slide curves and trace events are encoded as 32 rows × 8 qwords in
the p0 table, and the search identifies the device's actual kernel offset.

## Build configuration

The release payload was built with Android NDK r21 (ARM64 target API 36) using Clang 19:

```
make TARGET=a155m-A155MUBUAEZF1 ANDROID_NDK_HOME=/path/to/android-ndk
```

The release outputs are:

| File | Size | SHA-256 |
| --- | ---: | --- |
| `cve-2026-43499` (LD_PRELOAD) | 97,064 | `4999C1A0A7538867D7766E09058BD39F1A527AA9F04F7EFF5FF69C33340080AC` |
| `cve-2026-43499-app.so` (app payload) | 125,680 | `627832D1CA57F7BA6076696FE3B6BE89CBECF291E7624494491C72721EB9209CF` |
| `cve-2026-43499-root` (root helper) | 22,752 | `A8A9592126C94472FCF278DF272A55077D90CE647DD6A52CED32BD1C8D4DEBBF` |

## KernelSU 6.12 compatibility

The A155M uses KMI `android15-6.6`, identical to the S25 Ultra (SM-S N938).
The `ksud-s25u-kdp` binary embeds the `android15-6.6`-compatible kernel module.
Since both devices share the same KMI, the existing `ksud-s25u-kdp` binary
from the S25U deployment is reused for the A155M.

During the device test, a separate kmod build from the A155M kernel source
may be required for exact kernel version matching. The standalone module
`android15-6.6_kernelsu-s25u-kdp.ko` is retained for auditability.

## P0 oracle

The Rust-based ashmem implementation (called `ashmem_rust`) changes the
attack surface:

1. **fops access**: The Rust modules register `miscdevice fops` via the Rust
   `module_init` routine. At load time, the ashmem miscdevice fops pointer points to
   a Rust-initialized vtable inside the kernel BSS at `0x0292006c`.
2. **ioctl dispatch**: Due to Rust fat-pointer ABI and vtables, the
   effective ashmem ioctl/compat_ioctl/mmaps/open/release/fdshow functions are
   thin wrappers that call into the Rust module, so the usual C-style
   `file_operations` probing will not find them. The Rust module vtable
   contains wrapper stubs that pass the call.
3. **page cache**: A VMFILE_FOPS structure is used internally by the module
   for page pinning.

All those conditions are handled by the distinct `target.h` offsets and the
KASLR search patterns.

## Device testing note

The firmware is for the ZTO/OWO region (Brazil). The A155M hardware was
available briefly but could not be unlocked (Exynos 850 bootloader
requires a proprietary unlock procedure). The device test has not been
conducted. However, static analysis, all kernel offsets, the p0 fingerprint,
and the sha256 of the raw Image all match. The compiled payloads were
binary-identical to a second independent build from the same source,
confirming that the toolchain was identical.