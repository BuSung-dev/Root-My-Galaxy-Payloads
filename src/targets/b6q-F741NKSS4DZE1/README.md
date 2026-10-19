# b6q-F741NKSS4DZE1 target profile

Galaxy Z Flip6 Korea (`SM-F741N`) build `F741NKSS4DZE1`, kernel
`6.1.145-android14-11-33418572-abF741NKSS4DZE1`.

This is the first `b6q` (Galaxy Z Flip6) profile in the repository. The kernel
belongs to the same 6.1.145 branch as the S24 DZE2 builds: the full recovered
symbol table (103,520 name/type entries) differs from the US
`e2q-S926USQS6DZE2` kernel in two `.rodata` string labels only, both by
`+0x2e`, and every code/data symbol address is identical.

The only audited non-symbol location is the `"nfnetlink_log"` string that
locates the `nfulnl_logger` KASLR anchor:

```c
#define SLIDE_NFULNL_LOGGER_OFF 0x016a61b8ULL /* HK DZE2: 0x016a61e6 */
```

Verified by raw Image byte probe and by the `nfulnl_logger` first-qword
pointer.

The P0 fingerprint table is generated from the exact F741N raw kernel image
(probe `0x1f0000`, readback 256/256) and is 256/256 qwords identical to the
Hong Kong DZE2 table; it differs from the US table in one qword.

This payload is hardware-validated: the exact `cve-2026-43499-app.so` shipped
in `artifacts/b6q-F741NKSS4DZE1/` was run on an SM-F741N running this firmware
and completed the full chain - KASLR slide recovery, physical read/write,
KernelSU late-load and a granted `su` - together with the matching
`android14-6.1_kernelsu-b6q-F741NKSS4DZE1-kdp.ko` / `ksud-b6q-F741NKSS4DZE1-kdp`
pair.

The KernelSU pair is built from KernelSU v3.3.0 (version code 32601) with the
Samsung KDP/RKP/DEFEX changes and the no-patch-text late-load path; the module
`vermagic` matches this release exactly. Requires the official KernelSU v3.3.0
(32601) manager.

Detail: `docs/SM-F741N-F741NKSS4DZE1.md`. Use only on this exact firmware.
