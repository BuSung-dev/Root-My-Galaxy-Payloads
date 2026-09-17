# Galaxy S23+ SM-S916B (S916BXXS8CYBD) payload

Exact firmware profile for the international Galaxy S23+ on firmware
`S916BXXS8CYBD`
(`samsung/dm2qxxx/dm2q:14/UP1A.231005.007/S916BXXS8CYBD`), kernel
`5.15.148-android13-8-29539737-abS916BXXS8CYBD`.

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
| `cve-2026-43499-app.so` | 104128 | `a85f37011c1661d6d388f475cbe279eb1d1dd77169e1ed48dd650d806cc4bd21` |
| `../../kernelsu/ksud-dm2q-S916BXXS8CYBD-kdp` | 4920304 | `0e149b6be467cc2ce222057039e2ea977d1aec2e564b58369a0056ad19d35ac1` |
| `../../kernelsu/android13-5.15.148_kernelsu-dm2q-S916BXXS8CYBD-kdp.ko` | 356040 | `4ced75234a61807b62bbaafae30ba82fb6d054657a7434d4ce8033e748bf1664` |

`cve-2026-43499-app.so` is the exact artifact used for the app run, built from
this tree with Android NDK and the S916BXXS8CYBD profile:

```sh
make TARGET=dm2q-S916BXXS8CYBD ANDROID_NDK_HOME=/path/to/android-ndk release
```

## Usage notes

- The KASLR route uses tracefs, which is not reachable from the app domain, so
  Root My Galaxy must run in **Shizuku mode**; or run the payload from an
  `adb shell` with `--run-payload`.
- Run close to boot for the best odds. Per-boot success is probabilistic and a
  failed attempt can reboot the phone.
- After the stack-writer stage, a failed attempt leaves PI state behind;
  reboot and run again.
- Temporary root: everything is gone after a reboot.
