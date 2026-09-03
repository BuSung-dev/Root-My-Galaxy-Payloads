# Galaxy S23 Ultra SM-S918B (S918BXXSAFZF5) payload

Exact firmware profile for the European Galaxy S23 Ultra on firmware
`S918BXXSAFZF5` (`samsung/dm3qxeea/dm3q:16/BP4A.251205.006/S918BXXSAFZF5`),
kernel `5.15.189-android13-8-33413713-abS918BXXSAFZF5`.

## Hardware evidence

The full chain completed on real hardware through the Root My Galaxy app
(Shizuku execution mode): tracefs KASLR discovery, controlled 32-object
`mm_struct` collection, shaped order-3 SKB reclaim, the MCAST waiter writer,
fake ashmem fops, configfs arbitrary read/write, pipe physical read/write,
root usermode helper, and KernelSU late-load. The run reported
`done=1 root=1 uid=2000->0`, the app confirmed `KernelSU control channel
verified` / `KernelSU active`, and `su -c id` returned
`uid=0(root) context=u:r:ksu:s0` with SELinux enforcing.

## Files

| File | SHA-256 |
| --- | --- |
| `cve-2026-43499-app.so` | `3bf7edcbab642c346ac0cb5564ef7406b835eb555b1163ebd498a9860dc6c83f` |
| `cve-2026-43499-root` | `f5efabbf91dcbfb7451925627b110df634fe1451dacd5aa26f6d2cb60ea3080c` (source-built helper for standalone shell use) |
| `../../kernelsu/ksud-dm3q-S918BXXSAFZF5-kdp` | `5da5818d36da2d589496f91016078a43f50489e5c98b319db4eaa5ee475b86bd` |

`cve-2026-43499-app.so` is the exact artifact that completed the chain on
hardware. It is built from this tree (`make TARGET=dm3q-S918BXXSAFZF5`,
Android NDK r29; the profile selects the MCAST stack writer via
`-DSLIDE_STACK_WRITER=1`).
The KernelSU loader is the `android13-5.15.189` exact-source build from the
same `33413713` kernel-build family (built from Samsung's released SM-S916B
FZG1 opensource tree, embedded kallsyms-aware module, RKP syscall-table and
live text patching disabled). It is hardware-verified on this SM-S918B FZF5
device: module loads (`Live` in `/proc/modules`), KernelSU Manager v3.2.5
reports `Working <LKM> [Jailbreak mode]`, and granted `su` works under
SELinux enforcing.

## Build

```sh
make TARGET=dm3q-S918BXXSAFZF5 ANDROID_NDK_HOME=/path/to/android-ndk-r29
```

## Usage notes

> **⚠️ EXPECT RETRIES: the hardware-verified success took 4 tries with Shizuku mode (fresh reboot before each retry). Failed attempts are normal — see below.**


- Run soon after boot (ideally within the first two minutes); the supervisor
  uses its built-in attempt/timeout defaults (8 attempts, 300 s attempt
  timeout, 180 s P0 timeout) because the profile sets
  `requiresFreshP0Session`.
- After the stack-writer stage has fired, a failed attempt leaves PI state
  behind; the engine detects this and refuses in-boot retries. Reboot and
  run again. Success is not guaranteed per boot — expect occasional
  panic-reboots, freezes, or clean failures before a successful run.
- Temporary root is volatile; everything is gone after reboot.
