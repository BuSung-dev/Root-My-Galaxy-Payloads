# SM-X710 X710XXS6EZF1

Galaxy Tab S9 (Wi-Fi) `SM-X710`, firmware `X710XXS6EZF1` (EUX), Android 16.
Kernel: `5.15.189-android13-8-33413632-abX710XXS6EZF1` (GKI KMI `android13-5.15`).
SoC: Snapdragon 8 Gen 2 (kalama). Qualcomm path applies; `P0_KERNEL_PHYS_LOAD = 0x80080000`
matches the three verified kalama profiles (dm1q/dm2q/dm3q0).

## Offline profile derivation

- `boot.img` extracted from the AP archive; kernel Image recovered (100659200 bytes,
  ARM64, `text_offset=0`, embedded BTF validated at `0x21ef2ac`).
- Symbols recovered with `vmlinux-to-elf` (base `0xffffffc008000000`) + `llvm-nm`.
- Kernel physical load: this firmware's `xbl_config.elf` contains two FDTs but no
  NOMAP/Kernel memory map (extract_target.py rejects it), so the kalama reference
  `0x80080000` is used; all three kalama profiles agree.
- Struct layouts derived from the embedded BTF: `task_struct` 0x1200 (cred 0x798),
  compact `rt_mutex_waiter` 0x58 (pi_tree_entry 0x18, lock 0x38), `struct page` 0x40
  (slab_cache 0x18), KABI `file_operations` 0x120, `worker_pool` (worklist 0x20,
  nr_idle 0x34), `mm_struct` 0x3e0.
- `SLIDE_TRACEFS_EVENT_ID 108` read live from the device
  (`/sys/kernel/tracing/events/sched/sched_blocked_reason/id`).
- `SLIDE_TRACEFS_WORKER_CALLER_OFF 0x0010db44` (worker_thread `bl schedule` + 4)
  and `SLIDE_TRACEFS_VFORK_CALLER_OFF 0x000c8fe4` (wait_for_vfork_done
  `bl wait_for_common` + 4) match the dm2q (S916BXXSAFZG1) values byte-for-byte;
  the .text of both kernels is otherwise symbol-identical except three .data
  objects shifted by 0x3c0 (kmalloc_caches, anon_pipe_buf_ops, ashmem_fops).
- `SLIDE_NFULNL_LOGGER_NAME_OFF 0x01d5da7e` verified by reading the
  `nfulnl_logger.name` pointer from the recovered ELF ("nfnetlink_log").
- `SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_OFF 0x02bba9c8` verified: the slot
  contains `&sysctl_bootid` (0x02e6c0b1), same value as dm2q.
- P0 fingerprint table generated from the raw kernel (32 slides, probe
  offset 0x1f0000).

## Device validation

- Exploit (MCAST stack writer) completed the full chain on hardware from
  `adb shell`: tracefs slide leak (slide 0x50000), P0 physical write, fake
  fops + configfs ARW, pipe physical read/write (`rw64=1/1`), UMH root.
  Final: `uid=0` `u:r:kernel:s0`, SELinux permissive. One success in three
  exploit runs; failure mode is kernelsnitch collision randomness, not offsets.
- KernelSU late-load: `ksud-gts9-X710XXS6EZF1-kdp` loaded
  `android13-5.15.189_kernelsu-gts9-X710XXS6EZF1.ko` through the guarded
  `--late-load` path (DEFEX Safeplace kills ksud executed from
  `/data/local/tmp`; the logcat bind-mount route avoids it). KernelSU Manager
  reports `Working <LKM> [Jailbreak mode]`. No reboot observed.
- Root and module are volatile per boot (no boot image modification; the
  bootloader remains locked).

## GhostLock note

GhostLock-Galaxy's pselect/futex route is geometrically infeasible on this
kernel: futex chain frames sum to 0x370 vs pselect chain 0x260, a 200-byte
depth delta against a 24-byte feasibility window
(`extract_target.py` reports `overlap is not a non-negative qword: -200`).
This profile therefore uses the RMG MCAST writer instead.
