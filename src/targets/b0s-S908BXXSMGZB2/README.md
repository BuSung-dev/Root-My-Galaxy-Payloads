# SM-S908B / S908BXXSMGZB2 source provenance

This directory carries the source provenance for the hardware-tested
`SM-S908B` / `S908BXXSMGZB2` payload.

## Provenance

- Upstream: `sarabpal-dev/IonStack-S22U`
- Base commit: `2b4e8a64b78d18f236f0d5b26cfd204bc46363ce`
- Target used for the validated rebuild: `S908BXXSMGZB2`
- Reproduction toolchain: Android NDK r28c (`28.2.13676358`)
- Main payload API: 35
- Embedded ARM32 API: 28
- Current payload SHA-256: `d2b52dc52a69571a145a64f0f57ae53884383a996661066ca84b52a45eb691bf`
- Previous hardware-reference SHA-256: `19a219a94660a60c06a4c34f9a873b5c9ccacda7129d409ba8486ffe61453235`
- Embedded ARM32 stage SHA-256: `023190461719fc4cc11a7d114ff8da5132f1afa0eba32c907053e83682914152`

The nested `ionstack-s908b-gzb2/` tree preserves the build-relevant source
state for the current published payload. It intentionally retains the diagnostic
and retry instrumentation present during hardware validation; this contribution
does not substitute a stripped, repadded, or merely behaviorally equivalent
build.

## Current cleanup-hardened revision

The current published payload adds one lifecycle fix to the previously tested
source state: `reset_pipe_attempt()` is called before the CFI retry path returns
status 70 to the external supervisor. This prevents pipe-attempt state from
surviving into the next independent child.

The rebuilt payload was then tested through the official Root My Galaxy
execution path in seven reboot-separated hardware runs. All seven completed
bootstrap root and KernelSU verification. The raw logs are under
`docs/validation/SM-S908B-S908BXXSMGZB2/2026-09-04-official-app/`.

No S908B offset, physical-KASLR rule, ARM32 stage, CFI primitive, PhysRW logic,
root path, or KernelSU candidate was changed for this revision.

## Snapshot fidelity and inherited metadata

The source tree is intentionally not normalized after validation. In
particular, the inherited IonStack `README.md` still describes the earlier
SM-S908W/b0q target, and
`src/targets/S908BXXSMGZB2/target.h` retains an inherited
`BUILD_FINGERPRINT` string for that older target. The latter macro is not
referenced by the compiled source outside target headers; the validated target
label is `S908BXXSMGZB2`, and the hardware-proven payload does not contain the
legacy S908W fingerprint string.

The embedded ARM32 stage also retains the historical diagnostic banner
`CVE-2026-43499 32-bit ARM stage (b0q)`. That string is a provenance/debug
label, not a target-selection mechanism. It is present in both tested payload revisions and is documented rather than
rewritten, because rewriting it would produce another payload identity.

Those inherited metadata strings remain documented rather than silently
rewritten. The lifecycle change above is recorded as a separate rebuilt and
hardware-validated payload revision, while the previous reference identity is
kept in this README and in the validation document.

The original `.gitignore` from the pinned base commit is retained. The only
file omitted from the CI source snapshot is `MEMORY.md`, a non-build developer
notebook containing machine-local development paths. Its omission does not
change any build input.

## Integration scope

This source snapshot is supplied for reproducibility and review. It is **not**
wired into the repository top-level `Makefile` by this contribution, and this
contribution does not add an automatic support-feed entry. The validated scope
remains the exact `SM-S908B / S908BXXSMGZB2` build.

The modifications relative to the pinned upstream commit include the Root My
Galaxy root-helper handoff, cleanup/retry lifecycle changes, corrected GZB2
physical kernel load base, independent physical-KASLR slot resolution, and
separation of physical direct-map translation from the virtual KASLR slide.

## Deterministic rebuild

Use Android NDK r28c (`28.2.13676358`) on Linux x86_64. The source
Makefile prefers a host `arm-linux-gnueabi-gcc` for exp32 when one is available,
so the command below sets a deliberately unavailable `HOST_CC32` name and
thereby forces the exact NDK ARM32 compiler path used by the successful CI
reproduction. It also pins the NDK root/toolchain and disables Buildroot
selection.

Run from the upstream repository root after placing this source directory:

```sh
NDK=/absolute/path/to/android-ndk-r28c

env -u CC -u NDK_ROOT -u ANDROID_NDK_ROOT -u NDK_TOOLCHAIN \
  ANDROID_NDK_HOME="$NDK" \
  make -C src/targets/b0s-S908BXXSMGZB2/ionstack-s908b-gzb2 \
    PROJECT=S908BXXSMGZB2 \
    API=35 \
    API32=28 \
    NDK_ROOT="$NDK" \
    NDK_TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64" \
    HOST_CC32=__force_ndk_arm32__ \
    USE_BUILDROOT= \
    clean preload
```

Expected outputs:

```text
src/targets/b0s-S908BXXSMGZB2/ionstack-s908b-gzb2/
  build/S908BXXSMGZB2/bin/cve-2026-43499
  build/S908BXXSMGZB2/bin/cve-exp32
```

The rebuilt current payload must have SHA-256:

```text
d2b52dc52a69571a145a64f0f57ae53884383a996661066ca84b52a45eb691bf
```

The embedded `cve-exp32` stage must have SHA-256:

```text
023190461719fc4cc11a7d114ff8da5132f1afa0eba32c907053e83682914152
```

The source retains the upstream Apache-2.0 license. The modification summary
and inherited-metadata disclosure are contained in this README and in the
validation document under `docs/`.
