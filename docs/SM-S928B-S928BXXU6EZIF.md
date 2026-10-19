# SM-S928B / S928BXXU6EZIF porting record

Port of the Galaxy S24 Ultra International (`SM-S928B`) One UI 9.0 firmware
support to the Root-My-Galaxy-Payloads project. This is the first
6.1.162-generation (Android 17) profile in the repository.

## Evidence

| Field | Value |
| --- | --- |
| Package model | `SM-S928B` (OXM multi-CSC) |
| AP/PDA package | `S928BXXU6EZIF` |
| Device codename | `e3q` |
| Product name | `e3qoxm` (device reports `e3qeea`) |
| Android | 17 / One UI 9.0 |
| Build fingerprint | captured from a device: `samsung/e3qeea/e3q:17/CP2A.260605.016/S928BXXU6EZIF:user/release-keys` (the profile metadata string is a placeholder; it does not affect payload logic) |
| Kernel release | `6.1.162-android14-11-34343818-abS928BXXU6EZIF` |
| KMI | `android14-6.1` |
| Source | stock `boot.img`, sha256 `b98326455c825062e5350ae88b75488f4de4c7b1e587a47105fffdc41be37500` |
| Samsung signature | `SM-S928B_EUR_XX_QKEY2` |

## Offsets

This is not a suffix swap: the kernel generation moved from 6.1.145 to
6.1.162, so every audited offset shifted by a different amount. Twenty-three
`*_OFF` were re-resolved by symbol name against the recovered vmlinux (107,567
symbol records). Representative deltas against the Hong Kong DZE2 kernel:

| Macro | HK DZE2 | EZIF | Delta |
| --- | ---: | ---: | ---: |
| `ASHMEM_FOPS_OFF` | `0x0013d1140` | `0x0013e2e30` | `+0x11cf0` |
| `INIT_TASK_OFF` | `0x00224f8c0` | `0x00226f6c0` | `+0x1fe00` |
| `KMALLOC_CACHES_OFF` | `0x00176c6f8` | `0x001781458` | `+0x14d60` |
| `CONFIGFS_READ_ITER_OFF` | `0x0004712a4` | `0x000472b84` | `+0x18e0` |
| `CALL_USERMODEHELPER_EXEC_WORK_OFF` | `0x0000d39cc` | `0x0000d3be0` | `+0x214` |

## Non-symbol locations (measured by hand)

```c
#define SLIDE_NFULNL_LOGGER_OFF 0x016ba45dULL        /* "nfnetlink_log" string; HK DZE2: 0x016a61e6 */
#define SLIDE_RANDOM_BOOT_ID_DATA_OFF 0x02396a98ULL  /* random_table + 0x108; HK DZE2: 0x023762f0 */
```

Both were verified byte-for-byte against the raw Image:

- `"nfnetlink_log"` is a string constant; its offset is additionally
  cross-checked through the `nfulnl_logger+0` pointer.
- The qword at `random_table+0x108` equals the `sysctl_bootid` symbol address
  on both kernels, confirming the `+0x108` structural rule.

## P0 fingerprint

Regenerated from the exact EZIF raw kernel image at probe `0x1f0000` (32 rows
x 8 qwords, readback verified). 235/256 qwords differ from both the Hong Kong
and the US DZE2 tables, as expected for a kernel-generation change.

## CVE chain re-verification

- `remove_waiter` (154 instructions, same as DZE2): the normalized instruction
  sequence is 0-diff / 154; raw-byte differences are relocation targets of
  branches and calls only. The race window ("clear `current->pi_blocked_on`
  instead of `waiter->task->pi_blocked_on`") is present unchanged on 6.1.162.
- `SLIDE_TRACEFS_EVENT_ID = 106` provisional: `sched_blocked_reason` sits at
  `__ftrace_events` index 86 on both kernels (+20 reserved slots). Device
  check: `cat /sys/kernel/tracing/events/sched/sched_blocked_reason/id`.
- `SLIDE_TRACEFS_WORKER_CALLER_OFF` rule (`worker_thread + 0xA0`) verified:
  `0xdb2fc -> 0xdb39c`.

## Builds

| Artifact | Size | sha256 |
| --- | ---: | --- |
| `artifacts/e3q-S928BXXU6EZIF/cve-2026-43499-app.so` | 104,128 B | `5215587861d4a6ae2ddb9d7437dee3d71bdb0910b0542f789e4478a866ac6be6` |
| `kernelsu/android14-6.1_kernelsu-e3q-S928BXXU6EZIF-kdp.ko` | 404,840 B | `7973e28bff2a04aa2c9d4e9692307dd7a7999cf3833dd9e3d3fef6fe3066795a` |
| `kernelsu/ksud-e3q-S928BXXU6EZIF-kdp` | 5,107,192 B | `51881cdd113adb80cf00e18669ebb8a8fa4af00c27ae4a9a88ad7fdc704c00fe` |

The payload is the `stable` (104,128 B) truncation whose embedded variant
label `e3q-S928BXXU6EZIF-app-physical-p0-oracle` verifies.

The KernelSU module was built from KernelSU v3.3.0 (version code 32601) with
the Samsung KDP/RKP/DEFEX changes and the no-patch-text late-load path, via
the DDK rebuild pipeline against the exact target kernel (`Module.symvers` +
`vermagic` injection). Module `vermagic`:
`6.1.162-android14-11-34343818-abS928BXXU6EZIF SMP preempt mod_unload modversions aarch64`.
Static audit: 202 undefined symbols / 0 missing, 70 `kallsyms` resolutions,
0 CRC mismatches; section-level comparison against the shipped DZF2/DZH3
modules differs only in `.modinfo`, build-id and a few `.symtab` bytes. The
module embedded in the ksud binary is byte-identical to the shipped `.ko`.
Requires the official KernelSU v3.3.0 (32601) manager; there is no v3.2.5
fallback build for this release.

## Hardware validation

| Step | Status |
| --- | --- |
| Symbol calibration | done (107,567 records; 23 `*_OFF` by identity + 2 byte probes) |
| P0 fingerprint | done (readback 256/256) |
| Payload build | done (104,128 B) |
| CVE chain audit | done (remove_waiter 154/154 normalized) |
| KernelSU module + static audit | done |
| Full chain on device | **pending** - test-in-progress through the RootMyS24 beta channel |

## Notes

- `BUILD_FINGERPRINT` is metadata-only; the profile carries a placeholder
  until the next payload refresh. The captured device value is
  `samsung/e3qeea/e3q:17/CP2A.260605.016/S928BXXU6EZIF:user/release-keys`.
- `SLIDE_TRACEFS_EVENT_ID` to be confirmed with the device command above; the
  DZE2 value is carried over and is not yet hardware-verified.
- Only valid for the exact `S928BXXU6EZIF` build; other S24 Ultra One UI 9
  builds require their own calibration.
