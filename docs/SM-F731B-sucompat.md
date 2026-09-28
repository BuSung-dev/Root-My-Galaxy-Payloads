# SM-F731B su compatibility fix

Target: `F731BXXS7GZF1`, kernel
`5.15.189-android13-8-33404244-abF731BXXS7GZF1`, KernelSU v3.2.5.

## Cause and change

The RKP guard in `ksu_syscall_hook_init()` returned before resolving
`sys_call_table`. The existing Samsung su compatibility fallback requires
that pointer to register its syscall probes, so it returned `-ENOENT` and
`su_compat` remained unsupported. The temporary userspace workaround exposed
a real `su` frontend through an overlay on `/system/bin`. Native Root Detector
7.7.0 reported both **Detected Suspicious Mount** and **Found SU Binary**.

[The incremental patch](../kernelsu/patches/KernelSU-v3.2.5-b5q-sucompat.patch)
moves the RKP guard after read-only table resolution. It still returns before
any syscall-table write. The existing Samsung probes can then handle `execve`,
`newfstatat`, `faccessat`, `statx`, and `faccessat2` for authorized UIDs. Other
UIDs retain the original syscall behavior. No `/system/bin/su` inode or
`/system/bin` mount is needed.

The build retains the [SELinux hide fix](SM-F731B-selinux-hide.md), the Samsung
credential compatibility changes, and `CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y`.

## Artifacts and deployment

Apply the incremental patch to the existing Samsung b5q source tree after
the SELinux hide patch. Rebuild the module for the exact release, strip debug
sections only, embed it as
`userspace/ksud/bin/aarch64/android13-5.15_kernelsu.ko`, and rebuild `ksud`.

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `android13-5.15.189_kernelsu-b5q-F731BXXS7GZF1-kdp.ko` | 381216 | `dd4a7d2cad7d45b367a93c68c2b8fbb74f3d300d7ddca1c276661115f27b8285` |
| `ksud-b5q-F731BXXS7GZF1-kdp` | 4888048 | `0ba2bf39f163169319f0fe9cbb0236e572d99810d934587f8280a7e90ef5c521` |

These published canonical artifacts are byte-identical to the local
`-sucompat` pair used for validation. The prior pair is retained locally in
`build/b5q-F731BXXS7GZF1/sucompat/previous-working/`.

Use a fresh boot to remove the previous overlay and pinned module. Restore
root through the existing bootstrap, then use the guarded late-load helper
with this matched loader and `--allow-shell` on the first module load. Do not
run the old overlay restoration script. Do not unload the live module.
The locked boot image is unchanged, so root and the fix remain per-boot.

## Verification

Both builds passed. The module audit reports 205 undefined imports, zero
missing target symbols, an empty `__versions` section, and zero CRC mismatches.
There are no `stop_machine`, `aarch64_insn_patch_text`, or `set_memory_rw`
imports. Extracting the embedded module on the phone reproduces the standalone
module's SHA-256.

[The regression probe](../kernelsu/tools/sucompat_probe.c) checks the four path
lookup syscalls and `execve` as authorized shell UID 2000 and denied app UID
10390. It also checks that opening the real path fails, directory enumeration
contains no `su`, and mountinfo contains no `/system/bin` mount. Samsung
denies directory listing for the tested app UID; the probe records that
limitation and continues testing lookup and execution without relaxing
permissions. The denied UID test retains the caller's SELinux domain to
isolate UID authorization. Native Root Detector is checked separately in its
actual app domain.

```sh
aarch64-linux-android35-clang -O2 -Wall -Wextra -Werror \
  sucompat_probe.c -o sucompat-probe
# From ADB shell after loading the module:
/data/local/tmp/sucompat-probe allow
su -c '/data/local/tmp/sucompat-probe deny 10390'
```

Hardware validation of the kernel behavior passed on 2026-09-23:

| Check | Result |
| --- | --- |
| `su_compat` | Enabled and saved; previously unsupported |
| Authorized shell UID 2000 | All four lookup calls succeed; `su -c id` returns root in `u:r:ksu:s0` |
| Denied detector UID 10390 | All four lookup calls and `execve` return `ENOENT` |
| Real `/system/bin/su` file | `open` returns `ENOENT` for both tested UIDs; shell directory listing has no `su` entry |
| `/system/bin` mount | Absent from both tested processes' mountinfo |
| `su -v` / `su -V` | `3.2.5:KernelSU` / `32525` |
| SELinux hide regression, UID 10000 | `failures=0`, stock contexts work, KSU contexts rejected, status enforcing 1 |
| SELinux | Enforcing |
| Installed daemon | SHA-256 matches the versioned loader above |
| Native Root Detector UI | User confirmed the environment is normal after unlocking the phone |

Both su compatibility probe runs reported `failures=0`. Five feature states
were saved, with `su_compat` and `selinux_hide` enabled. The module remained
live with one reference from the retained SELinux hooks.

Selected recorded outputs are included for review:
[authorized UID](../artifacts/b5q-F731BXXS7GZF1/validation/sucompat-allowed.txt),
[denied UID](../artifacts/b5q-F731BXXS7GZF1/validation/sucompat-denied.txt),
[SELinux hiding regression](../artifacts/b5q-F731BXXS7GZF1/validation/selinux-hide-regression.txt),
and [firmware audit](../artifacts/b5q-F731BXXS7GZF1/validation/kernelsu-audit.txt).

The user confirmed Native Root Detector's normal result after the automated
checks. The saved `nativecheck-after.xml` contains the earlier PIN screen,
so it is not evidence of that result; the app result is user-observed.

The existing root bootstrap blocked module loading on the first two
validation boots:

- First boot: the first allocation attempt timed out; attempt two reached
  the kernel stage before ADB disconnected. A subsequent lower uptime
  confirmed a reboot.
- Second boot: the first allocation attempt timed out; attempt two completed
  the memory read/write checks but stopped with
  `root umh prepublish write failed counters=1/1/0` and
  `stack writer ran; refusing retry on this boot`. The log was recovered and
  ADB accepted a reboot request; the phone later reconnected on a fresh boot.

The replacement module had not loaded on either failed boot. On the third
boot, allocation attempts one and two timed out safely; attempt three
completed with `done=1 root=1`, then the matched module loaded successfully.
No bootstrap code or guards were changed. No overlay was recreated.
Local build and device evidence is stored in
`build/b5q-F731BXXS7GZF1/sucompat/`.
