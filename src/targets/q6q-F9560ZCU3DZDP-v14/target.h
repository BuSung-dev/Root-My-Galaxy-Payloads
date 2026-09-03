#include "../q6q-F9560ZCU3DZDP-v11/target.h"

/* v14: revert to the fixed static SLIDE_PSELECT_WORD_SHIFT=3 (drop the
 * v12 shift sweep and the v13 kstack-offset diag).
 *
 * Root cause of the v12/v13 crashes, established offline from the four
 * panic register dumps (kp10..kp13) and the exact q6q disassembly:
 *
 * 1. The runtime kstack geometry is STATIC. In all four panics the stale
 *    waiter (task->pi_blocked_on) sits at stack offset 0x3c8 from the
 *    16K stack top (0xffffffc0XXXXXXXXc38), and the consumer crash frame
 *    sits at 0x3d0, which is exactly the static depth
 *    0x150(pt_regs)+0x10+0x20+0x10+0x30+0x20(invoke)+0x80+0x90+0x50+0x80+0x10.
 *    CONFIG_RANDOMIZE_KSTACK_OFFSET is compiled in, but its invoke_syscall
 *    randomize/XOR-evolve blocks (invoke_syscall+0x74/+0x118) are jump-label
 *    blocks behind the kstack_offset_ready static key and are not patched at
 *    runtime (nops), so every syscall entry sp is deterministic.
 *
 * 2. With R_f = R_p = 0: waiter = E - 0x1e8 (futex 0x70 + do_futex 0x60 +
 *    futex_wait_requeue_pi 0x1b0 frame - rt_waiter at sp+0x98) and
 *    fd_set base = E - 0x200 (pselect6 0x90 + core_sys_select 0x1c0 frame,
 *    stack_fds at sp+0x50), i.e. S = 3 qwords ALWAYS. The observed kp11
 *    read 0x8200000000 is the forged wake_state+prio word
 *    (FAKE_WAITER_PRIO=130 << 32 | 0) at word 10 = shift 2 + 8, confirming
 *    S=3; kp12/kp13 (shift 6) read word 10 = pi_right = 0. Only shift 3
 *    aligns fake_lock with waiter->lock (word 10). The v8/v9/v10 gate hits
 *    all used shift 3.
 *
 * 3. The ~7% gate hit rate is the reclaim/allocator choreography (the
 *    sk_buff page landing in the oracle pipe), the same stochastic behavior
 *    documented for e3q; it is not a geometry variance. The kp10 anomaly
 *    (shift 3, next_lock=fake_lock read by rt_mutex_adjust_pi, but 0 at the
 *    chain's second read of the same word) remains an unexplained race;
 *    v14 re-tests the fixed shift-3 gate path on hardware.
 *
 * 4. The v13 configfs slot read returned 0 bytes because
 *    configfs_read_once() requires the FOPS-stage fops hijack
 *    (ashmem_fops -> configfs_read_iter), which the SLIDE stage has not
 *    installed; the primitive is therefore not exercisable before FOPS and
 *    the diagnostic was removed (the runtime evidence already proves the
 *    slot is never applied).
 */
#undef BUILD_VARIANT_LABEL
#define BUILD_VARIANT_LABEL \
  "q6q-F9560ZCU3DZDP-app-production-v14-fixed-shift3"
