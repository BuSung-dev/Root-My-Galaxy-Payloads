# SM-F956B / F956BXXS4DZI1

This profile adds support for the Samsung Galaxy Z Fold6 SM-F956B running
firmware F956BXXS4DZI1.

The target is identified by the exact model, firmware build, and kernel
release below. Other Fold6 variants and kernel releases remain outside the
scope of this profile.

- Model: `SM-F956B`
- Firmware: `F956BXXS4DZI1`
- Kernel: `6.1.145-android14-11-33418572-abF956BXXS4DZI1`
- Platform: Snapdragon q6q, Android 14

## Artifacts

| Artifact | Size | SHA-256 |
| --- | ---: | --- |
| `artifacts/q6q-F956BXXS4DZI1/cve-2026-43499-app.so` | 180,760 | `07F836AA819840A65B4C1BB675B1EE77BD7F29AED4CC9B665CBBA90EBB97CEA1` |
| `artifacts/q6q-F956BXXS4DZI1/pompomsu-helper` | 33,472 | `C67682AF8936E990EA948CA5CEE5A0F75311F4BB110D63099F3BA17B869BE1D2` |
| `kernelsu/ksud-q6q-F956BXXS4DZH5-kdp-next` | 4,259,304 | `983047DB4808A2DF5818576F721312A90542C2978280905EDBB9C5742DF88A30` |

The DZH5 profile remains available as a separate target. The KernelSU loader
is shared because both profiles use the same q6q Android 14 and kernel family.
