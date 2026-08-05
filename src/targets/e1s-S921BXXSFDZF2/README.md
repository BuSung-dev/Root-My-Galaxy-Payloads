# e1s-S921BXXSFDZF2

```text
device: Samsung Galaxy S24 (SM-S921B, e1s)
firmware: S921BXXSFDZF2 / XSP
display build: BP4A.251205.006.S921BXXSFDZF2
fingerprint: samsung/e1sxxx/essi:16/BP4A.251205.006/S921BXXSFDZF2:user/release-keys
kernel: 6.1.157-android14-11
raw kernel SHA-256: F89829E4A7F6C833F1D60F59085F29AC16190245125831BF432771A4D5A11C97
```

`target.h` contains the exact symbol, layout, physical-load, trace, and KASLR
values recovered from that firmware. `p0_fingerprint.h` contains 32 target
kernel page fingerprints and is checked against all 256 source qwords during
the release verification.

This kernel has the same `uname -r` as the existing S24 FE profile, but its raw
Image and several required symbol offsets differ. The profile therefore does
not reuse the S24 FE offset table.

The profile and build artifacts are statically verified but hardware-untested.
