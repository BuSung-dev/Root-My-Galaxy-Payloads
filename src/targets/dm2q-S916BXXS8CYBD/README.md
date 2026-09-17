# dm2q-S916BXXS8CYBD target profile

This directory contains the firmware-specific profile for the international
Galaxy S23+ `SM-S916B`, build `S916BXXS8CYBD`, kernel
`5.15.148-android13-8-29539737-abS916BXXS8CYBD`.

`target.h` and `p0_fingerprint.h` were derived from this firmware's own boot
image and recovered `vmlinux.elf`, not copied from another S23 target. The
profile uses the tracefs KASLR route, the controlled `mm_struct` group reclaim,
the MCAST stack writer, the closed fops/configfs route, and the physical P0
fail-closed fingerprint table at probe offset `0x1f0000`.

The profile was device-validated end-to-end through the Root My Galaxy app in
Shizuku mode. It is specific to `S916BXXS8CYBD` and must not be selected for
another firmware build or model without a separate port. See
`../../docs/SM-S916B-S916BXXS8CYBD.md`.
