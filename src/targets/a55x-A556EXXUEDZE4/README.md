# a55x-A556EXXUEDZE4

Target profile for Samsung Galaxy A55 5G `SM-A556E` on the
ZTO firmware `A556EXXUEDZE4`.

```text
build: BP4A.251205.006.A556EXXUEDZE4
fingerprint: samsung/a55xnsxx/essi:16/BP4A.251205.006/A556EXXUEDZE4:user/release-keys
kernel: 6.1.157-android14-11
page size: 4096
image base: 0xffffffc008000000
```

`target.h` and `p0_fingerprint.h` were generated from the exact raw Image;
live ADB properties matched this profile on 2026-09-07.
The profile uses the shared Android 14 / 6.1 physical-P0 route, compact
`rt_mutex_waiter` layout, MTE-aware KernelSnitch matching, and a fresh
same-process P0 session. The legacy inverse-slide fingerprint mode is not
enabled.

The payload and KernelSU pair are statically built, audited, and device-tested.
On 2026-09-07, the app's Shizuku path completed the physical read/write and
UID transition, then late-loaded KernelSU. KernelSU Manager reported
`Working <LKM> [Jailbreak mode]`, version `32525-2`.

The result is temporary and must be repeated after a reboot. A failed attempt
can panic and reboot the kernel; wait for Android to finish booting, then retry
from a fresh app run.
