# Galaxy S23 Ultra (Canada) SM-S918W (S918WVLS6CYB3) payload

Exact firmware profile for the Canadian Galaxy S23 Ultra on firmware
`S918WVLS6CYB3`
(`samsung/dm3qcsx/dm3q:14/UP1A.231005.007/S918WVLS6CYB3`), kernel
`5.15.148-android13-8-29539737-abS918WVLS6CYB3`.

## Status

**Device-tested end-to-end on a physical SM-S918W (S918WVLS6CYB3),
2026-09-18.** Exploit: slide/KASLR OK every attempt, full 32-object group
on attempt 2, `uid=2000->0`, no panic, Knox warranty bit 0. KernelSU
late-load: `kernelsu ... Live`, ksud stages complete, no panic. Manager
v3.2.5 reports `Working <LKM> [Jailbreak mode]`, version `32525-2`, exact
kernel/fingerprint, SELinux Enforcing. App-facing `su` in Termux (after
Manager grant): `uid=0(root) gid=0(root) groups=0(root)
context=u:r:ksu:s0`. Root is temporary (this boot only); no boot image
modified, bootloader locked.

## Files

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `cve-2026-43499-app.so` | 104128 | `cb14959bf64182616f323e59a70aa821bf3b19ba6d696a9800fec6b55718197e` |
| `../../kernelsu/ksud-dm3q-S918WVLS6CYB3-kdp` | 4629456 | `d972cd679209c864433c85f4c13dde80c36e9bdd15b8826d360a45c148f76a78` |
| `../../kernelsu/android13-5.15.148_kernelsu-dm3q-S918WVLS6CYB3-kdp.ko` | 357048 | `eb7e4b4a4518d130faec24e8203e4a8469096e4ac1d4c0432cdf4f355d8189bf` |

Root helper (`cve-2026-43499-root`, su_daemon.c) build
`30a1ef2698b14f4e6e9c0bd3dae1a1f646e349dd884e22dfe57f7f35a69d47ba`
(27,056 bytes): guarded `--late-load` via bind-mounted logcat path
(DEFEX kills any ksud exec parented by `sh`); passes `--ephemeral`, which
the dm3q ksud implements (skips replacing `/data/adb/ksud`).

`cve-2026-43499-app.so` was built from this tree with Android NDK r28c:

```sh
make TARGET=dm3q-S918WVLS6CYB3 ANDROID_NDK_HOME=/path/to/android-ndk release
```

(Windows-native equivalent: NDK `clang.exe --target=aarch64-linux-android35`
with the Makefile `release` flags; output padded with `truncate -s 104128`.)

The profile selects the MCAST stack writer via `-DSLIDE_STACK_WRITER=1`.

## Usage notes (once a KernelSU pair exists and testing is approved)

- The KASLR route uses tracefs, which is not reachable from the app domain.
  Run Root My Galaxy in **Shizuku mode** (the app then executes the payload as
  the shell user).
- Run close to boot for the best odds. Per-boot success is probabilistic.
- After the stack-writer stage, a failed attempt leaves PI state behind;
  reboot and run again.
- Temporary root: everything is gone after a reboot. No boot image is
  modified and the bootloader stays locked.
