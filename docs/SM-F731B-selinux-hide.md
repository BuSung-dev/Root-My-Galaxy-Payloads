# SM-F731B SELinux hide fix

Target: `F731BXXS7GZF1`, kernel
`5.15.189-android13-8-33404244-abF731BXXS7GZF1`, KernelSU v3.2.5.

## Cause

The original Samsung build sets `CONFIG_KSU_SAMSUNG_NO_PATCH_TEXT=y`.
Upstream SELinux hide tries to replace entries in `write_op`,
`sel_handle_status_ops`, and the SELinux LSM hook table. The compatibility
guard rejects those writes with `-EOPNOTSUPP`; activation then returns
`-ENOSYS` (`38`). The upstream setter nevertheless changes its enabled flag
before attempting installation, so `feature get` can incorrectly report `1`.
Both the error and exposed KSU contexts were reproduced on the connected phone.

## Change

[Source patch](../kernelsu/patches/KernelSU-v3.2.5-b5q-selinux-hide.patch):

- Use address-based kernel kprobes for `sel_write_context`, `sel_write_access`,
  `selinux_setprocattr`, and `sel_open_handle_status` on the Samsung ARM64
  5.15 late-loaded module.
- Redirect only UIDs of 10000 or greater while the feature is enabled. The
  existing upstream handlers use the policy and status snapshots captured
  before KernelSU changes them; actual SELinux enforcement remains enabled.
- Run handlers in the original task context after the probe exception returns,
  so policy operations can allocate memory and sleep normally.
- Prevent recursion when the attribute handler calls the original function.
  Raw kernel entry calls bypass the 5.15 CFI jump-table check.
- Resolve every target and install every probe before publishing enabled state.
  Roll back registered probes on an installation error, preserve its errno,
  and report disabled after failure.
- Retain the probes, policy snapshot, status page, and module after first
  successful activation. Disabling stops new redirects. The module is pinned
  until reboot so sleeping callbacks and existing status mappings cannot
  reference freed module state.

The generic text-patching restriction and Samsung credential compatibility
remain in place. This patch does not enable the fallback on other kernel
versions. Kprobes use the kernel's own supported instruction-patching path;
the module does not directly overwrite protected operation tables.

## Build and load

Apply the patch to the existing working v3.2.5 Samsung b5q source tree, retain
the original target build settings, and rebuild the module. Strip debug
sections only, then embed that exact module as
`userspace/ksud/bin/aarch64/android13-5.15_kernelsu.ko` and rebuild `ksud`.
The loader and standalone module from the intermediate SELinux-hide-only
revision are a matched pair retained locally for evidence:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `android13-5.15.189_kernelsu-b5q-F731BXXS7GZF1-selinux-hide.ko` | 380960 | `b4ee0cd6c2abf3f49e94e2b6986dd86f04bec97e0be255bd778f775cca7ca883` |
| `ksud-b5q-F731BXXS7GZF1-selinux-hide` | 4887920 | `608bcef311aa4f1d4fdd4bb50cc04bd32c574a162f08c37d8660a952786c5685` |

The `-selinux-hide` files are not published in this PR. The canonical
`android13-5.15.189_kernelsu-b5q-F731BXXS7GZF1-kdp.ko` and
`ksud-b5q-F731BXXS7GZF1-kdp` now contain the newer
[su compatibility revision](SM-F731B-sucompat.md), which includes this fix
and removes the need for the `/system/bin` overlay. Pre-hide binaries are
retained locally under
`build/b5q-F731BXXS7GZF1/selinux-hide/previous-working/`.

Load through the existing guarded late-load flow after a fresh boot and root
bootstrap, selecting `--allow-shell` on the first load. Do not unload/reload
the live module. No boot image is changed; root and this fix remain per-boot.
The official v3.2.5 Manager remains compatible.

## Verification

The module and Android `ksud` release builds passed. The final module has the
exact target vermagic, 205 undefined imports all present in the recovered
firmware, an empty `__versions` section, and zero CRC mismatches. There are no
`stop_machine`, `aarch64_insn_patch_text`, or `set_memory_rw` imports.

[Regression probe source](../kernelsu/tools/selinux_hide_probe.c) builds with:

```sh
aarch64-linux-android35-clang -O2 -Wall -Wextra -Werror \
  selinux_hide_probe.c -o selinux-hide-probe
```

From an existing root shell, run
`selinux-hide-probe <uid> <expected-hide> [app-context]`.
By default, the probe drops UID/GID while retaining the caller's SELinux
domain to test UID selection separately from Android app-domain permissions.
The optional final argument transitions to the specified domain. It checks stock,
KSU, and invalid context queries; access decisions; status read/mmap; and
attribute writes in isolated child processes. A valid stock attribute write
also exercises the call-through recursion guard. This is not a claim that
every app-specific root detector is bypassed.

Hardware validation completed on SM-F731B on 2026-09-23:

| Check | Result |
| --- | --- |
| Enable feature | Success, `Value: 1`; no `ENOSYS` |
| UIDs 0 and 9999 | Original context/access responses retained |
| UIDs 10000, 10123 and 110123 | KSU/ksu_file contexts and access queries return `EINVAL` |
| Stock shell context/access queries | Succeed with hiding on or off |
| Invalid context and malformed access request | Remain rejected |
| `/proc/self/attr/current` | KSU transition rejected for app UIDs; stock shell transition succeeds |
| Status read and mmap | Agree; app sequence 0, system sequence 2, enforcing 1 |
| Disable, then enable again | Original responses return while disabled; hiding resumes when enabled |
| KernelSU Manager | `Working <LKM> [Jailbreak mode]`, version `32525-2` |
| Root frontend | `su -c id` returns UID 0 in `u:r:ksu:s0`; `su -v` and `su -V` succeed |

All eight UID-scoped probe runs completed with `failures=0`. An additional
test in `u:r:untrusted_app:s0:c123,c256,c512,c768` could enter that domain,
but Samsung policy denied opening every tested SELinux node with hiding
both off and on. Those two runs report eight permission failures each and
are **inconclusive for query hiding**, not passed app-domain tests. No policy
permissions were relaxed to make the test pass. Device logs, the firmware
audit, extracted-module hashes, and Manager XML are stored locally under
`build/b5q-F731BXXS7GZF1/selinux-hide/`.

The final setting is enabled and saved. SELinux remains Enforcing, with
`kernelsu 212992 2 - Live` after opening Manager (one persistent hook reference
and one Manager reference). The first
root bootstrap after reboot failed before module loading and required a
manual restart; the next boot restored root through the existing payload's
second allocation attempt. No exploit changes were needed for this fix.
