# gts10fewifi-X520XXS6BZA3

Exact-build compatibility profile for Samsung Galaxy Tab S10 FE:

```text
model: SM-X520
device: gts10fewifi
firmware: X520XXS6BZA3 / OXM6BZA3
display build: BP2A.250605.031.A3.X520XXS6BZA3
kernel: 6.6.77-android15-8-abX520XXS6BZA3-4k
page size: 4096
```

Current status:

- `target.h` and `p0_fingerprint.h` are derived from the exact BZA3 Image,
  recovered kallsyms, BTF, and bootloader;
- the release app payload is
  `artifacts/gts10fewifi-X520XXS6BZA3/cve-2026-43499-app.so`;
- the exact-release KernelSU pair is
  `kernelsu/android15-6.6_kernelsu-X520XXS6BZA3-kdp.ko` and
  `kernelsu/ksud-X520XXS6BZA3-kdp`;
- the KernelSU module has exact target vermagic, an empty `__versions`
  section, zero missing target symbols, and zero CRC mismatches;
- offline validation is complete; tablet execution and race-timing validation
  are still in progress.

A56 binaries remain incompatible with this profile; only the X520-specific
artifacts above may be selected.
