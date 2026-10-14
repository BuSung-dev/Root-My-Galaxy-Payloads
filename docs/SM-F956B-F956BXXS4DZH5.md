# SM-F956B / F956BXXS4DZH5

This is a PomPomd shell contribution for one exact Fold6 firmware. It is
excluded from the Root My Galaxy app feed.

| Identifier | Value |
| --- | --- |
| Model | `SM-F956B` |
| Firmware | `F956BXXS4DZH5` |
| Kernel release | `6.1.145-android14-11-33418572-abF956BXXS4DZH5` |
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

The matching GhostLock shell route was previously tested on DZH5. That result does not validate Root My Galaxy app execution of this artifact.

## Retained artifacts

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `artifacts/q6q-F956BXXS4DZH5/cve-2026-43499-app.so` | 142,832 | `fd046021cc599b0cd61de0fd63c489236802a85c09952ecef1fc60dedbaab557` |
| `artifacts/q6q-F956BXXS4DZH5/pompomsu-helper` | 32,664 | `ff6a067cd7701c06d93c2e0299e08d7384133d290e6bce6dd12e64b4760c2225` |
| `kernelsu/ksud-q6q-F956BXXS4DZH5-kdp-next` | 4,259,304 | `983047db4808a2df5818576f721312a90542c2978280905edbb9c5742df88a30` |

These files have not been replaced by the EZIH update. Their hashes and
status are recorded in [the experimental catalog](../support/experimental.json),
which the app does not consume.

The discarded S928-derived DZH5 app port that triggered a Qualcomm watchdog
reboot is a separate build. Its payload hash begins `4e6bb4e9` and it is not
included here. Working shell code does not validate that app-domain route.
