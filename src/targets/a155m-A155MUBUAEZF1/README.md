# a155m-A155MUBUAEZF1

```text
device: Samsung Galaxy A15 4G (SM-A155M, a15ks)
firmware: A155MUBUAEZF1 / ZTO
display build: BP4A.251205.006.A155MUBUAEZF1
fingerprint: samsung/a15ks/a15:16/BP4A.251205.006/A155MUBUAEZF1:user/release-keys
kernel: 6.12.38-android16-5-abA155MUBUAEZF1-4k
raw kernel SHA-256: 33A0146DD1497F5EA9476DF2F5ED73D952934D83119B2A149C9C2041823FBB41
```

`target.h` contains the exact symbol, layout, physical-load, trace, and KASLR
values recovered from that firmware. `p0_fingerprint.h` contains 32 target
kernel page fingerprints and is checked against all 256 source qwords during
the release verification.

This kernel uses Rust-based ashmem (`ashmem_rust` module). The fops table and
function offsets were extracted from the Rust-generated vtables. All 6.12
layout values (rt_mutex_waiter, task_struct, workqueue, etc.) match the Linuxoid
generate_target.py output.

The profile and build artifacts are statically verified but hardware-untested.
