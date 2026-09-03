# SM-S911B (dm1q) — S911BXXU9FZDP

Open-source profile for Galaxy S23 `SM-S911B` on firmware
`S911BXXU9FZDP` (kernel `5.15.189-android13-8-33413713-abS911BXXU9FZDP`).

Status: **derived and build-verified — pending device validation.**

## Target

| Field | Value |
| --- | --- |
| Model | `SM-S911B` |
| Device | `dm1q` |
| Firmware | `S911BXXU9FZDP` |
| Kernel release | `5.15.189-android13-8-33413713-abS911BXXU9FZDP` |
| Build family | `33413713` (same as the FZE1 / S916B FZG1 profiles) |
| Stack writer | MCAST (`SLIDE_STACK_WRITER=1`) |
| Profile label | `dm1q-S911BXXU9FZDP-tracefs-shaped-configfs-pipe-root` |

## How this port was made

`S911BXXU9FZDP` is a newer build of the same `5.15.189` / `33413713` kernel
family as the `S911BXXSAFZE1` profile from the dm1q PR. The text (function)
offsets, the tracefs caller gates (`wait_for_vfork_done+0x44` /
`worker_thread+0x78`) and the KASLR slide candidates are **identical** to FZE1.
Only four data-side symbols moved, and the P0 probe page differs:

| Constant | FZE1 | FZDP |
| --- | --- | --- |
| `KMALLOC_CACHES_OFF` | `0x02064638` | `0x02064ab8` |
| `ANON_PIPE_BUF_OPS_OFF` | `0x01e7f620` | `0x01e7faa0` |
| `ASHMEM_FOPS_OFF` | `0x0200d678` | `0x0200daf8` |
| `SLIDE_NFULNL_LOGGER_NAME_OFF` | `0x01d5de56` | `0x01d5e2d6` |

`p0_fingerprint.h` was regenerated from the exact FZDP raw `Image` at probe
`0x1f0000` (6 of 32 rows differ from FZE1).

## Why the FZE1 profile fails on FZDP

Running the FZE1 payload on FZDP hardware gets through KASLR discovery
(`slide-kaslr-ok`, canonical) and the fops/CFI stage, then fails in the pipe
physical-RW stage:

```
[*] pipe caches normal1k=0000000000000002 normal2k=0100020100000010 ...
[*] phys step cache gate failed slab=... want=...
[*] fresh physrw retry page attempt=N/12 ... (all 12 fail)
[!] stack writer ran; refusing retry on this boot
```

The `pipe caches` values are garbage because `KMALLOC_CACHES_OFF` (and
`ANON_PIPE_BUF_OPS_OFF`) point to the FZE1 addresses, which do not exist at the
same offset on FZDP. The FZDP profile fixes exactly those constants.

## Artifacts

See [`artifacts/dm1q-S911BXXU9FZDP/README.md`](../artifacts/dm1q-S911BXXU9FZDP/README.md)
for hashes and manual (adb) usage.

## KernelSU

- `kernelsu/android13-5.15.189_kernelsu-dm1q-S911BXXU9FZDP.ko` — standalone
  module with the exact FZDP `vermagic`.
- `kernelsu/ksud-dm1q-S911BXXU9FZDP-kdp` — late-load loader, byte-identical to
  the FZE1 build. KernelSU's `late-load` uses a kallsyms-aware manual loader
  (plain `insmod` is not supported) that does not verify `vermagic`, and the
  detected KMI (`android13-5.15`) is the same on FZE1 and FZDP, so the same
  loader works for both.

## Build

```bash
export ANDROID_NDK_HOME=/path/to/ndk
make TARGET=dm1q-S911BXXU9FZDP
```

Rebuilds the shipped `cve-2026-43499-app.so`
(`10f72b86…`) byte-identically.

## Validation status

- [x] Offsets cross-checked against the exact FZDP `Image` kallsyms.
- [x] `make` reproduces the artifact.
- [ ] Device test on `S911BXXU9FZDP` (one attempt per boot; root is volatile).
