# generate_target.py — firmware-to-target generator

Generate `src/targets/<profile>/target.h` and `p0_fingerprint.h`.

This automates the mechanical parts of the porting procedure in
[`PORTING.md`](PORTING.md): symbol recovery, BTF struct/field offsets,
trace event id, worker caller, nfulnl/random-table pointers, physical load
addresses, and the P0 fingerprint are all derived from the boot.img instead
of being recovered by hand. Treat the generated profile as
correct-on-structure; the on-device exploit tuning (bank geometry, pselect
timing, reclaim strategy) still has to be validated on hardware.

Derives every firmware-dependent constant from a raw Android boot.img plus
firmware load-address information. The physical load address comes from one
of:

- `--xbl-config` — Qualcomm `xbl_config` partition image: the DTB memory map
  gives `P0_PHYS_OFFSET` (NOMAP region, 1 GiB aligned) and
  `P0_KERNEL_PHYS_LOAD` (Kernel region).
- `--sboot` — decompressed Exynos `sboot.bin`: the "Starting kernel" jump
  path (phys base + Image `text_offset`) gives `P0_KERNEL_PHYS_LOAD`.
- `--phys-offset` / `--kernel-phys` — explicit overrides (always win).

## Sources per value

| Value | Source |
|---|---|
| `KIMAGE_TEXT_BASE`, `*_OFF` symbols | recovered ELF (`vmlinux-to-elf` + `llvm-nm`) |
| struct/field offsets | raw BTF blob inside the Image (`bpftool`) |
| `SLIDE_TRACEFS_EVENT_ID` | `__TRACE_LAST_TYPE` + ftrace event index |
| `SLIDE_TRACEFS_WORKER_CALLER_OFF` | `worker_thread`: `bl schedule` successor |
| `SLIDE_NFULNL_LOGGER_NAME_OFF` | `"nfnetlink_log"` string image offset |
| `SLIDE_RANDOM_TABLE_BOOT_ID_DATA_PTR_OFF` | `boot_id` entry in `random_table` |
| `P0_PHYS_OFFSET` / `P0_KERNEL_PHYS_LOAD` | `xbl_config` FDT memory map or `sboot` |
| `p0_fingerprint.h` | raw bytes at `PROBE_OFFSET - slide` |

## Usage

```text
generate_target.py --boot boot.img --profile P --fingerprint F \
    (--xbl-config xbl_config.elf | --sboot sboot.bin | --kernel-phys A)
```

required tools: `vmlinux-to-elf`, `llvm-nm`, `llvm-objdump`, `bpftool`, and
`aarch64-linux-gnu-objdump` (only when `--sboot` is given).

## Example (Exynos, sboot path)

```text
generate_target.py \
  --boot boot.img \
  --sboot sboot.bin \
  --profile e2s-S926BXXUEDZDR \
  --fingerprint "samsung/e2sxeea/e2s:16/BP4A.251205.006/S926BXXUEDZDR:user/release-keys"
```

## Derived-by-BTF layout notes

- `rt_mutex_waiter` is auto-detected from BTF: the node-based layout
  (`tree`/`pi_tree` + `rt_waiter_node`, >= 6.6) emits the `TREE_*`/`PI_TREE_*`
  macros with no `*_RT_MUTEX_WAITER` flag; the flat layout (6.1 style) emits
  the direct `PRIO_OFF`/`DEADLINE_OFF` macros plus `COMPACT_RT_MUTEX_WAITER 1`
  when `wake_state`/`ww_ctx` exist, otherwise `LEGACY_RT_MUTEX_WAITER 1`.
- `PAGE_SLAB_CACHE_OFF` is taken from `struct slab.slab_cache` in BTF
  (`0x08` on 6.6, `0x18` on 6.1), with `--slab-cache-off` as an override.

## Pre-6.0 kernels (Android 12/13, 5.10/5.15)

Detected from the kernel version string; no flags needed:

- clang LTO local symbols (e.g. `configfs_read_iter.llvm.8090890824163915520`)
  are resolved automatically via a unique `.llvm.` prefix match.
- There is no `struct slab` in BTF; `PAGE_SLAB_CACHE_OFF` is recovered by
  walking `struct page`'s anonymous unions (5.15: `0x18`).
- `MM_STRUCT_SZ` is emitted as the kmalloc slab size of `mm_struct` (BTF size
  rounded up to a power of two; 5.15 A54: `0x400`).
- `KMALLOC_CGROUP_TYPE 1` / `KMALLOC_CACHE_TYPES 3` are emitted (5.15 has
  normal + cgroup + DMA caches; 6.x defaults to type 2/4).
- `p0_fingerprint.h` rows use a `0x4000` slide step (16K-aligned KASLR
  slides; 125 rows up to `PROBE_OFFSET`) instead of the 6.x `0x10000` step.

## Optional tuning flags

Kernel-derived values are automatic; the exploit tuning knobs that firmware
analysis cannot infer are flags:

```text
--skb-data-delta N          SKB_DATA_DELTA (default -0xe80; 4K-page builds
                            typically use -0x1000)
--pselect-word-shift N      SLIDE_PSELECT_WORD_SHIFT (default 0; 4K-page
                            builds typically use 3)
--pselect-timeout-nsec N    SLIDE_PSELECT_TIMEOUT_NSEC (default 100000000)
--bank-slots N              SLIDE_BANK_SLOTS (default 4; some builds use 5)
--bank-task-off N           SLIDE_BANK_TASK_OFF (default 0x1000; some builds
                            use 0x3200)
--slab-cache-off N          STRUCT_SLAB_CACHE_OFF override
--inverse-slide             emit APP_P0_FINGERPRINT_INVERSE_SLIDE 1 and key
                            fingerprint rows by physical source offset
                            (probe - slide) instead of by slide
--trace-event-id N          explicit SLIDE_TRACEFS_EVENT_ID base
                            (default 20; + ftrace event index)
--no-trace-event            omit SLIDE_TRACEFS_EVENT_ID entirely
                            (src/slide.c falls back to its built-in 109)
--probe-offset N            P0 oracle probe offset (default 0x1f0000)
--model M                   hardware model (default: build id)
--out DIR                   output dir (default src/targets/<profile>)
--keep                      keep the temporary work directory
```

The device-tuned `--skb-data-delta` / `--pselect-word-shift` /
`--bank-slots` / `--bank-task-off` values for existing targets can be read
from the corresponding checked-in `src/targets/<profile>/target.h`.
