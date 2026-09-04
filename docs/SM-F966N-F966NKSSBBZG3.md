# Galaxy Z Fold 7 Korean firmware port

This is an offline-derived profile for the Korean `SM-F966N` on the exact
`F966NKSSBBZG3` firmware. It is not a hardware-execution report.

```text
model: SM-F966N
region: KOO
AP/PDA: F966NKSSBBZG3
CSC: F966NOKRBBZG3
CP: F966NKSSBBZG1
display build: BP4A.251205.006.F966NKSSBBZG3
fingerprint: samsung/q7qksx/q7q:16/BP4A.251205.006/F966NKSSBBZG3_OKRBBZG3:user/release-keys
kernel release: 6.6.98-android15-8-pd6ff1cd-abogkiF966NKSSBBZG3-4k
kernel SHA-256: D026EA41C854BF3E6BD63B80F1D7D96EE589E65E3FAC2CA36AEAF2EB5FC13A12
```

The profile was derived from the exact Samsung firmware kernel. Symbols were
recovered with `vmlinux-to-elf`; the P0 fingerprint table was regenerated from
the raw ARM64 Image. Compared with the existing US Fold 7 profile, the
`kmalloc_caches` and `nfnetlink_log` string offsets were re-derived for this
build.

Hardware execution and KernelSU module integration remain separate validation
steps. Do not publish this profile as device-tested until those checks pass.
