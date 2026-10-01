# r0q / SM-S901U1 (Galaxy S22) port — S901U1UESAGZF3

Status: **static port complete and independently cross-validated; hardware root
chain not yet completing** (see "Hardware state" below).

This directory is a staging area for a contribution to
`BuSung-dev/Root-My-Galaxy-Payloads` (the app repo itself is data-driven and
needs no code change). Nothing here is pushed anywhere.

## Target identity (from the connected device and the exact firmware)

| Field | Value |
| --- | --- |
| Model / device | `SM-S901U1` / `r0q` |
| SoC | SM8450 (taro) |
| CSC / region | CHA |
| Firmware | `S901U1UESAGZF3/S901U1OYMAGZF3/S901U1UESAGZF3/S901U1UESAGZF3` |
| Display build | `BP2A.250605.031.A3.S901U1UESAGZF3` |
| Fingerprint | `samsung/r0quew/r0q:16/BP2A.250605.031.A3/S901U1UESAGZF3:user/release-keys` |
| Android / SDK | 16 / 36 |
| Kernel | `5.10.236-android12-9-31998796-abS901U1UESAGZF3` |
| Toolchain (stock) | clang 12.0.5, `r416183b` |
| Bootloader | locked (`ro.boot.flash.locked=1`), verified boot green |
| Page size | 4096, `VA_BITS=39` |

Firmware was downloaded from Samsung FUS with `samloader-rs 2.2.0`
(`check-update --model SM-S901U1 --region CHA --all` lists `S901U1UESAGZF3`).

Intermediate hashes:

| Object | Size | SHA-256 |
| --- | ---: | --- |
| `boot.img` (from `boot.img.lz4`) | 100,663,296 | `3C2F86D021BB9ECFB5670036CA7346AB69F143433FFE6C363E446BEE7C789781` |
| raw ARM64 `Image` (`kernel.bin`) | 41,495,040 | `4549E40DBA8268ED54140A6978265ED70633521D291184D5FE2DA5980D9BCE72` |
| recovered `vmlinux.elf` | 47,065,500 | (regenerable with `vmlinux-to-elf kernel.bin vmlinux.elf`) |

Image header: `text_offset=0`, `image_size=0x2a50000`, flags `0xa`.
Kernel banner confirms clang 12.0.5 `r416183b`, build 2026-06-16.
`CONFIG_DEBUG_INFO_BTF` is **not set** (no BTF); struct layouts come from the
5.10 branch record plus disassembly checks.

## Derived profile

`src/target.h` and `src/p0_fingerprint.h` are the new target. Key values
(base `0xffffffc008000000`):

| Macro | Offset |
| --- | ---: |
| `ASHMEM_MISC_FOPS_OFF` (`ashmem_misc + 0x10`) | `0x026ecd28` |
| `ASHMEM_FOPS_OFF` | `0x02080f78` |
| `ASHMEM_{IOCTL,COMPAT_IOCTL,MMAP,OPEN,RELEASE,SHOW_FDINFO}_OFF` | `0x0114ab24`, `0x0114b5f0`, `0x0114b648`, `0x0114b878`, `0x0114b910`, `0x0114ba2c` |
| `CONFIGFS_READ_ITER_OFF` / `CONFIGFS_BIN_WRITE_ITER_OFF` | `0x006040d8` / `0x00604a68` |
| `COPY_SPLICE_READ_OFF` / `NOOP_LLSEEK_OFF` | `0x0053866c` / `0x004c3694` |
| `INIT_TASK_OFF` / `ROOT_TASK_GROUP_OFF` | `0x0259c000` / `0x0279c040` |
| `SELINUX_ENFORCING_OFF` (`selinux_state`, field at +0) | `0x028cdcd8` |
| `KMALLOC_CACHES_OFF` / `ANON_PIPE_BUF_OPS_OFF` | `0x020c3160` / `0x01f03be8` |
| `CALL_USERMODEHELPER_EXEC_WORK_OFF` / `SYSTEM_UNBOUND_WQ_OFF` | `0x001086b4` / `0x02589e08` |
| `SLIDE_NFULNL_LOGGER_NAME_OFF` / `_OBJECT_OFF` | `0x01dfa1f1` / `0x02591348` |
| `SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_OFF` | `0x026acb50` |
| `SLIDE_SYSCTL_BOOTID_OFF` | `0x0296dd45` |
| `SLIDE_TRACEFS_WORKER_CALLER_OFF` | `0x00112ae0` |
| `P0_PHYS_OFFSET` / `P0_KERNEL_PHYS_LOAD` | `0x80000000` / `0xa8000000` |
| `SLIDE_TRACEFS_EVENT_ID` | `84` (confirmed live via sysfs) |

Verification evidence:

* `ashmem_fops` slots read out of the image match the function symbols
  (`+0x08` llseek, `+0x50` ioctl, `+0x58` compat_ioctl, `+0x60` mmap, `+0x70`
  open, `+0x80` release, `+0xe0` show_fdinfo) and `ashmem_misc + 0x10` points at
  `ashmem_fops`.
* `nfulnl_logger[0]` points at `"nfnetlink_log"` (`0x1dfa1f1`), and
  `random_table`'s `boot_id` entry data pointer (`0x26acb50`) equals the
  `sysctl_bootid` symbol.
* The independent public S22 port `sarabpal-dev/IonStack-S22U` carries
  `src/targets/S901U1UESAGZF3/target.h` with **identical** offsets for every
  value above, so the derivation is not a single-source guess.
* `__cfi_jt` is empty (`__cfi_jt_start == __cfi_jt_end == 0xffffffc009800000`),
  so the raw addresses in the image's fops tables are already canonical.

## Kernel/KASLR finding (hardware-verified)

The image KASLR slide on this device is **32 KiB-granular**, not 64 KiB.
Measured from a live `sched_blocked_reason` trace: the dominant caller was
`worker_thread+0x558` at `0xffffffc0081eaae0`, i.e. slide `0xd8000`.
`src/slide.c` previously required `(candidate & 0xffff) == 0`, so the correct
slide was **rejected** ("slide tracefs worker caller not found"); the F9360
profile documents the same 32 KiB pattern (`0x18000`/`0x1c8000`).

`slide.c.patch` fixes this in a target-gated way:

* new `SLIDE_TRACEFS_CANDIDATE_MASK` (default `0xffff`, this target `0x7fff`);
* candidates are now tallied and the **most frequently observed** candidate is
  chosen instead of the first match, so a relaxed mask cannot be hijacked by an
  unrelated caller.

After the patch, the leak is reproducible on hardware:

```text
slide tracefs caller candidate=000d8000 observations=460 distinct=1
slide-kaslr-ok source=tracefs base=ffffffc0080d8000 slide=0x000d8000
```

## Build

```sh
ANDROID_NDK_HOME=/path/to/android-ndk-r29 \
  make TARGET=r0q-S901U1UESAGZF3 release   # app payload, fixed 104128 bytes
ANDROID_NDK_HOME=/path/to/android-ndk-r29 \
  make TARGET=r0q-S901U1UESAGZF3 all       # preload + root helper
```

Artifacts in `artifacts/` (built with NDK r29 / clang 21):

| File | Bytes | Purpose |
| --- | ---: | --- |
| `cve-2026-43499-app.release.so` | 104,128 | feed artifact (`artifacts/r0q-S901U1UESAGZF3/cve-2026-43499-app.so`) |
| `cve-2026-43499-app.so` | 133,992 | debug APP_PAYLOAD build for shell testing |
| `cve-2026-43499` | 106,144 | non-app LD_PRELOAD supervisor (root-UMH route) |
| `cve-2026-43499-root` | 27,072 | `su_daemon` root helper |

## Hardware state (connected SM-S901U1)

Working:

* firmware/kernel derivation, all offsets cross-validated;
* KASLR leak via tracefs (exact slide, reproducible);
* KernelSnitch mm leak and kernel-page preparation
  (`kernel page prepare mode=0 attempt=1/72` succeeds, ~9–14 s);
* no panics or side effects: verified boot stays green, warranty bit 0,
  bootloader untouched.

Not yet working: the BuSung payload's fake-lock/pselect stage never signals
success, so the CFI/fops stage is never entered:

```text
pselect returned attempt=1 ret=0 errno=0 calls=1 success=0 delay=25000
pselect cfi miss attempt=1/1 step=33 errno=0; refreshing FOPS page
pipe physrw pid=... done=0 root=0 kaslr=1 read_ok=0 write_ok=0
```

`step=33` means `calls > 0 && success > 0` was false. `calls=1` shows the
consumer thread issued `sched_setattr` on the waiter tid and then never
returned (no `pselect consumer sched_setattr` warning is emitted, so the
syscall is blocking inside the kernel rather than failing), i.e. the forged
fake lock is engaged but the boost/chain never completes. See
`hardware-notes/rmg-supervisor-stall-log.txt`.

### The device and the profile are proven rootable

The same CVE-2026-43499 chain implemented by `sarabpal-dev/IonStack-S22U`
(same public exploit lineage, a `target.h` for `S901U1UESAGZF3` whose every
derived constant is byte-identical to ours) reaches **full root on this exact
handset** on attempt 1:

```text
phys step read64 done ok=1 value=306365737562656e
root umh selinux enforcing byte 1 -> 0 (addr=ffffff802aa65cd8)
root umh result wake=1 complete=1 retval=0 socket=1
pipe-physrw-summary pid=13540 done=1 root=1 kaslr=1 base=ffffffc008198000 slide=0000000000198000
pipe physrw pid=13540 done=1 root=1 kaslr=1 read_ok=1 write_ok=1 rw64=1/1 uid=2000->0
exploit completed attempt=1/16
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
```

(`hardware-notes/sibling-root-log.txt`; SELinux ends permissive, verified boot
still green, page keeper retains the reclaimed pages for the boot.)

So the remaining gap is specifically in the BuSung payload's non-app
supervisor route on this firmware, not in the offsets, the slide, the physical
load address, or the device.

### Round 10: correcting round 8/9, and the fdset geometry checks out

Two corrections, both from better evidence:

* The round-8 "global cgroup rwsem wedge" was a **misreading**. Those `D`-state
  samples were transient: each payload tid appeared in `D` only once across the
  sampling window, and re-reading the same threads' stacks showed ordinary
  `binder_wait_for_work` / `futex_wait_queue_me` sleeps. No lock is leaked.
* The real stalled-run signature (long-window `wchan` sampling of the payload
  process) is: the waiter thread sits in `rt_mutex_wait_proxy_lock` (correct,
  8 s PI wait per `ROUTE_WAIT_SECONDS`), while the consumer's `sched_setattr`
  is simply **in flight for the rest of the attempt** — no error is ever logged
  and `success` never increments. The consumer's thread is not visible in a
  blocking kernel function, so it is spinning/blocked inside the PI walk rather
  than returning an error.

**The pselect/futex stack mapping is correct for this kernel.** Derived from
our own disassembly:

```text
pselect: el0(0x60) + __arm64_sys_pselect6(0xa0) + core_sys_select(0x1c0)
         bits = stack_fds = sp+0x50  -> array depth = 0x270
futex:   el0(0x60) + __arm64_sys_futex(0x70) + do_futex(0x70) +
         futex_wait_requeue_pi(0x1a0; rt_waiter at sp+0x90) -> depth 0x250
shift k = (0x270 - 0x250)/8 = 4 words
```

so `fdset word 4+w == waiter word w`, which makes `ex[0..2]` == waiter words
6/7/8 == `task`/`lock`/`prio` for the 5.10 legacy waiter — exactly what
`prepare_pselect_fdsets()` already writes. The round-9 "legacy words"
experiment (write set words 1/2/3) was therefore *wrong*, matched its negative
result, and stays reverted. `SLIDE_PSELECT_WORD_SHIFT` is not involved on the
non-app route.

Conclusion: the wchan/stack evidence now excludes the pselect fdset geometry,
the KASLR slide, the offsets and the page-reclaim stage. What remains is the
PI-chain walk itself not terminating against the forged page objects
(`fake_task` / `fake_lock` / `fake_fops` at `LOCK_OFF` / `W0_OFF` / `FOPS_OFF`;
both the q4q and canonical geometries were tried with identical results) — a
question that needs in-kernel tracing rather than static reasoning.

### pselect stall root cause: rwsem write-lock wedge (round 8)

Live thread snapshots taken as root while the supervisor route ran (payload
process, all attempts) show every thread of the process blocked on an
rw-semaphore write lock, not on the forged rt_mutex:

```text
pid=22848 tid=26749 D  id  rwsem_down_write_slowpath
pid=22848 tid=26715 D  id  rwsem_down_write_slowpath   (12+ threads)
```

(`hardware-notes/pselect-wedge-dstate.log`) This is the
`threadgroup_change_begin()` / `cgroup_threadgroup_rwsem` family: once the
forged PI chain drives the kernel into a threadgroup change, the process's
per-threadgroup rwsem stays write-locked, so the consumer's `sched_setattr`
never returns (`calls=1 success=0 step=33`) and every later attempt in that
process wedges the same way — exactly the deterministic signature observed
since round 1. It is therefore a security/robustness problem in the forged
`task_struct`/lock geometry for this kernel, not a race or timing issue.

### mcast stack writer cannot seed the futex waiter on this kernel

Applying the 5.15 targets' own derivation to our vmlinux:

```text
abs(greqs)  = 0x60 el0 + 0xd0 outer + (0x2e0 - 0x58) = 0x3b8
abs(waiter) = 0x60 el0 + 0xe0 outer + (0x1a0 - 0x90) = 0x250
MCAST_WAITER_OFF = abs(greqs) - abs(waiter) = 0x168
```

`0x168 + 0x50 = 0x1b8` exceeds both the kernel's `copy_from_user` length for
`MCAST_JOIN_SOURCE_GROUP` (`sizeof(struct group_source_req) = 0x104`) and the
0x108-byte stamp the writer sends, so the mcast technique (0x78 on 5.15) is not
expressible on this 5.10 build. The 0xb8 value tried in round 7 lands short of
the futex waiter (it only seeds the tail of the region), which is consistent
with the observed panic once the trigger is enabled.

Conclusion: the pselect writer is the only usable stack-writer route here, and
its remaining blocker is the forged-task geometry that wedges the threadgroup
rwsem. Deriving `PSELECT_ROUTE_NFDS` / the waiter word mapping for this kernel
(the same way `MCAST_WAITER_OFF` was derived) is the next concrete step.

### mcast stack writer: derived and reached the forgery stage (round 7)

The 5.15 Samsung targets build the app payload with `SLIDE_STACK_WRITER=1`
(mcast) and derive `MCAST_WAITER_OFF 0x78`. The same method applied to our
vmlinux gives **0xb8**:

```text
mcast: __arm64_sys_setsockopt(0x10) + __sys_setsockopt(0x70) +
       sock_common_setsockopt(0x10) + ipv6_setsockopt(0x40) +
       do_ipv6_setsockopt(0x2e0 total, greqs at sp+0x58)
       -> greqs is 0x358 below the wrapper-entry sp
futex: __arm64_sys_futex(0x70) + do_futex(0x70) +
       futex_wait_requeue_pi(0x1a0, rt_waiter at sp+0x90)
       -> rt_waiter spans 0x250..0x2a0 below the same sp
copy covers stamp offsets [0,0x104) -> waiter seeded from stamp+0xb8,
and 0xb8 + 0x50 == stamp_size (0x108).
```

Two concrete results on hardware:

1. With the q4q-inherited pselect guards still enabled, the mcast syscall
   returns exactly as designed (`ret=-1 errno=99 EADDRNOTAVAIL`) but the
   consumer trigger is skipped: `slide pselect ready=0 ... trigger skipped`
   (`hardware-notes/rmg-mcast-guards-on.log`). Those guards are pselect-only —
   none of the tested mcast targets define them.
2. With the guards gated behind `#if !defined(SLIDE_STACK_WRITER)`, the chain
   now engages and the run panics at `writer-enter`
   (`hardware-notes/rmg-mcast-guards-off-panic.log`; device rebooted clean,
   verified boot green).

So the route has moved from "deterministic stall, chain never engages"
(pselect) to "chain engages, forged stack-waiter geometry wrong" (mcast). The
next iteration is to confirm `MCAST_WAITER_OFF` against the preserved stack
bytes (or fall back to the pselect geometry) with a kernel-side trace of the
first faulting access, then re-derive.

### 32 KiB-aware P0 oracle (implemented, untested end-to-end)

The app (fresh-P0) route panics during `prepare_p0_pipe_oracle()` before it
ever samples a fingerprint page — the same pre-oracle panic the F9360 record
documents — so the oracle rows were not the cause of that panic. They were
still wrong for this device and are now fixed:

* `tools/generate_p0_fingerprint.pl` takes an optional `STEP` argument;
* `src/p0_fingerprint.h` now has 65 rows at `0x8000` steps covering
  `0x000000..0x200000`, generated with `P0_ORACLE_PROBE_OFFSET 0x200000`;
* `SLIDE_P0_OFFSET_CANDIDATES` lists all 64 candidates at `0x8000` steps, which
  is required by `select_slide_payload_slot()` for both routes.

These changes are unverified on hardware because the route that would exercise
them (app payload) panics earlier, and the supervisor route stalls earlier.

A follow-up run of the BuSung supervisor while root was already held for the
boot (sibling exploit active, SELinux permissive) ended in a **hard reboot**
with the payload's log truncated to NULs — i.e. on this firmware the supervisor
route can also panic at the fake-lock stage, not merely stall. The device came
back with verified boot green and warranty bit 0.

Tried without effect:

* q4q geometry (`FOPS_OFF 0x1000`, `LOCK_OFF 0x1350`, `W0_OFF 0x2220`) and the
  canonical 5.10 fops geometry (`0x2000`/`0x2210`/`0x2350`) — identical result;
* running immediately after a fresh boot — identical result;
* 4, 6, 12 and 16 supervisor attempts with different delays — identical result
  (deterministic, not race luck);
* `SLIDE_STACK_WRITER` is only plumbed into APP builds by the Makefile, so it
  cannot change the supervisor route.

### Root-session constraint found in round 2

The sibling root helper serves `-c` commands only until the RMG run starts; a
poller launched through it during/after the RMG attempt produces no output, so
the stalled consumer's kernel stack was not captured. `ps -AT -o
PID,TID,STAT,NAME,WCHAN` does work on this device, and `.port/work/dbg3.sh`
matches payload processes by `rmg-sup` in `/proc/<pid>/maps` and prints
per-thread `stat` + `stack`. Next attempt must either start the poller before
the helper is consumed by another command, or keep the root session in a
separate process (e.g. run the RMG supervisor as a child of the root session
rather than from `adb shell`).

Next debugging steps (in order of expected value):

1. Diff the BuSung non-app route (`fops.c` state machine, `main.c`
   `run_main_route_threads`, `util.c` page crafting) against the IonStack
   implementation that works on this handset, and against the BuSung commits
   that made the F9360/A536E supervisor route work. Our clone is `main`.
2. With root available for the boot (sibling exploit), capture `dmesg` and
   `/proc/*/stack` while the BuSung consumer thread is blocked to identify the
   exact function it is stuck in.
3. Only after the chain completes: build the matching KernelSU module and
   publish the feed entry.

### Round 11: the consumer's sched_setattr *succeeds* — it is a counter race

With an isolated trace instance (the payload truncates the global
`/sys/kernel/tracing/trace` during its own KASLR leak, which is why round-11's
first kprobe attempt recorded nothing), live kprobes show:

```text
id-32613 [001] d..1 20032.928094: ksa:     (__arm64_sys_sched_setattr+0x0/0x2c)
id-32613 [001] d..2 20033.909227: ksa_ret: (el0_svc_common+0x10c/0x214 <- __arm64_sys_sched_setattr) ret=0x0
```

* `rt_mutex_adjust_prio_chain` is entered and **returns** on every occasion
  (3 entries / 3 returns, one of them taking ~1.0 s inside the walk with
  `dN` need-resched set) — the walk is not looping.
* The consumer's `sched_setattr` **returns 0x0 (success)** after ~0.98 s.

So the forged geometry is doing its job; the exploit still logs
`calls=1 success=0 step=33` because the main thread samples the consumer
counters as the successful call lands at almost exactly the 1 s default
window. Widening `PSELECT_TIMEOUT_SEC` to 5 (`common.h` now guards it with
`#ifndef`, target sets 5) did **not** change the outcome
(`hardware-notes/pselect-window5-negative.log`), so the next step is to confirm
which timeout is actually in effect in the built binary (disassemble the
`pselect` timespec literal) and/or make the boost complete earlier — e.g. set
the per-attempt route delay to 0 and/or let the consumer burst more than
`CONSUMER_MAX_CALLS == 1`. `hardware-notes/pi-chain-kprobe-trace.log`.

### Round 12: the pselect route now engages deterministically (step 33 -> step 4)

Root cause of the long-standing `step=33` was **macro placement plus a
sampling race**, both now fixed:

1. `PSELECT_TIMEOUT_SEC` and `ROUTE_WAIT_SECONDS` had been added inside the
   `#if defined(APP_PAYLOAD)` block, so the **supervisor** build (the one that
   reaches this stage) never saw them and kept the 1 s default. Both are now
   top-level: `PSELECT_TIMEOUT_SEC 20`, `ROUTE_WAIT_SECONDS 30`.
2. kprobes show the consumer's `sched_setattr` is *released by the pselect
   timeout itself* and returns `0x0` after ~20.00 s:

   ```text
   id-6479  [001] 21025.747593: ksa:     (__arm64_sys_sched_setattr)
   id-6479  [001] 21045.744787: ksa_ret: (... ) ret=0x0        (19.997 s)
   ```

   so the main thread always sampled `success` a moment too early. `fops.c`
   now polls the consumer counters for up to `PSELECT_CONSUMER_SETTLE_MS`
   (1000 ms, new `common.h` override) after `pselect` returns.

Result: **4/4 attempts at `success=1`** (previously 0/14 across the round-11/12
sweeps), i.e. the forged PI chain is engaged deterministically. The run now
fails one layer deeper, at `try_cfi_stage` step 4:

```text
cfi misc_fops mismatch ret=0 target=ffffff802a80cd28 read=0000000000000000
                                   want=ffffff8023861180 errno=0
```

`ret=0` means the `configfs_read_file` primitive returned 0 bytes (EOF) rather
than the forged pointer, so the next work item is the `configfs_buffer` /
`CFG_*` overlay or the fops-table page layout, not the PI chain. Also settled
this round by A/B test: the canonical geometry
(`FOPS_OFF 0x2000`, `LOCK_OFF 0x2210`, `W0_OFF 0x2350`) is the one that
engages; q4q's fresh-P0 app-oracle geometry reverts to `step=33`.

### Round 13: the walk completes but never invokes a forged callable

With engagement deterministic (previous section), kprobes on the pure
entry/return counters of the intended primitives show **none of them ever
run** in a full attempt:

```text
configfs_read_file              = 0   (yet configfs_read_once() returned 0 bytes)
configfs_write_bin_file         = 0
call_usermodehelper_exec_work   = 0
process_one_work                = generic kworker noise only
```

So `ret=0`/`read=0` at step 4 is not a broken overlay: the verification read
went to the *real* ashmem fops (`ashmem_read_iter` returns 0), meaning the
PI-chain write never installed `fake_fops` into `ashmem_misc.fops`. The chain
walk returns 0 (success) without performing the write.

Page-crafting analysis found a concrete cause candidate: for
`PAGE_PAYLOAD_FOPS` the forged owner task's `pi_waiters` is written as zero,
while the slide-payload branch points it at the forged waiter -- with an empty
PI waiters tree the walk cannot descend into our objects. A target macro
(`APP_FOPS_TASK_PI_WAITERS`, default off) now gates the armed variant in
`util.c`. Enabling it **changes behaviour and panics inside the walk** (device
rebooted clean, verified boot green), i.e. the armed tree *is* traversed, but
the forged tree contents/pointers still have to be derived for this kernel.
It is therefore left off in the shipped profile; the hook stays for the next
iteration.

### Round 14: the walk traverses forged objects but still performs no write

Following round 13's finding (no forged callable ever fires), I tested the
missing half of the forged object graph, gated behind
`APP_FOPS_TASK_PI_WAITERS`:

1. **Armed PI waiters tree** (owner task's `pi_waiters` -> forged waiter):
   the walk now descends into the forged objects, but panics
   (device rebooted clean, verified boot green).
2. **Plus forged task as the waiter's `task`** (instead of the real
   `init_task`, mirroring the slide-payload branch): the panic disappears and
   the run returns to `success=1 step=4` cleanly - so the panic was the walk
   reaching real kernel state; with the forged task it stays inside the page.
3. **Plus `pi_blocked_on` pointing at `install_target - offsetof(pi_tree)`**,
   so the node the walk enqueues *is* the install address and
   `rb_link_node()`'s `node->__rb_parent_color = parent` would land
   `fake_fops` in `ashmem_misc.fops`: still `success=1 step=4`, no panic, and
   `configfs_read_file` is still never called.

Verified along the way from the kernel's own disassembly that our layout
constants are the ones the walk uses: `add ..., #0x18` (waiter `pi_tree`),
`#0x880` (task `pi_waiters`), `#0x86c` (task `pi_lock`), `#0x40` (waiter
`prio`) all appear in `rt_mutex_adjust_prio_chain` /
`mark_wakeup_next_waiter`. So the layouts are right; the missing piece is the
exact node/edge shape the walk needs for the write, which needs argument-level
tracing (`$arg1`/`$arg2` of `rt_mutex_adjust_prio_chain` compared against
`fake_w0` / `fake_lock` / `fake_task` / the install target) rather than more
parameter guessing.

All three variants are reverted (macro default off); the verified profile is
the one that engages deterministically and reaches step 4.

### Round 15: argument-level trace of the walk (and the real missing mapping)

Capturing the walk's arguments on the engaged build (page base
`0xffffff802afb8000`, `fake_fops` = `want` = base+0x1180) gives:

```text
tb : task_blocks_on_rt_mutex   w1=0xffffff8788026810 (real rt_mutex) w2=0xffffffc0490c3c60
rma: rt_mutex_adjust_prio_chain w1=<real task> ... w3=0x0 w4=0xffffff802afb9390
```

Two decisive facts:

1. **This kernel's signature is 6-argument**
   (`rt_mutex_adjust_prio_chain(task, chwalk, orig_lock, next_lock, orig_waiter,
   top_task)`, `kernel/locking/rtmutex.c:448`), so the captured `w3=0` is
   `orig_lock = NULL` and `w4 = base+0x1390` is **`next_lock` = our forged
   lock** (`LOCK_OFF 0x2210` minus the `SKB_DATA_DELTA = -0xe80` bias). The
   walk is therefore reached through the pre-requeue path with our fake lock.
2. `orig_lock == NULL` selects `rt_mutex_adjust_pi()`, and the body then does
   `rt_mutex_dequeue(lock = next_lock, waiter = orig_waiter)` — i.e.
   `rb_erase(&real_waiter->tree_entry, &fake_lock->waiters)`. The write
   therefore depends on the **real waiter's `tree_entry` bytes**, which live on
   the pselect thread's stack where our fd_set words land. With the derived
   mapping (`fdset word 4+w == waiter word w`), waiter words 0..2
   (`tree_entry.parent/left/right`) are fdset words 4..6 = **`out[0..2]`** —
   and `prepare_pselect_fdsets()` zeroes `out` and instead writes `fake_w0`
   into `in[0]`, which maps to waiter word **-4** (before the struct). The
   tree-entry bytes the kernel erases are therefore all zero.

That is the concrete missing mapping: the fake waiter's tree links must be
placed at the fdset words that actually overlap them (`out[0..2]` for this
kernel), not at `in[0..3]`, and their values are what steer `rb_erase`'s
pointer writes. A test that simply moved the forged waiter onto the fops table
(`W0_OFF == FOPS_OFF`) was negative (still `success=1 step=4`, device healthy),
consistent with this analysis; it is reverted.

### Round 16: the fd_set link words, and why guessed values fail

Round 15 established the mapping `fdset word 4+w == waiter word w`. That pins
every field of the 5.10 `rt_mutex_waiter` the pselect writer must forge:

| waiter field | offset | waiter word | fd_set word | real fd_set slot |
| --- | ---: | ---: | ---: | --- |
| `tree_entry.parent` | 0x00 | 0 | 4 | `in[4]` |
| `tree_entry.right` | 0x08 | 1 | 5 | `out[0]` |
| `tree_entry.left` | 0x10 | 2 | 6 | `out[1]` |
| `pi_tree_entry.parent` | 0x18 | 3 | 7 | `out[2]` |
| `pi_tree_entry.right` | 0x20 | 4 | 8 | `out[3]` |
| `pi_tree_entry.left` | 0x28 | 5 | 9 | `out[4]` |
| `task` | 0x30 | 6 | 10 | `ex[0]` |
| `lock` | 0x38 | 7 | 11 | `ex[1]` |
| `prio` | 0x40 | 8 | 12 | `ex[2]` |
| `deadline` | 0x48 | 9 | 13 | `ex[3]` |

`prepare_pselect_fdsets()` populates only the last four; the six link words are
zero, which is why `rb_erase()`/`rb_link_node()` have no attacker-controlled
parent pointer to write through. I added a gated `PSELECT_FDSET_LINKS` path
that copies the page-side waiter's own link values into them
(`in[4]=1`, `out[0..1]=0`, `out[2]=fake_fops`,
`out[3]=data_addr(ASHMEM_MISC_FOPS)`, `out[4]=0`) plus a companion change so
`open_selected_fds()` does not arm fds from the write set (whose words now
carry links, not watched fds). Results on hardware:

* write set armed + links -> `success=0 step=33` (pselect returns immediately,
  route loses engagement);
* links, write set not armed -> `success=0 step=33` still, and even with the
  settle raised to 25 s the consumer's `sched_setattr` never returns 0: the walk
  no longer completes.

So those link values are wrong -- they must be **derived**, exactly like
`MCAST_WAITER_OFF` and the fdset shift were. Recommended next step: from
`rb_erase()` / `__rb_change_child()` / `rb_link_node()` in our own vmlinux,
work out which parent/child store can land `fake_fops` in
`ashmem_misc.fops`, and set the waiter's link words accordingly. The macro is
left off; all experiments are reverted and the profile is back to the verified
state (canonical geometry, 20 s window, 1 s settle).

### Round 17: the missing piece was the arming, not the link values

The repo already contains a reference writer for this exact technique:
`prepare_slide_pselect_fdsets()` in `src/slide_app.c`. Porting it verbatim
(gated as `PSELECT_FDSET_LINKS`) fixed the round-16 dead end:

| waiter word | value (reference) | fd_set slot |
| ---: | --- | --- |
| 0 `tree_pc` | `slide_oracle_parent` (= `fake_fops`) | `in[4]` |
| 1 `tree_right` | 0 | `out[0]` |
| 2 `tree_left` | `slide_oracle_target` (= `data_addr(ASHMEM_MISC_FOPS)`) | `out[1]` |
| 3 `pi_pc` | `slide_oracle_parent` | `out[2]` |
| 4 `pi_right` | 0 | `out[3]` |
| 5 `pi_left` | `slide_oracle_target` | `out[4]` |

The critical companion change is **how the fds are armed**. The non-app route
`dup2()`s the pipe *write* end over every set bit, so populating the link words
made pselect return immediately (`success=0`). The reference instead blocks on
a **timerfd** (`open_slide_selected_fds()` + `slide_pselect_stack_copy()`), which
is never readable until it expires. Mirroring that keeps the route engaged:

* links + write-end arming -> `success=0 step=33` (immediate return)
* links + read-end arming -> `success=0 step=33` (pipe already readable)
* links + **timerfd** arming -> `success=1 step=4` (engaged, stable over 2/2
  attempts; `hardware-notes/fdset-links-timerfd.log`)

So the round-16 conclusion ("the link values are wrong") was itself wrong: the
values are the reference's and are now in place. The walk still does not write
(`configfs_read_file`/`configfs_write_bin_file` remain uncalled). Combining
this with the armed PI tree (`APP_FOPS_TASK_PI_WAITERS`) also stays at
`success=1 step=4` without panicking
(`hardware-notes/armed-pi-with-links.log`), so that macro stays off.

Next step remains the derivation: with `orig_lock = NULL` and
`next_lock = fake_lock`, the body runs `rt_mutex_dequeue(fake_lock, real_waiter)`
= `rb_erase(&real_waiter->tree_entry, &fake_lock->waiters)`; work out from
`rb_erase()`/`__rb_change_child()`/`rb_link_node()` in our vmlinux which of the
now-populated link fields must be retargeted so that the parent store lands
`fake_fops` in `ashmem_misc.fops`.

### Round 18: why the link fields never get read (MIN_CHAINWALK)

The kernel source answers the remaining question. `rt_mutex_adjust_pi()` calls

```c
rt_mutex_adjust_prio_chain(task, RT_MUTEX_MIN_CHAINWALK, NULL,
                           next_lock, NULL, task);      /* rtmutex.c:1143 */
```

and the chain body only performs the requeue (step `[7]`,
`rt_mutex_dequeue(lock, waiter)` -> `rb_erase`) when `requeue` is still true.
`requeue` is cleared at `rtmutex.c:558/573`, and for `MIN_CHAINWALK`
(`detect_deadlock == false`) the function instead takes `goto out_unlock_pi`
when the waiter is not the task's top PI waiter or when
`rt_mutex_waiter_equal(waiter, task_to_waiter(task))`. So on this path the
dequeue - and therefore the `rb_erase` store that reads the waiter's
`tree_entry` link fields - is **skipped**, which is why populating those words
(round 17) and even steering them at the install target (round 18) changes
nothing observable. A probe on `rb_erase` during an engaged run confirms the
function is busy in the system (302k calls) but the walk only enters
`rt_mutex_adjust_prio_chain` 251 times, and the write never lands.

Two targeted experiments this round, both negative (no panic, device healthy,
`success=1 step=4`):

* install-targeted links (`tree_pc = data_addr(ASHMEM_MISC_FOPS) - 16`,
  `tree_left = fake_fops`, derived from `__rb_change_child()`'s store);
* the forged waiter placed on the fops table (`W0_OFF == FOPS_OFF`) *with* the
  round-17 links + timerfd arming (the round-15 version of this test predated
  the arming fix and was invalid).

Both are reverted; the profile carries the reference link table from
`prepare_slide_pselect_fdsets()` and the canonical geometry.

Consequence for the port: the missing write is **not** reachable through the
MIN_CHAINWALK `[7]` path. The remaining candidates are the enqueue side
(`rb_link_node()`'s `*link = node` in `task_blocks_on_rt_mutex`, which stores a
*node address* - so the forged node would have to live at `fake_fops`, tested
here and still negative) or a different step of the chain that performs a
pointer store. Deriving which store the reference targets on the *device
tested* 5.15 builds (`dm3q`/`gts9u`, where this route is hardware-verified) and
comparing the two kernels' `rtmutex.c` bodies is the next concrete step.

### Round 19: the forged lock only ever gets a MIN chain walk

Tracing `rt_mutex_adjust_prio_chain`'s `chwalk` argument during an engaged run
(page base `0xffffff8020c00000`) shows the decisive split:

```text
w2=0x1 w3=0xffffff8040c69a10 w4=0xffffff8040c69310 w5=0xffffffc04acbbc60   <- FULL, real futex
w2=0x0 w3=0x0                w4=0xffffff8020c01390 w5=0x0                  <- MIN, our fake lock
```

The **only** call carrying our forged lock (`w4` = base+0x1390 = `LOCK_OFF`) is
the MIN walk from `rt_mutex_adjust_pi()` (`orig_lock = NULL`, `orig_waiter =
NULL`). The FULL walks that would execute step `[7]`'s
`rt_mutex_dequeue(...)` -> `rb_erase(...)` belong to unrelated kernel futexes.
Since `[7]` is skipped for MIN walks, the link fields of the waiter - both the
stack waiter's (fd_set words, round 17) and the page waiter's (round 19) - are
**never read**, which is why every construction tried so far is inert, and why
the fops install never happens.

Round-19 experiment (reverted): place the forged waiter so its `pi_tree` lands
on the fops table (`W0_OFF = FOPS_OFF - 0x18 = 0x1fe8`) and set the forged
task's `pi_waiters` root to `data_addr(ASHMEM_MISC_FOPS) - 8`, so that a PI-tree
link would store `fake_fops`. Result: `success=1 step=4`, no panic, no write -
exactly as predicted by the MIN-walk finding.

**Consequence for the port:** this is a *trigger* problem, not a data problem.
The consumer's `sched_setattr` must drive a **FULL** chain walk over the forged
lock (for example by having the forged lock's owner contend on a second real PI
futex so the chain continues past the MIN limit, or by using a trigger whose
path passes `RT_MUTEX_FULL_CHAINWALK`). Changing that path in `fops.c`/`main.c`
is the next step; no further page/fd_set value changes will help until the walk
is full.

### Round 20: the walk receives our lock but bails before the dequeue

Refining round 19 with argument-level probes:

* `rb_erase` runs constantly (180k calls, 16k in the payload process) but the
  forged page's address prefix (`ffffff8020f`, page base `ffffff8020f38000`)
  appears **zero** times in the trace - the walk never erases from our forged
  lock's tree.
* `rt_mutex_adjust_prio_chain` does receive our forged lock (`w4` =
  base+0x1390 = `LOCK_OFF`, `w3 = orig_lock = NULL`, `w5 = orig_waiter = NULL`,
  i.e. the `rt_mutex_adjust_pi()` call), yet no store ever lands on the target.

Source reading explains the gap. `detect_deadlock` is false for
`RT_MUTEX_MIN_CHAINWALK` (`rtmutex.c:530-545`: true only for
`FULL_CHAINWALK`, or with `CONFIG_DEBUG_RT_MUTEXES`, which this kernel has
**off**). The only `RT_MUTEX_FULL_CHAINWALK` caller in the whole kernel is
`__rt_mutex_start_proxy_lock()` ("We enforce deadlock detection for futexes"),
i.e. the PI-futex proxy/requeue path - and it receives the lock from
`q->pi_state->pi_mutex`, a *real* futex's mutex.

The route's choreography (`main.c`) explains why our lock never sees it: the
owner thread's `FUTEX_CMP_REQUEUE_PI` (which triggers that proxy path, and the
only FULL walks we observe) runs **before** `do_pselect_fake_lock_route()`
overwrites the waiter's stack fields; once it has run, the waiter is no longer
queued, and every later walk over the forged lock is a MIN walk that returns
without the requeue step.

So the remaining gap is architectural, not a data value: the forged-lock FULL
walk required by this primitive cannot occur with the current ordering.

Next options, in order of tractability:

1. Reorder the route so the proxy/requeue path uses the forged lock (the
   pselect fd-set overwrite must be in place when the kernel reads
   `q->pi_state`/`q->rt_waiter` - e.g. re-block and requeue after the
   overwrite, or drive the requeue from the waiter side).
2. Port the sibling project's approach, which roots this device with a
   **32-bit (armeabi-v7a) stage** rather than an in-process pselect route
   (`src/main.c` in `sarabpal-dev/IonStack-S22U`: "This replaces the in-process
   pselect route: there is no pselect"); its compat `select` path has a
   different stack layout.
3. Compare against the F9360 profile, which is the same 5.10.236 branch and was
   device-verified with this code - its kernel config/runtime may reach a FULL
   walk we have not reproduced.

## KernelSU module (built and statically audited)

Built this round from the exact Samsung 5.10.236 tree with the device's own
`clang-r416183b`:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernelsu/android12-5.10_kernelsu-S901U1UESAGZF3-kdp.ko` | 353,104 | `3609e3bfb38fedf58b757c1fb01934d0640d87434e1232e517b0844d8d6bf9e6` |
| `kernelsu/ksud-S901U1UESAGZF3-kdp` | 4,810,920 | `10a5dc32299a584f2c6950423131b0e3e6ee1d481748a29ecf170ee99a19d828` |

Verified properties:

* vermagic `5.10.236-android12-9-31998796-abS901U1UESAGZF3 SMP preempt mod_unload modversions aarch64`;
* zero-length `__versions`, `.symtab`/`.strtab` retained (manual-relocation contract);
* audit against the recovered target `vmlinux.elf`: 198 undefined symbols,
  **0 missing from the target symbol table, 0 CRC mismatches**;
* `ksud` embeds the KO as `android12-5.10_kernelsu.ko` (KernelSU v3.2.5,
  version code 32525) with the Samsung KDP/RKP/DEFEX patch applied and
  `CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y`.

On-device status: **loaded and live on the connected SM-S901U1.**

```text
[ksu-load] kallsyms entries: 300883
[ksu-load] resolved=198 unresolved=0
[ksu-load] init_module ret=0 errno=0 (Success)
uid=0(root) gid=0(root) groups=0(root) context=u:r:ksu:s0
kernelsu 200704 0 - Live 0xffffffc0035ca000 (O)
```

The calling process ended up in `u:r:ksu:s0`, i.e. KernelSU's own domain, so
its SELinux hooks are active. `ksuload3.log` and the `/proc/modules` line are
preserved in `kernelsu/ksu-load-device.log`.

### Why ksud cannot be used here (and what replaced it)

Samsung **DEFEX** kills any privileged process that executes a helper outside
its allowlist (`security/samsung/defex_lsm/defex_rules.c` lists `/system/bin/*`,
never `/data/local/tmp`):

```text
[DEFEX] Safeplace violation [task=sh (/system/bin/sh), child=/data/local/tmp/ksu-load, uid=0]
```

That is why both `ksud late-load` and a standalone loader binary died with
SIGKILL (137) when run from `/data/local/tmp`. The working approach is the same
one the payloads use: preload a shared object into an allowlisted executable,
so DEFEX only sees `/system/bin/id`:

```sh
adb shell "/data/local/tmp/cve-2026-43499-root -c \
  'KSU_MODULE=/data/local/tmp/kernelsu-rmg.ko \
   LD_PRELOAD=/data/local/tmp/ksu-load.so /system/bin/id'"
```

`kernelsu/ksu-load.so` (source `kernelsu/ksu-load-lib.c`) is a ~7.7 KB
constructor shared object that:

1. writes `0` to `/proc/sys/kernel/kptr_restrict` (it is `2` on this device, so
   `/proc/kallsyms` reports zero addresses even to root);
2. builds a kallsyms name→address table (300,883 entries);
3. rewrites every `SHN_UNDEF` symbol in the KO's `.symtab` into an absolute
   (`SHN_ABS`) symbol holding the kallsyms address;
4. calls `init_module()` with the patched image, letting the kernel apply the
   module's own relocations (CALL26/JUMP26/ADR_PREL_PG_HI21/ADD_ABS_LO12_NC/
   LDST*_ABS_LO12_NC/PREL32/PREL64/ABS64 — 11 types, 5,282 relocations).

This is the "manual relocation loader" route the F9360 record describes, and it
removes the dependency on `ksud` entirely for this target.

### Build recipe (reproducible)

```sh
# tree + toolchain
git clone --depth 1 https://github.com/mirvora/samsung-r0q-resukisu-kernel
#   ships prebuilts-master/clang/host/linux-x86/clang-r416183b (device's clang 12.0.5)
cp target.config kernel_platform/common/out/.config
echo "kernel_platform/common/security/baseband-guard/Kconfig"  # stub: mirror omits it
# repoint CONFIG_UNUSED_KSYMS_WHITELIST at a local (empty) file
make O=out ARCH=arm64 LLVM=1 LLVM_IAS=1 olddefconfig modules_prepare
# host has no bc: supply a shim that emits include/generated/timeconst.h (HZ=250)
out/scripts/selinux/genheaders/genheaders out/security/selinux/flask.h out/security/selinux/av_permissions.h
echo 5.10.236-android12-9-31998796-abS901U1UESAGZF3 > out/include/config/kernel.release
make O=out M=<KernelSU>/kernel src=<KernelSU>/kernel ARCH=arm64 LLVM=1 \
  CONFIG_KSU=m CONFIG_KSU_SAMSUNG_KDP=y CONFIG_KSU_SAMSUNG_RKP=y \
  CONFIG_KSU_SAMSUNG_DEFEX=y CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y \
  KBUILD_MODPOST_WARN=1 modules
llvm-strip -d kernelsu.ko
```

`ksud` needs KernelSU v3.2.5 (commit `b0bc817`) with the Samsung patch, and
three dependency URLs that have since moved: `KernelSU2/{adb_client,
java-properties,ksu_props}` (the old `Kernel-SU/*` org 404s), plus crates.io
`rustix 0.38.34` for `ksuinit`. Built for `aarch64-linux-android` with an
official rustc 1.98.1 sysroot assembled manually (no rustup on the host).

## Original KernelSU notes (superseded, kept for provenance)

Exact vermagic required:

```text
5.10.236-android12-9-31998796-abS901U1UESAGZF3 SMP preempt mod_unload modversions aarch64
```

Facts established for the module build:

* `CONFIG_LTO_CLANG=y` (full, not thin), `CONFIG_CFI_CLANG=y`,
  `CONFIG_KASAN_HW_TAGS=y`, `CONFIG_ARM64_MTE=y`, `CONFIG_MODVERSIONS=y`,
  `CONFIG_TRIM_UNUSED_KSYMS=y`, `CONFIG_MODULE_FORCE_LOAD` unset;
* `kdp_assign_pgd`, `kdp_usecount_dec_and_test`, `prepare_ro_creds`,
  `get_task_creds`, `set_task_creds`, `task_defex_enforce`, `sys_call_table`,
  `__arm64_sys_ni_syscall`, `__arm64_sys_setresuid` exist in the target image
  but are **not** exported, so the empty-`__versions` + kallsyms manual
  relocation path is mandatory (same recipe as `a15-A155NKSS6BYH1`);
* `LOCALVERSION` can force the vermagic via Samsung's `sec_scm_version()`
  (`-31998796-abS901U1UESAGZF3`, KMI generation 9);
* candidate source trees: Samsung OSR `uploadId=13620`
  (`SM-S901E_16_Opensource.zip` + AGZF3 delta + U1 DTS, captcha-gated), or the
  public full `kernel_platform` mirror
  `mirvora/samsung-r0q-resukisu-kernel` (5.10.236, GZE3/GZB6-era); the
  published `kernelsu-android12-5.10.ko` from `IonStack-S22U` is 5.10.245 and
  not loadable.

## Reproduce

```sh
# scratch tree used for this port lives outside version control
#   <workspace>/.port/fw/S901U1UESAGZF3_CHA.zip     downloaded firmware
#   <workspace>/.port/work/kernel.bin               raw Image
#   <workspace>/.port/payloads                      payloads clone (+ this target)

perl tools/generate_p0_fingerprint.pl kernel.bin 0x1f0000 p0_fingerprint.h
make TARGET=r0q-S901U1UESAGZF3 release
adb push artifacts/cve-2026-43499-app.so /data/local/tmp/
adb shell "CVE43499_ROOT_HELPER=/data/local/tmp/cve-2026-43499-root \
  LD_PRELOAD=/data/local/tmp/cve-2026-43499-app.so /system/bin/id"
```

Use only on devices you own or are explicitly authorized to test.

## Round 21 — stack-writer flavor audit

`SLIDE_STACK_WRITER` selects a *writer family*, not a boolean:

| value | meaning | used by |
| --- | --- | --- |
| `1` (`SLIDE_STACK_WRITER_MCAST`) | mcast writer | every profile in `Makefile` (dm1q/dm2q/dm3q/gts9/gts9u) **and our r0q build** |
| `2` (`SLIDE_STACK_WRITER_SIGRETURN`) | sigreturn writer | **no profile**; needs `SIGRETURN_FPSIMD_WAITER_OFF`/`SIGRETURN_SVE_WAITER_OFF`, which upstream marks "not yet derived" (gts9u ships S918B placeholders) |
| undefined | 5.10 default route | q4q-F9360, pa2q-S9360, a53x |

Device experiments this round (r0q, S901U1UESAGZF3, 2 attempts each):

| configuration | result |
| --- | --- |
| mcast(=1), links on, guards on | `success=1 step=4` |
| mcast(=1), links on, guards **off** | `success=1 step=4` |
| mcast(=1), links **off**, guards off | `success=1 step=4` |
| sigreturn(=2), placeholder offsets | `success=1 step=4` |
| no writer (default) | **panic** after `kernel page prepare mode=0`; device reboots clean, verified boot intact |

`step=4` is the *first* check in `try_cfi_stage()` (`fops.c:420`):
`*(ASHMEM_MISC_FOPS) != fake_fops`, reported as
`cfi misc_fops mismatch ret=0 target=ffffff802a71cd28 read=0000000000000000`.
The forged-lock write never lands, and `pipe physrw ... read_ok=0 write_ok=0 rw64=0/0`
confirms the physrw stage never comes up either.

**Correction (supersedes an earlier note in this file):** the sibling
`sarabpal-dev/IonStack-S22U` `src/targets/S901U1UESAGZF3/target.h` is **not**
byte-identical to ours. Its forged-page geometry is
`LOCK 0x1350 / W0 0x2220 / FOPS 0x1000` (identical to our q4q values); ours is
`0x2210 / 0x2350 / 0x2000`. Matching values: `ASHMEM_*_OFF`, `P0_*`,
`MM_STRUCT_SZ 960`, `KSNITCH_COLLISIONS 6`, `VMEMMAP_START`, all `FOPS_*` slots.
`MM_CPU_PARTIAL` is dead code (defined in q4q/sibling headers, referenced nowhere in `src/`).

The sibling's **proven** route for this exact firmware is a 32-bit
(armeabi-v7a) compat stage: `src/api.c` embeds and execs `cve-exp32`; the
Makefile states "exp32 MUST stay 32-bit: the stack-stamp only lines up via the
compat syscall path". Stage size: `exp32/main.c` 335 + `exp32/stack.c` 114 +
`exp32_blob.S` 12 + `api.c` 138 lines. Prebuilt outputs for our firmware exist
at `build/S901U1UESAGZF3/bin/{cve-2026-43499,cve-exp32,cve-2026-43499-root}`
and `build/embed/cve_exp32_arm32`.

## Round 22 — BREAKTHROUGH: the proven exp32 (32-bit compat) route roots this handset

Following the round-21 audit, the sibling's proven stage was run on the connected
S901U1UESAGZF3 and **it roots the device**:

```
build config ... label=b0q_taro_v5.10 slide=pselect main=exp32
exp32 payload fake_fops=ffffff8038148180 fake_lock=ffffff80381484d0 write_target=ffffffc00a834d28
install_embedded_exp32: /data/local/tmp/cve-exp32 (1644984 bytes)
stamp probe: rc=-1 errno=99 — late validation, copy done (stamp lands)
consumer: trigger ret=0 errno=22 (cfs-nice)
cfi write ret=35 errno=0
cfi read ret=35 errno=0
root umh result wake=1 complete=1 retval=0 socket=1
```
then `ion-root -c id` -> `uid=0(root) gid=0(root) context=u:r:kernel:s0`.

KernelSU (this port's own artifact) on the same boot, as root:
`LD_PRELOAD=/data/local/tmp/ksu-load.so /system/bin/id` ->
`[ksu-load] kallsyms entries: 300883 / resolved=198 unresolved=0 /
init_module ret=0 errno=0 (Success)` (module live: `kernelsu` in /proc/modules).

### Why exp32 succeeds where the 64-bit writer stops at step 4

The 32-bit child does the whole choreography itself:
1. waiter blocks in `FUTEX_WAIT_REQUEUE_PI` (allocates `rt_mutex_waiter` on its
   own kernel stack) and becomes PI owner of `pi_chain`;
2. main fires `FUTEX_CMP_REQUEUE_PI`, which runs the **only**
   `RT_MUTEX_FULL_CHAINWALK` caller, `rt_mutex_start_proxy_lock()`, and cleans
   the *wrong* thread's `pi_blocked_on` -> the waiter stays dangling;
3. the waiter stamps its own kernel stack with the 128-byte payload via a
   **compat** `MCAST_JOIN_SOURCE_GROUP` (`compat_group_source_req` is 260 bytes
   packed; native is 264 and rejects optlen=260) - payload lands at
   buffer+0x58 (`EXP32_STAMP_OFF`);
4. a consumer thread calls `sched_setattr` on the waiter -> `rt_mutex_adjust_pi`
   -> `rt_mutex_adjust_prio_chain` step [7] `rt_mutex_dequeue(lock, waiter)`
   -> `rb_erase_cached(&waiter->tree_entry, &fake_lock->waiters)`
   -> `rb_set_parent(child=rb_left, parent=pc&~3)` writes
   `*ASHMEM_MISC_FOPS = fake_fops`.

No pselect return-writeback, no `fake_w0`, no MIN-walk limitation. The leak is
unchanged (`slide=pselect`, i.e. the same tracefs KASLR leak this port fixed
for the 32 KiB slide granularity).

### Data delta between the proven config and our port

Only the three geometry values differ; every other page-layout constant
(`FAKE_TASK_*`, `FOPS_TABLE_OFF`, `SCRATCH_OFF`, `SKB_*`, `MM_*`, `FOPS_*` slot
offsets) already matches:

| macro | proven | our round-20 port |
| --- | --- | --- |
| `LOCK_OFF` | `0x1350` | `0x2210` |
| `W0_OFF` | `0x2220` | `0x2350` |
| `FOPS_OFF` | `0x1000` | `0x2000` |

### Vendored into `.port/payloads` this round

`src/api.c`, `src/exp32/main.c`, `src/exp32/stack.c`, `src/exp32_blob.S`
(`.incbin prebuilt/cve_exp32_arm32`), `prebuilt/cve_exp32_arm32` (1644984 B).
Source: `sarabpal-dev/IonStack-S22U`, itself forked from
`BuSung-dev/CVE-2026-43499-S25U` (Apache-2.0) - attribution to be added.

Still to do: adopt the three geometry values + `PAGE_PAYLOAD_EXP32` branch in
`util.c`, hook `doreplacefops()` into `main.c` for `TARGET=r0q`, wire `api.c` +
the blob into the r0q build, rebuild, clean-room verify, refresh artifacts and
the feed entry.

### Round 22 (cont.) — exp32 wired into our tree; remaining delta isolated to the reclaim path

Wired into `.port/payloads` for `TARGET=r0q-S901U1UESAGZF3`:
* `common.h`: `PAGE_PAYLOAD_EXP32 2` (gated on `APP_EXP32_ROUTE`)
* `util.c`: `IS_FOPS_MODE()` normalisation of the 8 FOPS mode checks + a ~35-line
  `PAGE_PAYLOAD_EXP32` payload branch in `prepare_skb_payload()`
* `main.c`: exp32 route (`build_exp_buffer_fops` / `doreplacefops` /
  `cfi_thread` / `run_main_route_threads`) under `APP_EXP32_ROUTE`, old
  non-app route gated off
* `target.h`: proven geometry `LOCK_OFF 0x1350 / W0_OFF 0x2220 / FOPS_OFF 0x1000`
  and `#define APP_EXP32_ROUTE 1`
* `Makefile`: `PRELOAD_SRCS += src/api.c src/exp32_blob.S` for r0q

Gotcha worth remembering: `APP_EXP32_ROUTE` must be at **file scope** in
`target.h`. The first insertion landed inside the `#if defined(APP_PAYLOAD)`
block (opens line 345, closes 384), so the supervisor build never saw it and
silently ran the old pselect route.

Built supervisor (1755872 B) now runs the exp32 route on device: the 32-bit
child launches, the compat stamp lands, the consumer triggers, and
`write_target=ffffffc00a834d28` is byte-identical to the proven sibling run -
**but the rb write still does not land** (`cfi misc_fops mismatch ... read=0`).

Log comparison with the proven run isolates the remaining delta to the page
forge, not the exp32 stage:

| | proven sibling run | our port |
| --- | --- | --- |
| reclaim | `mmm skb reclaim sends=24/24 (pairs=8 hold)` | `sk_buff reclaim sends=4/4 mode=2` |
| config | `label=b0q_taro_v5.10 slide=pselect main=exp32` | `... stack_writer=pselect reclaim=legacy fops=bank pipe=before-fops` |

The sibling's `util.c` contains an **`order3_hold`** subsystem our `util.c`
lacks entirely: `order3_hold_sv[ORDER3_HOLD_PAIRS_MAX][2]`,
`order3_hold_pairs`, `order3_hold_inited`, `order3_hold_begin()`,
`order3_hold_end()` - it holds 8 socket pairs (24 order-3 pages) so the
reclaimed page stays captured while the payload is stamped. Our port's
`fops=bank` / `reclaim=legacy` path sends only 4 and therefore never places the
forged `fake_lock`/`fake_task`/`fake_fops` page where the chain walk reads it.

Next step: adopt the sibling's reclaim implementation (or vendor its `util.c`
as the r0q override) so the page forge reports `sends=24/24 (pairs=8 hold)`,
then re-run; the exp32 write should then land and `try_cfi_stage()` progress
past `cfi write ret=35`.

## Round 23 — vendored tree builds and reproduces the proven run

The proven reclaim implementation (`order3_hold`) is not a parameter of the shared
code our tree carries (`SKB_RECLAIM_SENDS 4`, `reclaim=legacy`/`fops=bank`), so
instead of merging it, the sibling's coherent source unit is vendored as an
**isolated** build target (nothing else in the repo is touched):

* `src/targets/r0q-S901U1UESAGZF3/vendor/{main.c,util.c,slide.c,fops.c,pipe.c,root.c,preload.c,api.c,exp32_blob.S,common.h,target.h,kernelsnitch/,exp32/}`
* `prebuilt/cve_exp32_arm32` (sha256 `1b4f5171c6fdef6e22947d75734b2cef94e075e42e840bb5e363a768b818932b`,
  byte-identical to the proven embed)
* `make TARGET=r0q-S901U1UESAGZF3 vendor-r0q` -> `build/r0q-S901U1UESAGZF3/cve-2026-43499-vendor`
  (1749352 B, same size as the proven binary)
* `vendor/offset.h` must **not** be vendored: a quoted `#include "offset.h"` in the
  vendored `common.h` resolves next to itself and breaks `TARGET_HEADER`.

On a fresh boot this build reproduces the proven behaviour exactly:

```
mmm skb reclaim sends=24/24 (pairs=8 hold)
exp32 payload fake_fops=ffffff8033588180 fake_lock=ffffff80335884d0 write_target=ffffffc00a7dcd28
stamp probe: rc=-1 errno=99 - late validation, copy done (stamp lands)
cfi write ret=35 errno=0
root umh selinux enforcing byte 1 -> 0 (addr=ffffff802a9bdcd8)
```

Two operational findings:

1. **The root helper path is hardcoded** to `/data/local/tmp/cve-2026-43499-root`
   (the sibling's own `cve-2026-43499-root` binary). Pushed under any other name
   the root UMH fails with `helper stat failed errno=2` and `socket=0`, even
   though the fops write already landed.
2. **One exploit run per boot.** Repeated runs inside a single boot panic the
   kernel (observed: device dropped mid-run while still printing `sends=24/24`).
   Every validation must reboot first.

After fixing the helper path the device went offline (USB enumerates as
`04e8:6860` MTP, adb list empty - `adbd` not available), so device validation is
paused pending the handset being reachable again.

### Round 23 (cont.) — the write is deterministic; the physrw/root stage is probabilistic

Two clean-boot runs of our vendored build both reached the hard step:

```
mmm skb reclaim sends=24/24 (pairs=8 hold)
cfi write ret=35 errno=0            <- forged-lock write landed
root umh selinux enforcing byte 1 -> 0
```

The second run then died in the **physrw/pipe stage** (`phys step read64 done ok=0
value=6161616161616161`, then a repeating `phys gate caches ... base=...` loop
until the kernel panicked). The first run reached the root UMH but failed only
because the helper was not at the hardcoded
`/data/local/tmp/cve-2026-43499-root` (`helper stat failed errno=2`, `socket=0`).

This matches the sibling's own track record on this handset: root lands about
**1 attempt in 16**, and failed attempts can panic. Validation therefore needs
repeated fresh boots, not a single run. There is confirming evidence from a
successful boot on this device: after one such run `getenforce` reported
`Permissive` (the exploit had flipped SELinux), the `temp_su.sock` existed as
root, `ion-root -c id` returned `uid=0(root) ... u:r:kernel:s0`, and the
port's KernelSU module loaded with `resolved=198 unresolved=0 init_module ret=0`.

## Round 23 — deliverables state (offline wrap-up)

Working, device-validated configuration is staged:

* `vendor-r0q/` — the vendored exp32 payload unit (sources, proven `target.h`,
  prebuilt 32-bit stage, `NOTICE` with attribution).
* `artifacts/cve-2026-43499` — supervisor built from it by our own Makefile
  (`make TARGET=r0q-S901U1UESAGZF3 vendor-r0q`); on device it reaches
  `cfi write ret=35`.
* `artifacts/cve-2026-43499-root`, `artifacts/cve-exp32-arm32` (sha256
  `1b4f5171…`), `artifacts/cve-2026-43499-app.so` (old route, **not** validated).
* KernelSU: `kernelsu/android12-5.10_kernelsu-S901U1UESAGZF3-kdp.ko` +
  `ksu-load.so` + `ksud-S901U1UESAGZF3-kdp` — module load validated on device
  (`resolved=198 unresolved=0 init_module ret=0`).
* `feed-entry.json` now carries `status: experimental` and explicit notes.

Remaining before this can be published as a supported target:

1. `apply.sh` still ships the pre-exp32 payload/patches — it must be updated to
   install `vendor-r0q/`, the `vendor-r0q` Makefile rule, the vendored artifacts
   and the updated target data, then re-run the clean-room check
   (fresh clone + apply.sh + `make TARGET=r0q-S901U1UESAGZF3 all release`).
2. The in-app payload path (`cve-2026-43499-app.so`, `APP_PAYLOAD`) still uses
   the old route and does not root this handset; the validated route is the
   preloadable supervisor. The app interface needs to be re-based on the exp32
   stage (or the feed must ship the supervisor invocation) before claiming support.
3. On-device validation of a *clean* end-to-end run (exploit → root → KernelSU)
   in a single boot, repeated across fresh boots to absorb the ~1-in-16 odds.
4. Device hygiene: this session disabled SELinux enforcing and late-loaded a
   self-built KernelSU module during runs, which is the likely cause of the
   reported System UI crash; a reboot restores enforcing/clean state (verified:
   warranty bit 0, bootloader locked, verified boot green).

### Round 24 — apply.sh extended; clean-room acceptance passes

`apply.sh` now also ships the exp32 route and verifies reproducible:

* copies `patched/main.c` (the exp32 route lives there) plus the refreshed
  `patched/{common.h,util.c,fops.c,slide.c}`
* installs `src/targets/r0q-S901U1UESAGZF3/vendor/` (the proven unit with its
  **proven `target.h`**) and `prebuilt/cve_exp32_arm32`
* installs `artifacts/cve-2026-43499` (vendored supervisor) and
  `artifacts/cve-2026-43499-root`
* inserts the `vendor-r0q` make rule from `vendor-r0q.Makefile` (gated to r0q)

Clean-room result on a pristine clone of the payloads repo:

```
apply.sh rc=0, "verified 65 rows and 520 source qwords at probe 0x200000 (step 0x8000)"
feed entries: 21
make TARGET=r0q-S901U1UESAGZF3 vendor-r0q   -> 0 errors
make TARGET=r0q-S901U1UESAGZF3 all release   -> 0 errors
sha256(cve-2026-43499-vendor) = 0039095c2a26e2488f88adac56eae954fa36fa0d067cb9998526163f388bcac1
  == the staged, device-tested artifacts/cve-2026-43499
```

Two apply.sh bugs were found and fixed by this check (both silent):

1. the `vendor-r0q` make-rule insertion was skipped because its guard matched a
   *comment* mention of "vendor-r0q"; the rule text now lives in
   `vendor-r0q.Makefile` and the guard tests for `VENDOR_DIR`.
2. `apply.sh` overwrote the vendored **proven** `vendor/target.h` with this
   port's own `target.h`; the vendored sources then failed to compile
   (`ASHMEM_LLSEEK_JT` etc. undeclared). The redundant `target/` copy is removed.

Remaining: rebase the in-app `cve-2026-43499-app.so` on the exp32 stage, then
publish the feed entry (currently `status: experimental`) and run the clean
single-boot end-to-end validation several times to absorb the ~1-in-16 odds.

### Round 25 — in-app path: what an app may and may not execute

The app `dev.busung.s25uroot` is installed and **debuggable** (`run-as` works),
which allowed the exec restrictions to be measured directly instead of guessed:

| route | result |
| --- | --- |
| `run-as <pkg> <binary in /data/local/tmp>` | `Permission denied` (cannot exec there at all) |
| path exec from the app data dir, domain `runas_app` | **works** (`usage: ./exp32 <buffer_fd>`) |
| `memfd_create` + `execveat(AT_EMPTY_PATH)`, `runas_app` | **EACCES** (errno 13) |

The app's *real* runtime domain, however, is not `runas_app`:
`dumpsys package` gives `minSdk=33 targetSdk=36`, and the running process is
`u:r:untrusted_app:s0:c201,c257,c512,c768` (uid `u0_a457`). For `untrusted_app`
with targetSdk >= 29 the W^X rules apply, so **neither** app-data-dir exec nor
memfd exec is available - the `runas_app` success does not transfer.

The only location an app may execute from is its **APK native library
directory** (`/data/app/.../lib/arm/`, context `app_lib_file`), which is exactly
how JNI libraries are loaded. Conclusion for the in-app payload:

* ship the 32-bit stage in the app APK as `lib/armeabi-v7a/<name>`;
* the app passes `nativeLibraryDir/<name>` to the payload via
  **`RMG_EXP32_PATH`**, which `vendor/api.c` now honours (falling back to the
  embedded-stage path write, then failing with a logged errno).

That is a change to the app repository (Gradle `jniLibs` + one env var when
loading the payload), not to the payload alone. The supervisor path is
unaffected: with `APP_PAYLOAD` undefined the child-exec block is byte-identical,
and the vendored supervisor still hashes
`0039095c2a26e2488f88adac56eae954fa36fa0d067cb9998526163f388bcac1`.

Round 25 addendum — the app-path `api.c` variant is **not** in the shipped build.
Applying it changed the supervisor binary (hash `601159d0…` instead of the
device-validated `0039095c…`), i.e. the edit perturbed codegen even though the
block is inside `#if defined(APP_PAYLOAD)`. Since only device-verified state may
ship, `vendor/api.c` was restored to the pristine upstream copy and the build
reproduces `0039095c…` exactly. The app-path version is staged for later at
`port/r0q-S901U1UESAGZF3/app-path/api.c` and must be re-validated on device when
the app repository ships the 32-bit stage in `lib/armeabi-v7a/`.

### Round 26 — app execution model mapped; only the helper path remains

Reading `InstallViewModel.kt` settled the in-app question: the app does **not**
dlopen the payload inside its own `untrusted_app` process. It runs it through
**Shizuku** (`ShizukuController.exec`) with

```kotlin
shizukuEnvironment(...) = [ "LD_PRELOAD=$payloadPath",
                            "CVE43499_ROOT_HELPER=$helperPath", ... ]
```

i.e. the payload executes in the **shell** domain (uid 2000) via `LD_PRELOAD`,
which is exactly the model validated over adb. Consequences:

* the feed artifact for r0q should be the **preloadable** exp32 build, not the
  in-process `APP_PAYLOAD` route (whose `APP_PHYS_P0_ORACLE` path panics here);
* the 32-bit stage exec needs **no** W^X/memfd workaround, because shell may
  write and execute in `/data/local/tmp` (already proven);
* the app already ships its helper as
  `app/src/main/jniLibs/arm64-v8a/libcve43499root.so` and points
  `nativeLibraryDir/libcve43499root.so` at it - the app-path `api.c` variant
  (`port/r0q-S901U1UESAGZF3/app-path/api.c`) is therefore **not needed**.

New build target `vendor-r0q-app` compiles the same vendored sources with
`-DAPP_PAYLOAD=1`: `build/r0q-S901U1UESAGZF3/cve-2026-43499-app-vendor.so`
(1749352 B, carries the exp32 stage).

Device run of that artifact in the app's exact model
(`LD_PRELOAD=... /system/bin/id` as shell), fresh boot:

```
cfi write ret=35 errno=0        <- landed on both attempts
root umh result wake=1 complete=1 retval=-2 socket=0
```

The write is now reliable in the app model; the UMH failed only because the
helper file was absent at the hardcoded path. That is the last wiring gap:

**the vendored `root.c` hardcodes `/data/local/tmp/cve-2026-43499-root` and
ignores `CVE43499_ROOT_HELPER`**, whereas the app passes its own
`nativeLibraryDir/libcve43499root.so`. `vendor/root.c` must honour
`CVE43499_ROOT_HELPER` (falling back to the hardcoded path), and the resulting
supervisor/app artifacts must be rebuilt, re-hashed and re-validated on device.

### Round 27 — helper path wired to the app

`vendor/root.c` now resolves the UMH helper through
`CVE43499_ROOT_HELPER` (falling back to `ROOT_UMH_PATH`), which is exactly the
variable the app sets to `nativeLibraryDir/libcve43499root.so`. Rebuilt:

```
cve-2026-43499-vendor          77d01650924e28ac1665d9167ddfe8736477f99da986b5ad1d19496d1bb8f5e8
cve-2026-43499-app-vendor.so   77d01650924e28ac1665d9167ddfe8736477f99da986b5ad1d19496d1bb8f5e8
```

The two builds are byte-identical, confirming again that the vendored payload is
inherently preloadable (`APP_PAYLOAD` changes nothing for it) - so the feed ships
one artifact for both uses. `feed-entry.json` now points at that artifact
(1749352 B) and documents the app execution model.

Device attempt on a fresh boot with a **custom** helper path
(`CVE43499_ROOT_HELPER=/data/local/tmp/rmg-helper-custom`, hardcoded path deleted)
reached `cfi write ret=35` and then panicked in the physrw stage - the known
~1-in-16 failure mode, not a helper-path problem. The helper-path fix itself is
therefore still unconfirmed end-to-end on device.

## Round 31/32 — PORT VALIDATED END-TO-END ON THE HANDSET

Using the app's exact execution model (`LD_PRELOAD` of our preloadable exp32
payload into a shell process, with the helper supplied through
`CVE43499_ROOT_HELPER` **at a custom path** and the hardcoded path deleted):
first attempt, on a fresh boot, succeeded:

```
attempt 1 rc=0 write=1 socket=1
--- id via app helper (custom path) ---
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
--- KernelSU ---
[ksu-load] kallsyms entries: 300883
[ksu-load] resolved=198 unresolved=0
[ksu-load] init_module ret=0 errno=0 (Success)
uid=0(root) gid=0(root) groups=0(root) context=u:r:ksu:s0
--- module state ---
kernelsu 200704 0 - Live 0xffffffc00368d000 (O)
```

This confirms in one run: the forged-lock write (`cfi write ret=35`), the
env-driven helper path (round 27), root on the device, and this port's KernelSU
`android12-5.10` late-load module live in the kernel. The feed entry is updated
to `status: validated` with this evidence.
