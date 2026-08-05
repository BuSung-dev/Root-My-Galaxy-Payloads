# essi-A566EXXSCCZG6

Exact target profile for Samsung Galaxy A56 5G:

```text
model: SM-A566E
device: a56x
firmware: A566EXXSCCZG6 / OXECCZG6
display build: BP4A.251205.006.A566EXXSCCZG6
fingerprint: samsung/a56xnsxx/a56x:16/BP4A.251205.006/A566EXXSCCZG6_OXECCZG6:user/release-keys
kernel: 6.6.102-android15-8-abA566EXXSCCZG6-4k
```

`target.h` and `p0_fingerprint.h` were derived from the exact boot image and
recovered `vmlinux.elf` for this firmware. The profile uses the physical P0
oracle path and converts raw oracle offsets to the real KASLR/P0 slide.

Hardware status:

- app-domain payload/root daemon: device-tested on the connected SM-A566E;
- successful root evidence: `uid=2000 -> 0`, Kernel context `u:r:kernel:s0`;
- KernelSU module: exact-vermagic no-patch-text build and target-symbol audit
  passed;
- KernelSU late-load: device-tested with the no-patch-text artifact; Manager
  accepts KernelSU version code `32525`; see
  `docs/SM-A566E-A566EXXSCCZG6.md`.
