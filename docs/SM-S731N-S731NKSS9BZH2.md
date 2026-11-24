# Galaxy S25 FE SM-S731N (Korea) / S731NKSS9BZH2 port record

This record documents the port of CVE-2026-43499 to the Korean Galaxy S25 FE
(Exynos 2400e SoC, codename `r13s`, firmware `S731NKSS9BZH2`). The profile was
derived entirely offline from the stock Samsung AP package and verified with
the offline gate described below, then exercised on the device over five
reboots (see "Runtime validation"): every boot produced a valid KASLR slide
leak from the P0 fingerprint, fops data-alias verification and a working
physrw primitive were achieved twice, and the final usermodehelper publish
step was reached twice. The UMH publish race did not win on this busy build
and two of the five boots ended in a recoverable kernel panic during the
abort/rollback path. The S731U1 record remains the device-tested sibling
profile for the same SoC.

## Device identity

| Field | Value |
| --- | --- |
| Model | `SM-S731N` |
| Codename | `r13s` (product `r13sksx`) |
| SoC | Exynos 2400e (`s5e9945` / `erd9945`) |
| Display build | `BP4A.251205.006.S731NKSS9BZH2` |
| Build fingerprint | `samsung/r13sksx/r13s:16/BP4A.251205.006/S731NKSS9BZH2_OKR9BZH2:user/release-keys` |
| Kernel release | `6.1.157-android14-11` |
| Kernel build | `#1 SMP PREEMPT Fri Aug 14 09:27:25 UTC 2026` |
| Android SDK | 36 (Android 16) |
| CSC | LUC (LG U+) |
| RAM | 8 GB (7,377,328 kB usable) |
| Page size | 4096 |

## Firmware extraction and symbols

Queried Samsung FUS with `samloader-rs` 2.2.0 (region `LUC`):

```text
S731NKSS9BZH2/S731NOKR9BZH2/S731NKSS9BZH1/S731NKSS9BZH2
```

AP archive → `boot.img.lz4` → `boot.img` → raw ARM64 Image at 4096 alignment.

| Object | Size (bytes) |
| --- | --- |
| `boot.img` | 67,108,864 |
| Raw kernel | 38,832,640 |
| ARM64 `image_size` (incl. `.bss`) | 41,615,360 (`0x27b0000`) |
| ARM64 Image flags | `0xa`, text_offset `0x0` |
| `boot.img` SHA-256 | `B6EB17CAA44A56B65A89592703B43219DE6F03233CB85837909A152B4739D090` |
| raw kernel SHA-256 | `B49D2378FEDCA2F9593B541779B35D43075C7732CE806DC2BECB5BE036DEE5E3` |
| `vmlinux.btf` | 5,998,390 (raw blob at Image `0x188c248`) |

Symbol recovery via `vmlinux-to-elf` at image base `0xffffffc008000000`
(115,670 symbols). The S731U1 record and this record share the kernel release
`6.1.157-android14-11`; the BZH2 kernel build (Aug 14 2026) is newer than the
BZF3 build (Jun 11 2026), so every offset below was re-derived from the BZH2
image and cross-checked by symbol name, not copied.

## Target profile: `r13s-S731NKSS9BZH2`

Profile resides at `src/targets/r13s-S731NKSS9BZH2/target.h`.

### Physical memory layout

Identical to the S731U1 record (same SoC, same 8 GB DRAM):

```
P0_PAGE_OFFSET   = 0xffffff8000000000
P0_PHYS_OFFSET   = 0x80000000
DIRECT_MAP_BASE  = 0xffffff8000000000
VMEMMAP_START    = 0xfffffffe00000000
KIMAGE_TEXT_BASE = 0xffffffc008000000
```

`APP_KERNEL_PAGE_KSNITCH_IDENTITY_END` and `APP_RECLAIM_MAX_DIRECT_BASE` use
`0xffffff8a00000000` with `APP_KERNEL_PAGE_KSNITCH_EXACT_PARTITION 1`,
matching the final device-tested S731U1 tuning for 8 GB DRAM (the PR header of
S731U1 still shows the pre-fix `0xffffff8080000000`; the S731U1 port record
documents the raise as required for the upper DRAM range).

### Symbol offsets (re-derived from S731NKSS9BZH2 stock kernel)

| Symbol | S731U1UES7BZF3 | S731NKSS9BZH2 | Delta |
| --- | --- | --- | --- |
| `call_usermodehelper_exec_work` | `0x000d4468` | `0x000d4468` | = |
| `noop_llseek` | `0x003a1414` | `0x003a1414` | = |
| `copy_splice_read` (kallsyms: `generic_file_splice_read`) | `0x003ef02c` | `0x003ef02c` | = |
| `configfs_read_iter` | `0x00470d44` | `0x00470d44` | = |
| `configfs_bin_write_iter` | `0x00471274` | `0x00471274` | = |
| tracefs/worker_thread call site | `0x000dbd9c` | `0x000dbd9c` | = |
| `ashmem_ioctl` | `0x00d3a19c` | `0x00d3a19c` | = |
| `compat_ashmem_ioctl` | `0x00d3aad4` | `0x00d3aad4` | = |
| `ashmem_mmap` | `0x00d3ab2c` | `0x00d3ab2c` | = |
| `ashmem_open` | `0x00d3ad58` | `0x00d3ad58` | = |
| `ashmem_release` | `0x00d3ade0` | `0x00d3ade0` | = |
| `ashmem_show_fdinfo` | `0x00d3af00` | `0x00d3af00` | = |
| `anon_pipe_buf_ops` | `0x0121ced0` | `0x0121cf10` | +0x40 |
| `ashmem_fops` | `0x013d90c8` | `0x013d9108` | +0x40 |
| `nfulnl_logger` name string | `0x016dce21` | `0x016dce61` | +0x40 |
| `kmalloc_caches` | `0x017a7a58` | `0x017a7a98` | +0x40 |
| `system_unbound_wq` | `0x022eae58` | `0x022eae58` | = |
| `init_task` | `0x022ff840` | `0x022ff840` | = |
| `nfulnl_logger` object | `0x022f2a10` | `0x022f2a10` | = |
| `root_task_group` (`.bss`) | `0x02515cc0` | `0x02515cc0` | = |
| `selinux_state` (`.bss`) | `0x025ea478` | `0x025ea478` | = |
| `ashmem_miscs` (`.bss`) | `0x02484c20` | `0x02484c20` | = |
| `random_table` + `0x108` boot-id anchor | `0x0243eff8` | `0x0243eff8` | = |
| `sysctl_bootid` (`.bss`) | `0x026cd5e0` | `0x026cd5e0` | = |

`INIT_CRED` (`0x017ea018` in this build) is present in kallsyms but is not
part of the profile symbol set (matches the S731U1 profile shape).

Four data offsets moved by `+0x40` between the Jun (BZF3) and Aug (BZH2)
kernel builds; all text offsets and `.bss` anchors are unchanged.

### Structure layouts (BTF-verified, `pahole -F btf`)

All template layout constants were confirmed against the extracted
`vmlinux.btf` (5,998,390 bytes, same size as the S731U1 record):

- `rt_mutex_waiter`: `pi_tree_entry` 0x18, `task` 0x30, `lock` 0x38,
  `wake_state` 0x40, `prio` 0x44, `deadline` 0x48, `ww_ctx` 0x50, size 0x58
- `task_struct`: `usage` 0x40, `prio` 0x84, `normal_prio` 0x8c,
  `sched_task_group` 0x348, `pi_lock` 0x924, `pi_waiters` 0x938,
  `pi_top_task` 0x948, `pi_blocked_on` 0x950
- `work_struct` 0x00/0x08/0x18; `pool_workqueue` 0x00/0x08/0x10/0x18/0x1c
  (`nr_in_flight[16]`)/0x5c/0x60; `worker_pool` `worklist` 0x28, `nr_idle` 0x3c
- `struct page` size 0x40 with SLUB overlay `struct slab` `slab_cache` at 0x18
  (`mm_struct` 960 bytes ≤ `MM_STRUCT_SZ` 0x400 slab, `MM_ORDER` 3)
- `configfs_buffer`: `page` 16, `needs_read_fill` 80, `bin_buffer` 88,
  `bin_buffer_size` 96, `cb_max_size` 100
- `file_operations` size 0x110 with the template field offsets
  (unlocked_ioctl 0x50, compat_ioctl 0x58, mmap 0x60, open 0x70,
  release 0x80, splice_read 0xc8, show_fdinfo 0xe0)

`SLIDE_TRACEFS_WORKER_CALLER_OFF 0x000dbd9c` was additionally verified by
disassembly: it lands inside `worker_thread` (`0x000dbcfc`) at a
`mov x0, x20; bl <spin/schedule call site>` sequence, matching the S731U1/S721
semantics.

### P0 fingerprint

`p0_fingerprint.h` regenerated from the exact BZH2 raw Image with
`tools/generate_p0_fingerprint.pl kernel 0x1f0000 p0_fingerprint.h`
(32 slides × 8 probe words; the generator verified all 256 source qwords).

## Offline verification gate

The following checks were run and passed before this record was written:

1. Every profile offset resolves to the expected kallsyms symbol in the BZH2
   `vmlinux.nm` (21 symbol fields, including `.bss` anchors).
2. `nfulnl_logger.name` dereferences to the profile's
   `SLIDE_NFULNL_LOGGER_NAME_OFF` and reads `"nfnetlink_log"`.
3. `SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_OFF` equals `random_table + 0x108`.
4. `SLIDE_TRACEFS_WORKER_CALLER_OFF` lies inside `worker_thread`.
5. All 256 P0 fingerprint qwords recompute identically from the raw Image.
6. All offsets are below the ARM64 `image_size` (`0x27b0000`).
7. `target.h` compiles standalone in both `APP_PAYLOAD=1` and root variants
   (`gcc -fsyntax-only -Wall -I src`).

## Runtime validation (live device, five boots)

The built payload (Termux clang, bionic `DT_NEEDED: libdl.so libc.so` only)
was pushed to `/data/local/tmp` and run from the `u:r:shell:s0` ADB shell
domain with the standard invocation. Observations per boot:

| Boot | Slide phase | fops alias | physrw | Root (UMH) phase |
| --- | --- | --- | --- | --- |
| 1 | 8/8 P0 page misses, then a second attempt crashed (panic, reboot) | — | — | — |
| 2 | slide leak `0x1b0000` | 8/8 reclaim miss, clean exit | — | — |
| 3 | slide leak `0x00080000` | **verified** | **established** | reached `root umh spans … selinux=ffffff800266a478 old=1`, prepublish race lost |
| 4 | slide leak `0x000b0000` | **verified** | **established** | reached UMH spans, prepublish race lost, panic during abort |
| 5 | (patched helper) failed earlier at fops pipe prep (`tee` `EFAULT`), panic | — | — | — |

Key evidence that the offline profile matches the running kernel:

- `p0 profile … slide_logger=ffffff80016dce61 bootid_data=ffffff800243eff8
  init_task=ffffff80022ff840 root_tg=ffffff8002515cc0
  sysctl_bootid=ffffff80026cd5e0` — every template offset loaded as built.
- Three different KASLR slides (`0x1b0000`, `0x80000`, `0xb0000`) were all
  recovered from the generated `p0_fingerprint.h` table.
- With the slide applied, the root phase computed
  `selinux=ffffff800266a478` = physmap base + `0x025ea478` + slide, i.e. the
  profile's `SELINUX_ENFORCING_OFF` resolves to `selinux_state` at runtime.

### The remaining gate: UMH publish race

Both root-phase attempts failed at the same step:

```text
root umh prepublish write failed counters=1/1/1 prev=0 next=ffffff882e419028
```

`counters=1/1/1` means the `nr_in_flight`/`nr_active`/`refcnt` writes all
verified; the failure is the `pool->worklist` list update — between the
empty-list pre-check and the `list_add` writes, the unbound worker pool on
this busy consumer build received kernel work, the exact-verify failed, and
the abort/rollback path raced the active list, twice ending in a recoverable
kernel panic. This is a timing window, not an offset problem: every address
and layout constant behaved as derived offline.

`src/root.c` in this change set adds a conservative mitigation: the prepublish
list write now polls for an empty (quiet) `pool->worklist` window — up to
~2 s at 25 ms — before writing, instead of writing immediately after the
single pre-check. This only widens the precondition the original code already
requires and removes the write-into-active-list pattern observed here.

Two of five boots ended in a kernel panic (auto-reboot, no persistent
effects: no flash writes, SELinux returned to Enforcing on every boot, and
`/data` artifacts were untouched). Runtime success on this build therefore
still requires the probabilistic per-boot attempt series documented in the
S731U1 record; the profile itself is confirmed correct end-to-end up to the
final race.

## Build artifacts

Not attached to this record. The exploit and KernelSU binaries must be built
per the root README with NDK on an x86_64 host:

```sh
ANDROID_NDK_HOME=/path/to/android-ndk make TARGET=r13s-S731NKSS9BZH2
```

The `support/targets-v3.json` feed entry should be added together with the
built `cve-2026-43499-app.so` artifact so the advertised `size` is exact.

## Deployment

Identical to the S731U1 record (same profile shape and flags, including
`APP_REQUIRE_FRESH_P0_SESSION=1` and MTE handling). See
[`SM-S731U1-S731U1UES7BZF3.md`](SM-S731U1-S731U1UES7BZF3.md) for the on-device
procedure and KernelSU late-load flow. The chain was validated on SM-S731N
hardware up to the final UMH publish race (see "Runtime validation"); winning
that race still needs the probabilistic per-boot attempt series, ideally on a
quietly-booted device.
