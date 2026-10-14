# SM-F956B / F956BXXS4DZI1

This is a PomPomd shell contribution for one exact Fold6 firmware. It is
excluded from the Root My Galaxy app feed.

| Identifier | Value |
| --- | --- |
| Model | `SM-F956B` |
| Firmware | `F956BXXS4DZI1` |
| Kernel release | `6.1.145-android14-11-33418572-abF956BXXS4DZI1` |
| Execution context | Android shell through ADB or Shizuku |
| Status | Shell-only; Root My Galaxy app integration unsupported |

## Artifact format

The historical filename `cve-2026-43499-app.so` is preserved, but this file is
an ELF64 AArch64 PIE executable with `/system/bin/linker64` as its interpreter
and a nonzero program entry point. It is not the constructor-based shared
library Root My Galaxy loads with `LD_PRELOAD` or `dlopen`. The filename alone
does not make these execution paths compatible.

The app feed previously included this profile. It was removed after the ELF
format review. Schema 3 also matches only model and numeric kernel version,
so DZH5 and DZI1 would both match `SM-F956B` with `6.1.145`; the firmware in
the display name did not enforce an exact build check.

This contribution was prepared from the DZI1 firmware. A successful Root My Galaxy app run is not recorded. The older DZH5 KernelSU daemon is retained as the original shared runtime; its compatibility with DZI1 still needs device validation.

## Retained artifacts

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `artifacts/q6q-F956BXXS4DZI1/cve-2026-43499-app.so` | 180,760 | `07f836aa819840a65b4c1bb675b1ee77bd7f29aed4cc9b665cbba90ebb97cea1` |
| `artifacts/q6q-F956BXXS4DZI1/pompomsu-helper` | 33,472 | `c67682af8936e990ea948ca5cee5a0f75311f4bb110d63099f3ba17b869be1d2` |
| `kernelsu/ksud-q6q-F956BXXS4DZH5-kdp-next` | 4,259,304 | `983047db4808a2df5818576f721312a90542c2978280905edbb9c5742df88a30` |

These files have not been replaced by the EZIH update. Their hashes and
status are recorded in [the experimental catalog](../support/experimental.json),
which the app does not consume.

The discarded S928-derived DZH5 app port that triggered a Qualcomm watchdog
reboot is a separate build. Its payload hash begins `4e6bb4e9` and it is not
included here. Working shell code does not validate that app-domain route.
