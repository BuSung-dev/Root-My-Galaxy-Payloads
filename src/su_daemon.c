#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <limits.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define BOOTSTRAP_SOCK_PATH "/data/local/tmp/temp_su.sock"
#define HOLD_READY_SOCKET "cve43499_roothold"
#define SH_PATH "/system/bin/sh"
#define KSU_LOADER_PATH "/data/local/tmp/ksud-s25u-kdp"
#define LOGCAT_PATH "/system/bin/logcat"
#define BOOTSTRAP_MARKER_CONFIG "/data/local/tmp/cve43499-root-marker-path"

static uid_t allowed_client_uid = 2000;
static char bootstrap_marker_path[PATH_MAX];

#define SU_PROTOCOL_MAGIC 0x53553235U
#define SU_PROTOCOL_VERSION 1U
#define SU_RESPONSE_MAGIC 0x53555235U
#define SU_MAX_ARGC 256U
#define SU_MAX_ENVC 512U
#define SU_MAX_STRING 65536U
#define SU_MAX_REQUEST_BYTES (1024U * 1024U)
#define SU_PASSED_FDS 5U
#define HOLD_REF_FDS 3U

extern char **environ;

struct su_tty_state {
  uint8_t has_termios;
  uint8_t has_winsize;
  struct termios termios;
  struct winsize winsize;
};

struct su_request_header {
  uint32_t magic;
  uint32_t version;
  uint32_t argc;
  uint32_t envc;
  uint8_t interactive;
  uint8_t reserved[3];
  struct su_tty_state tty;
};

struct su_response {
  uint32_t magic;
  int32_t status;
};

struct su_request {
  struct su_request_header header;
  char **argv;
  char **envp;
  int stdin_fd;
  int stdout_fd;
  int stderr_fd;
  int cwd_fd;
  int io_fd;
};

static int saved_terminal_fd = -1;
static struct termios saved_terminal;

static void restore_terminal(void) {
  if (saved_terminal_fd >= 0) {
    tcsetattr(saved_terminal_fd, TCSANOW, &saved_terminal);
    saved_terminal_fd = -1;
  }
}

static void set_root_env(void) {
  char hostname[PROP_VALUE_MAX];

  setenv("PATH",
         "/product/bin:/apex/com.android.runtime/bin:/apex/com.android.art/bin:"
         "/apex/com.android.virt/bin:/system_ext/bin:/system/bin:/system/xbin:"
         "/odm/bin:/vendor/bin:/vendor/xbin",
         1);
  setenv("HOME", "/data/local/tmp", 1);
  setenv("USER", "root", 1);
  setenv("LOGNAME", "root", 1);
  if (__system_property_get("ro.product.device", hostname) > 0) {
    setenv("HOSTNAME", hostname, 1);
  }
}

static int write_full(int fd, const void *buf, size_t len) {
  const char *p = buf;

  while (len) {
    ssize_t n = write(fd, p, len);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n <= 0) {
      return 0;
    }
    p += n;
    len -= (size_t)n;
  }
  return 1;
}

static int read_full(int fd, void *buf, size_t len) {
  char *p = buf;

  while (len) {
    ssize_t n = read(fd, p, len);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n <= 0) {
      return 0;
    }
    p += n;
    len -= (size_t)n;
  }
  return 1;
}

static int connect_daemon(void) {
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    perror("su: socket");
    return -1;
  }

  struct sockaddr_un sun;
  memset(&sun, 0, sizeof(sun));
  sun.sun_family = AF_UNIX;
  snprintf(sun.sun_path, sizeof(sun.sun_path), "%s", BOOTSTRAP_SOCK_PATH);

  if (connect(fd, (struct sockaddr *)&sun, sizeof(sun)) != 0) {
    perror("su: connect daemon");
    close(fd);
    return -1;
  }
  return fd;
}

static uint32_t vector_count(char *const values[], uint32_t limit) {
  uint32_t count = 0;

  while (values[count]) {
    if (count == limit) {
      return UINT32_MAX;
    }
    count++;
  }
  return count;
}

static int send_fds(int socket_fd, const int fds[SU_PASSED_FDS]) {
  char marker = 'F';
  struct iovec iov = {
      .iov_base = &marker,
      .iov_len = sizeof(marker),
  };
  char control[CMSG_SPACE(sizeof(int) * SU_PASSED_FDS)];
  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  memset(control, 0, sizeof(control));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = control;
  msg.msg_controllen = sizeof(control);

  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(int) * SU_PASSED_FDS);
  memcpy(CMSG_DATA(cmsg), fds, sizeof(int) * SU_PASSED_FDS);

  return sendmsg(socket_fd, &msg, 0) == (ssize_t)sizeof(marker);
}

static int recv_fds(int socket_fd, int fds[SU_PASSED_FDS]) {
  char marker = 0;
  struct iovec iov = {
      .iov_base = &marker,
      .iov_len = sizeof(marker),
  };
  char control[CMSG_SPACE(sizeof(int) * SU_PASSED_FDS)];
  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  memset(control, 0, sizeof(control));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = control;
  msg.msg_controllen = sizeof(control);

  if (recvmsg(socket_fd, &msg, MSG_CMSG_CLOEXEC) != (ssize_t)sizeof(marker) ||
      marker != 'F') {
    return 0;
  }

  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  if (!cmsg || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS ||
      cmsg->cmsg_len != CMSG_LEN(sizeof(int) * SU_PASSED_FDS)) {
    return 0;
  }
  memcpy(fds, CMSG_DATA(cmsg), sizeof(int) * SU_PASSED_FDS);
  return 1;
}

static int send_vector(int fd, char *const values[], uint32_t count) {
  for (uint32_t i = 0; i < count; i++) {
    size_t len = strlen(values[i]);
    if (len > SU_MAX_STRING) {
      return 0;
    }
    uint32_t wire_len = (uint32_t)len;
    if (!write_full(fd, &wire_len, sizeof(wire_len)) ||
        !write_full(fd, values[i], wire_len)) {
      return 0;
    }
  }
  return 1;
}

static char **recv_vector(int fd, uint32_t count, size_t *total_bytes) {
  char **values = calloc((size_t)count + 1, sizeof(*values));
  if (!values) {
    return NULL;
  }

  for (uint32_t i = 0; i < count; i++) {
    uint32_t len;
    if (!read_full(fd, &len, sizeof(len)) || len > SU_MAX_STRING ||
        *total_bytes + len > SU_MAX_REQUEST_BYTES) {
      goto fail;
    }
    values[i] = calloc(1, (size_t)len + 1);
    if (!values[i] || !read_full(fd, values[i], len)) {
      goto fail;
    }
    *total_bytes += len;
  }
  return values;

fail:
  for (uint32_t i = 0; i < count; i++) {
    free(values[i]);
  }
  free(values);
  return NULL;
}

static void close_request_fds(struct su_request *request) {
  int *fds[] = {&request->stdin_fd, &request->stdout_fd, &request->stderr_fd,
                &request->cwd_fd, &request->io_fd};
  for (size_t i = 0; i < SU_PASSED_FDS; i++) {
    if (*fds[i] >= 0) {
      close(*fds[i]);
      *fds[i] = -1;
    }
  }
}

static void free_request(struct su_request *request) {
  if (request->argv) {
    for (uint32_t i = 0; i < request->header.argc; i++) {
      free(request->argv[i]);
    }
    free(request->argv);
  }
  if (request->envp) {
    for (uint32_t i = 0; i < request->header.envc; i++) {
      free(request->envp[i]);
    }
    free(request->envp);
  }
  close_request_fds(request);
}

static int recv_request(int conn, struct su_request *request) {
  int fds[SU_PASSED_FDS];
  size_t total_bytes = 0;
  memset(request, 0, sizeof(*request));
  request->stdin_fd = -1;
  request->stdout_fd = -1;
  request->stderr_fd = -1;
  request->cwd_fd = -1;
  request->io_fd = -1;

  if (!recv_fds(conn, fds)) {
    return 0;
  }
  request->stdin_fd = fds[0];
  request->stdout_fd = fds[1];
  request->stderr_fd = fds[2];
  request->cwd_fd = fds[3];
  request->io_fd = fds[4];

  if (!read_full(conn, &request->header, sizeof(request->header)) ||
      request->header.magic != SU_PROTOCOL_MAGIC ||
      request->header.version != SU_PROTOCOL_VERSION ||
      request->header.argc == 0 || request->header.argc > SU_MAX_ARGC ||
      request->header.envc > SU_MAX_ENVC) {
    return 0;
  }

  request->argv =
      recv_vector(conn, request->header.argc, &total_bytes);
  if (!request->argv) {
    return 0;
  }
  request->envp =
      recv_vector(conn, request->header.envc, &total_bytes);
  return request->envp != NULL;
}

static int wait_status(pid_t pid) {
  int status;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) {
      return 1;
    }
  }
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  if (WIFSIGNALED(status)) {
    return 128 + WTERMSIG(status);
  }
  return 1;
}

static int recv_hold_fds(int socket_fd, int fds[HOLD_REF_FDS]) {
  char marker = 0;
  struct iovec iov = {
      .iov_base = &marker,
      .iov_len = sizeof(marker),
  };
  char control[CMSG_SPACE(sizeof(int) * HOLD_REF_FDS)];
  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  memset(control, 0, sizeof(control));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = control;
  msg.msg_controllen = sizeof(control);

  if (recvmsg(socket_fd, &msg, MSG_CMSG_CLOEXEC) != (ssize_t)sizeof(marker) ||
      marker != 'P') {
    return 0;
  }
  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  if (!cmsg || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS ||
      cmsg->cmsg_len != CMSG_LEN(sizeof(int) * HOLD_REF_FDS)) {
    return 0;
  }
  memcpy(fds, CMSG_DATA(cmsg), sizeof(int) * HOLD_REF_FDS);
  return 1;
}

static void hold_kernel_references(int conn) {
  int fds[HOLD_REF_FDS] = {-1, -1, -1};
  if (!recv_hold_fds(conn, fds)) {
    return;
  }
  int ready_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (ready_fd < 0) {
    return;
  }
  struct sockaddr_un ready_address;
  memset(&ready_address, 0, sizeof(ready_address));
  ready_address.sun_family = AF_UNIX;
  memcpy(ready_address.sun_path + 1, HOLD_READY_SOCKET,
         sizeof(HOLD_READY_SOCKET) - 1);
  socklen_t ready_length = (socklen_t)(
      offsetof(struct sockaddr_un, sun_path) + sizeof(HOLD_READY_SOCKET));
  if (bind(ready_fd, (struct sockaddr *)&ready_address, ready_length) != 0 ||
      listen(ready_fd, 4) != 0) {
    close(ready_fd);
    return;
  }
  char acknowledged = 'K';
  if (!write_full(conn, &acknowledged, sizeof(acknowledged))) {
    return;
  }
  prctl(PR_SET_NAME, "cve43499-roothold", 0, 0, 0);
  close(conn);
  for (;;) {
    int probe_fd = accept4(ready_fd, NULL, NULL, SOCK_CLOEXEC);
    if (probe_fd >= 0) {
      close(probe_fd);
    }
  }
}

struct ksu_get_info_cmd {
  uint32_t version;
  uint32_t flags;
  uint32_t features;
  uint32_t uapi_version;
};

static int verify_kernelsu_control(void) {
  int fd = -1;
  syscall(SYS_reboot, 0xDEADBEEF, 0xCAFEBABE, 0, &fd);
  if (fd < 0) {
    dprintf(STDERR_FILENO, "late-load: KernelSU driver fd unavailable\n");
    return 13;
  }

  struct ksu_get_info_cmd info;
  memset(&info, 0, sizeof(info));
  int ret = ioctl(fd, _IOR('K', 2, struct ksu_get_info_cmd), &info);
  int saved_errno = errno;
  close(fd);
  if (ret != 0 || info.version == 0 || (info.flags & 1U) == 0 ||
      (info.flags & 4U) == 0) {
    dprintf(STDERR_FILENO,
            "late-load: KernelSU control check failed ret=%d errno=%d "
            "version=%u flags=0x%x\n",
            ret, saved_errno, info.version, info.flags);
    return 14;
  }

  dprintf(STDOUT_FILENO,
          "KernelSU control verified version=%u flags=0x%x "
          "uapi=%u features=0x%x\n",
          info.version, info.flags, info.uapi_version, info.features);
  return 0;
}

static int run_kernelsu_late_load(struct su_request *request, int conn) {
  pid_t pid = fork();
  if (pid < 0) {
    return 1;
  }
  if (pid == 0) {
    if (dup2(request->stdin_fd, STDIN_FILENO) < 0 ||
        dup2(request->stdout_fd, STDOUT_FILENO) < 0 ||
        dup2(request->stderr_fd, STDERR_FILENO) < 0 ||
        fchdir(request->cwd_fd) != 0) {
      _exit(126);
    }
    close(conn);
    close_request_fds(request);

    if (unshare(CLONE_NEWNS) != 0 ||
        mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0) {
      dprintf(STDERR_FILENO, "late-load: private mount namespace: %s\n",
              strerror(errno));
      _exit(10);
    }
    if (mount(KSU_LOADER_PATH, LOGCAT_PATH, NULL, MS_BIND, NULL) != 0) {
      dprintf(STDERR_FILENO, "late-load: bind mount: %s\n", strerror(errno));
      _exit(11);
    }

    pid_t loader = fork();
    if (loader < 0) {
      dprintf(STDERR_FILENO, "late-load: fork: %s\n", strerror(errno));
      _exit(12);
    }
    if (loader == 0) {
      /* Let the downloaded target-specific ksud select its embedded module
       * from the running kernel.  Ephemeral mode avoids replacing an existing
       * /data/adb/ksud while the app only needs the module for this boot. */
      execl(LOGCAT_PATH, "logcat", "late-load", "--ephemeral",
            "--package-name", "me.weishu.kernelsu", (char *)NULL);
      dprintf(STDERR_FILENO, "late-load: exec: %s\n", strerror(errno));
      _exit(12);
    }

    int loader_status = wait_status(loader);
    if (loader_status != 0) {
      _exit(loader_status);
    }
    _exit(verify_kernelsu_control());
  }
  close_request_fds(request);
  return wait_status(pid);
}

static void send_response(int conn, int status) {
  struct su_response response = {
      .magic = SU_RESPONSE_MAGIC,
      .status = status,
  };
  write_full(conn, &response, sizeof(response));
}

static int prepare_child(struct su_request *request) {
  environ = request->envp;
  if (fchdir(request->cwd_fd) != 0) {
    return 0;
  }
  set_root_env();
  request->argv[0] = "sh";
  return 1;
}

static void close_child_request_fds(struct su_request *request) {
  close_request_fds(request);
}

static int run_direct(struct su_request *request, int conn) {
  pid_t pid = fork();
  if (pid < 0) {
    return 1;
  }
  if (pid == 0) {
    if (dup2(request->stdin_fd, STDIN_FILENO) < 0 ||
        dup2(request->stdout_fd, STDOUT_FILENO) < 0 ||
        dup2(request->stderr_fd, STDERR_FILENO) < 0 ||
        !prepare_child(request)) {
      _exit(126);
    }
    close(conn);
    close_child_request_fds(request);
    execv(SH_PATH, request->argv);
    _exit(127);
  }
  close_request_fds(request);
  return wait_status(pid);
}

static int open_pty_master(char *slave, size_t slave_len) {
  int master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
  if (master < 0) {
    return -1;
  }
  if (grantpt(master) != 0 || unlockpt(master) != 0 ||
      ptsname_r(master, slave, slave_len) != 0) {
    close(master);
    return -1;
  }
  return master;
}

static int pump_client_io(int tty_fd, int io_fd) {
  char buf[4096];
  int tty_open = 1;

  while (1) {
    struct pollfd pfd[2];
    int nfd = 0;
    if (tty_open) {
      pfd[nfd].fd = tty_fd;
      pfd[nfd].events = POLLIN;
      nfd++;
    }
    pfd[nfd].fd = io_fd;
    pfd[nfd].events = POLLIN;
    int io_index = nfd++;

    int ret = poll(pfd, (nfds_t)nfd, -1);
    if (ret < 0 && errno == EINTR) {
      continue;
    }
    if (ret < 0) {
      return 1;
    }

    if (tty_open) {
      short events = pfd[0].revents;
      if (events & POLLIN) {
        ssize_t n = read(tty_fd, buf, sizeof(buf));
        if (n > 0) {
          if (!write_full(io_fd, buf, (size_t)n)) {
            return 0;
          }
        } else {
          tty_open = 0;
          shutdown(io_fd, SHUT_WR);
        }
      } else if (events & (POLLHUP | POLLERR | POLLNVAL)) {
        tty_open = 0;
        shutdown(io_fd, SHUT_WR);
      }
    }

    short io_events = pfd[io_index].revents;
    if (io_events & POLLIN) {
      ssize_t n = read(io_fd, buf, sizeof(buf));
      if (n > 0) {
        if (!write_full(STDOUT_FILENO, buf, (size_t)n)) {
          return 1;
        }
      } else {
        return 0;
      }
    }
    if (io_events & (POLLHUP | POLLERR | POLLNVAL)) {
      return 0;
    }
  }
}

static int pump_server_pty(int io_fd, int master_fd) {
  char buf[4096];

  while (1) {
    struct pollfd pfd[2] = {
        {.fd = io_fd, .events = POLLIN},
        {.fd = master_fd, .events = POLLIN},
    };
    int ret = poll(pfd, 2, -1);
    if (ret < 0 && errno == EINTR) {
      continue;
    }
    if (ret < 0) {
      return 1;
    }

    if (pfd[0].revents & POLLIN) {
      ssize_t n = read(io_fd, buf, sizeof(buf));
      if (n > 0) {
        if (!write_full(master_fd, buf, (size_t)n)) {
          return 1;
        }
      } else {
        return 1;
      }
    }
    if (pfd[0].revents & (POLLHUP | POLLERR | POLLNVAL)) {
      return 1;
    }

    if (pfd[1].revents & POLLIN) {
      ssize_t n = read(master_fd, buf, sizeof(buf));
      if (n > 0) {
        if (!write_full(io_fd, buf, (size_t)n)) {
          return 1;
        }
      } else {
        return 0;
      }
    }
    if (pfd[1].revents & (POLLHUP | POLLERR | POLLNVAL)) {
      return 0;
    }
  }
}

static int run_interactive(struct su_request *request, int conn) {
  char slave_name[128];
  int master = open_pty_master(slave_name, sizeof(slave_name));
  if (master < 0) {
    close_request_fds(request);
    return 1;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(master);
    close_request_fds(request);
    return 1;
  }
  if (pid == 0) {
    setsid();
    int slave = open(slave_name, O_RDWR | O_NOCTTY);
    if (slave < 0) {
      _exit(126);
    }
    if (request->header.tty.has_termios) {
      tcsetattr(slave, TCSANOW, &request->header.tty.termios);
    }
    if (request->header.tty.has_winsize) {
      ioctl(slave, TIOCSWINSZ, &request->header.tty.winsize);
    }
    ioctl(slave, TIOCSCTTY, 0);
    if (dup2(slave, STDIN_FILENO) < 0 || dup2(slave, STDOUT_FILENO) < 0 ||
        dup2(slave, STDERR_FILENO) < 0 || !prepare_child(request)) {
      _exit(126);
    }
    if (slave > STDERR_FILENO) {
      close(slave);
    }
    close(master);
    close(conn);
    close_child_request_fds(request);
    execv(SH_PATH, request->argv);
    _exit(127);
  }

  int io_fd = request->io_fd;
  request->io_fd = -1;
  close_request_fds(request);
  int client_gone = pump_server_pty(io_fd, master);
  if (client_gone) {
    kill(pid, SIGHUP);
  }
  int status = wait_status(pid);
  close(master);
  close(io_fd);
  return status;
}

static int client_send_request(int conn, int argc, char **argv,
                               int interactive, int io_server_fd) {
  uint32_t envc = vector_count(environ, SU_MAX_ENVC);
  if ((uint32_t)argc > SU_MAX_ARGC || envc == UINT32_MAX) {
    return 0;
  }

  int cwd_fd = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (cwd_fd < 0) {
    return 0;
  }
  int fds[SU_PASSED_FDS] = {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO,
                            cwd_fd, io_server_fd};

  struct su_request_header header;
  memset(&header, 0, sizeof(header));
  header.magic = SU_PROTOCOL_MAGIC;
  header.version = SU_PROTOCOL_VERSION;
  header.argc = (uint32_t)argc;
  header.envc = envc;
  header.interactive = interactive != 0;
  if (interactive) {
    header.tty.has_termios =
        tcgetattr(STDIN_FILENO, &header.tty.termios) == 0;
    header.tty.has_winsize =
        ioctl(STDIN_FILENO, TIOCGWINSZ, &header.tty.winsize) == 0;
  }

  int ok = send_fds(conn, fds) &&
           write_full(conn, &header, sizeof(header)) &&
           send_vector(conn, argv, header.argc) &&
           send_vector(conn, environ, header.envc);
  close(cwd_fd);
  return ok;
}

static int client_main(int argc, char **argv) {
  int conn = connect_daemon();
  if (conn < 0) {
    return 127;
  }

  char auth;
  if (!read_full(conn, &auth, sizeof(auth))) {
    close(conn);
    return 1;
  }
  if (auth != 'A') {
    dprintf(STDERR_FILENO, "su: permission denied\n");
    close(conn);
    return 1;
  }

  int interactive = isatty(STDIN_FILENO);
  int io_pair[2];
  if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, io_pair) != 0) {
    close(conn);
    return 1;
  }
  if (!client_send_request(conn, argc, argv, interactive, io_pair[1])) {
    close(io_pair[0]);
    close(io_pair[1]);
    close(conn);
    return 1;
  }
  close(io_pair[1]);

  if (interactive) {
    if (tcgetattr(STDIN_FILENO, &saved_terminal) == 0) {
      struct termios raw = saved_terminal;
      cfmakeraw(&raw);
      if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
        saved_terminal_fd = STDIN_FILENO;
        atexit(restore_terminal);
      }
    }
    pump_client_io(STDIN_FILENO, io_pair[0]);
    restore_terminal();
  }
  close(io_pair[0]);

  struct su_response response;
  if (!read_full(conn, &response, sizeof(response)) ||
      response.magic != SU_RESPONSE_MAGIC) {
    close(conn);
    return 1;
  }
  close(conn);
  return response.status;
}

static int get_peer_cred(int conn, struct ucred *peer) {
  socklen_t peer_len = sizeof(*peer);
  return getsockopt(conn, SOL_SOCKET, SO_PEERCRED, peer, &peer_len) == 0 &&
         peer_len == sizeof(*peer);
}

static void serve_one(int conn) {
  struct ucred peer;
  if (!get_peer_cred(conn, &peer) || peer.uid != allowed_client_uid) {
    char denied = 'D';
    write_full(conn, &denied, sizeof(denied));
    return;
  }
  char allowed = 'A';
  if (!write_full(conn, &allowed, sizeof(allowed))) {
    return;
  }

  char operation = 0;
  if (recv(conn, &operation, sizeof(operation), MSG_PEEK) ==
          (ssize_t)sizeof(operation) &&
      operation == 'H') {
    if (!read_full(conn, &operation, sizeof(operation))) {
      return;
    }
    hold_kernel_references(conn);
    return;
  }

  struct su_request request;
  if (!recv_request(conn, &request)) {
    free_request(&request);
    send_response(conn, 1);
    return;
  }

  int is_kernelsu_late_load = request.header.argc == 2 &&
                              strcmp(request.argv[1], "--late-load") == 0;
  int status = is_kernelsu_late_load
                   ? run_kernelsu_late_load(&request, conn)
                   : request.header.interactive
                         ? run_interactive(&request, conn)
                         : run_direct(&request, conn);
  send_response(conn, status);
  free_request(&request);
}

static int daemon_main(void) {
  signal(SIGPIPE, SIG_IGN);
  set_root_env();

  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    return 1;
  }

  struct sockaddr_un sun;
  memset(&sun, 0, sizeof(sun));
  sun.sun_family = AF_UNIX;
  unlink(BOOTSTRAP_SOCK_PATH);
  snprintf(sun.sun_path, sizeof(sun.sun_path), "%s", BOOTSTRAP_SOCK_PATH);

  if (bind(fd, (struct sockaddr *)&sun, sizeof(sun)) != 0 ||
      listen(fd, 16) != 0) {
    close(fd);
    return 1;
  }
  chmod(BOOTSTRAP_SOCK_PATH, 0666);

  if (bootstrap_marker_path[0]) {
    int marker_fd = open(bootstrap_marker_path,
                         O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (marker_fd >= 0) {
      static const char ready[] = "uid=0\n";
      write_full(marker_fd, ready, sizeof(ready) - 1);
      fsync(marker_fd);
      close(marker_fd);
    }
  }

  for (;;) {
    int conn = accept4(fd, NULL, NULL, SOCK_CLOEXEC);
    if (conn < 0 && errno == EINTR) {
      continue;
    }
    if (conn < 0) {
      sleep(1);
      continue;
    }

    pid_t pid = fork();
    if (pid == 0) {
      close(fd);
      serve_one(conn);
      close(conn);
      _exit(0);
    }
    close(conn);
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
  }
}

static void load_bootstrap_marker_config(void) {
  int fd = open(BOOTSTRAP_MARKER_CONFIG, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return;
  }
  ssize_t count = read(fd, bootstrap_marker_path,
                       sizeof(bootstrap_marker_path) - 1);
  close(fd);
  unlink(BOOTSTRAP_MARKER_CONFIG);
  if (count <= 0) {
    bootstrap_marker_path[0] = '\0';
    return;
  }
  bootstrap_marker_path[count] = '\0';
  bootstrap_marker_path[strcspn(bootstrap_marker_path, "\r\n")] = '\0';
  if (bootstrap_marker_path[0] != '/') {
    bootstrap_marker_path[0] = '\0';
  }
}

static int umh_main(int argc, char **argv) {
  if (geteuid() != 0) {
    return 126;
  }
  if (argc != 3 && argc != 4) {
    return 124;
  }
  char *end = NULL;
  errno = 0;
  unsigned long parsed_uid = strtoul(argv[2], &end, 10);
  if (errno || end == argv[2] || *end || parsed_uid == 0 ||
      parsed_uid > UINT32_MAX) {
    return 123;
  }
  allowed_client_uid = (uid_t)parsed_uid;
  if (argc == 4 && argv[3][0] == '/') {
    snprintf(bootstrap_marker_path, sizeof(bootstrap_marker_path), "%s",
             argv[3]);
  } else {
    load_bootstrap_marker_config();
  }
  if (setresgid(0, 0, 0) != 0 || setresuid(0, 0, 0) != 0 ||
      getuid() != 0 || geteuid() != 0 || getgid() != 0 || getegid() != 0) {
    return 125;
  }
  return daemon_main();
}

static void relay_payload_log_tail(const char *path, int transport_fd) {
  const off_t tail_size = 64 * 1024;
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    dprintf(transport_fd, "[runner-tail] open failed errno=%d\n", errno);
    return;
  }
  struct stat st;
  if (fstat(fd, &st) != 0) {
    dprintf(transport_fd, "[runner-tail] stat failed errno=%d\n", errno);
    close(fd);
    return;
  }
  off_t start = st.st_size > tail_size ? st.st_size - tail_size : 0;
  if (lseek(fd, start, SEEK_SET) < 0) {
    dprintf(transport_fd, "[runner-tail] seek failed errno=%d\n", errno);
    close(fd);
    return;
  }
  dprintf(transport_fd, "[runner-tail] path=%s bytes=%lld start=%lld\n",
          path, (long long)st.st_size, (long long)start);
  char buffer[4096];
  for (;;) {
    ssize_t got = read(fd, buffer, sizeof(buffer));
    if (got < 0 && errno == EINTR) {
      continue;
    }
    if (got <= 0 || !write_full(transport_fd, buffer, (size_t)got)) {
      break;
    }
  }
  close(fd);
}

static pid_t follow_payload_log(const char *path, int transport_fd,
                                pid_t payload_pid, int *status) {
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  int transport_ok = 1;
  if (fd < 0) {
    dprintf(transport_fd, "[runner-live] open failed errno=%d\n", errno);
    pid_t waited;
    do {
      waited = waitpid(payload_pid, status, 0);
    } while (waited < 0 && errno == EINTR);
    return waited;
  }

  if (dprintf(transport_fd, "[runner-live] following path=%s interval_ms=100\n",
              path) < 0) {
    transport_ok = 0;
  }

  char buffer[4096];
  for (;;) {
    for (;;) {
      ssize_t got = read(fd, buffer, sizeof(buffer));
      if (got < 0 && errno == EINTR) {
        continue;
      }
      if (got < 0) {
        if (transport_ok) {
          dprintf(transport_fd, "[runner-live] read failed errno=%d\n", errno);
        }
        close(fd);
        pid_t waited;
        do {
          waited = waitpid(payload_pid, status, 0);
        } while (waited < 0 && errno == EINTR);
        return waited;
      }
      if (got == 0) {
        break;
      }
      if (transport_ok &&
          !write_full(transport_fd, buffer, (size_t)got)) {
        transport_ok = 0;
      }
    }

    pid_t waited = waitpid(payload_pid, status, WNOHANG);
    if (waited == payload_pid) {
      /* The writer has exited. Drain bytes appended between the last EOF and
       * waitpid before returning the final status to the adb shell. */
      for (;;) {
        ssize_t got = read(fd, buffer, sizeof(buffer));
        if (got < 0 && errno == EINTR) {
          continue;
        }
        if (got <= 0) {
          break;
        }
        if (transport_ok &&
            !write_full(transport_fd, buffer, (size_t)got)) {
          transport_ok = 0;
        }
      }
      if (transport_ok) {
        dprintf(transport_fd, "[runner-live] child=%d complete status=0x%x\n",
                payload_pid, *status);
      }
      close(fd);
      return waited;
    }
    if (waited < 0) {
      close(fd);
      return waited;
    }
    usleep(100000);
  }
}

static int payload_runner_main(int argc, char **argv) {
  if (argc != 5) {
    return 2;
  }

  int transport_fd = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 3);
  if (transport_fd < 0) {
    return errno;
  }

  int log_fd = open(argv[4], O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  if (log_fd < 0 || dup2(log_fd, STDOUT_FILENO) < 0 ||
      dup2(log_fd, STDERR_FILENO) < 0) {
    int saved_errno = errno ? errno : EIO;
    dprintf(transport_fd, "[runner-tail] log setup failed errno=%d\n",
            saved_errno);
    close(transport_fd);
    return saved_errno;
  }
  if (log_fd > STDERR_FILENO) {
    close(log_fd);
  }
  if (setvbuf(stdout, NULL, _IONBF, 0) != 0 ||
      setvbuf(stderr, NULL, _IONBF, 0) != 0) {
    return errno ? errno : EIO;
  }

  /*
   * RDB can drop while the allocator search is still running.  Keep a
   * waitable foreground supervisor for normal adb behavior, but execute the
   * payload in a new session so loss of the shell cannot kill the only run on
   * this boot.  The child owns the persistent log and remains observable by
   * the same helper process when the transport stays alive.
   */
  signal(SIGHUP, SIG_IGN);
  pid_t payload_pid = fork();
  if (payload_pid < 0) {
    close(transport_fd);
    return errno;
  }
  if (payload_pid > 0) {
    int status = 0;
    pid_t waited = follow_payload_log(argv[4], transport_fd, payload_pid,
                                      &status);
    if (waited < 0) {
      int saved_errno = errno;
      relay_payload_log_tail(argv[4], transport_fd);
      close(transport_fd);
      return saved_errno;
    }
    close(transport_fd);
    if (WIFEXITED(status)) {
      return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
      return 128 + WTERMSIG(status);
    }
    return ECHILD;
  }

  close(transport_fd);
  signal(SIGHUP, SIG_IGN);
  if (prctl(PR_SET_PDEATHSIG, 0) != 0 || setsid() < 0) {
    return errno ? errno : EPERM;
  }
  prctl(PR_SET_NAME, "cve43499-run", 0, 0, 0);

  char root_helper_path[PATH_MAX];
  if (!realpath(argv[3], root_helper_path)) {
    dprintf(STDERR_FILENO,
            "[app] root helper realpath failed path=%s errno=%d\n", argv[3],
            errno);
    return errno ? errno : ENOENT;
  }
  if (setenv("CVE43499_ROOT_HELPER", root_helper_path, 1) != 0) {
    return errno;
  }
  dprintf(STDERR_FILENO, "[app] root helper=%s\n", root_helper_path);
  dprintf(STDERR_FILENO, "[app] loading verified payload=%s\n", argv[2]);
  void *handle = dlopen(argv[2], RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    dprintf(STDERR_FILENO, "[app] dlopen failed: %s\n", dlerror());
    return ENOEXEC;
  }
  dprintf(STDERR_FILENO, "[app] payload constructor returned\n");
  fflush(NULL);
  fsync(STDOUT_FILENO);
  return 0;
}

int main(int argc, char **argv) {
  signal(SIGPIPE, SIG_IGN);
  if (argc >= 2 && strcmp(argv[1], "--run-payload") == 0) {
    return payload_runner_main(argc, argv);
  }
  if (argc >= 2 && strcmp(argv[1], "--daemon") == 0) {
    return daemon_main();
  }
  if (argc >= 2 && strcmp(argv[1], "--umh") == 0) {
    return umh_main(argc, argv);
  }
  return client_main(argc, argv);
}
