# SM-X818U X818USQS7EZF1 validation

This profile was validated on a physical Galaxy Tab S9+ 5G. The compatibility
match is intentionally exact: model, firmware build, and kernel release must
all match the values below.

| Field | Value |
| --- | --- |
| Model | `SM-X818U` |
| Device / product | `gts9p` / `gts9psqw` |
| Firmware build | `BP4A.251205.006.X818USQS7EZF1` |
| Android / API | Android 16 / API 36 |
| One UI | 8.5 |
| Kernel release | `5.15.189-android13-8-33413632-abX818USQS7EZF1` |
| Kernel family | `android13-5.15` |
| Security patch | `2026-06-05` |
| SELinux | Enforcing |

## Device validation

Root My Galaxy `0.3.11-x818u-kernelsu-embedded-wait` (version code `41`)
completed the full chain in Shizuku USB ADB mode on 2026-09-10. The recorded
install history is `2cffa2cd-6728-4acb-8175-a27fd7eb5c88`.

The decisive log lines were:

```text
[+] pipe-physrw-summary ... done=1 root=1 kaslr=1
[+] pipe physrw ... uid=2000->0
[+] exploit completed attempt=1/24
KernelSU control verified version=32525 flags=0x5 uapi=2 features=0x5
[+] KernelSU 控制通道已验证
[*] KernelSU 已启用
[+] 安装完成
```

Post-install shell evidence showed the module live and the control API
available:

```text
$ cat /proc/modules | grep kernelsu
kernelsu 208896 1 - Live ... (OE)

$ ksud debug info
version: 32525
flags: 0x5
uapi_version: 2
features: 0x5
lkm: true
late_load: true
pr_build: false
```

This validates the temporary KernelSU LKM/control path. It does not claim that
KernelSU Manager was installed or recognized on the tablet. The root is
per-boot: rebooting clears the loaded module because no boot image or partition
was modified.

## Published artifacts

| Artifact | Size | SHA-256 |
| --- | ---: | --- |
| `artifacts/gts9p-X818USQS7EZF1/cve-2026-43499` | 106160 | `cb347b1e8456e40f3c5bbe7b587e5f3b202fe626254f362d7f31764f7f743234` |
| `artifacts/gts9p-X818USQS7EZF1/cve-2026-43499-app.so` | 133088 | `cfcbf6db58f4ad8537728f9b1888e5754324a5170efb7d01125ab45cd451861e` |
| `artifacts/gts9p-X818USQS7EZF1/cve-2026-43499-root` | 29280 | `f4459a7ca01fad96387ce535fb606fbad8f574d8c0d1e783f5675cb631687714` |
| `kernelsu/android13-5.15.189_kernelsu-gts9p-X818USQS7EZF1.ko` | 377536 | `d2c3195cee42f260822eca76a0b0221c61318ddd172afc9ed46be15c37119177` |
| `kernelsu/ksud-gts9p-X818USQS7EZF1-kdp` | 4890208 | `0537469769352739794d5fc64a2ef971182e8e9ad0df24018123adb6b594e5f3` |

The application support feed points to the `app.so` and embedded `ksud`
artifacts and requires a fresh P0 session. The standalone `.ko` is retained for
auditing and is not interchangeable with another 5.15.189 device.

Use only on devices you own or are explicitly authorized to test.
