# Galaxy S23 Ultra SM-S9180 (S9180ZHS8FZG1) payload

Exact firmware profile for the Galaxy S23 Ultra (Taiwan / Hong Kong / China, `dm3q` / `dm3qzhx`) on firmware
`S9180ZHS8FZG1` (`samsung/dm3qzhx/dm3q:16/BP4A.251205.006/S9180ZHS8FZG1`),
kernel `5.15.189-android13-8-33413713-abS9180ZHS8FZG1`.

## Hardware evidence

The full chain completed on real hardware: tracefs KASLR discovery, controlled
32-object `mm_struct` collection, shaped order-3 SKB reclaim, the MCAST waiter writer,
fake ashmem fops, configfs arbitrary read/write, pipe physical read/write (physrw),
root usermode helper, and KernelSU late-load. The run reported
`done=1 root=1 uid=2000->0`, KernelSU loaded and verified, and `su -c id` returned
`uid=0(root) gid=0(root) groups=0(root) context=u:r:ksu:s0` with SELinux Enforcing.

## Files

| File | SHA-256 |
| --- | --- |
| `cve-2026-43499-app.so` | `648cfa81abc7e1e8437e826db2e2ce6507939ff5fbddb20e3168d830c2f3681c` |
| `cve-2026-43499-root` | `c49a655708bed646441f6a7d848c5a400e6fb444f7fcf05fcb0704e547ddcd55` |
| `../../kernelsu/ksud-dm3q-S918BXXSAFZF5-kdp` | `5da5818d36da2d589496f91016078a43f50489e5c98b319db4eaa5ee475b86bd` |

Loader note: the `ksud-dm3q-S918BXXSAFZF5-kdp` reference above is confirmed —
FZG1 reuses the S918B 5.15 loader (no independent FZG1 ksud).
v3.3.0 (32601) resolution: reuse carries forward; FZG1 tracks the S918B
rebuild, no new binary (see `kernelsu/REBUILD-dm3q-v3.3.0.md` §0).

`cve-2026-43499-app.so` is the exact artifact that completed the chain on
hardware. It is built from this tree (`make TARGET=dm3q-S9180ZHS8FZG1`,
Android NDK r29).

## Build

```sh
make TARGET=dm3q-S9180ZHS8FZG1 ANDROID_NDK_HOME=/path/to/android-ndk-r29
```

## Usage notes

- Run soon after boot (ideally after uptime reaches 120s for the boot quiet window).
- Uses shaped order-3 SKB slab reclaim and controlled 32-object mm_struct collection
  (`S918_KSNITCH_FULL_COLLISIONS 4`), single-attempt per boot.
- Temporary root is volatile; all changes disappear cleanly upon reboot.
