# SM-A256E / A256EXXUCEZE6

This profile supports the Samsung Galaxy A25 5G (`SM-A256E`). It requires the
exact `A256EXXUCEZE6` firmware and its Samsung 5.10.240 kernel. Testing used the
standalone LD_PRELOAD entry point on a physical device.

## Validation status

- Standalone transient root: device-tested.
- App payload: not validated and disabled at compile time.
- KernelSU and reboot persistence: not validated and not included.

The successful run reached UID 0 in `u:r:kernel:s0`. The run verified and
restored the captured `ashmem_misc.fops` pointer. It owned all 32 objects in the
selected order-3 `kmalloc-1k` slab. Physical read and write tests passed. The
usermode-helper work completed with return value 0. The temporary usercopy
cache pointer was restored before the PI route finished.

The retained success log has this SHA-256:
`D0EA61E5FB6E13D6DF60E3A55D50F42627A41E923CE2591350CF652349C83628`.

The exploit is probabilistic. A successful run leaves SELinux permissive. It
also keeps an allocation holder alive until reboot. Root access is transient.
Rebooting removes access and releases the retained allocations.

## Port-specific behavior

- 32 KiB KASLR granularity and the exact tracefs worker caller are used.
- The legacy futex waiter layout uses a six-word pselect overlay shift.
- The 960-byte `mm_struct` cache uses order-3 slabs and requires late CPU
  partial draining.
- Exact 32 KiB AF_UNIX carriers are interleaved with 32-object pipe batches.
- Classic configfs callbacks use the low-24-bit address split, vmemmap rebase,
  and padded buffer-size encoding required by this firmware.
- Hardened usercopy is handled with a temporary, verified `kmem_cache` shim.
- A clean allocator miss unwinds the PI route.
- An uncertain worker result enters a reboot-only hold.
- The process does not terminate while a PI owner is pending.

## Build

Use the standalone target only:

```sh
make TARGET=a25x-A256EXXUCEZE6 \
  ANDROID_NDK_HOME=/path/to/android-ndk-r27d standalone
```

Do not use `all` or `release` for this profile. The app-domain physical-P0
route needs a 64-entry fingerprint table with 32 KiB spacing. It also needs
hardware testing before the repository can publish an app artifact or feed
entry.

Use only on a device you own or are explicitly authorized to test.
