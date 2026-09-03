# SM-S911B (dm1q) — S911BXXSAFZG1

Open-source profile for Galaxy S23 `SM-S911B` on firmware
`S911BXXSAFZG1` (kernel `5.15.189-android13-8-33413713-abS911BXXSAFZG1`).

Status: **derived, build-verified, and device-tested (root + KernelSU).**

## Target

| Field | Value |
| --- | --- |
| Model | `SM-S911B` |
| Device | `dm1q` |
| Firmware | `S911BXXSAFZG1` |
| Kernel release | `5.15.189-android13-8-33413713-abS911BXXSAFZG1` |
| Build family | `33413713` (same as the FZE1 / FZDP / S916B FZG1 profiles) |
| Stack writer | MCAST (`SLIDE_STACK_WRITER=1`) |
| Profile label | `dm1q-S911BXXSAFZG1-tracefs-shaped-configfs-pipe-root` |

## How this port was made

`S911BXXSAFZG1` is a newer build (Android 16, `BP4A.251205.006`, security patch
`2026-07-05`) of the same `5.15.189` / `33413713` kernel family as the
`S911BXXSAFZE1` profile. The text (function) offsets, the tracefs caller gates
(`wait_for_vfork_done+0x44` / `worker_thread+0x78`) and the KASLR slide
candidates are **identical** to FZE1. Only four data-side symbols moved, each by
`-0x80`:

| Constant | FZE1 | SAFZG1 |
| --- | --- | --- |
| `KMALLOC_CACHES_OFF` | `0x02064638` | `0x020645b8` |
| `ANON_PIPE_BUF_OPS_OFF` | `0x01e7f620` | `0x01e7f5a0` |
| `ASHMEM_FOPS_OFF` | `0x0200d678` | `0x0200d5f8` |
| `SLIDE_NFULNL_LOGGER_NAME_OFF` | `0x01d5de56` | `0x01d5ddd6` |

`p0_fingerprint.h` was regenerated from the exact SAFZG1 raw `Image` at probe
`0x1f0000`.

## Why the FZE1 profile fails on SAFZG1

Running the FZE1 payload on SAFZG1 hardware gets through KASLR discovery
(`slide-kaslr-ok`, canonical) and the fops/CFI stage, but then the hardcoded
data offsets (`KMALLOC_CACHES_OFF`, `ANON_PIPE_BUF_OPS_OFF`, `ASHMEM_FOPS_OFF`)
point to the wrong addresses on SAFZG1. The MCAST physical write lands on the
wrong kernel data, which corrupts kernel state and panics the device on every
run — matching the reported "never gets root, always reboots". The SAFZG1
profile fixes exactly those constants.

## Artifacts

See [`artifacts/dm1q-S911BXXSAFZG1/README.md`](../artifacts/dm1q-S911BXXSAFZG1/README.md)
for hashes and manual (adb) usage.

## KernelSU

- `kernelsu/android13-5.15.189_kernelsu-dm1q-S911BXXSAFZG1.ko` — standalone
  module with the exact SAFZG1 `vermagic`.
- `kernelsu/ksud-dm1q-S911BXXSAFZG1-kdp` — late-load loader, byte-identical to
  the FZE1 build. KernelSU's `late-load` uses a kallsyms-aware manual loader
  (plain `insmod` is not supported) that does not verify `vermagic`, and the
  detected KMI (`android13-5.15`) is the same across FZE1/FZDP/SAFZG1, so the
  same loader works for all three.

## Build

```bash
export ANDROID_NDK_HOME=/path/to/ndk
make TARGET=dm1q-S911BXXSAFZG1
```

Rebuilds the shipped `cve-2026-43499-app.so`
(`7b1ad03f…`) byte-identically.

## Validation status

- [x] Offsets cross-checked against the exact SAFZG1 `Image` kallsyms.
- [x] `make` reproduces the artifact.
- [x] Device test on `S911BXXSAFZG1` (root + KernelSU; one attempt per boot,
      root is volatile).
