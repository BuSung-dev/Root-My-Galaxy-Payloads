# SM-F956B / F956BXXU4EZIH

This contribution records the PomPomd shell profile for Fold6 firmware EZIH.
The offsets, native builds and dedicated KernelSU Next runtime were checked
against the supplied firmware. A root attempt on this release has not been
performed.

| Identifier | Value |
| --- | --- |
| Model | `SM-F956B` |
| Device | `q6q` |
| Firmware | `F956BXXU4EZIH` |
| Kernel release | `6.1.162-android14-11-34343818-abF956BXXU4EZIH` |
| Execution context | Android shell, UID 2000, through ADB or Shizuku |
| Status | Offline validated; hardware root untested |

## Firmware evidence

The supplied `boot.img` is 100,663,296 bytes with SHA-256
`3e7e2aa4eabd56bbd6d97367beeb7c5c3573872066efb9052d1b2efa60072827`.
Its version 4 boot header contains an ARM64 Image of 38,140,416 bytes with
SHA-256 `8a777690a06afcda55f89308778689f06f244224b7c566f49e114234f0744d9b`.
The linked text base is `0xffffffc008000000`; `text_offset` is zero.

The source audit checked 61 symbol and layout values. The EZIH entry uses
the compact 6.1 task/waiter layout and separates the writable boot-ID pointer
from its backing storage. Extraction and audit summaries are retained in
[reference/F956BXXU4EZIH](reference/F956BXXU4EZIH).

The physical load address `0xa8000000`, CPU choices and allocator timing
are inherited from the existing Fold6 profile. No XBL image was supplied,
so the load address has not been independently recovered for EZIH. Those
assumptions still need device validation.

## Artifacts

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `artifacts/q6q-F956BXXU4EZIH/payload` | 181,792 | `453bf870a8cf9305e97ea0a2efa6c7ccbf513816b9ee96a69d3ba0f204ebf6e7` |
| `artifacts/q6q-F956BXXU4EZIH/pompomsu-helper` | 33,856 | `b634d6f6fb6eb1b2451738b6ffba23a019fcd86f8d93655bdf71770ece966f46` |
| `kernelsu/ksud-q6q-F956BXXU4EZIH-kdp-next` | 4,253,800 | `e9b3677a24c203670ef21b7d4f531d746f8f2fe04603c155628536f2f91a7dce` |

The payload and helper are ELF64 AArch64 PIE executables. They are the same
files bound to the EZIH PomPomd installer profile. Runtime paths are supplied
through `GHOSTLOCK_HOME`, `GHOSTLOCK_HELPER_PATH` and `GHOSTLOCK_KSUD_PATH`.

The dedicated KernelSU Next 3.4.0 daemon embeds the module built for the full
EZIH release. The module is 456,288 bytes with SHA-256
`d35af25c46a5a90597338f29c2b98e2613d6190db241dba5c5e671610b082b27`.
The compressed module was decoded and compared with the standalone build.
The daemon's late-load guard requires the exact kernel release and runtime
key `samsung-F956BXXU4EZIH-6.1.162`.

All 190 module imports and 18 dynamically resolved names are present in the
kernel. The layout audit covers 16 types. One signedness metadata difference
in `kernel_param.level` has the same physical layout and matches the target's
signed load. The manual loader has an empty module version table, so these
checks do not provide a CRC compatibility proof.

## Source and build

A self-contained snapshot of the native shell code is in
[experimental/q6q-F956BXXU4EZIH](../experimental/q6q-F956BXXU4EZIH).
The source retains its existing firmware tables so the snapshot can be
rebuilt unchanged. Only the EZIH artifact set is included here. The metadata
and hashes are in `profile.json`; the Apache 2.0 license and project credits
are included with the source.

Build from that directory using Android NDK 29.0.14206865:

```sh
make NDK_ROOT=/path/to/android-ndk
```

On Windows:

```powershell
./build.ps1 -NdkRoot <ndk-directory>
```

The commands produce `build/payload` and `build/pompomsu-helper`. They do not
rebuild KernelSU. The matching daemon is a separate audited runtime artifact.
Its corresponding source and build tools are included in the
[runtime source archive](../kernelsu/source/F956BXXU4EZIH-runtime.tar.gz).

The Windows build script reproduced both distributed native files byte for
byte. `tools/verify-fold6-contributions.py` checks the catalog hashes, ELF
format and the app feed boundary without executing a device payload.

## Root My Galaxy integration

This profile is excluded from `support/targets-v3.json`. Root My Galaxy loads
its app artifact with `LD_PRELOAD` or `dlopen` and expects a constructor-based
shared library. The PomPomd artifact starts at `main` and uses a different
helper environment contract. Renaming it to `cve-2026-43499-app.so` would not
make it compatible.

The prior DZH5 Root My Galaxy app test that rebooted through the Qualcomm
watchdog is a separate, discarded build. Its app payload is not used by this
contribution. The existing DZH5 and DZI1 artifacts are preserved.

A working app profile will need its own integration and device validation.
Installing PomPomd on an EZIH phone confirms installation and firmware
selection; it does not establish successful temporary root.
