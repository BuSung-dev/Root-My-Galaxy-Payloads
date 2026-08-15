# e3q-S928BXXS6DZF2

```text
device: Samsung Galaxy S24 Ultra (SM-S928B, e3q)
firmware: S928BXXS6DZF2 / TPA
display build: BP4A.251205.006.S928BXXS6DZF2
system fingerprint: samsung/e3qxxx/e3q:16/BP4A.251205.006/S928BXXS6DZF2:user/release-keys
bootimage fingerprint: samsung/e3qxxx/e3q:14/UP1A.231005.007/S928BXXS6DZF2:user/release-keys
kernel: 6.1.145-android14-11
SoC: Qualcomm Snapdragon 8 Gen 3 for Galaxy (SM8650, pineapple)
```

`target.h` is derived from the existing `e3q-S928USQS6DZF2` Snapdragon target:
the SM-S928B and SM-S928U share the same SM8650 SoC and the same
`6.1.145-android14` kernel series, so all symbol offsets and struct layouts are
identical. Only `BUILD_FINGERPRINT` and the variant label differ.
`p0_fingerprint.h` is generated from the exact raw SM-S928B `S928BXXS6DZF2`
Image and contains 32 target kernel page fingerprints.

## Hardware validation status (2026-08-15)

This profile has been run on hardware (SM-S928B, S928BXXS6DZF2):

- slide KASLR leak: OK
- pipe oracle preparation: OK
- gate physical write: OK (`physical write ok=1`)
- **gate hit: OK (`gate hits=1`, reproduced multiple times, ~1/10 probability)**
- probe physical write: OK (`ok=1`)
- probe scan (`scan_p0_pipe_oracle`): **kernel panic (deterministic)**

The gate hit confirms the Snapdragon S928U baseline is correct for the S928B
international variant. The remaining issue is a deterministic panic after the
probe slot's second slide physical write (gate slide `pselect ret=2` vs probe
slide `ret=1`), before the fingerprint sampling in `scan_p0_pipe_oracle()`.

Full hardware test record:
`docs/SM-S928B-S928BXXS6DZF2.md`.
