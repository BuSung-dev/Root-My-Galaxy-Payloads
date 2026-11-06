# SM-G990B G990BXXSLIZH2 source set

This directory contains the exact source used for the hardware-proven
`SM-G990B` / `G990BXXSLIZH2` payload.

Samsung's 5.4 kernel requires target-specific compiler stack geometry for the
legacy `rt_mutex_waiter` overlay. The proven route uses a pselect result-copy
COW fault and `overlay_asm.S`; newer shared-engine changes must not silently
alter those frames. For that reason the Makefile selects the frozen `port/`
source set only for this target. The app payload also fixes the runtime to the
phone-validated controlled reclaim and readiness-pause route, with one
900-second attempt per boot. After the FOPS swap, the app gets three chances
to take over the pipe page. The old drain arrays are released while the
captured AF_UNIX probe still pins the target; the probe is then drained last,
so reclaim growth reaches the target after reusing the old slabs. A failed
takeover keeps the fake FOPS page alive until reboot instead of releasing a
stale kernel function table.

Build with NDK r27b:

```sh
ANDROID_NDK_HOME=/path/to/android-ndk-r27b \
  make TARGET=r9q-G990BXXSLIZH2
```

The target automatically uses API 30 for the exploit and API 35 for the
generic Root My Galaxy helper. A clean build reproduces:

```text
f0a2b39c3d3a0e05beccd839f3e2af2417ca7f60e849b4e5e6bee898688b437b  cve-2026-43499-app.so
fd193ddecbc5db5d13e72281cf54ce3ae054e1b3d8728c1dfc64ff1fc80d0bb8  cve-2026-43499-root
```

The full hardware record and KernelSU details are in
[`../../../docs/SM-G990B-G990BXXSLIZH2.md`](../../../docs/SM-G990B-G990BXXSLIZH2.md).
