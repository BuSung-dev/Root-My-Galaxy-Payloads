#!/usr/bin/env python3
"""Extract the raw ARM64 kernel Image from a Samsung boot.img.

Reads ap/boot.img.lz4 (already decompressed to ap/boot.img by the caller)
and writes the raw kernel to ap/kernel, printing the provenance data that
docs/PORTING.md requires.

The Android boot image header version 4 is page aligned at 4096 bytes.
`kernel_size` is a little-endian u32 at boot header offset 0x08 and the
kernel payload starts at offset 0x1000.
"""

import hashlib
import pathlib
import re
import struct
import sys

BOOT = pathlib.Path("ap/boot.img")
OUT = pathlib.Path("ap/kernel")


def main() -> int:
    if not BOOT.exists():
        print("ERROR: ap/boot.img missing")
        return 1

    boot = BOOT.read_bytes()
    kernel_size = struct.unpack_from("<I", boot, 0x08)[0]
    kernel = boot[0x1000 : 0x1000 + kernel_size]
    OUT.write_bytes(kernel)

    print("boot.img size:", len(boot))
    print("boot.img sha256:", hashlib.sha256(boot).hexdigest().upper())
    print("kernel size:", kernel_size)
    print("kernel sha256:", hashlib.sha256(kernel).hexdigest().upper())

    # ARM64 Image header: magic "ARM\\x64" at 0x38, text_offset u64 at 0x08.
    magic = kernel[0x38:0x3C]
    text_offset = struct.unpack_from("<Q", kernel, 0x08)[0]
    image_size = struct.unpack_from("<Q", kernel, 0x10)[0]
    flags = struct.unpack_from("<Q", kernel, 0x30)[0]
    print("image magic:", magic.hex(), "(expect 41524d64)")
    print("ARM64 Image text_offset:", hex(text_offset))
    print("ARM64 Image size:", hex(image_size))
    print("ARM64 Image flags:", hex(flags))

    banner = re.search(rb"Linux version [0-9][^\x00]{0,200}", kernel)
    if banner:
        text = banner.group(0).decode("ascii", "replace")
        print("banner:", text)
        # The kernel release is the first whitespace separated field after
        # "Linux version".
        parts = text.split()
        if len(parts) > 2:
            print("kernel release:", parts[2])
    else:
        print("banner: NOT FOUND")

    # Sanity check that the raw BTF blob header is present, as documented in
    # docs/PORTING.md.
    prefix = b"\x9f\xeb\x01\x00"
    hits = []
    cursor = 0
    while True:
        start = kernel.find(prefix, cursor)
        if start < 0:
            break
        cursor = start + 1
        if start + 24 > len(kernel):
            continue
        header = struct.unpack_from("<HBBIIIII", kernel, start)
        magic, version, flags_btf, header_len, type_off, type_len, str_off, str_len = header
        if magic != 0xEB9F or version != 1 or flags_btf != 0 or header_len < 24:
            continue
        payload_len = max(type_off + type_len, str_off + str_len)
        end = start + header_len + payload_len
        string_start = start + header_len + str_off
        if end > len(kernel) or string_start >= end or kernel[string_start] != 0:
            continue
        hits.append((start, end))

    print("raw BTF candidates:", [(hex(s), hex(e)) for s, e in hits])
    if len(hits) == 1:
        start, end = hits[0]
        pathlib.Path("ap/vmlinux.btf").write_bytes(kernel[start:end])
        print(f"raw BTF: [0x{start:x}, 0x{end:x}) ({end - start} bytes)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
