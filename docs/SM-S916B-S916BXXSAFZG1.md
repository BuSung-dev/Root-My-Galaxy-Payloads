# Galaxy S23+ SM-S916B / S916BXXSAFZG1 experimental port

## Status

The MCAST payload completed the full root chain on real `SM-S916B` hardware running `S916BXXSAFZG1`. This is one confirmed hardware success, not a reliability claim.

The exact successful chain was:

```text
adb shell
-> tracefs KASLR slide
-> controlled 32-object mm_struct slab
-> SKB head shaping
-> order-3 SKB reclaim
-> MCAST stale waiter write
-> fake ashmem fops
-> configfs arbitrary read/write
-> pipe physical read/write
-> workqueue usermode helper
-> uid 0 root daemon
```

The root client returned:

```text
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
root
Permissive
```

## Exact target

| Field | Value |
| --- | --- |
| Model | `SM-S916B` Galaxy S23+ |
| Firmware | `S916BXXSAFZG1` |
| Kernel | `5.15.189-android13-8-33413713-abS916BXXSAFZG1` |
| Payload profile | `dm2q-S916BXXSAFZG1` |
| Proven writer | MCAST |
| Proven execution domain | `uid=2000`, `u:r:shell:s0` |

The profile is exact-firmware bound. Do not use it on a nearby S916B build or another S23 model.

## APK and shell boundary

The successful payload uses tracefs to obtain the exact KASLR slide. That path is available to the tested ADB shell domain but not to a normal Android application domain.

The payload is therefore intentionally absent from `support/targets-v3.json`. Current Root My Galaxy automatic APK execution must not select it. An APK frontend could invoke the same runner through Shizuku or another authorized shell bridge, but direct app-domain use has not been implemented or validated.

## Reclaim change that made hardware root work

The first real order-3 SKB allocation performs smaller `sk_buff` and linear-head allocations before allocating the fragment page. Without shaping, one of those earlier allocations can steal the freshly released target page and leave stale bytes where fake fops is expected.

The working route sends one exact-size shaping SKB before the target tail is released. It then builds the trigger slabs, closes the shaping socket on CPU0, releases the target tail last, and starts the real SKB sends immediately. This produced a page containing the intended fake-fops data and allowed the chain to pass configfs and pipe gates on hardware.

## Writer result

MCAST is the only published FZG1 writer. Its hardware run reported `sched_ok=1`, passed fake-fops verification, completed configfs and pipe read/write, and reached root.

SIGRETURN is intentionally not selectable in this profile. A hardware test used FPSIMD offset `0x18`, reported `sched_ok=1`, and passed fake fops plus configfs read/write. The phone then froze immediately after `cfi starting pipe physrw` and required a forced reboot. That proves its waiter write worked but rejects the full SIGRETURN route as safe for this firmware.

## KernelSU handoff

The published pair is built from Samsung's released `SM-S916B_16_Opensource` source with the live FZG1 config and Android clang `r450784e`. It uses the Samsung KDP/RKP/DEFEX patch, disables live text patching, and hard-stops the RKP syscall-table write. Exact FZG1 source also required changing the KDP ucount helper type from `enum rlimit_type` to its real `enum ucount_type` ABI.

Static audit against the recovered exact FZG1 `vmlinux.elf` produced:

```text
undefined imports: 200
names present in target: 200
__versions size: 0
symtab/strtab: retained
```

The non-exported imports require the kallsyms-aware `ksud` loader. Plain `insmod` is not supported. The documented path uses the root helper's guarded `--late-load` operation so the loader runs in a private mount namespace and its security-domain and stdio transition can complete safely.

The old release-string-only module plus stale S9180 loader returned only `Killed`, and `/proc/modules` confirmed that it did not load. Do not repeat that pair or mix this new module with another `ksud`. Use the guarded `--late-load` path with the new matched pair.

KernelSU initialization is not yet confirmed on this exact S916B build. The module contains Samsung KDP/RKP/DEFEX handling, but its live hook setup can still panic or reboot the device. Treat it as a separate one-shot experiment after root is confirmed.

## Artifacts and commands

See [`../artifacts/dm2q-S916BXXSAFZG1/README.md`](../artifacts/dm2q-S916BXXSAFZG1/README.md) for hashes, build commands, the exact ADB shell invocation, root verification, and KernelSU late-load steps.

## Authorship

This experimental port and report were prepared with OpenAI Codex assistance. Hardware execution and logs were supplied by `@manups4e`.
