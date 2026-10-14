#include <assert.h>
#include <string.h>
#include "../src/core/target.h"
#include "../src/devices/offsets.h"

int main(void) {
  const struct kernel_offsets *fold6 = NULL, *fold6_dzi1 = NULL, *fold6_ezih = NULL, *g985f = NULL;
  for (int i = 0; known_offsets[i].uname_r; ++i) {
    if (!strcmp(known_offsets[i].uname_r, "4.19.87-27102101")) g985f = &known_offsets[i];
    if (!strcmp(known_offsets[i].uname_r,
                "6.1.145-android14-11-33418572-abF956BXXS4DZH5")) fold6 = &known_offsets[i];
    if (!strcmp(known_offsets[i].uname_r,
                "6.1.145-android14-11-33418572-abF956BXXS4DZI1")) fold6_dzi1 = &known_offsets[i];
    if (!strcmp(known_offsets[i].uname_r,
                "6.1.162-android14-11-34343818-abF956BXXU4EZIH")) fold6_ezih = &known_offsets[i];
  }
  assert(fold6 && fold6_dzi1 && fold6_ezih && g985f);
  assert(fold6_ezih != fold6 && fold6_ezih != fold6_dzi1);
  assert(fold6_ezih->waiter_compact && !fold6_ezih->waiter_legacy_4_19);
  assert(fold6_ezih->tracefs_leak && fold6_ezih->tracefs_event_id == 106);
  assert(fold6_ezih->tracefs_caller_off == 0xdb39c);
  assert(fold6_ezih->pselect_waiter_shift == 1 && fold6_ezih->mm_struct_sz == 0x3c0);
  assert(fold6_ezih->task_pi_blocked_on == 0x950 && fold6_ezih->task_cred == 0x838);
  assert(fold6_ezih->off_configfs_read_iter != fold6_dzi1->off_configfs_read_iter);
  assert(fold6_ezih->off_slide_boot_id != fold6_ezih->off_slide_sysctl_bootid);
  assert(fold6_ezih->wq_dfl_pwq == 0xb0 && fold6_ezih->pool_worklist == 0x28);
  assert(!fold6->diagnostic_only && !fold6->kaslr_virtual_only);
  assert(fold6->kernel_phys_load == 0xa8000000ULL);
  assert(fold6->task_cred == 0x838 && fold6->mm_struct_sz == 0x3c0);
  assert(fold6_dzi1->kernel_phys_load == 0xa8000000ULL);
  assert(fold6_dzi1->task_cred == 0x838 && fold6_dzi1->mm_struct_sz == 0x3c0);
  assert(fold6_dzi1->umh_skip_pre_drain);
  assert(fold6_dzi1->off_selinux_blob_sizes == 0x176d528);
  assert(fold6_dzi1->off_security_hook_heads == 0x176ce18);
  assert(fold6_dzi1->off_kmalloc_caches == 0x176c958);
  assert(fold6_dzi1->off_configfs_read_iter == 0x4712dc);
  assert(fold6_dzi1->off_configfs_bin_write_iter == 0x47180c);
  assert(fold6_dzi1->off_ashmem_misc_fops == 0x23bb600);
  assert(fold6_dzi1->off_ashmem_ioctl == 0xd3a67c);
  assert(fold6_dzi1->off_ashmem_show_fdinfo == 0xd3b3e0);
  assert(g985f->diagnostic_only && g985f->kaslr_virtual_only);
  /* The signal-return carrier is not a validated overlap on HYB1. */
  assert(g985f->waiter_legacy_4_19 && !g985f->waiter_compact);
  assert(g985f->rt_sigreturn_route);
  assert(g985f->rt_sigreturn_waiter_payload_off == 0xf0);
  assert(g985f->rt_sigreturn_waiter_size == 0x50);
  assert(g985f->configfs_legacy_rw);
  assert(g985f->consumer_core == 4);
  /* Legacy selects .read/.write slots, not a different configfs_buffer ABI. */
  assert(CFG_PAGE_OFF == 16);
  assert(CFG_NEEDS_READ_FILL_OFF == 80);
  assert(CFG_BIN_BUFFER_OFF == 88);
  assert(CFG_BIN_BUFFER_SIZE_OFF == 96);
  assert(CFG_CB_MAX_SIZE_OFF == 100);
  assert(g985f->kaslr_slide_alignment == 0x8000);
  assert(!g985f->kernel_phys_load && !g985f->sync_route && g985f->umh_root);
  assert(g985f->canonical_data && g985f->page_offset == 0xffffffc000000000ULL);
  assert(g985f->direct_map_size == 0x4000000000ULL);
  assert(g985f->off_memstart_addr == 0x19c25e0);
  assert(g985f->off_kimage_voffset == 0x19c25f0);
  assert(g985f->pipe_buffer_slots == 16 && g985f->kmalloc_pipe_index == 10);
  assert(g985f->pipe_object_size == 0x400 && g985f->pipe_objs_per_slab == 32);
  assert(g985f->pipe_mm_stride == 0x380);
  assert(g985f->umh_subprocess_legacy_4_19 && g985f->umh_completion_size == 0xd0);
  assert(g985f->pwq_nr_active == 0x58 && g985f->pwq_max_active == 0x5c);
  assert(g985f->pool_worklist == 0x20 && g985f->pool_nr_idle == 0x34);
  assert(g985f->task_cred == 0x858 && g985f->mm_struct_sz == 0x380);
  assert(g985f->task_usage == 0x28);
  assert(g985f->kimage_text_base + g985f->off_ashmem_fops == 0xffffff8009322910ULL);
  assert(g985f->kimage_text_base + g985f->off_configfs_read == 0xffffff800831174cULL);
  assert(g985f->kimage_text_base + g985f->off_configfs_bin_write == 0xffffff80083118d0ULL);
  assert(g985f->kimage_text_base + g985f->tracefs_caller_off == 0xffffff80080d0208ULL);
  assert(g985f->kimage_text_base + g985f->off_global_trace ==
         0xffffff8009f11ce0ULL);
  assert(g985f->trace_control_word == 0x50);
  assert(g985f->vmemmap_start == 0xffffffbf00000000ULL);
  return 0;
}
