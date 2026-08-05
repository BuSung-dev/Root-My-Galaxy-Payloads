# a36xq-A366WVLS3AYG1

Device-tested target profile for the Canadian Galaxy A36 5G:

```text
model: SM-A366W
device: a36xq
firmware: A366WVLS3AYG1
display build: AP3A.240905.015.A2.A366WVLS3AYG1
fingerprint: samsung/a36xqcs/a36xq:15/AP3A.240905.015.A2/A366WVLS3AYG1_OYV3AYG1:user/release-keys
kernel: 6.6.46-android15-8-30526735-abogkiA366WVLS3AYG1-4k
page size: 4096
```

All offsets and the P0 fingerprint table are derived from the exact AYG1
kernel. The physical P0 offset and virtual kernel base are handled separately;
the virtual base is recovered from the live `ashmem_misc.fops` pointer and is
never treated as a firmware constant.

Generate the 32-row fingerprint table from the raw kernel Image with:

```sh
tools/generate_p0_fingerprint.pl Image 0x1f0000 p0_fingerprint.h
```

Build with Android NDK r29:

```sh
make TARGET=a36xq-A366WVLS3AYG1 ANDROID_NDK_HOME=/path/to/android-ndk
```

The hardware validation and release hashes are recorded in
[`docs/SM-A366W-A366WVLS3AYG1.md`](../../../docs/SM-A366W-A366WVLS3AYG1.md).
