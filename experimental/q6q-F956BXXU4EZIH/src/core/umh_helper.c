#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * The external helper combines two separate concerns: a root UMH
 * daemon and a local Unix-socket client.  Its --late-load client also checks a
 * ticket and a verified payload, which is intentionally absent here.  Keep
 * only the useful local boundary: a root daemon, a peer-UID-checked socket,
 * and a direct root-side lkmloader invocation.
 */

static const char *const ksu_log_path =
    "/data/local/tmp/.ghostlock_ksu.log";
static const char *const local_socket_path =
    "/data/local/tmp/temp_su.sock";

static const char *const direct_work_dir = "/data/local/tmp";
static const char *const direct_insmod_path = "/system/bin/insmod";
static const char *const direct_loader_path = "/data/local/tmp/lkmloader.ko";
static const char *const direct_module_path = "/data/local/tmp/kernelsu.ko";

#define LOCAL_PROTOCOL_MAGIC 0x474c4b31U /* "GLK1" */
#define LOCAL_PROTOCOL_VERSION 1U
#define LOCAL_OP_LATE_LOAD 1U

struct local_request {
  uint32_t magic;
  uint32_t version;
  uint32_t operation;
};

struct local_response {
  uint32_t magic;
  uint32_t version;
  int32_t status;
};

static uid_t allowed_client_uid = 2000;

static void helper_log_begin(int argc, char **argv) {
  int fd = open(ksu_log_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                0644);
  if (fd < 0) return;
  const char *arg1 = argc > 1 && argv && argv[1] ? argv[1] : "<null>";
  const char *arg2 = argc > 2 && argv && argv[2] ? argv[2] : "<null>";
  dprintf(fd,
          "[*] helper entry pid=%d uid=%u euid=%u argc=%d arg1=%s arg2=%s\n",
          (int)getpid(), (unsigned)getuid(), (unsigned)geteuid(), argc, arg1,
          arg2);
  close(fd);
}

static void helper_log(const char *fmt, ...) {
  int fd = open(ksu_log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC,
                0644);
  if (fd < 0) return;

  char line[512];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  if (n > 0) {
    size_t len = (size_t)n;
    if (len >= sizeof(line)) len = sizeof(line) - 1;
    (void)write(fd, line, len);
  }
  close(fd);
}

static const char *choose_ksud(void) {
  const char *env = getenv("GHOSTLOCK_KSUD");
  if (env && env[0] && access(env, X_OK) == 0) return env;
  if (access("/data/local/tmp/ksud-selected", X_OK) == 0)
    return "/data/local/tmp/ksud-selected";
  if (access("/data/local/tmp/ksud", X_OK) == 0)
    return "/data/local/tmp/ksud";
  if (access("/data/adb/ksu/bin/ksud", X_OK) == 0)
    return "/data/adb/ksu/bin/ksud";
  return NULL;
}

static int stage_ksud_for_custom_loader(const char *ksud) {
  static const char *const stage_path = "/data/local/tmp/.ksud-stage";
  static const char *const temp_path = "/data/local/tmp/.ksud-stage.tmp";
  int src = open(ksud, O_RDONLY | O_CLOEXEC);
  if (src < 0) {
    helper_log("[!] helper ksud stage open src=%s errno=%d\n", ksud, errno);
    return 0;
  }

  int dst = open(temp_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0755);
  if (dst < 0) {
    helper_log("[!] helper ksud stage open tmp=%s errno=%d\n", temp_path,
               errno);
    close(src);
    return 0;
  }

  char buf[16384];
  uint64_t total = 0;
  int ok = 1;
  for (;;) {
    ssize_t n = read(src, buf, sizeof(buf));
    if (n == 0) break;
    if (n < 0) {
      if (errno == EINTR) continue;
      helper_log("[!] helper ksud stage read src=%s errno=%d\n", ksud,
                 errno);
      ok = 0;
      break;
    }
    ssize_t written = 0;
    while (written < n) {
      ssize_t m = write(dst, buf + written, (size_t)(n - written));
      if (m < 0) {
        if (errno == EINTR) continue;
        helper_log("[!] helper ksud stage write tmp=%s errno=%d\n",
                   temp_path, errno);
        ok = 0;
        break;
      }
      written += m;
      total += (uint64_t)m;
    }
    if (!ok) break;
  }
  if (ok && fchmod(dst, 0755) < 0) {
    helper_log("[!] helper ksud stage chmod tmp=%s errno=%d\n", temp_path,
               errno);
    ok = 0;
  }
  if (ok && fsync(dst) < 0) {
    helper_log("[!] helper ksud stage fsync tmp=%s errno=%d\n", temp_path,
               errno);
    ok = 0;
  }
  close(dst);
  close(src);
  if (!ok || total == 0) {
    unlink(temp_path);
    if (total == 0) {
      helper_log("[!] helper ksud stage empty src=%s\n", ksud);
    }
    return 0;
  }
  if (rename(temp_path, stage_path) < 0) {
    helper_log("[!] helper ksud stage rename tmp=%s dst=%s errno=%d\n",
               temp_path, stage_path, errno);
    unlink(temp_path);
    return 0;
  }
  helper_log("[*] helper ksud stage refreshed src=%s dst=%s bytes=%llu\n",
             ksud, stage_path, (unsigned long long)total);
  return 1;
}

static int kernelsu_loaded(void) {
  int fd = open("/proc/modules", O_RDONLY | O_CLOEXEC);
  if (fd < 0) return 0;
  char buf[8192];
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0) return 0;
  buf[n] = 0;
  return strstr(buf, "kernelsu") != NULL || strstr(buf, "KernelSU") != NULL;
}

static void close_inherited_fds(void) {
  for (int fd = 3; fd < 1024; fd++) close(fd);
}

static void drain_child_output(int fd) {
  if (fd < 0) return;
  char buf[512];
  for (;;) {
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) return;
    buf[n] = 0;
    for (ssize_t i = 0; i < n; i++) {
      if (buf[i] == 0) buf[i] = '?';
    }
    helper_log("[*] helper direct insmod stdio: %s\n", buf);
  }
}

static void log_proc_context(const char *where) {
  static const char *const fields[] = {
      "Name:",          "Uid:",       "Gid:",
      "Groups:",        "CapInh:",    "CapPrm:",
      "CapEff:",        "CapBnd:",    "CapAmb:",
      "NoNewPrivs:",    "Seccomp:",   "Seccomp_filters:",
      NULL,
  };
  FILE *status = fopen("/proc/self/status", "r");
  if (status) {
    char line[256];
    while (fgets(line, sizeof(line), status)) {
      for (size_t i = 0; fields[i]; i++) {
        size_t len = strlen(fields[i]);
        if (strncmp(line, fields[i], len) == 0) {
          line[strcspn(line, "\r\n")] = 0;
          helper_log("[*] helper %s %s\n", where, line);
          break;
        }
      }
    }
    fclose(status);
  } else {
    helper_log("[!] helper %s open /proc/self/status errno=%d\n", where,
               errno);
  }

  int fd = open("/proc/self/attr/current", O_RDONLY | O_CLOEXEC);
  if (fd >= 0) {
    char context[128];
    ssize_t n = read(fd, context, sizeof(context) - 1);
    close(fd);
    if (n > 0) {
      context[n] = 0;
      context[strcspn(context, "\r\n")] = 0;
      helper_log("[*] helper %s selinux=%s\n", where, context);
    }
  } else {
    helper_log("[!] helper %s open selinux context errno=%d\n", where, errno);
  }

  struct rlimit nproc;
  if (getrlimit(RLIMIT_NPROC, &nproc) == 0) {
    helper_log("[*] helper %s rlimit_nproc=%llu/%llu\n", where,
               (unsigned long long)nproc.rlim_cur,
               (unsigned long long)nproc.rlim_max);
  } else {
    helper_log("[!] helper %s getrlimit nproc errno=%d\n", where, errno);
  }
  struct rlimit nofile;
  if (getrlimit(RLIMIT_NOFILE, &nofile) == 0) {
    helper_log("[*] helper %s rlimit_nofile=%llu/%llu\n", where,
               (unsigned long long)nofile.rlim_cur,
               (unsigned long long)nofile.rlim_max);
  } else {
    helper_log("[!] helper %s getrlimit nofile errno=%d\n", where, errno);
  }
  helper_log("[*] helper %s pid=%d ppid=%d pgid=%d sid=%d\n", where,
             (int)getpid(), (int)getppid(), (int)getpgrp(), (int)getsid(0));
}

/*
 * ksud's late-load entry begins with a small daemonizer: fork, have the
 * intermediate child detach/reset stdio, fork once more, and report the
 * intermediate child's status to the original process.  This probe performs
 * only those generic process operations; it never invokes ksud or touches
 * KernelSU state.  It lets the log distinguish a fork/stdio/SELinux failure
 * from an error inside ksud itself.
 */
static void probe_ksud_daemonize(void) {
  helper_log("[*] helper daemonize probe begin\n");
  log_proc_context("probe-parent");

  pid_t child = fork();
  if (child < 0) {
    helper_log("[!] helper probe first fork failed errno=%d\n", errno);
    return;
  }
  if (child == 0) {
    (void)prctl(PR_SET_PDEATHSIG, 0);
    errno = 0;
    int pg_rc = setpgid(0, 0);
    int pg_errno = pg_rc < 0 ? errno : 0;
    helper_log("[*] helper probe child setpgid rc=%d errno=%d pgid=%d sid=%d\n",
               pg_rc, pg_errno, (int)getpgrp(), (int)getsid(0));

    int nullfd = open("/dev/null", O_RDWR | O_CLOEXEC);
    if (nullfd < 0) {
      helper_log("[!] helper probe open /dev/null failed errno=%d\n", errno);
      _exit(21);
    }
    int dup0 = dup2(nullfd, STDIN_FILENO);
    int dup1 = dup2(nullfd, STDOUT_FILENO);
    int dup2_rc = dup2(nullfd, STDERR_FILENO);
    int dup_errno = (dup0 < 0 || dup1 < 0 || dup2_rc < 0) ? errno : 0;
    helper_log("[*] helper probe reset-std nullfd=%d dup=%d/%d/%d errno=%d\n",
               nullfd, dup0, dup1, dup2_rc, dup_errno);
    if (nullfd > STDERR_FILENO) close(nullfd);
    if (dup0 < 0 || dup1 < 0 || dup2_rc < 0) _exit(22);
    log_proc_context("probe-after-std");

    errno = 0;
    pid_t grandchild = fork();
    if (grandchild < 0) {
      helper_log("[!] helper probe second fork failed errno=%d\n", errno);
      _exit(23);
    }
    if (grandchild > 0) {
      helper_log("[*] helper probe intermediate exiting grandchild=%d\n",
                 (int)grandchild);
      _exit(0);
    }
    helper_log("[*] helper probe grandchild reached post-fork\n");
    _exit(0);
  }

  int status = 0;
  pid_t waited;
  do {
    waited = waitpid(child, &status, 0);
  } while (waited < 0 && errno == EINTR);
  if (waited < 0) {
    helper_log("[!] helper probe waitpid failed errno=%d\n", errno);
    return;
  }
  helper_log("[*] helper daemonize probe raw_status=0x%x exited=%d code=%d signaled=%d signal=%d\n",
             status, WIFEXITED(status), WIFEXITED(status) ? WEXITSTATUS(status) : -1,
             WIFSIGNALED(status), WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}

static void normalize_ksud_signals(void) {
  struct sigaction old_action;
  memset(&old_action, 0, sizeof(old_action));
  int old_ok = sigaction(SIGCHLD, NULL, &old_action) == 0;
  helper_log("[*] helper SIGCHLD before ok=%d ignored=%d flags=0x%lx\n",
             old_ok, old_ok && old_action.sa_handler == SIG_IGN,
             old_ok ? (unsigned long)old_action.sa_flags : 0UL);

  struct sigaction default_action;
  memset(&default_action, 0, sizeof(default_action));
  sigemptyset(&default_action.sa_mask);
  default_action.sa_handler = SIG_DFL;
  int rc = sigaction(SIGCHLD, &default_action, NULL);
  helper_log("[*] helper SIGCHLD reset rc=%d errno=%d\n", rc, rc < 0 ? errno : 0);
}

static int ensure_directory(const char *path, mode_t mode) {
  if (mkdir(path, mode) == 0 || errno == EEXIST) {
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return 1;
    helper_log("[!] helper path is not a directory path=%s errno=%d\n", path,
               errno);
    return 0;
  }
  helper_log("[!] helper mkdir failed path=%s errno=%d\n", path, errno);
  return 0;
}

static int prepare_ksud_install_dirs(void) {
  /* The patched ksud moves .ksud-stage to /data/adb/ksud.  A device without
   * an already-installed Manager may not have the parent tree yet. */
  static const char *const dirs[] = {
      "/data/adb",
      NULL,
  };
  for (size_t i = 0; dirs[i]; i++) {
    if (!ensure_directory(dirs[i], 0755)) return 0;
  }
  helper_log("[*] helper ksud install directories ready\n");
  return 1;
}

static int make_private_ksud_mount(const char *ksud) {
  if (unshare(CLONE_NEWNS) < 0) {
    helper_log("[!] helper unshare mount namespace failed errno=%d\n", errno);
    return 0;
  }
  if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0) {
    helper_log("[!] helper private mount propagation failed errno=%d\n",
               errno);
    return 0;
  }
  if (!prepare_ksud_install_dirs()) return 0;
  /*
   * The reference uses /system/bin/logcat as the exec path after this bind
   * mount.  The mount is namespace-local, so the real system utility is not
   * changed for the rest of the device.
   */
  if (mount(ksud, "/system/bin/logcat", NULL, MS_BIND, NULL) < 0) {
    helper_log("[!] helper ksud bind mount failed src=%s errno=%d\n",
               ksud, errno);
    return 0;
  }
  helper_log("[*] helper ksud bind-mounted src=%s dst=/system/bin/logcat\n",
             ksud);
  return 1;
}

static int run_late_load(void) {
  struct stat loader_stat;
  struct stat module_stat;
  if (geteuid() != 0 || getuid() != 0) {
    helper_log("[!] helper direct late-load is not root uid=%u euid=%u\n",
               (unsigned)getuid(), (unsigned)geteuid());
    return 0;
  }
  if (stat(direct_loader_path, &loader_stat) != 0) {
    helper_log("[!] direct lkmloader stat failed path=%s errno=%d\n",
               direct_loader_path, errno);
    return 0;
  }
  if (stat(direct_module_path, &module_stat) != 0) {
    helper_log("[!] direct kernelsu module stat failed path=%s errno=%d\n",
               direct_module_path, errno);
    return 0;
  }
  if (chdir(direct_work_dir) != 0) {
    helper_log("[!] direct late-load chdir failed path=%s errno=%d\n",
               direct_work_dir, errno);
    return 0;
  }
  helper_log("[*] direct late-load start uid=%u euid=%u cwd=%s loader=%s "
             "loader_bytes=%llu module=%s module_bytes=%llu\n",
             (unsigned)getuid(), (unsigned)geteuid(), direct_work_dir,
             direct_loader_path, (unsigned long long)loader_stat.st_size,
             direct_module_path, (unsigned long long)module_stat.st_size);
  helper_log("[*] direct insmod command: cd %s && %s lkmloader.ko "
             "module_path=kernelsu.ko\n",
             direct_work_dir, direct_insmod_path);

  int direct_pipe[2] = {-1, -1};
  if (pipe2(direct_pipe, O_CLOEXEC) < 0) {
    helper_log("[!] direct insmod stdio pipe failed errno=%d\n", errno);
    return 0;
  }
  pid_t child = fork();
  if (child < 0) {
    helper_log("[!] direct insmod fork failed errno=%d\n", errno);
    close(direct_pipe[0]);
    close(direct_pipe[1]);
    return 0;
  }
  if (child == 0) {
    (void)prctl(PR_SET_PDEATHSIG, 0);
    close(direct_pipe[0]);
    if (direct_pipe[1] >= 0) {
      int out_rc = dup2(direct_pipe[1], STDOUT_FILENO);
      int err_rc = dup2(direct_pipe[1], STDERR_FILENO);
      int pipe_errno = (out_rc < 0 || err_rc < 0) ? errno : 0;
      helper_log("[*] direct insmod stdio redirect dup=%d/%d errno=%d\n",
                 out_rc, err_rc, pipe_errno);
    }
    if (direct_pipe[1] > STDERR_FILENO) close(direct_pipe[1]);
    close_inherited_fds();
    if (chdir(direct_work_dir) != 0) _exit(126);
    execl(direct_insmod_path, "insmod", "lkmloader.ko",
          "module_path=kernelsu.ko", (char *)NULL);
    helper_log("[!] direct insmod exec failed path=%s errno=%d\n",
               direct_insmod_path, errno);
    _exit(127);
  }

  close(direct_pipe[1]);
  direct_pipe[1] = -1;
  {
    int flags = fcntl(direct_pipe[0], F_GETFL, 0);
    if (flags >= 0) (void)fcntl(direct_pipe[0], F_SETFL, flags | O_NONBLOCK);
  }

  int child_status = 0;
  int loaded_seen = 0;
  for (int i = 0; i < 300; i++) {
    drain_child_output(direct_pipe[0]);
    if (kernelsu_loaded()) loaded_seen = 1;
    errno = 0;
    pid_t waited_child = waitpid(child, &child_status, WNOHANG);
    if (waited_child == child) {
      if (WIFEXITED(child_status)) {
        drain_child_output(direct_pipe[0]);
        if (direct_pipe[0] >= 0) close(direct_pipe[0]);
        helper_log("[*] direct insmod exit raw_status=0x%x code=%d "
                   "module_seen=%d\n",
                   child_status, WEXITSTATUS(child_status), loaded_seen);
        return WEXITSTATUS(child_status) == 0 && loaded_seen;
      } else if (WIFSIGNALED(child_status)) {
        drain_child_output(direct_pipe[0]);
        if (direct_pipe[0] >= 0) close(direct_pipe[0]);
        helper_log("[!] direct insmod exit raw_status=0x%x signal=%d "
                   "module_seen=%d\n",
                   child_status, WTERMSIG(child_status), loaded_seen);
        return 0;
      } else {
        helper_log("[!] direct insmod unexpected raw_status=0x%x\n",
                   child_status);
      }
    } else if (waited_child < 0) {
      helper_log("[!] direct insmod waitpid errno=%d\n", errno);
      if (direct_pipe[0] >= 0) close(direct_pipe[0]);
      return 0;
    }
    usleep(100000);
  }
  drain_child_output(direct_pipe[0]);
  (void)kill(child, SIGKILL);
  (void)waitpid(child, &child_status, 0);
  if (direct_pipe[0] >= 0) close(direct_pipe[0]);
  helper_log("[!] direct insmod timed out module_seen=%d\n", loaded_seen);
  return 0;
}

static int write_full(int fd, const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  while (len > 0) {
    ssize_t n = write(fd, p, len);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return 0;
    p += n;
    len -= (size_t)n;
  }
  return 1;
}

static int read_full(int fd, void *data, size_t len) {
  uint8_t *p = (uint8_t *)data;
  while (len > 0) {
    ssize_t n = read(fd, p, len);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return 0;
    p += n;
    len -= (size_t)n;
  }
  return 1;
}

static int detach_local_daemon(void) {
  pid_t first = fork();
  if (first < 0) {
    helper_log("[!] local helper first daemon fork failed errno=%d\n", errno);
    return 0;
  }
  if (first > 0) _exit(0);

  (void)prctl(PR_SET_PDEATHSIG, 0);
  if (setsid() < 0) {
    helper_log("[!] local helper setsid failed errno=%d\n", errno);
    return 0;
  }

  pid_t second = fork();
  if (second < 0) {
    helper_log("[!] local helper second daemon fork failed errno=%d\n",
               errno);
    return 0;
  }
  if (second > 0) _exit(0);

  (void)prctl(PR_SET_PDEATHSIG, 0);
  (void)chdir("/");
  int nullfd = open("/dev/null", O_RDWR | O_CLOEXEC);
  if (nullfd >= 0) {
    (void)dup2(nullfd, STDIN_FILENO);
    (void)dup2(nullfd, STDOUT_FILENO);
    (void)dup2(nullfd, STDERR_FILENO);
    if (nullfd > STDERR_FILENO) close(nullfd);
  }
  close_inherited_fds();
  return 1;
}

static void send_local_response(int conn, int status) {
  struct local_response response = {
      .magic = LOCAL_PROTOCOL_MAGIC,
      .version = LOCAL_PROTOCOL_VERSION,
      .status = status,
  };
  (void)write_full(conn, &response, sizeof(response));
}

static void serve_local_connection(int conn) {
  struct ucred peer;
  socklen_t peer_len = sizeof(peer);
  if (getsockopt(conn, SOL_SOCKET, SO_PEERCRED, &peer, &peer_len) < 0) {
    helper_log("[!] local helper peer credential read failed errno=%d\n",
               errno);
    send_local_response(conn, 126);
    return;
  }
  helper_log("[*] local helper client uid=%u expected=%u\n",
             (unsigned)peer.uid, (unsigned)allowed_client_uid);
  if (peer.uid != allowed_client_uid) {
    helper_log("[!] local helper client rejected uid=%u\n",
               (unsigned)peer.uid);
    send_local_response(conn, 126);
    return;
  }

  struct local_request request;
  if (!read_full(conn, &request, sizeof(request)) ||
      request.magic != LOCAL_PROTOCOL_MAGIC ||
      request.version != LOCAL_PROTOCOL_VERSION) {
    helper_log("[!] local helper invalid request\n");
    send_local_response(conn, 2);
    return;
  }
  if (request.operation != LOCAL_OP_LATE_LOAD) {
    helper_log("[!] local helper unsupported operation=%u\n",
               request.operation);
    send_local_response(conn, 2);
    return;
  }

  helper_log("[*] local helper late-load request accepted\n");
  int ok = run_late_load();
  helper_log("[*] local helper late-load result=%d\n", ok);
  send_local_response(conn, ok ? 0 : 1);
}

static int local_daemon_main(void) {
  /* Keep the UMH process itself as the resident daemon.  Do not
   * double-fork here: returning from the UMH entry changes the lifetime and
   * cgroup relationship that the kernel helper establishes. */
  signal(SIGPIPE, SIG_IGN);
  umask(0);

  int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (listener < 0) {
    helper_log("[!] local helper socket failed errno=%d\n", errno);
    return 1;
  }

  struct sockaddr_un address;
  memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  if (snprintf(address.sun_path, sizeof(address.sun_path), "%s",
               local_socket_path) >= (int)sizeof(address.sun_path)) {
    helper_log("[!] local helper socket path too long\n");
    close(listener);
    return 1;
  }
  unlink(local_socket_path);
  if (bind(listener, (struct sockaddr *)&address, sizeof(address)) < 0 ||
      listen(listener, 8) < 0) {
    helper_log("[!] local helper bind/listen failed errno=%d\n", errno);
    close(listener);
    unlink(local_socket_path);
    return 1;
  }
  if (chmod(local_socket_path, 0666) < 0) {
    helper_log("[!] local helper socket chmod failed errno=%d\n", errno);
  }
  helper_log("[+] local helper socket ready path=%s uid=%u\n",
             local_socket_path, (unsigned)allowed_client_uid);

  for (;;) {
    int conn = accept4(listener, NULL, NULL, SOCK_CLOEXEC);
    if (conn < 0) {
      if (errno == EINTR) continue;
      helper_log("[!] local helper accept failed errno=%d\n", errno);
      usleep(100000);
      continue;
    }

    pid_t worker = fork();
    if (worker < 0) {
      helper_log("[!] local helper request fork failed errno=%d\n", errno);
      close(conn);
      continue;
    }
    if (worker == 0) {
      close(listener);
      serve_local_connection(conn);
      close(conn);
      _exit(0);
    }
    close(conn);
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
  }
}

int ghostlock_late_load_client(void) {
  int conn = -1;
  int last_errno = ENOENT;
  for (int attempt = 0; attempt < 450; attempt++) {
    conn = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (conn < 0) {
      last_errno = errno;
      break;
    }
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s",
             local_socket_path);
    if (connect(conn, (struct sockaddr *)&address, sizeof(address)) == 0) {
      break;
    }
    last_errno = errno;
    close(conn);
    conn = -1;
    if (last_errno != ENOENT && last_errno != ECONNREFUSED &&
        last_errno != EAGAIN && last_errno != EINTR) {
      break;
    }
    usleep(100000);
  }
  if (conn < 0) {
    dprintf(STDERR_FILENO,
            "[local] late-load socket unavailable errno=%d\n", last_errno);
    return 127;
  }

  struct local_request request = {
      .magic = LOCAL_PROTOCOL_MAGIC,
      .version = LOCAL_PROTOCOL_VERSION,
      .operation = LOCAL_OP_LATE_LOAD,
  };
  if (!write_full(conn, &request, sizeof(request))) {
    dprintf(STDERR_FILENO, "[local] late-load request failed errno=%d\n",
            errno);
    close(conn);
    return 1;
  }

  struct local_response response;
  if (!read_full(conn, &response, sizeof(response)) ||
      response.magic != LOCAL_PROTOCOL_MAGIC ||
      response.version != LOCAL_PROTOCOL_VERSION) {
    dprintf(STDERR_FILENO, "[local] late-load response invalid\n");
    close(conn);
    return 1;
  }
  close(conn);
  dprintf(STDOUT_FILENO, "[local] late-load result=%d\n", response.status);
  return response.status;
}

static int ghostlock_umh_entry_impl(int argc, char **argv, int detach) {
  helper_log_begin(argc, argv);
  if (argc != 3 ||
      (strcmp(argv[1], "--umh") != 0 &&
       strcmp(argv[1], "--shell-umh") != 0)) {
    return 2;
  }
  if (geteuid() != 0) {
    helper_log("[!] helper UMH entry is not root uid=%u euid=%u\n",
               (unsigned)getuid(), (unsigned)geteuid());
    return 126;
  }

  char *end = NULL;
  errno = 0;
  unsigned long requested = strtoul(argv[2], &end, 10);
  if (errno || end == argv[2] || *end || requested == 0 ||
      requested > UINT32_MAX) {
    helper_log("[!] helper requested client uid invalid value=%s\n", argv[2]);
    return 124;
  }
  allowed_client_uid = (uid_t)requested;
  helper_log("[*] helper UMH entry uid=%u euid=%u requested_uid=%s\n",
             (unsigned)getuid(), (unsigned)geteuid(), argv[2]);
  if (setresgid(0, 0, 0) < 0 || setresuid(0, 0, 0) < 0) {
    helper_log("[!] helper setresid failed errno=%d\n", errno);
    return 125;
  }
  log_proc_context("umh-after-setresid");
  if (detach && !detach_local_daemon()) return 126;
  return local_daemon_main();
}

int ghostlock_umh_entry(int argc, char **argv) {
  return ghostlock_umh_entry_impl(argc, argv, 0);
}

int ghostlock_shell_umh_entry(int argc, char **argv) {
  return ghostlock_umh_entry_impl(argc, argv, 1);
}
