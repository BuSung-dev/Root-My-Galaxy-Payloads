# SCG26 / SCG26KDS1DZG1 temporary-root validation

## Status and scope

This is a device-validated field report for one exact Japanese Galaxy S24
Ultra firmware. On 2026-09-07 the guarded flow completed without rebooting the
device, loaded KernelSU once, and returned three consecutive root proofs with
the original boot ID.

The result is deliberately narrower than "SCG26 support":

- It applies only to model/product `SCG26`, device `e3q`, build
  `SCG26KDS1DZG1`, and the exact kernel release below.
- Root is temporary and is lost after a real reboot. No boot image or partition
  was modified.
- Validation covers one boot and one completed acquisition, not a multi-boot
  reliability sample.
- The payload was derived from `e3q-S928BXXS6DZF2` and locally modified to
  remove its boot-uptime gate. It is not an upstream SCG26 profile or an
  official Root My Galaxy release.
- The local payload, root helper, KernelSU artifacts, APK, wrapper, and device
  logs are not added by this report.

Use this only on a device you own or administer. A kernel exploit and late
module load can panic the kernel or lose data even when every preflight check
passes.

## Validated device identity

| Field | Required value |
| --- | --- |
| Model | `SCG26` |
| Product | `SCG26` |
| Vendor/ODM device | `e3q` |
| Display build | `BP4A.251205.006.SCG26KDS1DZG1` |
| Build suffix | `SCG26KDS1DZG1` |
| Kernel release | `6.1.145-android14-11-33419968-abSCG26KDS1DZG1` |
| Architecture | `arm64` |
| Expected pre-root identity | `uid=2000(shell)`, `u:r:shell:s0` |
| Required root identity | `uid=0(root)`, `u:r:ksu:s0` |

Do not weaken these checks to a model-only or `e3q`-only match. A shared device
codename does not prove that firmware-dependent symbols, structures, slide
recovery data, or KernelSU imports are compatible.

## Artifact and build provenance

The device-validated local generation used these SHA-256 values:

| Artifact | SHA-256 | Notes |
| --- | --- | --- |
| S928B reference payload | `a49b378d654c7e637697a701c3c4c5fd02d22b9b30a7069c03e64ec5844af206` | Repository artifact used as the reproducible baseline |
| Local no-uptime-gate payload | `be0fb7167a657e849ff5673690d09461fc5718a4946b4bf4ed5636f1c36a35c8` | Device-tested local derivative; not an official release |
| Root bootstrap helper | `4905b95c62447c86391a47b2000e6b2f34f3a08b3c738c9ad5e8fc15159188ce` | Invoked by the payload |
| KernelSU module | `14f805c6a03123e84f10a252eb5b47f6c65c56c05ad4ccccf1f836c6867f64a9` | Loaded at most once per boot |
| `ksud` | `43f451313dc111429187f8f93e76c57c42976323782aac936c1c09aa309b76b3` | Matches the module generation |
| Audited Termux wrapper | `2681013c663b7ef99845b3729ffc6295e4be52b6e92587e32068cc98f2f5d481` | Local orchestration entrypoint; not distributed here |

The payload baseline came from commit
[`fb1766dfbc61f1a6f8dc298bf5a562e1fdd01798`](https://github.com/BuSung-dev/Root-My-Galaxy-Payloads/commit/fb1766dfbc61f1a6f8dc298bf5a562e1fdd01798),
which added the S928B DZF2 target. The official payload was reproduced
byte-for-byte with Android NDK `28.2.13676358`, clang `19.0.1`, API 35. The
isolated no-uptime-gate source produced the same local hash in two builds, and
host-side checks passed 15/15.

Reproducible compilation and host checks are provenance, not hardware proof.
The final boundary remains execution and post-execution validation on the
exact target.

## Guarded acquisition contract

The reliable result came from the orchestration around the exploit, not from
launching concurrent retries until something appeared to work. The local
wrapper enforced all of the following:

1. **Exit when root is already healthy.** Check `su`, UID, SELinux context, and
   a valid boot UUID before staging or launching anything.
2. **Serialize the transaction.** Hold one non-blocking lock across preflight,
   exploit supervision, KernelSU load, and final proof.
3. **Pin every input.** Verify all five artifact hashes before copying them and
   verify the staged bytes again in `/data/local/tmp`.
4. **Require a real Shizuku shell.** Rish must return `uid=2000(shell)` and
   `u:r:shell:s0`.
5. **Fail closed on identity.** Require the exact model, product, at least one
   exact `e3q` vendor/ODM result, build, kernel, and valid boot UUID.
6. **Use a three-state KernelSU probe.** Distinguish `loaded`, `absent`, and
   read `error`. Never interpret a read error as "not loaded".
7. **Own detached exploit state.** Save its PID, exit marker, and boot UUID.
   Refuse active, ambiguous, or cross-boot state. Reuse exit 0 only on the same
   boot after revalidating its remote log.
8. **Detach from Rish correctly.** Launch through Toybox `setsid` and `nohup`,
   without a PTY. A merely backgrounded child can be reaped when the transport
   returns.
9. **Treat transport loss as unknown.** Retry an empty or invalid Rish boot-ID
   read. Only two valid, different UUIDs prove a reboot.
10. **Prove exploit completion remotely.** Require both the UID transition and
    completion marker in the remote log, then record that log's hash and size.
    Do not trust a large command-substitution capture over Rish.
11. **Have exactly one KernelSU load site.** Probe `/proc/modules` immediately
    before `--kernel-only-load`; skip if KernelSU appeared and refuse any
    double-load.
12. **Prove the result repeatedly.** Require three samples of `uid=0(root)`,
    `u:r:ksu:s0`, and the unchanged boot UUID, followed by a bounded `dmesg`
    audit for panic, fault, watchdog, RCU-stall, and hung-task signatures.

The detached exploit generation used these supervisor settings:

```text
EXPLOIT_ATTEMPTS=24
P0_ATTEMPT_TIMEOUT_SEC=45
EXPLOIT_ATTEMPT_TIMEOUT_SEC=120
LD_PRELOAD=/data/local/tmp/ksu-payload
CVE43499_ROOT_HELPER=/data/local/tmp/ksu-helper
```

An attempt-level safe failure is not permission to start a second supervisor.
Continue observing the one owned supervisor until it writes its exit marker or
the outer deadline expires.

## Device result

| Check | Result |
| --- | --- |
| Date | 2026-09-07 JST |
| Exploit supervisor | Completed on attempt 1 of 24 |
| Bootstrap transition | `uid=2000->0` |
| KernelSU state before load | Absent |
| KernelSU loads performed | 1 |
| Root proof samples | 3/3 passed |
| Root UID | `uid=0(root)` |
| Root SELinux context | `u:r:ksu:s0` |
| Boot UUID | Valid and unchanged; value omitted |
| Automatic reboot | None |
| Final bounded kernel-log audit | No configured alert signature found |
| Terminal marker | `FULL_SUCCESS no reboot boot_id=<unchanged-uuid>` |

The first wrapper invocation lost its Rish launch response even though the
detached process continued. A later same-boot invocation found the completed
exit-0 state, revalidated the remote exploit log by content, hash, and size,
loaded KernelSU once, and completed the three-sample proof. Transport return
status and exploit process status therefore need separate state machines.

## Reboot and runtime safety

Never use any of these operations in this temporary-root environment:

- `ksud soft-reboot`
- `ksud late-load`
- KernelSU Manager's soft reboot
- Android `stop` / `start`
- `reboot`
- KernelSU module unload, reload, or double-load

Those operations have previously destroyed temporary root on this device. If
a post-root runtime component genuinely needs a refresh, the only
device-validated alternative is a separately approved targeted zygote restart:

```sh
su -c 'setprop ctl.restart zygote'
```

That command is not part of root acquisition. Before and after it, prove that
the boot UUID is unchanged, root remains `u:r:ksu:s0`, and zygote plus
`system_server` received new PIDs. Wireless ADB or Shizuku may temporarily go
offline during this restart; transport loss alone is not proof that root was
lost. An ordinary Xposed scope change needs only a force-stop and relaunch of
the target app.

## Work required before upstream SCG26 support

Do not convert this result into a feed entry by renaming the S928B target. A
proper SCG26 contribution still needs:

1. The exact SCG26 firmware package and boot-kernel hashes.
2. A symbolized SCG26 kernel, validated raw BTF, struct-offset dump, and P0
   fingerprint table.
3. Independent derivation of every firmware-dependent constant, including the
   slide oracle, physical load addresses, trace event ID, pselect layout, and
   fake waiter/task layout.
4. A KernelSU module/import/relocation audit against the SCG26 kernel instead
   of an assumption based on the `e3q` codename.
5. Repeated cold acquisitions over multiple real boots, with preserved failure
   logs and no relaxation of the fail-closed guards.
6. Maintainer review before SCG26 is advertised as officially supported.
