# Galaxy Z Fold5 SM-F946U1 - F946U1UES6EYI1

Device-tested Root My Galaxy profile for the US unlocked Galaxy Z Fold5.

## Device

| Item | Value |
| --- | --- |
| Model | `SM-F946U1` |
| Device / board | `q5q` / `q5quew` |
| CSC | `XAA` |
| Android | 15 / API 35 |
| Build | `AP3A.240905.015.A2.F946U1UES6EYI1` |
| Fingerprint | `samsung/q5quew/q5q:15/AP3A.240905.015.A2/F946U1UES6EYI1:user/release-keys` |
| Kernel | `5.15.153-android13-8-30958511-abF946U1UES6EYI1` |
| Security state | locked bootloader, stock boot image |

The profile is marked `requiresShizuku: true`. The hardware-proven route uses
shell-owned tracefs and COW resources, together with the app payload's MCAST,
controlled-mm reclaim, configfs fake-fops, physical pipe read/write, and root
UMH stages.

## Artifacts

```text
9bb3f33c330ca3091811cf9a9392470da70f6c940397627e54eb6f7a07b4790e  cve-2026-43499-app.so
b4a474453c82dcfbbe6867c9fc2c59322fe09684f5a01e294d643847746befc0  cve-2026-43499-root
93315b24ad529b5147b02d06f6df3e68f6cac439d46f0ad2f173131a929a5e66  android13-5.15_kernelsu-q5q-F946U1UES6EYI1.ko
04f1f20c2f64f77e3b75d3e18a347167519831156994f43d185436eac9f3491f  ksud-q5q-F946U1UES6EYI1-kdp
```

Sizes:

```text
104128  cve-2026-43499-app.so
 27752  cve-2026-43499-root
358304  android13-5.15_kernelsu-q5q-F946U1UES6EYI1.ko
4873672 ksud-q5q-F946U1UES6EYI1-kdp
```

## Target

The target is based on the published CVE-2026-43499 exploit and the nearby
Samsung 5.15 Qualcomm reference. Key firmware-specific values include:

```text
smol KASLR slide logger offset  0x01c972bc
boot-id data pointer offset     0x02a7f958
init_task offset                0x02ac9bc0
root task-group offset          0x02b79ac0
sysctl boot-id offset           0x02ceaf29
ashmem_fops offset              0x01f217d0
ashmem misc fops offset         0x02ac1b88
configfs read_iter offset       0x005d30a8
configfs bin_write_iter offset  0x005d3ad0
```

The app payload selects:

```text
SLIDE_SOURCE=tracefs
stack writer=MCAST
reclaim=controlled mm
fops=direct
pipe=after-fops
```

The 5.15.153 KernelSnitch profile uses 2048 appended futexes and 32 repeated
measurements. The controlled-mm search explicitly consumes this target profile
instead of the shared 256/128 default.

## Hardware validation

The complete Root My Galaxy Shizuku path succeeded on the exact stock EYI1
build. A clean boot reached:

```text
app fops slide attempt=1/1 triggered=1 verified=1 step=0 errno=0
pipe-physrw-summary done=1 root=1 kaslr=1 base=ffffffc008098000 slide=0000000000098000
pipe physrw done=1 root=1 kaslr=1 read_ok=1 write_ok=1 rw64=1/1 uid=2000->0
exploit completed attempt=1/1
success:a935ee6c-2000-4730-b68c-68999c8a07c9
```

After the payload succeeded, the embedded KernelSU module late-loaded:

```text
kernelsu 208896 0 - Live ... (OE)
```

KernelSU Manager reported:

```text
Working <LKM> [Jailbreak mode]
version 32525-2
SELinux enforcing
kernel 5.15.153-android13-8-30958511-abF946U1UES6EYI1
```

The module audit reports 200 undefined imports, zero missing target symbols,
and zero CRC mismatches.

The fops race is probabilistic. A failed attempt after the stack writer starts
must be followed by a reboot; re-running on the same boot is not supported.
The app profile therefore requires Shizuku, waits for the boot allocator
window, and treats each boot as one production attempt.

The root helper verifies both the KernelSU UAPI identity and the crowned
manager app ID before reporting success. This prevents a loaded-but-unbound
module from being treated as a usable root channel.

## Build

```sh
make TARGET=q5q-F946U1UES6EYI1 \
  ANDROID_NDK_HOME=/path/to/android-ndk release
```

The release exploit is truncated to the shared 104128-byte payload slot and
the helper must remain 27752 bytes. The app must preserve
`libcve43499root.so` byte-for-byte when packaging.
