# Galaxy S23+ SM-S916B / S916BXXSAFZH3 porting record

## Status

The FZH3 profile completed the full chain twice on real `SM-S916B`
hardware: exploit root (`uid=0`, `u:r:kernel:s0`, SELinux Permissive) and
KernelSU late-load with granted `su` under enforcing
(`uid=0(root) context=u:r:ksu:s0`). Success rate across the hardware
session was 2 of 5 boots; both failures were early/mid race losses
(kernelsnitch collision scan empty; UMH prepublish write rejected), each
recoverable by reboot + rerun. The port was performed with the exact CAU
firmware `S916BXXSAFZH3/S916BOXMAFZH3/S916BXXSAFZE1/S916BXXSAFZH3`.

## Exact target

| Field | Value |
| --- | --- |
| Model | `SM-S916B` Galaxy S23+ |
| Firmware | `S916BXXSAFZH3` (CAU/OXM multi-CSC) |
| Fingerprint | `samsung/dm2qxxx/dm2q:16/BP4A.251205.006/S916BXXSAFZH3:user/release-keys` |
| Kernel | `5.15.189-android13-8-33413713-abS916BXXSAFZH3` |
| Payload profile | `dm2q-S916BXXSAFZH3` |
| Proven writer | MCAST |
| Proven execution domain | `uid=2000`, `u:r:shell:s0` |

The profile is exact-firmware bound. The FZG1 payload was also executed
once on this FZH3 device (with the owner's informed consent) as a control:
it passed every stage through configfs ARW and failed cleanly at the pipe
cache gate because the expected kmalloc-cache pointers read from the FZG1
`kmalloc_caches` offset were garbage (`cgroup2k=0xa527a1`, not a kernel
pointer). No freeze occurred. That run directly identified the drifted
data offsets re-derived below.

## Provenance

```text
firmware zip  S916BXXSAFZH3_CAU.zip  sha256 a2b3a8e9b9f00fad741ec105d772000f0d116e3dee85425ed9c602a2b3c8a5a2
boot.img      100663296 bytes        sha256 3cbe08953a16a6b752a79cfe8458dd7aa250789ebc8f8c9c5a7b5c06f7fb96e5
kernel Image  46860800 bytes         sha256 94148bec653529dfcb8774e47a4f26be665d25e1e13c95ce70418ea10c73e296
boot header   v4, kernel_size at 0x08, kernel blob at 0x1000
BTF blob      [0x21ef2ac, 0x27bf188), single validated candidate
ELF base      0xffffffc008000000 (126219 recovered symbols)
```

## Derivation

Symbol recovery followed `PORTING.md` verbatim (`vmlinux-to-elf`,
`llvm-nm`, raw-BTF header scan, `bpftool btf dump`). FZH3 text is
byte-identical to FZG1 for every symbol the payload uses; the drift is
confined to four data objects, and all remaining constants (event ID 108
= `__TRACE_LAST_TYPE 20 + (__event_sched_blocked_reason -
__start_ftrace_events)/8`, worker/vfork callers, task/mm/waiter/fops/wq/
configfs layouts, `selinux_state.enforcing` at byte 0, the
`random_table[4].data` boot-id slot at `+0x108`, `sysctl_bootid`) were
re-verified against the FZH3 ELF/BTF and matched FZG1 unchanged.

| Constant | FZG1 | FZH3 | Delta |
| --- | ---: | ---: | ---: |
| `KMALLOC_CACHES_OFF` | `0x020645f8` | `0x02063fb8` | -0x640 |
| `ANON_PIPE_BUF_OPS_OFF` | `0x01e7f5e0` | `0x01e7efa0` | -0x640 |
| `ASHMEM_FOPS_OFF` | `0x0200d638` | `0x0200cff8` | -0x640 |
| `SLIDE_NFULNL_LOGGER_NAME_OFF` | `0x01d5de62` | `0x01d5d842` | -0x620 |

`struct mm_struct` BTF size is `0x3e0`; the slab object stays `0x400`
(HWCACHE_ALIGN rounding), matching the FZF5 profile and the FZH3 control
run that passed the mm/reclaim/fops gates with the 0x400 stride. The
physical constants (`P0_PHYS_OFFSET 0x80000000`,
`P0_KERNEL_PHYS_LOAD 0x80080000`) were hardware-validated by the control
run's passing P0 write before this port.

The `p0_fingerprint.h` table was generated with
`tools/generate_p0_fingerprint.pl` at probe offset `0x1f0000` and
self-verified (32 rows, 256 qwords).

## KernelSU handoff

The published FZG1 loader pair `ksud-dm2q-S916BXXSAFZG1-kdp` (kallsyms-
aware, empty `__versions`, runtime relocation by name) loads and runs on
FZH3: module `Live`, KernelSU Manager `Working <LKM>`, granted
`su -c id` → `u:r:ksu:s0` under enforcing. Cross-build use mirrors the
dm3q FZF5 validation of the same binary on a later same-family build.

Operational notes discovered on hardware:

- Defex SIGKILLs any direct `ksud` execution from `/data/local/tmp`
  (`Killed`, rc=137); only the logcat bind-mount disguise inside a
  private mount namespace loads the module.
- Current `src/su_daemon.c` passes `late-load --ephemeral`, which this
  ksud build rejects (`rc=2`); the guarded invocation must omit it until
  a ksud accepting the flag ships.
- The module load re-enforces SELinux, after which the exploit daemon's
  unix socket is denied to the shell domain. Complete the KernelSU
  handoff (stage + late-load while Permissive) before depending on the
  daemon.
- `.ksud-stage` must be re-staged after every reboot; late-load renames
  it and fails with `Failed to rename ... .ksud-stage` otherwise.

## Artifacts and commands

See [`../artifacts/dm2q-S916BXXSAFZH3/README.md`](../artifacts/dm2q-S916BXXSAFZH3/README.md)
for hashes, the exact per-boot commands, and retry expectations.
