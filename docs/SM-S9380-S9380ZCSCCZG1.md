# SM-S9380 / S9380ZCSCCZG1 porting record

Device-tested temporary root + KernelSU (LKM jailbreak mode) on 2026-08-09
through 2026-08-14. Locked bootloader; no persistent boot.img modification.
Re-establish per boot.

## 1. Firmware identity

| field | value |
| --- | --- |
| model | `SM-S9380` (Galaxy S25 Ultra, Greater China) |
| AP/PDA | `S9380ZCSCCZG1` |
| CSC | `S9380CHCCCZG1` (CHC, China mainland) |
| codename | `pa3q` |
| build fingerprint | `samsung/pa3qzcx/pa3q:16/BP4A.251205.006/S9380ZCSCCZG1_CHCCCZG1:user/release-keys` |
| kernel release | `6.6.98-android15-8-p5a696e2-abogkiS9380ZCSCCZG1-4k` (built Wed Jul 8 10:26:07 UTC 2026) |
| kernel size | 38849024 bytes (raw Image from boot.img) |
| kernel SHA-256 | `8CBDD8D432A2B2B245DB8D570CD2AA0FF62DAD70479F413805A37410E7D3A813` |

The July (`ZG1`) security patch rebuilt this device's kernel on the
`p5a696e2-abogki` GKI base — the same base as the China `S9360ZCSCCZG1`
(S25+) and `S9370ZCS9CZG1` (S25 Edge) July kernels. The shared
`galaxy-s25-series` payload is built from `pa3q-S938NKSUACZF1` (June, `ZF1`)
and does not work on this kernel.

## 2. Why a separate profile — the cache-gate failure with the shared pa3q payload

Running the stock `galaxy-s25-series` payload fails on `S9380ZCSCCZG1`. The
root cause is the same deterministic offset mismatch described in
[`SM-S9360-S9360ZCSCCZG1.md`](SM-S9360-S9360ZCSCCZG1.md): the `ZF1` kernel's
`KMALLOC_CACHES_OFF` points to the wrong address on the July kernel, so the
oracle reads garbage instead of the kmalloc cache pointers and
`pipe_cache_matches()` never finds the pipe pages' slab cache. This is a
deterministic offset mismatch, not a race.

With the corrected value the gate matches and the full chain succeeds
(decisive lines from a first-attempt-after-boot run on 2026-08-14, pids
redacted):

```text
[*] p0 pipe gate hits=1 changed=0
[+] slide-kaslr-ok source=physical base=ffffffc0800f0000 slide=00000000000f0000
[*] pipe page idx=0 page=ffffff89422c0000 head=fffffffe2508b000 cache08=ffffff8001cf4c00 ... match=1
[+] pipe-physrw-summary done=1 root=1 kaslr=1 base=ffffffc0800f0000 slide=00000000000f0000
[+] pipe physrw done=1 root=1 kaslr=1 read_ok=1 write_ok=1 rw64=1/1 uid=10682->0
```

## 3. Offsets that differ from pa3q-S938NKSUACZF1

Only one kernel-data offset differs. All other offsets were re-derived from
this kernel's `vmlinux` / BTF / kallsyms and match the `ZF1` profile
(all `.text` symbol offsets, `task_struct`/`page`/`rt_mutex_waiter`/
`file_operations`/workqueue struct layouts, `SELINUX_ENFORCING_OFF`, `P0_*`,
etc.).

| macro | pa3q `ZF1` (June) | pa3q `ZG1` (this device) | source |
| --- | ---: | ---: | --- |
| `KMALLOC_CACHES_OFF` | `0x017da710` | `0x017dac30` | `kmalloc_caches` symbol |

`KMALLOC_CACHES_OFF` is identical to `pa2q-S9360ZCSCCZG1` and
`psq-S9370ZCS9CZG1`, as expected from the shared `p5a696e2-abogki` GKI base.
`SLIDE_NFULNL_LOGGER_NAME_OFF` stays at the `ZF1` value `0x0175e2a1` (it
differs from the `pa2q` July value `0x0175e75d`; rodata layout is
per-device-build even on the same GKI base). The profile is still keyed to
the exact PDA because the p0 fingerprint (below) is build-specific.

## 4. Physical load address

```c
#define P0_PHYS_OFFSET       0x80000000ULL
#define P0_KERNEL_PHYS_LOAD  0xa8000000ULL
```

Qualcomm device; same derivation as `pa2q`/`psq` (see
[`SM-S9360-S9360ZCSCCZG1.md`](SM-S9360-S9360ZCSCCZG1.md) section 4).

## 5. SLIDE_PSELECT_WORD_SHIFT

Not overridden in `target.h`; the default in `slide_app.c` is `0`, which is
correct for this kernel (same reasoning as the `psq`/`pa2q` records).

## 6. p0 fingerprint

Regenerated from this kernel's raw Image with
`tools/generate_p0_fingerprint.pl` (`PROBE_OFFSET=0x1f0000`). The table is
build-specific and differs from both the `ZF1` and the `pa2q` July tables.
All fingerprint rows were confirmed against the live kernel on device before
the first successful run.

## 7. KernelSU

The existing `kernelsu/ksud-s25u-kdp` (`android15-6.6` KMI) loads without
modification. Samsung DEFEX Safeplace blocks executing ksud from
`/data/local/tmp/`; the late-load path bypasses it by bind-mounting ksud over
`/system/bin/logcat` in a private mount namespace. KSU comes up in LKM
jailbreak mode. No KernelSU rebuild or binary patch is needed.

The Root My Galaxy app additionally offers an optional `boot-patch --flash`
step for a persistent-after-reboot setup; that path is not covered by this
record (all evidence here is from the CLI/exploit flow with per-boot
re-acquisition).

## 8. Build (Windows)

Built with the Android NDK on Windows using the exact `make release` recipe
flags (the Makefile hardcodes the `linux-x86_64` prebuilt path; invoke the
`windows-x86_64` clang wrapper directly or override `TARGET_CC`):

```sh
NDK=/path/to/android-ndk-r29
CC="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android35-clang.cmd"

make all TARGET=pa3q-S9380ZCSCCZG1 ANDROID_NDK_HOME="$NDK" TARGET_CC="$CC"
```

Flags used for the shipped artifact (identical to the `release` target):
`-DAPP_PAYLOAD=1 -fPIC -Oz -g0 -fno-unwind-tables
-fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections
-Wl,--gc-sections -Wl,--icf=all -s`, zero-padded to the fixed release size
104128. The shipped binary was built from the repository source state as of
mid-August 2026 and is the exact artifact used for every on-device validation
below.

Shipped `artifacts/pa3q-S9380ZCSCCZG1/cve-2026-43499-app.so`:
SHA-256 `2592ac034049e8f2831327b38fa8ed1119cf0a45ecf21bd51cbbdde39f410db4`
(size 104128). The current `main` compiles this profile cleanly (checked
2026-08-31), so rebuilding with the standard recipe is safe.

¹ Verify the hash when reproducing; a mismatch means the artifact or the
padded size differs from the validated build.

Outputs in `build/pa3q-S9380ZCSCCZG1/`:

| file | use |
| --- | --- |
| `cve-2026-43499` | root-umh variant (LD_PRELOAD) |
| `cve-2026-43499-app.so` | app variant (used via `--run-payload`) → copy to `artifacts/pa3q-S9380ZCSCCZG1/` |
| `cve-2026-43499-root` | root helper / su_daemon |

## 9. Device validation (2026-08-09 .. 2026-08-14)

### Exploit

Multiple full end-to-end successes across five days of testing, including a
first-attempt success immediately after boot on 2026-08-14 with the exact
artifact shipped here (embedded in an app build). Temporary root reached
`uid=0` in the app domain (`u:r:untrusted_app` profile upgrading to kernel
credentials via the physrw stage, `uid=10682->0` above).

Success rate is system-state dependent (futex/fops races): attempts started
within the first ~2 minutes after boot open the `pselect` write window far
more reliably than attempts on a long-uptimed system. Occasional failures
leave no persistent damage (the exploit is memory-only; worst case is a
kernel panic and reboot, observed during bring-up with no lasting effect).

### KernelSU install

```sh
$ adb shell "cp /data/local/tmp/ksud-s25u-kdp /data/local/tmp/.ksud-stage && chmod 755 /data/local/tmp/.ksud-stage"
$ adb shell "/data/local/tmp/cve-2026-43499-root --late-load"
```

`--late-load` is silent on success. Recreate `.ksud-stage` before every
`--late-load` (the loader renames it to `/data/adb/ksud`). KernelSU grants
root to apps afterwards.

## 10. Scope

Verified only for `SM-S9380` / `S9380ZCSCCZG1` (Greater China `CHC`).

**Manifest routing is intentionally not part of this contribution.** In the
current `targets-v3.json`, `SM-S9380` remains listed under the shared
`galaxy-s25-series` entry, whose artifact targets the `ZF1` kernel and does
not work on this `ZG1` build (see §2). Schema v3 matches on model + kernel
version only, which cannot distinguish the two firmware builds (both report
`SM-S9380` on kernel `6.6.98`). Routing `SM-S9380` to this profile would
knowingly misroute `ZF1` users; a safe automatic route needs a schema change
(fingerprint/PDA matching) and is left for upstream to decide. Until then,
`ZF1`-era users keep the working shared payload, and `ZG1` users should use
this payload manually after confirming their CSC from Settings. Hong Kong
`TGY` builds (`S9380ZHUB*`) ship their own kernel binaries and need their own
fingerprint ports — they are NOT covered by this payload.

Both temp root and KSU are per-boot (locked bootloader; no persistent
boot.img modification). Re-establish after reboot: exploit (`--run-payload`)
→ recreate `/data/local/tmp/.ksud-stage` → `--late-load`. The KernelSU
configuration under `/data/adb/ksu` survives reboots; only the privilege
itself must be re-acquired.
