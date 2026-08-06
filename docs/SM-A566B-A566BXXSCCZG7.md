# SM-A566B A566BXXSCCZG7 target record

## Firmware identity

```text
model: SM-A566B
device: a56x
display build: BP4A.251205.006.A566BXXSCCZG7
fingerprint: samsung/a56xnaeea/a56x:16/BP4A.251205.006/A566BXXSCCZG7_OXMCCZG7:user/release-keys
kernel release: 6.6.102-android15-8-abA566BXXSCCZG7-4k
SDK: 36
ABI: arm64-v8a
page size: 4096
```

## Target derivation

`target.h` and `p0_fingerprint.h` for `essi-A566BXXSCCZG7` reuse the
A566EXXSCCZG6 values. The A566B and A566E builds share the
`6.6.102-android15-8` kernel and use the physical P0 oracle
(`APP_PHYS_P0_ORACLE`). Rather than duplicate a separate firmware derivation,
the profile is copied from the validated A566E target and relabeled for the
A566B build. All constants used by the physical route, fops, slide, and P0
fingerprint paths are identical to the A566E target and are hardware-validated
on this SM-A566B.

## Exploit validation

The A56 app-domain payload was built for this profile as:

```text
artifacts/essi-A566BXXSCCZG7/cve-2026-43499-app.so
size: 104128
SHA-256: 738d0fe89e820b0ee01918f891fce3c6e17b3ca0e23ebea6b789e118b476205f
```

Hardware execution succeeded on the connected SM-A566B (firmware
`A566BXXSCCZG7`). The run used the physical-route oracle, succeeded on the
first attempt, and the modified `install_pipe_physrw()` spawned the p0
reference keeper. The log shows:

```text
[+] p0 reference keeper pid=22556 pipe=192
[*] root umh result wake=1 complete=1 retval=0 socket=1
[*] root p0 reference holder ready=1
[+] pipe physrw pid=<pid> done=1 root=1 kaslr=1 read_ok=1 write_ok=1 rw64=1/1 uid=10396->0
[+] stability keeper pid=<pid> retaining reclaimed kernel pages
[+] exploit completed attempt=1/24
```

The app UID transitioned from `10396` to `0`; the root daemon reported
`uid=0(root)` in Kernel context.

### Physical-route holder fix

The shared exploit change in `src/pipe.c` and `src/root.c` is required by (but
not specific to) this A56 profile. A physical-route win previously left the
root daemon blocked in `recvmsg` because `spawn_p0_ref_keeper()` was only
called on the gate-holder path, so the app timed out at `root p0 reference
holder ready=0`. `install_pipe_physrw()` now spawns the keeper on success, and
`transfer_p0_references_to_root()` always sends three retained pipe file
descriptors, falling back to the reclaim pipe when the gate holder is not
initialized. The holder-readiness wait was widened to give the daemon time to
bind.

## KernelSU

The A566B profile reuses the device-validated `ksud-A566EXXSCCZG6-kdp`
artifact. KernelSU late-load completed on the device, loaded as `<LKM>`, and
the Manager accepted KernelSU version code `32525`.