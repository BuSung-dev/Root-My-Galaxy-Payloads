# SM-S928B — S928BXXS6DZF2 hardware test record

## Device identity

```text
model: SM-S928B (Galaxy S24 Ultra international, codename e3q)
SoC: Qualcomm Snapdragon 8 Gen 3 for Galaxy (SM8650, pineapple)
region/CSC: TPA (Panama)
AP/PDA: S928BXXS6DZF2
security patch: 2026-06-05
display build: BP4A.251205.006.S928BXXS6DZF2
system fingerprint: samsung/e3qxxx/e3q:16/BP4A.251205.006/S928BXXS6DZF2:user/release-keys
bootimage fingerprint: samsung/e3qxxx/e3q:14/UP1A.231005.007/S928BXXS6DZF2:user/release-keys
kernel: 6.1.145-android14-11-33419968-abS928BXXS6DZF2
```

Note the mixed state: the `system` partition reports Android 16, while
`boot`/`kernel` is still the Android 14 image (`6.1.145-android14`), the same
three-part version as the existing `e3q-S928USQS6DZF2` target.

## Porting approach

The SM-S928B shares the same Snapdragon SM8650 SoC and the same
`6.1.145-android14` kernel series as the existing SM-S928U/S928U1 target. The
international S24 Ultra is Snapdragon-only worldwide (unlike the S24/S24+,
which split Snapdragon vs Exynos). This is confirmed from the live device:

```text
getprop ro.hardware       -> qcom
getprop ro.board.platform -> pineapple
getprop ro.soc.model      -> SM8650
```

Therefore `target.h` is derived from `e3q-S928USQS6DZF2` with only
`BUILD_FINGERPRINT` and the variant label changed. `p0_fingerprint.h` is
generated from the exact raw SM-S928B `S928BXXS6DZF2` Image.

## Hardware test results (2026-08-15)

| Stage | Result |
| --- | --- |
| slide KASLR leak | OK |
| pipe oracle preparation | OK |
| gate physical write | OK (`physical write ok=1`) |
| **gate hit** | **OK (`gate hits=1`, reproduced multiple times, ~1/10 probability)** |
| probe physical write | OK (`ok=1`) |
| probe scan (`scan_p0_pipe_oracle`) | **kernel panic (deterministic)** |

### Successful gate-hit run (key log)

```text
label=e3q-S928BXXS6DZF2-app-physical-p0-oracle slide=pselect main=pselect
p0 pipe oracle prepared base=ffffff89a0eb0000 object_index=30 gate_target=ffffff89a0eb0800 pipes=240 gate_slots=1
mm leaked=ffffff8825a54c00 base=ffffff8825a50000 object_index=19
slide pselect returned nfds=320 ret=2 errno=0 elapsed_usec=100199 ... sched_ok=1   (gate, write_window=1)
p0 physical write status=0 ok=1
p0 gate marker pipe=117 offset=0
p0 pipe gate hits=1 changed=0
p0 reference keeper pid=14831 pipe=117
slide pselect returned nfds=320 ret=1 errno=0 elapsed_usec=100185 ... sched_ok=1   (probe)
p0 physical write status=0 ok=1
p0 physical slot=1 write attempt=1/1 delay=25000 nfds=320 pad=0
(panic here — no "p0 fingerprint sample" output)
```

### Deterministic panic point

Three independent gate-hit runs (rounds 3, 10, 19) all panic at the same point:
immediately after the probe slot's second slide physical write succeeds
(`p0 physical slot=1 write attempt=1/1`), before `scan_p0_pipe_oracle()` emits
its first `p0 fingerprint sample` line.

The gate slide's pselect returns `ret=2`, the probe slide returns `ret=1`.
The panic is suspected to be leftover rt-mutex state from the second slide
trigger, corrupting the reclaim pipe state before the scan reads it.

## Conclusion

The gate hit confirms the Snapdragon S928U baseline is correct for the S928B
international variant, and this is the first hardware-validated gate hit for
the e3q (S24 Ultra Snapdragon) family. The remaining probe-stage panic needs
further investigation (or maintainer guidance).
