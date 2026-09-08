# SM-S908B / S908BXXSMGZB2 artifact

The current `cve-2026-43499-app.so` is the cleanup-hardened payload validated on
physical SM-S908B / S908BXXSMGZB2 hardware through the official Root My Galaxy
execution path.

| Revision | Bytes | SHA-256 |
| --- | ---: | --- |
| Current published payload | 1757752 | `d2b52dc52a69571a145a64f0f57ae53884383a996661066ca84b52a45eb691bf` |
| Previous hardware reference | 1757688 | `19a219a94660a60c06a4c34f9a873b5c9ccacda7129d409ba8486ffe61453235` |

The current revision adds `reset_pipe_attempt()` before the CFI retry path
returns status 70. This makes the pipe-attempt lifecycle explicit before the
external supervisor starts a new child. The exploit offsets, physical-KASLR
scan, ARM32 stage, CFI and PhysRW logic, root path, and KernelSU candidate are
unchanged.

The rebuilt binary completed **7 / 7** reboot-separated full-path runs. The raw
logs are under
`docs/validation/SM-S908B-S908BXXSMGZB2/2026-09-04-official-app/`.
The previous payload identity remains documented above and in the main device
validation record instead of being silently replaced.

The size is intentional. This remains an instrumented validation binary, not a
stripped or repadded 104,128-byte release substitute.

The embedded ARM32 stage is byte-identical to the previous validated build:

`023190461719fc4cc11a7d114ff8da5132f1afa0eba32c907053e83682914152`

It retains the historical diagnostic banner containing `b0q`. That text is
inherited provenance, not the selected device identity; the validated target
label is `S908BXXSMGZB2`.

The public source is available at
`src/targets/b0s-S908BXXSMGZB2/ionstack-s908b-gzb2/`.

The KernelSU package used during validation is intentionally not presented as
an S908B-exact build. It originated from the existing S901B GZD7 KDP candidate;
its empirical compatibility on this device is documented in
`docs/SM-S908B-S908BXXSMGZB2.md`.
