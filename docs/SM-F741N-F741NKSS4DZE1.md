# SM-F741N / F741NKSS4DZE1 porting record

Port of the Galaxy Z Flip6 Korea (`SM-F741N`) firmware support to the
Root-My-Galaxy-Payloads project. This is the first `b6q` (Galaxy Z Flip6)
profile in the repository.

## Evidence

| Field | Value |
| --- | --- |
| Package model | `SM-F741N` (Korea multi-CSC) |
| AP/PDA package | `F741NKSS4DZE1` |
| Device codename | `b6q` |
| Product name | `b6qkss` |
| Android | 16 |
| Build fingerprint | `samsung/b6qkss/b6q:16/.../F741NKSS4DZE1:user/release-keys` (build-ID part is a metadata-only placeholder in the profile) |
| Kernel release | `6.1.145-android14-11-33418572-abF741NKSS4DZE1` |
| KMI | `android14-6.1` |
| Source | stock `boot.img`, sha256 `0037d7ebd4f19cf858c471f22507947aa3087ac51d276f87700e87d2f86ce8c0` |

## Calibration

The recovery used `magiskboot` unpack, `vmlinux-to-elf` symbol recovery
(107,253 symbol records) and a full `llvm-nm` name/type table alignment
against the US `e2q-S926USQS6DZE2` and Hong Kong `e3q-S9280ZHS6DZE2` kernels
(103,520 keys).

| Comparison | Result |
| --- | --- |
| Code/data symbol addresses vs US DZE2 | identical |
| `.rodata` string labels vs US DZE2 | 2 differ, both `+0x2e` (`task_index_to_char.state_char`, `zero_mask`) |
| `.rodata` string labels vs HK DZE2 | 6 differ, `-0x2e` x5 / `-0x3c` x1 (known US/HK shift pattern) |
| `remove_waiter` disassembly (0x268 B) | byte-identical |

Anchor offsets relative to `KIMAGE_TEXT_BASE` (`0xffffffc008000000`), all
identical to the US template: `call_usermodehelper_exec_work` `0xd39cc`,
`anon_pipe_buf_ops` `0x1219d90`, `ashmem_fops` `0x13d1140`, `kmalloc_caches`
`0x176c6f8`, `system_unbound_wq` `0x223ae60`, `nfulnl_logger` `0x2242a20`,
`init_task` `0x224f8c0`, `root_task_group` `0x244cd80`, `selinux_state`
`0x2521588`, `sysctl_bootid` `0x26046e8`.

### The one non-symbol override

`"nfnetlink_log"` is a string constant and cannot be resolved from symbols; it
was measured against the raw Image and cross-checked through the
`nfulnl_logger` first-qword pointer:

```c
#define SLIDE_NFULNL_LOGGER_OFF 0x016a61b8ULL /* HK DZE2: 0x016a61e6 */
```

## P0 fingerprint

`p0_fingerprint.h` is generated from the exact F741N raw kernel image at probe
offset `0x1f0000` with a readback check (32 rows x 8 qwords). `256/256` qwords
match the Hong Kong DZE2 table; the US table differs in one qword (row
`slide=0xa0000`, second qword `0x9106d0219000b001`).

## Builds

| Artifact | Size | sha256 |
| --- | ---: | --- |
| `artifacts/b6q-F741NKSS4DZE1/cve-2026-43499-app.so` | 104,128 B | `cf0de020ee3bb1b3b9c16ced1ac5cc7193654087b199c9535d9e7a9cdd63e3ac` |
| `kernelsu/android14-6.1_kernelsu-b6q-F741NKSS4DZE1-kdp.ko` | 404,840 B | `e201114262160262e09134e6f972ee25601c5a819f428a5579eea75d18f05d3c` |
| `kernelsu/ksud-b6q-F741NKSS4DZE1-kdp` | 5,107,192 B | `f8648635d401a72aa8687bd5d6db0b8e84c2fe01e5c251c228b2e4e6c3852a23` |

The payload is the `stable` (104,128 B) truncation whose embedded variant
label `b6q-F741NKSS4DZE1-app-physical-p0-oracle` verifies.

The KernelSU module was built from KernelSU v3.3.0 (version code 32601) with
the Samsung KDP/RKP/DEFEX changes and the no-patch-text late-load path, via
the DDK rebuild pipeline against the exact target kernel (`Module.symvers` +
`vermagic` injection). Module `vermagic`:
`6.1.145-android14-11-33418572-abF741NKSS4DZE1 SMP preempt mod_unload modversions aarch64`.
Static audit: 202 undefined symbols / 0 missing, 70 `kallsyms` resolutions,
0 CRC mismatches; section-level comparison against the shipped DZF2 module
differs only in `.modinfo`, build-id and a few `.symtab` bytes. The module
embedded in the ksud binary is byte-identical to the shipped `.ko`. Requires
the official KernelSU v3.3.0 (32601) manager; there is no v3.2.5 fallback
build for this device.

## Hardware validation

| Step | Status |
| --- | --- |
| Symbol calibration | done (nm table + byte probes) |
| P0 fingerprint | done (readback 256/256) |
| Payload build | done (104,128 B) |
| KernelSU module + static audit | done |
| Full chain on device | **passed** - reported by the device owner through the RootMyS24 beta channel on an SM-F741N running `F741NKSS4DZE1`: KASLR slide recovery, physical read/write, KernelSU late-load, granted `su` |

## Notes

- `PIPE_DRAIN_SLABS` / `PIPE_RECLAIM_SLABS` remain at 10 (repo-wide value).
- `BUILD_FINGERPRINT` is metadata-only and not used by payload logic; the
  build-ID part is a placeholder until the owner captures
  `getprop ro.build.fingerprint`.
- Only valid for the exact `F741NKSS4DZE1` build; other `b6q` firmwares
  (F741B, F741U, F7410) require their own calibration.
