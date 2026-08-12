# SM-F731U F731USQS8GZF1 validation

This profile was validated on a US T-Mobile Galaxy Z Flip 5 (SM-F731U/DS,
carrier-locked bootloader, no OEM unlock option) running the exact firmware
and kernel below.

| Field | Value |
| --- | --- |
| Model | `SM-F731U` |
| Device | `q5q` |
| Firmware | `F731USQS8GZF1` |
| Android | 13 / API 33 |
| Page size | 4096 |
| Kernel | `5.15.189-android13-8-33404244-abF731USQS8GZF1` |

The app payload completed bootstrap root on the **first attempt**
(`temporary-root-ready`, attempt 1/24, zero reboots), and KernelSU was
late-loaded successfully. The device has a **locked bootloader** (US
carrier model), so the result is a volatile temporary root: KernelSU is
active until the next reboot, after which the app must be run again.

## Validation evidence

```text
[*] stage=starting-temporary-root
[+] stage=temporary-root-ready
[+] exploit completed attempt=1/24
[+] 已获取 bootstrap root
[*] 正在 late-load KernelSU
[+] KernelSU 暂存完成
[+] KernelSU 控制通道已验证
[*] KernelSU 已启用
[+] 安装完成
```

## Published artifacts

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `artifacts/f731u-F731USQS8GZF1/cve-2026-43499-app.so` | 131072 | `ea983cdd333c3b206591ff945df56ebb7b2d2317ff79a4aca96ed3e2f126227e` |

KernelSU uses the shared `ksud-s25u-kdp` artifact (kernel 5.15.189 family).

## Porting notes

- `mm_struct` slab object is `0x400` (not BTF `0x3e0`) — slab alignment.
- `KMALLOC_CGROUP=1`, `NR_KMALLOC_TYPES=3` (common.h defaults are wrong here).
- Kernel MTE disabled via boot cmdline (`arm64.nomte`); keep `MTE=0`.
- 8 cores: `futex_hashsize = 0x800`.
- `KERNELSNITCH_VERBOSE` must be 0 (verbose breaks the futex timing side-channel).
- App/Shizuku branch must receive `P0_ATTEMPT_TIMEOUT_SEC` + `SLIDE_P0_OFFSET`
  env or the write primitive is unreliable.

The profile and build artifacts are hardware-verified on this exact build.
This profile does not claim compatibility with other Galaxy Z Flip 5 models,
firmware, or kernel releases (only `SM-F731U` + `F731USQS8GZF1` was tested).
