# tools

Host-side helpers for re-rooting and rescuing Root My Galaxy targets. They
drive the phone over `adb`; a script only runs on the device when it explicitly
pushes a helper into `/data/local/tmp`.

| Tool | Purpose |
| --- | --- |
| [`reroot.sh`](reroot.sh) | One-click re-root: run the exploit and (optionally) late-load KernelSU. |
| [`module-rescue.sh`](module-rescue.sh) | Inspect / back up / disable / remove KernelSU modules, load the kernel driver without the late-load stage, recover a device that cold-reboots during late-load. |
| [`device/`](device/) | Device-side scripts used by `module-rescue.sh` (pushed automatically, never run by hand). |
| [`generate_p0_fingerprint.pl`](generate_p0_fingerprint.pl) | Generate `p0_fingerprint.h` from a kernel `Image` (see `docs/PORTING.md`). |

## Requirements

- Host: Linux/macOS, `bash`, `adb` in `PATH`; the Android NDK only when
  building payloads.
- Device: ADB authorized. `reroot.sh` refuses to run unless
  `ro.build.fingerprint` matches `src/targets/<TARGET>/target.h` (`--force`
  overrides).
- The exploit is probabilistic. A failed batch is not a bug: reboot the phone
  and run it again. The payload deliberately refuses to retry after a partial
  write ("refusing retry on this boot"), so one failed batch = one reboot.

## reroot.sh

```sh
tools/reroot.sh                        # default target pa3q-S938NKSUACZF1
tools/reroot.sh --target e3q-S9280ZCS6DZF2
tools/reroot.sh --attempts 16          # more exploit attempts per run
tools/reroot.sh --no-ksu               # temporary root only, KernelSU not loaded
tools/reroot.sh --serial <SERIAL>      # pick a device
tools/reroot.sh --force                # skip the fingerprint check
tools/reroot.sh --dry-run              # print every command it would run
```

The flow is: wait for `sys.boot_completed` -> verify model/kernel/fingerprint ->
push `cve-2026-43499-app.so`, `cve-2026-43499-root` and the `<KMI>` ksud ->
run the exploit in the **shell** domain (`LD_PRELOAD=<payload>` +
`CVE43499_ROOT_HELPER=<helper>`, uid 2000, Seccomp 0) -> poll the bootstrap root
daemon until `-c id` reports `uid=0` -> stage `/data/local/tmp/.ksud-stage` ->
run the helper's `--late-load` (which bind-mounts the loader over
`/system/bin/logcat` inside a private mount namespace) -> wait for adbd ->
verify `kernelsu` in `/proc/modules` and `su -c id`.

`--no-ksu` stops after the temporary root. That root is a *bootstrap* root:
uid 0 in `u:r:kernel:s0`, which is enough for `/data/local/tmp` and mounts, but
on Samsung kernels DEFEX still answers `EPERM` for `readdir` of
`/data/adb/modules` (see below).

## module-rescue.sh

```sh
tools/module-rescue.sh [--serial S] [--attempts N] [--reroot]
                       [--no-backup] [--no-su] [--shadow-modules]
                       [--dry-run] <action> [pattern ...]
```

Two transports are used, chosen automatically:

| Transport | When | Domain |
| --- | --- | --- |
| bootstrap root daemon | after `reroot.sh --no-ksu` | `u:r:kernel:s0` |
| `su` (KernelSU) | once KernelSU is loaded and answers | `u:r:ksu:s0` |

Module operations prefer `su`, because the bootstrap root cannot enumerate
`/data/adb`: DEFEX blocks **readdir** for that context while `stat`, `mount` and
KernelSU-domain access still work. `--no-su` forces the bootstrap daemon.

| Action | What it does |
| --- | --- |
| `status` | `boot_id`, uptime, SELinux enforce, `kernelsu` in `/proc/modules`, `su -c id`, module count. |
| `list` | Raw `ls -la` plus name/version/description/entries/`cacerts` count for every module; also `modules_update`, `ksu/` configs and the `*.d` script dirs. Handles non-ASCII ids. |
| `diag` | Raw state dump: SELinux context, `/data/adb`, mounts, ksud logs, DEFEX/RKP messages from dmesg, boot-reason props, pstore tail. |
| `match <pattern>` | Resolve patterns to module ids (`*cert*` globs the id; `cert` matches id/name/description, case-insensitive). |
| `snapshot` | Back up **every** module plus ksu/`*.d` config to `artifacts/module-backups/`. Run this before installing anything new. |
| `backup <pattern>` | Copy matching modules to `/data/local/tmp` and pull them into `artifacts/module-backups/`. |
| `disable <pattern>` | Write the KernelSU `disable` marker (module kept, skipped at late-load). |
| `disable-all` | `disable` every module — the safe-mode equivalent. |
| `purge <pattern>` | Backup, then delete the matching module dirs and their `module_configs` entries, then verify. |
| `metamodule-off` | Remove a dangling `/data/adb/metamodule` symlink (the target string is backed up first). |
| `ksud <args...>` | Run ksud through the DEFEX-safe `/system/bin/logcat` bind mount, e.g. `ksud feature list`. |
| `driver` | `ksud insmod` the kernel module **only** — no post-fs-data, no module mounts, no framework restart. |
| `driver-unload` | `ksud unload` (undo `driver`). |
| `chain` | One-root-request bootstrap (driver + `su_compat` + `/data/adb` probe) for the case where all post-load work has to happen inside a single request. |
| `load-ksu` | Stage ksud and run the helper's `--late-load`, comparing `boot_id` before/after. |
| `load-ksu --shadow-modules` | **Rescue load**: bind an empty dir over `/data/adb/modules(_update)` first, so ksud's post-fs-data sees zero modules, then late-load, then unshadow. |

### Why the rescue exists

KernelSU is late-loaded *after* the framework is already up, and the late-load
ends with a zygote/system_server restart. A module that wedges that restart
turns the expected warm restart into a **cold** reset: the LKM is unloaded, the
per-boot root is gone, and the module is still on disk — a loop with no window
to intervene from the UI. The failure is visible in logcat as the module being
processed and the vendor reboot path being taken immediately after:

```text
KernelSU: kernelsu.ko loaded successfully
KernelSU: remove module: /data/adb/modules/<id>
KernelSU: exec /data/adb/modules/<id>/uninstall.sh
qcc-vendor: Calling reboot mutex lock
```

The module trees are readable only from KernelSU's own domain:

- `u:r:kernel:s0` (bootstrap root) gets `EPERM` for `readdir("/data/adb/modules")`
  from DEFEX, even with `enforce=0`; `mount` and `stat` still succeed;
- loading `kernelsu.ko` immediately restores SELinux enforcing, which makes the
  bootstrap daemon's unix socket unreachable from the shell domain — so anything
  that must run *after* the load happens in the same request, or through `su`.

### Recovering from a bad module

| Situation | Do this |
| --- | --- |
| before installing anything | `tools/module-rescue.sh snapshot` (full module + config backup). |
| KernelSU loaded, `su` answers | `tools/module-rescue.sh list` -> `disable <id>` or `purge <id>` (auto `su` transport). |
| KernelSU not loaded, module already installed | `tools/reroot.sh --no-ksu` -> `tools/module-rescue.sh --shadow-modules load-ksu` -> `list` -> `purge <id>`. |
| only the bootstrap daemon, `/data/adb` unreadable | `tools/module-rescue.sh chain` / `driver`, or tap Volume Down 3-5 times while `load-ksu` runs (KSU `check_safemode` counts the events, safe mode disables all modules). |

`--shadow-modules` does not need to read `/data/adb` at all: the bind mount over
`/data/adb/modules` succeeds even though listing it does not, and ksud then has
nothing to mount. The command unshadows afterwards when `su` is available, and
reports an unchanged `boot_id` as proof that the late-load completed without a
cold reset.

### Device-side helpers

`tools/device/` is pushed to `/data/local/tmp` by `module-rescue.sh`:

- `rmg-module-tool.sh` — the actual module inventory/backup/disable/purge logic.
  Ids are validated (`/`, `.`, `..` rejected), arguments arrive through
  `/data/local/tmp/rmg-module-args` so nothing has to survive nested quoting, and
  ids are read line-by-line so non-ASCII or spaced names work.
- `rmg-driver-chain.sh` — the one-request driver bootstrap used by `chain`:
  clean leaked mounts -> `insmod` only -> enable `su_compat` -> probe
  `/data/adb` -> try `su`, logging everything to `/data/local/tmp/rmg-driver.log`.

Both are POSIX `sh` and only touch `/data/adb/modules(_update)`,
`/data/adb/ksu/module_configs` and `/data/local/tmp`.

## Safety

Use these tools only on devices you own or are explicitly authorized to test.
The exploit is a kernel memory-corruption attack: it is probabilistic, it can
reboot the device, and it leaves a temporary root only until the next reboot.
No boot image is modified, so a reboot always returns the device to its stock,
unrooted state.
