# r0s-S901BXXSNGZD7

```text
device: Samsung Galaxy S22 (SM-S901B/DS, r0s, Exynos 2200 / s5e9925)
firmware: S901BXXSNGZD7 / EUX (OXM CSC S901BOXMNGZD7)
display build: BP2A.250605.031.A3.S901BXXSNGZD7
fingerprint: samsung/r0sxxx/essi:16/BP2A.250605.031.A3/S901BXXSNGZD7:user/release-keys
kernel: 5.10.237-android12-9-31999025-abS901BXXSNGZD7
raw kernel SHA-256: 3A5C445B896F9C23130A393569DBDFAFE0C9A15212788B7562FE00964435096C
```

`target.h` contains the exact symbol, layout, physical-load, trace, and KASLR
values recovered from that firmware. `p0_fingerprint.h` contains 32 target
kernel page fingerprints and is checked against all 256 source qwords during
the release verification.

This is a no-BTF Samsung 5.10 target (android12-9 branch): layouts follow the
legacy `rt_mutex_waiter` (0x50), `KMALLOC_CGROUP_TYPE`/`KMALLOC_CACHE_TYPES`
are `0`/`2` (no `CONFIG_ZONE_DMA`, pipe buffers use the normal kmalloc-2k
cache), and the `configfs_buffer` offsets were measured from the binary
because Samsung's configfs diverges from the vanilla struct.

Generate the 32-row fingerprint table from the raw kernel Image with:

```sh
tools/generate_p0_fingerprint.pl Image 0x1f0000 p0_fingerprint.h
```

Build with Android NDK r29:

```sh
make TARGET=r0s-S901BXXSNGZD7 ANDROID_NDK_HOME=/path/to/android-ndk
```

The full analysis, KernelSU build record, and residual-risk notes are in
[`docs/SM-S901B-S901BXXSNGZD7.md`](../../../docs/SM-S901B-S901BXXSNGZD7.md).
