# essi-A566BXXSCCZG7

Exact target profile for Samsung Galaxy A56 5G:

```text
model: SM-A566B
device: a56x
firmware: A566BXXSCCZG7 / OXMCCZG7
display build: BP4A.251205.006.A566BXXSCCZG7
fingerprint: samsung/a56xnaeea/a56x:16/BP4A.251205.006/A566BXXSCCZG7_OXMCCZG7:user/release-keys
kernel: 6.6.102-android15-8-abA566BXXSCCZG7-4k
```

`target.h` and `p0_fingerprint.h` reuse the A566EXXSCCZG6 values, which were
hardware-validated on this SM-A566B running A566BXXSCCZG7. The two firmware
builds share the `6.6.102-android15-8` kernel and the physical P0 oracle path
that converts raw oracle offsets to the real KASLR/P0 slide.

Hardware status:

- app-domain payload/root daemon: device-tested on the connected SM-A566B;
- successful root evidence: app UID `10396 -> 0`, `root p0 reference holder
  ready=1`;
- KernelSU late-load: device-tested with the shared
  `ksud-A566EXXSCCZG6-kdp` artifact; KernelSU reports working `<LKM>` and the
  Manager accepts version code `32525`;
- see `docs/SM-A566B-A566BXXSCCZG7.md`.