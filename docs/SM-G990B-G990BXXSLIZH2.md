# SM-G990B G990BXXSLIZH2 validation

This Shizuku-required profile is hardware-validated on a Galaxy S21 FE 5G
(`r9q`, `SM-G990B`) with a locked bootloader.

| Field | Value |
| --- | --- |
| Firmware | `G990BXXSLIZH2` |
| Android | 16 / One UI 8 |
| Kernel | `5.4.289-qgki-32192773-abG990BXXSLIZH2` |
| SoC | Snapdragon 888 (`SM8350`) |
| KernelSU | v3.2.5 / version code `32525` |
| KMI asset | `android16-5.4` |

## Hardware results

- The tracefs/COW GhostLock route completed on independent clean boots.
- The app payload now selects that same validated route itself: controlled
  reclaim, the readiness-pause COW overlay, a 16 GiB PTE region, one attempt,
  and a 900-second timeout.
- The current app artifact completed three consecutive independent clean-boot
  ADB hardware retests. Releasing the old drain arrays before the target made
  pipe takeover succeed on attempt 1/3 in all three runs (pipe indices 239,
  238, and 151). Each run brought up the uid-0 daemon in `u:r:kernel:s0`, made
  SELinux permissive, restored the kernel globals, and remained stable. The
  final cleanup reboot returned the phone to enforcing mode.
- The app/Shizuku payload contract completed with exit status 0.
- Pipe takeover gets up to three attempts after a successful FOPS swap. If all
  miss, a detached keeper retains the fake FOPS backing page until reboot so
  an unrelated later file close cannot follow a freed function table.
- The exact KernelSU module passed the manual-relocation audit with no missing
  symbols, no module version entries, and no CRC mismatches.
- The generic Root My Galaxy helper staged the final `ksud`, invoked
  `late-load --ephemeral`, and verified the driver through `/dev/ksu`.
- Vendor `uname` has no Android KMI token, so `ksud` selected its sole embedded
  asset, `android16-5.4`; extracting that asset on-device reproduced SHA-256
  `927753d65ff813087ee84271c56f1e2b720a276cadebcb435c737b0767194823`.
- `PATH=/data/adb/ksu/bin:$PATH su -c id` returned:

```text
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
```

- A rebuilt Root My Galaxy APK preserved the exact helper bytes in
  `nativeLibraryDir` and displayed `KernelSU active` on-device.
- The kernel matched the official Manager certificate and crowned
  `me.weishu.kernelsu`. The stock Manager UI still labels Linux 5.4
  unsupported because its native client only scans inherited driver FDs; the
  Root My Galaxy app detects the live LKM through `/proc/modules`.

## Exact artifacts

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `artifacts/r9q-G990BXXSLIZH2/cve-2026-43499-app.so` | 134072 | `f0a2b39c3d3a0e05beccd839f3e2af2417ca7f60e849b4e5e6bee898688b437b` |
| `artifacts/r9q-G990BXXSLIZH2/cve-2026-43499-root` | 26960 | `fd193ddecbc5db5d13e72281cf54ce3ae054e1b3d8728c1dfc64ff1fc80d0bb8` |
| `kernelsu/android16-5.4_kernelsu-r9q-G990BXXSLIZH2-kdp.ko` | 264456 | `927753d65ff813087ee84271c56f1e2b720a276cadebcb435c737b0767194823` |
| `kernelsu/ksud-r9q-G990BXXSLIZH2-kdp` | 4844920 | `bafc40f0166418ec7564578d9f181083b33db6ae9815095a3cf67eba7ba551e9` |

The KO reports:

```text
5.4.289-qgki-32192773-abG990BXXSLIZH2 SMP preempt mod_unload modversions aarch64
```

## Samsung 5.4 compatibility work

FASTUH RKP watchdog-resets this firmware when KernelSU plants protected-text
kprobes or patches live text/syscall-table entries. This build therefore uses:

- no protected-text kprobes;
- no live text or syscall-table writes;
- a world-readable `/dev/ksu` misc device for the existing ioctl UAPI;
- userspace fallbacks that open `/dev/ksu` when reboot-supercall FD delivery is
  unavailable;
- permissive SELinux with existing `kernel` and `system_file` policy types;
- no kernel umount hiding;
- a hookless `su` applet installed at `/data/adb/ksu/bin/su`.

Samsung 5.4 also lacks CFI jump-table aliases for `prepare_ro_creds`,
`kdp_assign_pgd`, `get_task_creds`, and `set_task_creds`. Calling the raw
resolved symbols through ordinary function pointers caused CFI panics. The
final module uses narrow `__nocfi` wrappers around prototypes verified against
the exact target source.

Native `commit_creds()` was tested and rejected: its
`prepare_ro_creds(CMD_COMMIT_CREDS)` transition caused an EL2 data abort
(`ESR_EL2=0x96000047`, `FAR_EL2=1`) followed by
`TZBSP_ERR_FATAL_NON_SECURE_WDT`. The proven path installs a protected
credential with the task-scoped `CMD_COPY_CREDS` worker, updates the target PGD
through `kdp_assign_pgd`, and synchronizes DEFEX with `CRED_FLAGS_PROOT`.

## Reproduction notes

The exact exploit source is retained under
`src/targets/r9q-G990BXXSLIZH2/`. Its target-specific `port/` source set is
selected only for this profile because the Samsung 5.4 stack overlay depends
on exact compiler geometry. The app payload ignores the generic app retry and
timeout values so it cannot silently leave the phone-validated one-attempt
route. With NDK r27b, `make TARGET=r9q-G990BXXSLIZH2` reproduces the published
app payload and helper hashes byte-for-byte.

The exact config and target symbol versions are retained as:

```text
kernelsu/G990BXXSLIZH2.config.gz
kernelsu/G990BXXSLIZH2.Module.symvers.gz
```

The complete KernelSU source delta is:

```text
kernelsu/patches/KernelSU-v3.2.5-r9q-5.4.patch
```

Force-reboot before every GhostLock attempt. Never run a second controlled
reclaim on the same boot, even after a clean return.
