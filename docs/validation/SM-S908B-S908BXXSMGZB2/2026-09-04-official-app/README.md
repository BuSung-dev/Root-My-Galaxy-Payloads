# SM-S908B GZB2 official-app validation — 2026-09-04

These logs were collected on a physical Galaxy S22 Ultra `SM-S908B` running
`S908BXXSMGZB2` / kernel
`5.10.237-android12-9-31999025-abS908BXXSMGZB2`.

The rebuilt payload was run through the official Root My Galaxy Shizuku,
staging, supervisor, helper, and KernelSU late-load path. The phone was rebooted
between every run.

Payload:

- bytes: `1757752`
- SHA-256: `d2b52dc52a69571a145a64f0f57ae53884383a996661066ca84b52a45eb691bf`
- embedded ARM32 SHA-256: `023190461719fc4cc11a7d114ff8da5132f1afa0eba32c907053e83682914152`

| Run | Log | Successful attempt | Physical slot | Result |
| ---: | --- | ---: | ---: | --- |
| 01 | [`143649`](RootMyGalaxy-20260904-143649-succeeded.log.txt) | 7 / 24 | 61 | root + KernelSU |
| 02 | [`144652`](RootMyGalaxy-20260904-144652-succeeded.log.txt) | 7 / 24 | 24 | root + KernelSU |
| 03 | [`145430`](RootMyGalaxy-20260904-145430-succeeded.log.txt) | 1 / 24 | 30 | root + KernelSU |
| 04 | [`145742`](RootMyGalaxy-20260904-145742-succeeded.log.txt) | 2 / 24 | 0 | root + KernelSU |
| 05 | [`150418`](RootMyGalaxy-20260904-150418-succeeded.log.txt) | 1 / 24 | 59 | root + KernelSU |
| 06 | [`150955`](RootMyGalaxy-20260904-150955-succeeded.log.txt) | 1 / 24 | 51 | root + KernelSU |
| 07 | [`151659`](RootMyGalaxy-20260904-151659-succeeded.log.txt) | 5 / 24 | 50 | root + KernelSU |

Result: **7 / 7 successful full-path runs**.

Across the seven runs, the new cleanup marker was reached 17 times before an
external-supervisor retry. No kernel panic or
`F_SETPIPE_SZ: Operation not permitted` failure was observed.

The original `F_SETPIPE_SZ` failure was not deliberately reproduced in this
set, so these logs are not presented as a direct reproduction-and-fix of that
specific error. They document the rebuilt payload's regression and stability
validation on hardware.
