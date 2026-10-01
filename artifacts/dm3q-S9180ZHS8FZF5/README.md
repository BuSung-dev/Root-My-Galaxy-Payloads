# dm3q-S9180ZHS8FZF5 payload

Profile for Galaxy S23 Ultra SM-S9180 (`dm3q` / `dm3qzhx`) on firmware
`S9180ZHS8FZF5`, kernel `5.15.189-android13-8-33413713-abS9180ZHS8FZF5`.

## Status

Test in progress — profile and offsets statically validated; device
validation on physical hardware in progress
(see `docs/SM-S9180-S9180ZHS8FZF5.md`).

## Files

- `cve-2026-43499-app.so` — app-domain release payload
  (`make TARGET=dm3q-S9180ZHS8FZF5 release`).

## v3.3.0 plan

Current KernelSU pair is historical v3.2.5. v3.3.0 (32601) upgrade status:
patch ready
(`kernelsu/patches/KernelSU-v3.3.0-samsung-kdp-rkp-defex.patch`), binary
rebuild pending (see `kernelsu/REBUILD-dm3q-v3.3.0.md`).
