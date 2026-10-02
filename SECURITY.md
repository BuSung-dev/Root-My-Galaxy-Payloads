# Security Policy

This repository contains a proof-of-concept exploit chain for CVE-2026-43499
plus KernelSU kernel modules. Treat all binaries under `artifacts/` and
`kernelsu/` as untrusted until you have verified them against source.

## Reporting

- Do not open public issues for suspected vulnerabilities in this tooling.
- Contact the repository owner via a private channel (GitHub private
  vulnerability reporting if enabled, otherwise a private fork/PR).
- Include: affected `TARGET`/payload, commit hash, reproduction steps,
  and the output of `make info TARGET=...`.

## Trust boundaries (by design)

- `src/su_daemon.c` grants root to any client whose UID matches
  `allowed_client_uid` (default `2000`). Compromise of that UID = root.
- `CVE43499_ROOT_HELPER` / `--run-payload` / marker paths must point at
  attacker-controlled `/data/local/tmp` only in lab devices. Never run
  these helpers on production devices.
- `support/targets-v3.json` currently ships **without per-artifact hashes
  or signatures** (see README). Verify artifacts out-of-band before flashing.

## Hygiene

- Never commit `boot.img*`, `vmlinux*`, `*.btf`, `*.zip`, or `diagnostics/`.
- Offline analysis tools (`tools/*.py`, `kernelsu/tools/*.py`) take file
  paths from argv; do not pipe untrusted paths into automated pipelines
  without sandboxing.
