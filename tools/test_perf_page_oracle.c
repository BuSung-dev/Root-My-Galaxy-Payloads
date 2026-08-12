#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/perf_event.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#define PAGE_SIZE 4096UL
#define RING_PAGES 8UL
#define TEST_PAGES 3UL
#define PHYS_OFFSET 0x80000000ULL
#define PAGE_OFFSET 0xffffff8000000000ULL

static void fail(const char *where) {
  int error = errno;
  fprintf(stderr, "PERF_PAGE_FAIL where=%s errno=%d error=%s\n",
          where, error, strerror(error));
  exit(1);
}

static uint64_t read_event_id(void) {
  const char *path = "/sys/kernel/tracing/events/kmem/mm_page_alloc/id";
  char text[32];
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    fail("open-event-id");
  }
  ssize_t size = read(fd, text, sizeof(text) - 1);
  if (close(fd) != 0 || size <= 0) {
    fail("read-event-id");
  }
  text[size] = 0;
  char *end = NULL;
  errno = 0;
  unsigned long long id = strtoull(text, &end, 10);
  if (errno || end == text || id == 0) {
    errno = EINVAL;
    fail("parse-event-id");
  }
  return id;
}

static int open_event(uint64_t id) {
  struct perf_event_attr attr;
  memset(&attr, 0, sizeof(attr));
  attr.type = PERF_TYPE_TRACEPOINT;
  attr.size = sizeof(attr);
  attr.config = id;
  attr.sample_period = 1;
  attr.sample_type = PERF_SAMPLE_RAW;
  attr.wakeup_events = 1;
  attr.disabled = 1;
  attr.exclude_hv = 1;
  return (int)syscall(SYS_perf_event_open, &attr, 0, -1, -1,
                      PERF_FLAG_FD_CLOEXEC);
}

static void ring_copy(unsigned char *out, const unsigned char *ring,
                      size_t ring_size, uint64_t offset, size_t size) {
  size_t start = (size_t)(offset & (ring_size - 1));
  size_t first = ring_size - start;
  if (first > size) {
    first = size;
  }
  memcpy(out, ring + start, first);
  if (first < size) {
    memcpy(out + first, ring, size - first);
  }
}

static int find_allocation(struct perf_event_mmap_page *meta,
                           const unsigned char *ring, pid_t pid,
                           uint64_t event_id, uint64_t *pfn_out,
                           uint32_t *order_out) {
  uint64_t head = __atomic_load_n(&meta->data_head, __ATOMIC_ACQUIRE);
  uint64_t tail = meta->data_tail;
  uint64_t found_pfn = 0;
  unsigned int matches = 0;

  while (tail < head) {
    struct perf_event_header header;
    unsigned char record[256];
    ring_copy((unsigned char *)&header, ring, meta->data_size, tail,
              sizeof(header));
    if (header.size < sizeof(header) || header.size > sizeof(record)) {
      errno = EPROTO;
      fail("record-size");
    }
    ring_copy(record, ring, meta->data_size, tail, header.size);
    tail += header.size;
    if (header.type != PERF_RECORD_SAMPLE || header.size < 40) {
      continue;
    }
    uint32_t raw_size;
    uint16_t raw_id;
    int32_t raw_pid;
    uint64_t pfn;
    uint32_t order;
    memcpy(&raw_size, record + 8, sizeof(raw_size));
    if (raw_size < 28 || raw_size + 12 > header.size) {
      continue;
    }
    memcpy(&raw_id, record + 12, sizeof(raw_id));
    memcpy(&raw_pid, record + 16, sizeof(raw_pid));
    memcpy(&pfn, record + 20, sizeof(pfn));
    memcpy(&order, record + 28, sizeof(order));
    if (raw_id != event_id || raw_pid != pid) {
      continue;
    }
    if (order == 0) {
      found_pfn = pfn;
      matches++;
    }
  }
  __atomic_store_n(&meta->data_tail, tail, __ATOMIC_RELEASE);
  printf("PERF_PAGE_EVENTS order0_matches=%u pfn=%#llx\n",
         matches, (unsigned long long)found_pfn);
  if (matches != 1) {
    errno = ENOENT;
    return 0;
  }
  *pfn_out = found_pfn;
  *order_out = 0;
  return 1;
}

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  uint64_t event_id = read_event_id();
  int perf_fd = open_event(event_id);
  if (perf_fd < 0) {
    fail("perf-event-open");
  }
  size_t map_size = PAGE_SIZE * (RING_PAGES + 1);
  unsigned char *perf_map = mmap(NULL, map_size, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, perf_fd, 0);
  if (perf_map == MAP_FAILED) {
    fail("perf-ring-mmap");
  }

  unsigned char *pages = mmap(NULL, PAGE_SIZE * TEST_PAGES,
                              PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (pages == MAP_FAILED) {
    fail("page-mmap");
  }
  unsigned char *target = pages + PAGE_SIZE;
  pages[0] = 0;
  pages[PAGE_SIZE * 2] = 0;

  if (ioctl(perf_fd, PERF_EVENT_IOC_RESET, 0) != 0 ||
      ioctl(perf_fd, PERF_EVENT_IOC_ENABLE, 0) != 0) {
    fail("perf-enable");
  }
  *target = 0;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  if (ioctl(perf_fd, PERF_EVENT_IOC_DISABLE, 0) != 0) {
    fail("perf-disable");
  }

  struct perf_event_mmap_page *meta =
      (struct perf_event_mmap_page *)perf_map;
  uint64_t pfn;
  uint32_t order;
  if (!find_allocation(meta, perf_map + PAGE_SIZE, getpid(), event_id,
                       &pfn, &order)) {
    fail("no-contiguous-allocation");
  }
  uint64_t physical = pfn << 12;
  if (physical < PHYS_OFFSET) {
    errno = ERANGE;
    fail("physical-range");
  }
  uint64_t alias = PAGE_OFFSET | (physical - PHYS_OFFSET);
  printf("PERF_PAGE_OK event=%llu pid=%d user=%p pfn=%#llx order=%u "
         "physical=%#llx alias=%#llx\n",
         (unsigned long long)event_id, getpid(), target,
         (unsigned long long)pfn, order, (unsigned long long)physical,
         (unsigned long long)alias);
  return 0;
}
