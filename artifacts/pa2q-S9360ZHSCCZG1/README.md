# pa2q-S9360ZHSCCZG1 artifacts

Device-tested 2026-09-08 on `SM-S9360` / `S9360ZHSCCZG1` (Hong Kong `OZS`).

| file | size | SHA-256 | use |
| --- | ---: | --- | --- |
| `cve-2026-43499-app.so` | 126624 | `0af3b5d358f85e8642a637a672e069823b856e6e43058478e7e4e6fddc5231a7` | app payload (`--run-payload` / LD_PRELOAD); this is the published feed artifact |
| `cve-2026-43499-root` | 26896 | `1acf40b8658a7971401d8d3d315bacf2d2bd00b1dea7cb9c0103e34f9907b17b` | root helper / su client (local build without `--ephemeral`) |
| `cve-2026-43499` | 105872 | `25e08ccccc3209f63a6c92c3588fd57cf7fb7a8ce3ee2b241d642cea7f5bac17` | root-umh LD_PRELOAD variant |
| `validation-2026-09-08-exploit.log` | 11097 | `826a6e6d4819505448d884d5451d7e869da7986803d5160ab677458a6d8beb51` | full hardware run log (attempt 1/8) |
| `validation-2026-09-08-reroot-reboot.log` | 13068 | `75d0fefc4be0a8dc3a9a80a53f399221adc7df276e3310a14c7226b545dd018c` | `tools/reroot.sh` end-to-end run after a real reboot (shell fallback path) |
| `validation-2026-09-08-reroot-clean.log` | 13022 | `11b90877feb2bd27ed2c3862c6abf035f2046c44a0138fddf2829fe9c632f618` | `tools/reroot.sh` end-to-end run after a real reboot (helper `--late-load` path) |

Firmware identity and offset derivation: [`../../docs/SM-S9360-S9360ZHSCCZG1.md`](../../docs/SM-S9360-S9360ZHSCCZG1.md).

KernelSU uses the published `kernelsu/ksud-s25u-kdp` (`android15-6.6` KMI).
`src/su_daemon.c` is locally patched to drop `late-load --ephemeral`, which
this ksud build does not accept; the helper's own private-namespace
`--late-load` path then works. See the porting record for the fallback and the
expected post-late-load framework (zygote) restart.
