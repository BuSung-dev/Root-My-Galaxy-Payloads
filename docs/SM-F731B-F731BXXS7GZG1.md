# Galaxy Z Flip5 SM-F731B / F731BXXS7GZG1 Port (Hardware Validated)

## Status: Validated on Hardware

Target profile configuration, KernelSU LKM, and binaries successfully validated end-to-end on live hardware (`SM-F731B` running firmware `F731BXXS7GZG1`, `b5qxeea`).
Installation completed with KernelSU active, control channel verified, and KernelSU Manager reporting `Working <LKM> [Jailbreak mode]`.

The implementation follows the MCAST stack-writer route established on Snapdragon 8 Gen 2 for Galaxy (`kalama` / `SM8550`):

```text
adb shell
-> tracefs KASLR slide
-> controlled 32-object mm_struct slab
-> SKB head shaping
-> order-3 SKB reclaim
-> MCAST stale waiter write
-> fake ashmem fops
-> configfs arbitrary read/write
-> pipe physical read/write
-> workqueue usermode helper
-> uid 0 root daemon
```

## Exact target

| Field | Value |
| --- | --- |
| Model | `SM-F731B` Galaxy Z Flip5 |
| Device / Product | `b5q` (`b5qxeea`) |
| Firmware | `F731BXXS7GZG1` |
| Kernel | `5.15.189-android13-8-33404244-abF731BXXS7GZG1` |
| SoC Platform | Qualcomm Snapdragon 8 Gen 2 for Galaxy (`kalama` / `SM8550`) |
| Payload profile | `b5q-F731BXXS7GZG1` |
| Writer | MCAST (`SLIDE_STACK_WRITER=1`) |
| Execution domain | `uid=2000`, `u:r:shell:s0` (requires `readtracefs` group 3012) |

The profile is bound to the exact firmware build. Do not reuse on different builds without re-extracting symbols and verifying offsets.

## Symbol and Offset Alignment

The kernel ELF was reconstructed from the stock boot image, recovering 126,219 symbols at text base `0xffffffc008000000`.

Compared to sibling `kalama` builds (`dm2q-S916BXXSAFZG1`):
- All `.text` entry points and handlers are byte-for-byte identical (`init_task` `0x02c05080`, `system_unbound_wq` `0x02a90800`, `call_usermodehelper_exec_work` `0x001045d0`, `ashmem_ioctl` `0x0114c6dc`).
- The tracefs blocking caller offsets match dm2q:
  - `SLIDE_TRACEFS_WORKER_CALLER_OFF = 0x0010db44` (`worker_thread` `bl schedule` + 4)
  - `SLIDE_TRACEFS_VFORK_CALLER_OFF = 0x000c8fe4` (`wait_for_vfork_done` `bl wait_for_common` + 4)
  - `SLIDE_TRACEFS_EVENT_ID = 108` (`sched:sched_blocked_reason`, confirmed live via ADB)
- Three `.data` objects shifted by `+0x640` relative to dm2q:
  - `kmalloc_caches`: `0x02064c38`
  - `anon_pipe_buf_ops`: `0x01e7fc20`
  - `ashmem_fops`: `0x0200dc78`
- `nfulnl_logger.name` pointer resolves to `0x01d5e4c2` (`"nfnetlink_log"`).
- `random_table` boot_id `.data` slot is at `0x02bba9c8` pointing to `sysctl_bootid` at `0x02e6c0b1`.

## KernelSU LKM Module

The KernelSU module was aligned with the exact target release:
- Vermagic: `5.15.189-android13-8-33404244-abF731BXXS7GZG1 SMP preempt mod_unload modversions aarch64`
- Symbol Audit: 200 undefined imports; 200/200 confirmed present in `vmlinux.elf`; 0 missing; 0 CRC mismatches against target `Module.symvers` (8,543 exported symbols).
- Retains kallsyms-aware manual relocation support for Samsung KDP/RKP/DEFEX.

## Artifacts and Build Commands

Build commands:

```sh
make TARGET=b5q-F731BXXS7GZG1 ANDROID_NDK_HOME=/path/to/android-ndk
```

Generated outputs:
- `build/b5q-F731BXXS7GZG1/cve-2026-43499-app.so`
- `build/b5q-F731BXXS7GZG1/cve-2026-43499-root`
- `kernelsu/android13-5.15.189_kernelsu-b5q-F731BXXS7GZG1.ko`
- `kernelsu/ksud-b5q-F731BXXS7GZG1-kdp`

## Device validation

- Exploit (MCAST stack writer) completed the full chain on hardware (`SM-F731B` `b5qxeea`) through the Root My Galaxy app:
  - Tracefs KASLR slide leak (observed slide `0x30000`, candidate hit count > 200).
  - Controlled 32-object `mm_struct` group allocation and reclaim.
  - MCAST stale waiter write (`sched_ok=1`).
  - Fake ashmem fops + configfs ARW.
  - Pipe physical read/write (`rw64=1/1`).
  - UMH bootstrap root acquired (`uid=2000->0`, `u:r:kernel:s0`).
- KernelSU late-load:
  - `ksud-b5q-F731BXXS7GZG1-kdp` loaded `android13-5.15.189_kernelsu-b5q-F731BXXS7GZG1.ko` through the guarded `--late-load` path (avoiding DEFEX Safeplace restrictions).
  - KernelSU control channel verified and active.
  - KernelSU Manager reports `Working <LKM> [Jailbreak mode]`.
- System stability: zero kernel panics or unexpected reboots observed during successful runs.
- Root and module remain volatile per boot (stock bootloader and boot image partitions are completely unmodified).
