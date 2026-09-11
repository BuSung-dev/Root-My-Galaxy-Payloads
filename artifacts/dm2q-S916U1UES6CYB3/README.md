# Galaxy S23+ (US) SM-S916U1 (S916U1UES6CYB3) payload

Exact firmware profile for the US-unlocked Galaxy S23+ on firmware
`S916U1UES6CYB3`
(`samsung/dm2quew/dm2q:14/UP1A.231005.007/S916U1UES6CYB3`), kernel
`5.15.148-android13-8-29539737-abS916U1UES6CYB3`.

## Hardware evidence

The full chain completed on real hardware through the Root My Galaxy app
(Shizuku execution mode): tracefs KASLR discovery, controlled 32-object
`mm_struct` collection, shaped order-3 SKB reclaim, the MCAST waiter writer,
fake ashmem fops, configfs arbitrary read/write, pipe physical read/write,
root usermode helper, and KernelSU late-load. The app reported
`done=1 root=1` / `uid=2000->0`, then `KernelSU control channel verified` /
`KernelSU active`; `/proc/modules` shows `kernelsu ... Live` and KernelSU
Manager reports `Working <LKM> [Jailbreak mode]` under SELinux enforcing.

## Files

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `cve-2026-43499-app.so` | 132776 | `3c2170a13f9001231cde51652f6a901b62af8933cf474d06ece83bc55610f1e0` |
| `../../kernelsu/ksud-dm2q-S916U1UES6CYB3-kdp` | 4920304 | `127e9b4ad5bcaa0ef69c8d1b0b7c107464510da55821846eff0823be683ae179` |
| `../../kernelsu/android13-5.15.148_kernelsu-dm2q-S916U1UES6CYB3-kdp.ko` | 356040 | `3f8bfcfc0e382c161c34f9b1578a3874d574c5dd22abf172f5e7ba556a112ab7` |

`cve-2026-43499-app.so` is the exact artifact that completed the chain on
hardware, built from this tree with Android NDK r28:

```sh
make TARGET=dm2q-S916U1UES6CYB3 ANDROID_NDK_HOME=/path/to/android-ndk
```

The profile selects the MCAST stack writer via `-DSLIDE_STACK_WRITER=1`
(added to the Makefile).

## Usage notes

- The KASLR route uses tracefs, which is not reachable from the app domain.
  Run Root My Galaxy in **Shizuku mode** (the app then executes the payload as
  the shell user); or run the payload from an `adb shell` directly with
  `--run-payload`.
- Run close to boot for the best odds. Per-boot success is probabilistic.
- After the stack-writer stage, a failed attempt leaves PI state behind;
  reboot and run again.
- Temporary root: everything is gone after a reboot.
