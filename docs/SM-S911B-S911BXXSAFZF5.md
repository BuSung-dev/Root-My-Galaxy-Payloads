# Galaxy S23 SM-S911B / S911BXXSAFZF5 port record

Offline port of the CVE-2026-43499 temporary-root payload to the Galaxy S23
(`SM-S911B`, codename `dm1q`) on firmware `S911BXXSAFZF5` (June 2026 patch).

Status: **hardware-verified end-to-end on 2026-11-15** — root from
`uid=2000`/`u:r:shell:s0`, KernelSU late-load, and a granted `su` returning
`uid=0` in `u:r:ksu:s0` with SELinux enforcing. Root and KernelSU are volatile
per boot.

## Exact target

| Field | Value |
| --- | --- |
| Model | `SM-S911B` Galaxy S23 |
| Device | `dm1q` |
| FUS version | `S911BXXSAFZF5/S911BOXMAFZF5/S911BXXSAFZE1/S911BXXSAFZF5` |
| Region used for download | `EUX` (OXM multi-CSC) |
| Display build | `BP4A.251205.006.S911BXXSAFZF5` |
| Fingerprint (device) | `samsung/dm1qxeea/dm1q:16/BP4A.251205.006/S911BXXSAFZF5:user/release-keys` |
| Kernel release | `5.15.189-android13-8-33413713-abS911BXXSAFZF5` |
| Kernel build tag | `33413713` (same family as S911B FZE1/FZG1, S916B FZG1, S918B FZF5) |
| Kernel compiler | Android clang 14.0.7 (`r450784e`) |
| KMI | `android13-5.15` |

## Firmware provenance

Downloaded from Samsung FUS with `samloader 2.2.0`:

```text
model: SM-S911B
region: EUX
version: S911BXXSAFZF5/S911BOXMAFZF5/S911BXXSAFZE1/S911BXXSAFZF5
file: SM-S911B_5_20260610111920_uv81f6d7xp_fac.zip.enc4
zip: 15,971,922,587 bytes (deleted after extraction)
AP: AP_S911BXXSAFZF5_S911BXXSAFZF5_MQB110705658_REV00_user_low_ship_MULTI_CERT_meta_OS16.tar.md5
BL: BL_S911BXXSAFZF5_S911BXXSAFZF5_MQB110705658_REV00_user_low_ship_MULTI_CERT.tar.md5
CP: CP_S911BXXSAFZE1_CP34354213_MQB109769200_REV00_user_low_ship_MULTI_CERT.tar.md5
```

Files inside the AP/BL archives carry a build timestamp of 2026-06-09.

## Kernel extraction and hashes

AP tar → `boot.img.lz4` (LZ4 frame) → boot image header v4, 4096-byte pages,
`kernel_size` u32 at 0x08, kernel blob at 0x1000.

```text
boot.img size: 100663296
boot.img SHA-256: 14178A685776511C6B14EF91F2458ECF2A420A2C866B556039C462155D2FF5BF
kernel size: 46860800
kernel SHA-256: D8050B91C61221BD7847B863735C3EA85D9BFE2036F67921709A845605CA21CE
ARM64 Image text_offset: 0x0
ARM64 Image size: 0x2f50000
ARM64 Image flags: 0xa
raw BTF: [0x21f02ac, 0x27c0188), 6,094,556 bytes
```

Kernel config recovered from the embedded IKCONFIG (`/proc/config.gz` format,
192,818 bytes) at image offset `0x17344a8`:

```text
CONFIG_CC_VERSION_TEXT="Android (8508608, based on r450784e) clang version 14.0.7 ..."
CONFIG_DEBUG_INFO_BTF=y
CONFIG_DEBUG_INFO_BTF_MODULES=y
CONFIG_MODVERSIONS=y
CONFIG_TRIM_UNUSED_KSYMS=y
CONFIG_CFI_CLANG=y
CONFIG_LTO_CLANG_FULL=y
CONFIG_SHADOW_CALL_STACK=y
CONFIG_MODULE_SIG=y
# CONFIG_MODULE_FORCE_LOAD is not set
```

## Symbol and layout recovery

`vmlinux-to-elf` recovered 126,219 symbols at image base
`0xffffffc008000000`. All layouts below were derived from this image's raw BTF
(`bpftool btf dump ... format raw`), not copied from a sibling.

| Macro | Symbol / derivation | Offset |
| --- | --- | ---: |
| `SLIDE_TRACEFS_EVENT_ID` | `20 + (__event_sched_blocked_reason - __start_ftrace_events)/8` = `20 + 88` | `108` |
| `SLIDE_TRACEFS_WORKER_CALLER_OFF` | `worker_thread` call to `schedule` at `0x10db40`, return PC | `0x0010db44` |
| `SLIDE_TRACEFS_VFORK_CALLER_OFF` | `wait_for_vfork_done` call to `wait_for_common` at `0xc8fe0`, return PC | `0x000c8fe4` |
| `KIMAGE_TEXT_BASE` | ARM64 image base | `0xffffffc008000000` |
| `P0_KERNEL_PHYS_LOAD` | Qualcomm ABL convention, `0x80000000 + text_offset` | `0x80080000` |
| `INIT_TASK_OFF` | `init_task` | `0x02c05080` |
| `PREPARE_KERNEL_CRED_OFF` | `prepare_kernel_cred` | `0x0011e3c8` |
| `COMMIT_CREDS_OFF` | `commit_creds` | `0x00120104` |
| `OVERRIDE_CREDS_OFF` | `override_creds` | `0x0011f1dc` |
| `ROOT_TASK_GROUP_OFF` | `root_task_group` | `0x02cb9ac0` |
| `SELINUX_ENFORCING_OFF` | `selinux_state` (`enforcing` is first member) | `0x02d8e5c0` |
| `KMALLOC_CACHES_OFF` | `kmalloc_caches` | `0x02064838` |
| `ANON_PIPE_BUF_OPS_OFF` | `anon_pipe_buf_ops` | `0x01e7f820` |
| `SYSTEM_UNBOUND_WQ_OFF` | `system_unbound_wq` | `0x02a90800` |
| `CALL_USERMODEHELPER_EXEC_WORK_OFF` | `call_usermodehelper_exec_work` | `0x001045d0` |
| `ASHMEM_FOPS_OFF` | `ashmem_fops` | `0x0200d878` |
| `ASHMEM_MISC_FOPS_OFF` | `ashmem_misc + offsetof(miscdevice, fops) = +0x10` | `0x02bfcf28` |
| `ASHMEM_IOCTL_OFF` | `ashmem_ioctl` | `0x0114c6dc` |
| `ASHMEM_COMPAT_IOCTL_OFF` | `compat_ashmem_ioctl` | `0x0114cd38` |
| `ASHMEM_MMAP_OFF` | `ashmem_mmap` | `0x0114cd90` |
| `ASHMEM_OPEN_OFF` | `ashmem_open` | `0x0114d070` |
| `ASHMEM_RELEASE_OFF` | `ashmem_release` | `0x0114d108` |
| `ASHMEM_SHOW_FDINFO_OFF` | `ashmem_show_fdinfo` | `0x0114d224` |
| `CONFIGFS_READ_ITER_OFF` | `configfs_read_iter` | `0x005d7420` |
| `CONFIGFS_BIN_WRITE_ITER_OFF` | `configfs_bin_write_iter` | `0x005d7e48` |
| `COPY_SPLICE_READ_OFF` | `generic_file_splice_read` | `0x00528198` |
| `NOOP_LLSEEK_OFF` | `noop_llseek` | `0x004bbd34` |
| `SLIDE_NFULNL_LOGGER_NAME_OFF` | first qword of `nfulnl_logger` → `"nfnetlink_log"` (validated) | `0x01d5e08a` |
| `SLIDE_NFULNL_LOGGER_OBJECT_OFF` | `nfulnl_logger` | `0x02a91e48` |
| `SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_OFF` | unique `.data` qword holding `&sysctl_bootid`; `random_table + 0x108` | `0x02bba9c8` |
| `SLIDE_SYSCTL_BOOTID_OFF` | `sysctl_bootid` | `0x02e6c0b1` |
| `MCAST_WAITER_OFF` | family constant; text callers match exactly | `0x78` |

Independent validation performed on the raw Image:

- `ashmem_fops+0x50/0x58/0x60/0x70/0x80/0xe0` resolve to the six `ashmem_*`
  offsets above; `ashmem_misc+0x10 == &ashmem_fops`.
- `nfulnl_logger[0]` points at a NUL-terminated `"nfnetlink_log"` string
  (odd offset allowed for a string).
- `random_table + 0x108` is the only qword in the Image equal to
  `&sysctl_bootid`.
- `anon_pipe_buf_ops` entries point into `.text` (`0x4ce6d0`, `0x4ce788`,
  `0x4ce818`, `0x4f6690`).

### BTF layouts

```text
sizeof(file_operations) = 0x120
  llseek 0x08  read 0x10  write 0x18  read_iter 0x20  write_iter 0x28
  unlocked_ioctl 0x50  compat_ioctl 0x58  mmap 0x60  open 0x70
  release 0x80  splice_read 0xc8  show_fdinfo 0xe0

task_struct: usage 0x38  prio 0x7c  normal_prio 0x84  sched_task_group 0x400
             pi_lock 0x884  pi_waiters 0x898  pi_top_task 0x8a8
             pi_blocked_on 0x8b0  real_cred 0x790  cred 0x798

sizeof(page) = 0x40  compound_head 0x08  slab_cache 0x18  page_type 0x30
sizeof(rt_mutex_waiter) = 0x58 (compact; task 0x30, lock 0x38, wake_state 0x40, prio 0x44)
worker_pool: worklist 0x20, nr_idle 0x34
miscdevice.fops 0x10
```

These match the hardware-verified `dm1q`/`dm3q` 5.15.189 profiles exactly.

## Cross-check against the hardware-verified family

`.text`-side values are identical to `dm3q-S918BXXSAFZF5` and
`dm1q-S911BXXSAFZG1` (event 108, worker `0x10db44`, vfork `0xc8fe4`).
`init_task`, `root_task_group`, `system_unbound_wq`, `selinux_state`,
`ashmem_misc + 0x10`, `nfulnl_logger`, the `boot_id` pointer slot and
`sysctl_bootid` all match the S911B FZG1 record exactly. The three cache/ops
data symbols moved by **+0x280** relative to FZG1 and the nfnetlink string by
**+0x2b4**; those are the only build-specific data deltas.

## P0 fingerprint

`src/targets/dm1q-S911BXXSAFZF5/p0_fingerprint.h` was generated from the
exact raw Image at probe `0x1f8000`, 64 rows in `0x8000` steps, eight qwords
per row at page offsets `0x000..0xe00`. Every source qword was read back from
the Image after generation.

```text
kernel SHA-256: D8050B91C61221BD7847B863735C3EA85D9BFE2036F67921709A845605CA21CE
table SHA-256: c797435a7c79e233f20fc8ef99dfa6a9b51616fc0e3ad66939e29c7cc7f84779
```

## Build

```sh
export ANDROID_NDK_HOME=/path/to/android-ndk-r29
make TARGET=dm1q-S911BXXSAFZF5
```

The app payload is built by the default target, not `make release` (the
fixed 104128-byte `release` truncation predates the MCAST route and is too
small for this profile).

## Artifacts

| File | Size | SHA-256 |
| --- | --- | --- |
| `cve-2026-43499-app.so` | 135544 | `2dab5d9f58f209b61f96752bd051c4f58a240e2ce0fc70a0c6b3bc155d1d9fb4` |
| `cve-2026-43499-root` | 27072 | `e7a4f96ce391fc28471711542800113859055a295e1da9229f0a32ed544acdf2` |
| `android13-5.15.189_kernelsu-dm1q-S911BXXSAFZF5.ko` | 356928 | `318a4ceb4bd5f32e8f59d95c2272aaf9cc5ea141869d13d1a700caedba65b18b` |
| `ksud-dm1q-S911BXXSAFZF5-kdp` | 4879560 | `07302e5a79cc6c57d23a06cbb996c2f31c50c29589c03cf40278cfe43ab92639` |

## KernelSU

The target kernel has `CONFIG_MODULE_FORCE_LOAD=n`, so the module's exact
`vermagic` matters. The standalone KO started from the hardware-verified
`dm1q-S911BXXSAFZG1` module (same `33413713` build family, Samsung
KDP/RKP/DEFEX patch, `NO_PATCH_TEXT`, empty `__versions`, kallsyms manual
relocation) and its equal-length vermagic was rewritten in place:

```text
5.15.189-android13-8-33413713-abS911BXXSAFZG1
-> 5.15.189-android13-8-33413713-abS911BXXSAFZF5
```

`readelf -p .modinfo` reports the exact FZF5 release. The `ksud` binary is
the S911B FZG1 late-load loader (byte-identical to the FZE1 loader, embedded
KMI asset `android13-5.15`), which was hardware-verified on S911B with this
kernel family. If the embedded module is rejected on this build, load the
standalone FZF5 KO first and then run `late-load` (the loader skips the
embedded KO when KernelSU is already active).

## Verified hardware run

Run on a real `SM-S911B` (`RFCW600GSJH`) at ~90 s uptime after a fresh reboot:

```text
slide-kaslr-ok source=tracefs slide=0x00000000000f0000 data_mode=canonical
pipe physrw done=1 root=1 kaslr=1 read_ok=1 write_ok=1 rw64=1/1 uid=2000->0
exploit completed attempt=1/1
su -c id -> uid=0(root) gid=0(root) context=u:r:ksu:s0   (SELinux Enforcing)
kernelsu 208896 1 - Live 0x0000000000000000 (OE)
```

KernelSU Manager v3.2.5 (`32525-2`) reports
`Working <LKM> [Jailbreak mode]` and the exact kernel release above.

Evidence: `work/evidence/success-run.log`, `work/evidence/kernelsu-manager.png`.

### Two fixes that were required during bring-up

1. **Late-load flag.** The shared helper in this tree invoked
   `ksud late-load --ephemeral`. Neither the published S911B loader nor the
   S918B loader knows that flag; on hardware it aborts before loading the
   module. The helper now passes `--package-name me.weishu.kernelsu
   --allow-shell`, which is the PR231 hardware-proven form and is what makes
   `su` work for the `shell` UID.
2. **KernelSnitch timing oracle.** The first two boots failed in
   `kernelsnitch_find_collisions` ("only found 0–3 collisions", needs 4).
   That is allocator/timing state, not a bad offset. A fresh reboot run
   passed on the first attempt. Keep expectations: retry with a fresh reboot.

`sizeof(struct mm_struct)` in this image is `0x3E0`; the slab geometry the
payload uses (`MM_STRUCT_SZ 0x400`, `MM_ORDER 3`) was kept because the
successful hardware run confirms the actual order-3 slab object stride.

## App integration

The payload is the exact build verified on hardware. The feed entry requires
two app changes that are already open upstream:

- [#637](https://github.com/BuSung-dev/Root-My-Galaxy/pull/637) - exact
  `uname -r` matching, so the entry (which lists the full release) wins over
  other `SM-S911B` profiles with the same three-part version.
- [#613](https://github.com/BuSung-dev/Root-My-Galaxy/pull/613) -
  `requiresFreshP0Session`: one payload-native attempt, no cached
  `SLIDE_P0_OFFSET`, and no short timeout overrides. The verified run used one
  attempt with the payload defaults (300 s attempt / 180 s P0) and the
  tracefs-canonical slide; the cached-offset path fails at the fops stage.

## Hardware validation status

- [x] Exact firmware identity and kernel hashes recorded.
- [x] Symbols, BTF layouts and P0 table derived from this Image.
- [x] Payload builds cleanly with NDK r29 (`-DSLIDE_STACK_WRITER=1`, MCAST).
- [x] Cross-checks against the hardware-verified `33413713` family pass.
- [x] Exploit executed on `S911BXXSAFZF5` hardware (first attempt of a fresh boot).
- [x] KernelSU late-load verified; `su` returns uid 0 in `u:r:ksu:s0`.
