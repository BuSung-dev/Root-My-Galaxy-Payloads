# dm3q-S918BXXSAFZG1

```text
device: Samsung Galaxy S23 Ultra (SM-S918B, dm3q)
firmware: S918BXXSAFZG1
fingerprint: samsung/dm3qxxx/qssi:16/BP4A.251205.006/S918BXXSAFZG1:user/release-keys
kernel: 5.15.189-android13-8-33413713-abS918BXXSAFZG1
bootloader: locked
```

This profile is a port of the `f731u-F731USQS8GZF1` reference target
(PR #205: Add SM-F731U profile) to the S23 Ultra `S918BXXSAFZG1` build. Both
devices run the same `5.15.189-android13-8` GKI branch on the Snapdragon 8
Gen 2 (kalama), 8 cores.

## Porting status

**Offline port complete; not yet device-validated.** All structural and
per-build symbol offsets in `target.h` have been re-derived from the
recovered S918B kernel (`vmlinux.nm` + `vmlinux.btf`) and verified — five
offsets that had been carried over from the f731u reference were corrected to
the S918B values (`SELINUX_ENFORCING_OFF`, `KMALLOC_CACHES_OFF`,
`ANON_PIPE_BUF_OPS_OFF`, `ASHMEM_FOPS_OFF`,
`SLIDE_NFULNL_LOGGER_NAME_OFF`). The `p0_fingerprint.h` was regenerated from
the S918B raw kernel Image (probe `0x1f0000`) and verified byte-identical.
The release payload builds at 104128 bytes. Remaining items require the
device (see "Required on-device verification" below).

## Kernel-source-verified criteria (S918B dm3q open-source release)

- `MM_STRUCT_SZ = 0x400` — `mm_struct` slab object; `kmem_cache_create_usercopy`
  slab-aligns `0x3e8` → `0x400`. BTF `sizeof` (0x3e0) does not include slab
  alignment. Confirmed from the f731u device test on the same kernel family;
  re-check with S918B vmlinux BTF.
- `KMALLOC_CGROUP_TYPE=1`, `KMALLOC_CACHE_TYPES=3` — verified from
  `include/linux/slab.h` (`enum kmalloc_cache_type`): `CONFIG_MEMCG_KMEM=y`
  with `CONFIG_SLUB` and no `CONFIG_ZONE_DMA` gives `KMALLOC_CGROUP=1`,
  `NR_KMALLOC_TYPES=3`. common.h defaults (2/4) are wrong for this kernel.
- `KERNELSNITCH_FUTEX_HASH_SIZE = 0x800` — verified from
  `kernel/futex/core.c` `futex_init()`:
  `roundup_pow2(256 * num_possible_cpus())`; SD8G2 = 8 CPUs → 0x800.
- `COMPACT_RT_MUTEX_WAITER = 1` — verified from `kernel/locking/rtmutex_common.h`:
  `struct rt_mutex_waiter` layout (rb_node x2, task, lock, wake_state, prio,
  deadline, ww_ctx) matches `FAKE_WAITER_*` offsets exactly.
- `MTE = 0` — kernel is MTE-capable but disabled at runtime via
  `arm64.nomte` boot cmdline (same as f731u 5.15.189 family).
- `KERNELSNITCH_VERBOSE = 0` — verbose printf in the futex collision loop
  breaks the ns-scale timing side-channel and causes reboots.
- GKI base confirmed: `CONFIG_SLUB` (default allocator),
  `CONFIG_SLAB_FREELIST_RANDOM` + `CONFIG_SLAB_FREELIST_HARDENED`,
  `CONFIG_KALLSYMS_ALL`, `CONFIG_RANDOMIZE_BASE` (KASLR),
  `CONFIG_LTO_CLANG_FULL` + `CONFIG_CFI_CLANG` + `CONFIG_SHADOW_CALL_STACK`
  (Samsung KDP/RKP/DEFEX hardening context).

## Required on-device / on-firmware verification (not derivable from source)

1. **p0_fingerprint.h** — DONE: generated from the S918B raw kernel Image
   (probe `0x1f0000`) and verified byte-identical via
   `perl tools/generate_p0_fingerprint.pl kernel 0x1f0000 /tmp/p0_check.h`.
2. **Symbol offsets** — DONE: re-derived `INIT_TASK_OFF`, `KMALLOC_CACHES_OFF`,
   `ANON_PIPE_BUF_OPS_OFF`, `ASHMEM_*`, `CONFIGFS_*`, `SLIDE_*`, and the
   `TASK_STRUCT_*` / `FAKE_TASK_*` struct offsets from the S918B kernel
   (`vmlinux-to-elf kernel vmlinux.elf` + `llvm-nm` + BTF, per docs/PORTING.md);
   five f731u-copied offsets corrected.
3. **`SLIDE_TRACEFS_EVENT_ID`** — DONE (offline): event index 88 + LAST_TYPE 20
   = 108 from recovered `vmlinux.btf`; still confirm on-device:
   `cat /sys/kernel/tracing/events/sched/sched_blocked_reason/id`.
4. **`SLIDE_TRACEFS_WORKER_CALLER_OFF`** — DONE (offline): `0x0010db44`, the
   instruction after `bl schedule` in the recovered `worker_thread`.
5. **`SLIDE_PSELECT_WORD_SHIFT`** — f731u reference: 3 (derive from target
   `do_pselect` stack layout on device if needed).
6. **Physical load** — confirm `P0_KERNEL_PHYS_LOAD` from `sboot.bin`
   (`Starting kernel...` path). f731u reference used `0x80000000`.

## Usage

```sh
# 1. generate the real p0_fingerprint.h from the S918B kernel Image
perl tools/generate_p0_fingerprint.pl kernel 0x1f0000 \
  src/targets/dm3q-S918BXXSAFZG1/p0_fingerprint.h

# 2. build the app release payload
make TARGET=dm3q-S918BXXSAFZG1 \
  ANDROID_NDK_HOME=/path/to/android-ndk release
```

The app/Shizuku branch must receive `P0_ATTEMPT_TIMEOUT_SEC` + `SLIDE_P0_OFFSET`
env for a reliable write primitive.
