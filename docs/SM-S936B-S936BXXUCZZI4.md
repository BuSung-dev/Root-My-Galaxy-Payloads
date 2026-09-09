# SM-S936B / S936BXXUCZZI4 (Galaxy S25+) — One UI 9 beta 2

Port record for `pa2q-S936BXXUCZZI4`, derived from the shipped `pa1q-S931BXXUCZZI4`
target and the exact `S936BXXUCZZI4` boot image.

## 1. Kernel identity

| item | value |
| --- | --- |
| Device | Galaxy S25+ `SM-S936B` (Snapdragon 8 Elite, `pa2q`) |
| AP/PDA | `S936BXXUCZZI4` (One UI 9 beta 2) |
| Kernel release | `6.6.127-android15-8-p33f4ffe-abogkiS936BXXUCZZI4-4k` |
| Android / build ID | 17 (SDK 37) / `CP2A.260605.016`, SPL 2026-08-05 |
| boot.img | 101,122,048 bytes, header v4, kernel @ 0x1000 (39,115,264 bytes) |
| Image SHA-256 | `6b784d6af20f980ecc054e47000768ad8b5709c3b26ad66e396aaa57c0ddd350` |

Same `p33f4ffe` GKI build as `S931BXXUCZZI4` / `S938BXXUCZZI4`.

## 2. Comparison against the S931B ZZI4 Image

- Same decompressed Image size (39,115,264 bytes); 1,080,000 differing bytes
  (2.76 %) — per-device drivers/strings only.
- **kallsyms: 47 of 48 tracked values identical**, including `init_task`
  0x0234e2c0, `sysctl_bootid` 0x02683910, `nfulnl_logger` object
  0x02342080, tracefs event id 109.
- **One real delta**: `SLIDE_NFULNL_LOGGER_NAME_OFF` — the `nfnetlink_log`
  name string sits at `0x0179166c` on S936B (`0x01791638` on S931B ZZI4).
  The target carries the corrected value.
- **BTF: byte-identical** (6,425,607 bytes at 0x18aca6c, SHA-256
  `e13df32a…`).

## 3. P0 fingerprint

Regenerated from the exact S936B Image at probe offset 0x1f0000
(`p0_fingerprint.h` in this directory), 32 rows, readback-verified.
**5 of 32 rows differ** from the S931B table (slides 0x070000, 0x090000,
0x0a0000, 0x0c0000, 0x1a0000).

## 4. Symbol audit

The built app payload's undefined-import set is **identical** (82/82) to the
hardware-validated `pa1q-S931BXXUCZZI4` payload; the kernel-side verification
word from a differing S936B fingerprint row is present in the binary.

## 5. KernelSU

Reuses `kernelsu/ksud-pa1q-S931BXXUCZZI4-kdp`: the embedded module's import
set resolves against the identical S936B kallsyms table, and the loader is
vermagic-agnostic (kallsyms-relocation based). No separate S936B module build
is required.

## 6. Status

| stage | result |
| --- | --- |
| Symbol/table port | derived; single data-symbol delta corrected |
| P0 fingerprint | re-derived from exact Image (5/32 rows differ) |
| Payload build | 104,128 bytes, label `pa2q-S936BXXUCZZI4-app-physical-p0-oracle`, import audit clean |
| On-device validation | pending |

## Tracefs note

Same 6.6.127 pa2q/pa3q behavior as SM-S938B: the write window only opens after
the P0 oracle gate/probe writes, so the tracefs slide route is disabled for this
target too (APP_TRACEFS_SLIDE removed; P0-oracle-only). Artifact updated
(sha256 2b05d2c5...). On-device validation pending.
