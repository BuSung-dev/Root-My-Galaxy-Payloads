# e2s-S926BXXUEDZDR

Exact target profile for Samsung Galaxy S24+:

```text
model: SM-S926B
device: e2s
firmware: S926BXXUEDZDR / S926BOXMEDZDR (EUX/OXM)
display build: BP4A.251205.006.S926BXXUEDZDR
fingerprint: samsung/e2sxeea/e2s:16/BP4A.251205.006/S926BXXUEDZDR:user/release-keys
kernel: 6.1.157-android14-11
```

`target.h` and `p0_fingerprint.h` were derived from the exact boot image and
recovered `vmlinux.elf` for this firmware. The profile uses the physical P0
oracle, MTE-aware KernelSnitch matching, and a fresh same-process P0 session.
Its default fast profile preserves every collision, fingerprint, alias, CFI,
read/write and root-result gate while reducing only repeated statistical
sampling.

Hardware status:

- app-domain payload/root daemon: device-tested on the connected SM-S926B;
- successful root evidence: `uid=2000 -> 0`, Kernel context `u:r:kernel:s0`;
- current 104128-byte upstream-integrated release artifact: passed three
  independent clean-boot runs on the exact firmware with the fast profile and
  eight independent P0/FOPS physical gates enabled by default;
- KernelSU module: exact-vermagic no-patch-text build and target-symbol audit
  passed;
- KernelSU late-load: device-tested; Manager accepts KernelSU version code
  `32525`, the target-specific `ksud` auto-detects `android14-6.1` without a
  compatibility bridge, the module is live without changing boot ID, and the
  privileged loader runs in `u:r:ksu:s0`;
- detailed derivation, hashes and validation notes are in
  `docs/SM-S926B-S926BXXUEDZDR.md`.
