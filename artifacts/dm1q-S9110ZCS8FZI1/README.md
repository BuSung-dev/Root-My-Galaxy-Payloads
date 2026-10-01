# Galaxy S23 SM-S9110 (S9110ZCS8FZI1) payload
Exact firmware profile for the China Galaxy S23 base model (`SM-S9110`, `dm1q`,
Snapdragon 8 Gen 2 / SM8550) on firmware `S9110ZCS8FZI1`
(`samsung/dm1qzcx/dm1q:16/BP4A.251205.006/S9110ZCS8FZI1`), kernel
`5.15.189-android13-8-3251900-abS9110ZCS8FZI1`.
Status: **built and statically verified against this firmware's own kernel
image; not yet executed on hardware.** Everything the chain installs is
volatile per boot, because no boot image is modified.
## Why this profile exists

The kernel's `.text` is shared with the hardware-proven `dm2q-S916BXXSAFZG1`
and `dm3q-S918BXXSAFZF5` builds of the same `5.15.189-android13-8` family, but
the data-segment symbols sit `0x1C0` higher in this image (the
`SLIDE_NFULNL_LOGGER_NAME_OFF` string moved by a different amount, as it tracks
the rodata layout, not the pointer arrays):
| Macro | `dm2q-S916BXXSAFZG1` | this build |
| --- | --- | --- |
| `KMALLOC_CACHES_OFF` | `0x020645f8` | `0x020647b8` |
| `ANON_PIPE_BUF_OPS_OFF` | `0x01e7f5e0` | `0x01e7f7a0` |
| `ASHMEM_FOPS_OFF` | `0x0200d638` | `0x0200d7f8` |
| `SLIDE_NFULNL_LOGGER_NAME_OFF` | `0x01d5de62` | `0x01d5de6d` |
Reusing a neighbouring S23 payload therefore fails at the KASLR/P0 stage. See
[`../../docs/SM-S9110-S9110ZCS8FZI1.md`](../../docs/SM-S9110-S9110ZCS8FZI1.md)
for the complete derivation and the checks that were run against the image.
## Files
| File | SHA-256 |
| --- | --- |
| `cve-2026-43499-app.so` | `f8a9560d76bd4d7f86d3f0822960a40fff31a7eb579a0bbe95ef37407ac3909f` |
| `../../kernelsu/ksud-dm3q-S918BXXSAFZF5-kdp` | `5da5818d36da2d589496f91016078a43f50489e5c98b319db4eaa5ee475b86bd` |
The late-load binary is the `android13-5.15.189` no-patch-text build. Its
embedded module is byte-identical to the module inside
`ksud-dm2q-S916BXXSAFZG1-kdp`; both files are the same bytes as the loader that
was hardware-verified on `dm3q-S918BXXSAFZF5`, the closest verified device in
the same kernel-build family. The profile is published against the `dm3q`
name and audited separately against the recovered `SM-S9110` kernel
(200 undefined imports, 0 missing from the device's kallsyms names).

`cve-2026-43499-app.so` is a bionic shared library: no `PT_INTERP`, and
`DT_NEEDED` is only `libc.so` and `libdl.so`. That matters because the app path
loads it with `LD_PRELOAD` into `/system/bin/sh` or `dlopen`s it from the root
helper. A glibc-linked ELF (`ld-linux-aarch64.so.1 not found`) cannot be used
here; that failure mode was observed once and is now a build check.
## Build
```sh
make TARGET=dm1q-S9110ZCS8FZI1 ANDROID_NDK_HOME=/path/to/android-ndk release
```
The target selects the MCAST stack writer (`-DSLIDE_STACK_WRITER=1`). The
checked-in bytes are `104128` bytes after the release rule pads to the fixed
`APP_RELEASE_SIZE`.
Outputs:
```text
build/dm1q-S9110ZCS8FZI1/cve-2026-43499-app.so
```
The `release` rule compiles with `-Oz -g0` and links with
`--gc-sections --icf=all -s`, then truncates to `APP_RELEASE_SIZE`. Re-running
it reproduced the published SHA-256 byte for byte.
## Chain
`src/targets/dm1q-S9110ZCS8FZI1/target.h` uses the tracefs KASLR route with the
physical P0 oracle as the fallback, the MCAST waiter writer, controlled
32-object `mm_struct` collection, shaped order-3 SKB reclaim, fake ashmem
`fops`, configfs arbitrary read/write, pipe physical read/write, and a root
usermode helper. Its P0 geometry follows the `dm1q` base-model family (same as
`dm1q-S911U1UES6DYI3`):
```c
#define P0_ORACLE_PROBE_OFFSET 0x1f0000ULL
#define SLIDE_P0_OFFSET_CANDIDATES /* 32 entries, 0x10000 steps */
#define SLIDE_MAX_ATTEMPTS 32
```
`target.h` also fixes `SLIDE_TRACEFS_EVENT_ID 108` and the two blocking
`schedule` return offsets (`worker_thread+0x…`, `wait_for_vfork_done+0x…`) from
this image's disassembly. `p0_fingerprint.h` is generated from this image
(32 rows, 8 qwords per row, read back and re-verified during generation).
## Usage notes
- Run soon after boot, ideally within the first two minutes.
- Failures are normal and mostly harmless: some attempts end in a clean
  pre-writer failure, a panic reboot, or a hard freeze that needs a power-button
  hold. After the stack-writer stage the engine refuses in-boot retries
  (`stack writer ran; refusing retry on this boot`) — reboot and run again.
- The app can run the payload from the APK's own copy of this file, so no
  network access is required for this profile.
## KernelSU
The KernelSU stage is late-load: the root helper runs the `ksud` binary above,
which hand-relocates its embedded module through `/proc/kallsyms`. The module
was built with the Samsung KDP/RKP/DEFEX patch, live text patching disabled, and
an empty `__versions` section (this kernel sets `CONFIG_MODVERSIONS=y` without
`CONFIG_MODULE_FORCE_LOAD`).
Audit against the recovered kernel of this firmware:
```text
undefined imports: 200
missing from the target symbol table: 0
vermagic (module): 5.15.189-android13-8-33413713-abS916BXXSAFZG1 SMP preempt mod_unload modversions aarch64
vermagic (device): 5.15.189-android13-8-3251900-abS9110ZCS8FZI1 SMP preempt mod_unload modversions aarch64
```
The release-string part of `vermagic` differs, but with `CONFIG_MODVERSIONS=y`
the kernel compares only the part after the first space, which is identical.
Module initialization on this device has not been tested; Samsung RKP/KDP hook
setup can still reboot the phone. If it fails, the `ksud-gts9-X710XXS6EZF1-kdp`
5.15.189 module is the only alternate that is device-proven (on the Tab S9) and
also audits clean against this kernel.
## Authorship
This profile, the payload derivation, and this document were prepared with AI
assistance. Device identification, the firmware image, and the install logs were
supplied by the device owner.
