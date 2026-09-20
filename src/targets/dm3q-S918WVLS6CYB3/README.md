# dm3q-S918WVLS6CYB3 target profile

Firmware-specific profile for the Canadian Galaxy S23 Ultra `SM-S918W`
(`dm3q`, product `dm3qcsx`), build `S918WVLS6CYB3`, kernel
`5.15.148-android13-8-29539737-abS918WVLS6CYB3`.

`target.h` and `p0_fingerprint.h` were derived from this firmware's own boot
image and recovered `vmlinux.elf` (symbol table + BTF), not copied from
another S23 target. The profile uses the tracefs KASLR route, the controlled
`mm_struct` group reclaim, the MCAST stack writer, the closed fops/configfs
route, and the physical P0 fail-closed fingerprint table at probe offset
`0x1f0000`.

Values that differ from the sibling `dm2q-S916U1UES6CYB3` 5.15.148 profile
(re-derived here): `ANON_PIPE_BUF_OPS_OFF`, `ASHMEM_FOPS_OFF`,
`KMALLOC_CACHES_OFF`, `ashmem_misc` base, `SLIDE_NFULNL_LOGGER_NAME_OFF`,
plus identity labels, fingerprint, and the P0 table. All other offsets were
independently verified identical.

Device validation is pending. It is specific to `S918WVLS6CYB3` and must not
be selected for another firmware build or model without a separate port.
