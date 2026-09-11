# SM-S948U S948USQS3AZF1 porting record (m3q, Snapdragon)

Target identity:

```text
model: SM-S948U
device/product: m3q / m3qsqw
AP: S948USQS3AZF1_OYN3AZF1
display build: BP4A.251205.006
fingerprint: samsung/m3qsqw/m3q:16/BP4A.251205.006/S948USQS3AZF1_OYN3AZF1:user/release-keys
kernel release: 6.12.30-android16-5-pd30ff70-abogkiS948USQS3AZF1-4k
```

## Offsets

All exploit offsets in `src/targets/m3q-S948USQS3AZF1/target.h` were
verified byte-for-byte identical against the sibling `m3q` AZG3 image;
only the identity strings, the raw Image hash, and the P0 fingerprint
table are per-build:

- raw `kernel.img` SHA-256:
  `d8aaf115aaed7aaeb8e784a0ad09b13cb7efcfcd16a6238e1c4e443454e0d2f5`
- P0 probe offset `0x1f0000`, 32 rows independently re-read and verified
  (`src/targets/m3q-S948USQS3AZF1/p0_fingerprint.h`).

## Verification status

Confirmed working: full chain through a downstream temp-root app in
Shizuku (tracefs KASLR) mode, including the physical-P0 oracle fallback,
KernelSU 32525 late-load (`--kmi android16-6.12`), and granted `su` under
enforcing SELinux. Pair with KernelSU Manager v3.2.5 (32525).

Note: upstream KernelSU v3.3.0/32601 was tried on this kernel and its
embedded `android16-6.12` LKM panicked during late-load, so the feed pins
the 32525 `ksud` build (`kernelsu/ksud-m3q-S948USQS3AZF1-kdp`,
SHA-256 `3ce5753203c93f4d733fbc10eebd7a69152189afb1d2a15bfd855bd6b5d4f622`).
