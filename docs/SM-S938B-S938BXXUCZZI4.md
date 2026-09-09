# SM-S938B / S938BXXUCZZI4 (Galaxy S25 Ultra) — One UI 9 beta 2

Port record for `pa3q-S938BXXUCZZI4`, derived from the shipped `pa1q-S931BXXUCZZI4`
target and the exact `S938BXXUCZZI4` boot image. Community testers had already
confirmed the ZZI4 route on this device with adapted fingerprints; this port
re-derives everything from the Image itself.

## 1. Kernel identity

| item | value |
| --- | --- |
| Device | Galaxy S25 Ultra `SM-S938B` (Snapdragon 8 Elite, `pa3q`) |
| AP/PDA | `S938BXXUCZZI4` (One UI 9 beta 2) |
| Kernel release | `6.6.127-android15-8-p33f4ffe-abogkiS938BXXUCZZI4-4k` |
| Android / build ID | 17 (SDK 37) / `CP2A.260605.016`, SPL 2026-08-05 |
| boot.img | 101,122,048 bytes, header v4, kernel @ 0x1000 (39,115,264 bytes) |
| Image SHA-256 | `7811e9413a3928079219347a435eadbfe0241f74ac28c459ad71b5195fdb8aef` |

The GKI build id (`p33f4ffe`) is **identical** to `S931BXXUCZZI4`.

## 2. Comparison against the S931B ZZI4 Image

- Same decompressed Image size (39,115,264 bytes); 591,933 differing bytes
  (1.51 %) across ~15.7k regions — per-device drivers/strings only.
- **kallsyms: all 26 required symbols, 5 derived values, and 17 cross-check
  symbols are identical** to `pa1q-S931BXXUCZZI4` (e.g. `init_task`
  0x0234e2c0, `sysctl_bootid` 0x02683910, tracefs event id 109).
- **BTF: byte-identical** (6,425,607 bytes, same blob at 0x18aca6c, same
  SHA-256 `e13df32a…`) — every struct layout is unchanged.

Consequence: the entire offset table of `pa1q-S931BXXUCZZI4` is reused
verbatim; the only device-specific payload component is the P0 fingerprint.

## 3. P0 fingerprint

Regenerated from the exact S938B Image at probe offset 0x1f0000
(`p0_fingerprint.h` in this directory), 32 rows, readback-verified.
**4 of 32 rows differ** from the S931B table (slides 0x070000, 0x0a0000,
0x0c0000, 0x1a0000) — matching the community finding that only the
fingerprints need adapting per device.

## 4. Symbol audit

The built app payload's undefined-import set is **identical** (82/82) to the
hardware-validated `pa1q-S931BXXUCZZI4` payload, and the S938B kallsyms table
is symbol-for-symbol identical — all kernel-relevant imports resolve.

## 5. KernelSU pair

`kernelsu/android15-6.6_kernelsu-s938b.ko` + `kernelsu/ksud-pa3q-S938BXXUCZZI4-kdp`
are built by `.github/workflows/build-s938b-kernelsu.yml` from the same KSU
3.2.5 + Samsung KDP source as the ZZI4 pair, with the utsrelease set to the
S938B release string. **No-LTO** build is mandatory (thin-LTO modules are
rejected by this kernel with ENOEXEC at `init_module`).

## 6. Status

| stage | result |
| --- | --- |
| Symbol/table port | derived, identical to ZZI4 |
| P0 fingerprint | re-derived from exact Image (4/32 rows differ) |
| Payload build | 104,128 bytes, label `pa3q-S938BXXUCZZI4-app-physical-p0-oracle`, import audit clean |
| On-device validation | pending (community testers confirmed the route with adapted fingerprints; first-party validation to follow) |

## On-device validation & tracefs finding (community)

- A community-built pa3q binary (ZZHL-lineage, P0-only slide, identical symbol
  offsets and byte-identical fingerprint table) roots SM-S938B on this firmware.
- Our first build (tracefs-first slide) failed 0/24: tracefs slide succeeded
  (0x0c0000, gate passed) but the first raced physical write hit window=0 - the
  write window only opens after the P0 oracle gate/probe writes have run, which
  tracefs-derived slides skip.
- Fix: APP_TRACEFS_SLIDE removed from this target; payload is P0-oracle-only,
  matching the proven community flow. Artifact updated (sha256 b6898459...).
