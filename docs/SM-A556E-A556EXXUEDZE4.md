# Galaxy A55 5G `SM-A556E` / `A556EXXUEDZE4`

This is the port record for the Galaxy A55 profile
`a55x-A556EXXUEDZE4`. It is tied to the exact ZTO firmware identified from
the device screenshots and Samsung FUS metadata. The native payload, fixed
release size, and KernelSU compatibility checks pass. The full app flow was
validated on the device through Shizuku, including KernelSU late-load.

## Firmware identity

```text
model: SM-A556E
device: a55x
region: ZTO
AP/PDA: A556EXXUEDZE4
composite build: BP4A.251205.006.A556EXXUEDZE4
system fingerprint: samsung/a55xnsxx/essi:16/BP4A.251205.006/A556EXXUEDZE4:user/release-keys
boot/vendor fingerprint: samsung/a55xnsxx/a55x:14/UP1A.231005.007/A556EXXUEDZE4:user/release-keys
Android / SDK: 16 / 36
security patch: 2026-05-05
kernel: 6.1.157-android14-11
kernel build: #1 SMP PREEMPT Tue May 12 07:25:39 UTC 2026
page size: 4096
board: s5e8845
```

The OTA tooling file reports the same system fingerprint with `user/test-keys`;
the shipping system property above is the value used by this profile.

## Firmware provenance

```text
factory ZIP: SM-A556E_ZTO_A556EXXUEDZE4.zip
ZIP size: 10639372068
ZIP SHA-256: 1e717a19ea7bf97776d41f67eeeb7dc8ba6a24fd9d4d8375a18dc1eb88751481

AP archive size: 11682498683
AP SHA-256: 0b0e5cb7ea9546047a5acfde318b604b366cf459533adb4d49e944bf542a8468
BL archive size: 8396913
BL SHA-256: 8211b7aa092242a6f69c68f3f45cc096c31963a6533e158b88f08e295213a01e
fota.zip size: 1110678668
fota.zip SHA-256: 735c461bdffb7d897b36773401a53145a54aeae62365fd882a5bee294b37c6c6
```

The downloaded ZIP passed `unzip -tq`. Only AP/BL metadata, boot images,
`sboot.bin`, and `fota.zip` were extracted for analysis.

## Image recovery

```text
boot.img: 67108864 bytes
boot.img SHA-256: 6ecaffcbc9bea05b7fb00be9521df503d988da74e3fa32eb0eab898ff8b9d9d2
vendor_boot.img: 67108864 bytes
vendor_boot.img SHA-256: 7b2385e52acf9db7817d1b540f149f1ba1aeaafcb8d7024da00bb2ca1e2b020c
sboot.bin: 5260080 bytes
sboot.bin SHA-256: c7f7d8f6beb19ba63bfbb92ba103dd91079699ac77eb84d6d9ae76d67b52ecf1
raw Image kernel: 38697472 bytes
raw Image SHA-256: 0494f519c7b1abcae169d2ad598fa5c0b00c36c9d3560d7d9e15f29677593f91
ARM64 text_offset: 0x0
ARM64 image_size: 0x27a0000
ARM64 flags: 0xa
```

The `BOOT/kernel` member in `fota.zip` is byte-identical to the raw Image
kernel bytes. `vmlinux-to-elf` recovered an AArch64 ELF at
`0xffffffc008000000` with 115227 kallsyms symbols. The recovered kernel
reports Android clang 17.0.2 and `6.1.157-android14-11`.

Raw BTF was uniquely validated at `[0x1876a88, 0x1e2a8c4)` (5,979,708 bytes),
then dumped to both raw and C forms. Relevant BTF sizes/fields are:

```text
file_operations: 0x110; ioctl 0x50; compat_ioctl 0x58; mmap 0x60;
  open 0x70; release 0x80; splice_read 0xc8; show_fdinfo 0xe0
page: 0x40; compound_head 0x08; slab_cache 0x18; page_type 0x30
work_struct: 0x30; data 0x00; entry 0x08; func 0x18
task_struct: 0x12c0; usage 0x40; prio 0x84; normal_prio 0x8c;
  sched_task_group 0x348; pi_lock 0x924; pi_waiters 0x938;
  pi_top_task 0x948; pi_blocked_on 0x950
mm_struct: 0x3c0; selected SLUB stride: 0x400; order: 3
rt_mutex_waiter: 0x58; pi_tree_entry 0x18; task 0x30; lock 0x38;
  wake_state 0x40; prio 0x44; deadline 0x48; ww_ctx 0x50
configfs_buffer: page 0x10; needs_read_fill 0x50; bin_buffer 0x58;
  bin_buffer_size 0x60; cb_max_size 0x64
workqueue_struct: dfl_pwq 0xb0; pool_workqueue: pool 0x00, wq 0x08;
  work_color 0x10; refcnt 0x18; nr_in_flight 0x1c; nr_active 0x5c;
  max_active 0x60
worker_pool: worklist 0x28; nr_idle 0x3c
miscdevice.fops: 0x10; skb_shared_info: 0x158
```

The embedded IKCONFIG confirms `CONFIG_ARM64_MTE=y`,
`CONFIG_KASAN_HW_TAGS=y`, `CONFIG_MODVERSIONS=y`, `CONFIG_DEBUG_INFO_BTF=y`,
`CONFIG_KDP=y`, `CONFIG_RKP=y`, `CONFIG_SLUB=y`, and 4 KiB pages.

## Derived target constants

The following values are taken from the recovered symbol table, BTF, or raw
image pointer checks; they are not copied from a model-name template.

```text
call_usermodehelper_exec_work      0x000d4360
worker_thread -> schedule LR       0x000dbc94
noop_llseek                        0x0039ec9c
generic_file_splice_read           0x003ec8a0
configfs_read_iter                 0x0046cce8
configfs_bin_write_iter            0x0046d218
ashmem_ioctl                       0x00d2d4f8
compat_ashmem_ioctl                0x00d2de30
ashmem_mmap                        0x00d2de88
ashmem_open                        0x00d2e0a8
ashmem_release                     0x00d2e130
ashmem_show_fdinfo                 0x00d2e250
anon_pipe_buf_ops                  0x0120c610
ashmem_fops                        0x013c9b50
kmalloc_caches                     0x01792ed8
system_unbound_wq                  0x022cae60
nfulnl_logger                      0x022d29e0
init_task                          0x022df700
random_table boot-ID pointer       0x0241e878
ashmem_miscs.fops                  0x02464430
root_task_group                    0x024f4d40
selinux_state.enforcing            0x025c92f8
sysctl_bootid                      0x026b0518
nfnetlink_log string               0x016ca0d5
```

The `nfulnl_logger.name` pointer resolves to the unique `nfnetlink_log` string;
the selected `random_table` pointer resolves to `sysctl_bootid`; and
`ashmem_miscs + 0x10` points to `ashmem_fops`.

The target kernel computes the futex hash table size as
`roundup_pow_of_two(256 * num_possible_cpus())`.
Live topology is `possible=0-7`, so the effective size is `0x800`.

`__event_sched_blocked_reason - __start_ftrace_events = 0x2b0`, so event
index 86 plus `__TRACE_LAST_TYPE == 20` gives event ID 106. The A55
`worker_thread` disassembly places `bl schedule` at `0x...dbc90`; its return
instruction is `0x...dbc94`, which is the trace caller constant.

The `unix_stream_sendmsg` path uses a 0xe80 linear head and an order-3,
page-aligned 0x8000 data fragment for the 0x8e80 maximum chunk. The resulting
payload bias is `SKB_DATA_DELTA=-0x1000`.

The `sboot.bin` call path around file offset `0x30730` loads the Image entry
value, adds `-0x80000000`, and branches to it. With Image `text_offset=0`,
the physical load constants are `P0_PHYS_OFFSET=0x80000000` and
`P0_KERNEL_PHYS_LOAD=0x80000000`. The AArch64 VA geometry gives the shared
`0xffffff8000000000` direct map and `0xfffffffe00000000` vmemmap base.

`SLIDE_PSELECT_WORD_SHIFT=3` follows the compact 6.1 stack layout used by the
same `do_select` sequence; all ten waiter words therefore land in the three
fd-set arrays. The P0 probe offset is `0x1f0000`, and
`tools/generate_p0_fingerprint.pl` verified 32 slide rows and 256 source
qwords. This profile intentionally does not define the legacy inverse-slide
mode.

## Build and audit results

```sh
make TARGET=a55x-A556EXXUEDZE4 \
  ANDROID_NDK_HOME=/path/to/android-ndk \
  clean all release
```

The release payload is an AArch64 shared object, exactly 104128 bytes,
SHA-256 `dc44017e386f5b4296b868e82c58684b1bf0060a66a8b8f209859be658d70720`.
Two independent NDK r29 builds were byte-identical. The retained KernelSU
pair is the no-patch-text android14-6.1 build already audited against this
recovered ELF:

```text
android14-6.1_kernelsu-A556EXXUEDZE4-kdp.ko: 398368 bytes
SHA-256: a6c521a2f660f595f4ea359c243e27b85142cbcd832c84340dac0994f8d12135
ksud-A556EXXUEDZE4-kdp: 4780056 bytes
SHA-256: dc3eb02640492a8d6f78f8515c6ae5c75ddbfa593f53cd0f3efdfc82a29c4219
```

`extract_target_symvers.py` recovered 15450 exact target export CRCs. The
manual-relocation module audit reports 202 undefined imports, zero missing
target symbols, an empty `__versions` section, 47 kallsyms-resolved names,
zero CRC mismatches, and no `stop_machine` import.

## Validation state

The support feed entry is present for `SM-A556E` + kernel `6.1.157` and marks
the profile as requiring one fresh same-process P0 session. The Android app
now reads that flag, avoids the per-boot slide cache for this profile, and
does not impose the short legacy stall timeout (while retaining a bounded
overall run deadline).

On 2026-09-07, live ADB properties matched the profile exactly: model
`SM-A556E`, build `BP4A.251205.006.A556EXXUEDZE4`, kernel
`6.1.157-android14-11`, 4 KiB pages, Android 16, patch `2026-05-05`, and
enforcing SELinux. A shell-domain tracefs probe independently reported slide
`0x120000`.

The app's Shizuku path completed the full flow: physical read/write succeeded,
the shell UID changed from 2000 to 0, and the exact `ksud` late-loaded the
KernelSU module. Root My Galaxy then reported KernelSU active. KernelSU Manager
reported `Working <LKM> [Jailbreak mode]`, version `32525-2`, on kernel
`6.1.157-android14-11`.

![Root My Galaxy reporting KernelSU active](SM-A556E-A556EXXUEDZE4-Root-My-Galaxy.jpg)

![KernelSU Manager reporting a working LKM](SM-A556E-A556EXXUEDZE4-KernelSU.jpg)

The result is temporary per boot because no boot image is modified. A failed
attempt can panic and reboot the kernel; after Android finishes booting, retry
from a fresh app run.
