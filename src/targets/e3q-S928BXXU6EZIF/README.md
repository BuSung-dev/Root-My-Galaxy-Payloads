# e3q-S928BXXU6EZIF target profile

Galaxy S24 Ultra International (`SM-S928B`) build `S928BXXU6EZIF`, kernel
`6.1.162-android14-11-34343818-abS928BXXU6EZIF` (One UI 9.0 / Android 17).

This is the first 6.1.162-generation profile in the repository. The E3Q kernel
moved from 6.1.145 to 6.1.162 - every section grew, per-symbol deltas differ,
and no existing S24 Ultra payload applies.

- All 23 `*_OFF` were re-resolved by symbol identity from the recovered
  vmlinux (107,567 symbol records).
- Two non-symbol locations were measured against the raw Image:

```c
#define SLIDE_NFULNL_LOGGER_OFF 0x016ba45dULL        /* HK DZE2: 0x016a61e6 */
#define SLIDE_RANDOM_BOOT_ID_DATA_OFF 0x02396a98ULL  /* HK DZE2: 0x023762f0 */
```

- The P0 fingerprint table is regenerated from the exact EZIF raw kernel image
  (probe `0x1f0000`, readback 256/256); 235/256 qwords differ from both the
  Hong Kong and US DZE2 tables.
- `SLIDE_TRACEFS_EVENT_ID` stays 106 (`sched_blocked_reason` at
  `__ftrace_events` index 86, same as DZE2); to be confirmed on device via
  `cat /sys/kernel/tracing/events/sched/sched_blocked_reason/id`.

The `remove_waiter` race window was re-verified instruction by instruction
(154/154 identical after relocation normalization), so the exploit chain
assumptions carry over unchanged.

The KernelSU pair is built from KernelSU v3.3.0 (version code 32601) with the
Samsung KDP/RKP/DEFEX no-patch-text path, via the DDK rebuild pipeline against
this exact release - module `vermagic` matches. Static audit passed (202
imports / 0 missing, 70 `kallsyms` resolutions, 0 CRC mismatches); device-side
module load is pending.

Status: calibration and static audits are complete; on-device validation is in
progress (the same payload is currently being exercised through the RootMyS24
beta channel). Treat this profile as test-in-progress until a hardware report
is attached.

Detail: `docs/SM-S928B-S928BXXU6EZIF.md`. Use only on this exact firmware.
