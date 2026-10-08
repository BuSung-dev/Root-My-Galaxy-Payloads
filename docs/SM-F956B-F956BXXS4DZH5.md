# SM-F956B / F956BXXS4DZH5

This profile packages the verified PomPomd Fold6 route for one exact target:

- Model: `SM-F956B`
- Firmware: `F956BXXS4DZH5`
- Kernel: `6.1.145-android14-11-33418572-abF956BXXS4DZH5`
- Platform: Snapdragon q6q, Android 14

The runtime profile in PomPomd matches all three identifiers before staging
the payload. A different regional Fold6 model, firmware build, or kernel
release is rejected.

## Artifacts

| Artifact | Size | SHA-256 |
| --- | ---: | --- |
| `artifacts/q6q-F956BXXS4DZH5/cve-2026-43499-app.so` | 142,832 | `FD046021CC599B0CD61DE0FD63C489236802A85C09952ECEF1FC60DEDBAAB557` |
| `artifacts/q6q-F956BXXS4DZH5/pompomsu-helper` | 32,664 | `FF6A067CD7701C06D93C2E0299E08D7384133D290E6BCE6DD12E64B4760C2225` |
| `kernelsu/ksud-q6q-F956BXXS4DZH5-kdp-next` | 4,259,304 | `983047DB4808A2DF5818576F721312A90542C2978280905EDBB9C5742DF88A30` |

The support feed exposes the app payload and `ksud` artifact. The helper is
kept beside the payload for the matching manual/bootstrap flow; its hash is
recorded above even though schema version 3 has no separate helper field.

This is a prebuilt, exact-firmware contribution. It does not claim that the
same binary is compatible with other Fold6 models or kernel releases.
