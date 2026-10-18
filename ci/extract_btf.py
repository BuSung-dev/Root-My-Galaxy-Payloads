#!/usr/bin/env python3
"""Extract the raw BTF blob from a raw ARM64 kernel Image.

The validation follows docs/PORTING.md: a raw BTF header starts with the
16-bit magic 0xeb9f. These Samsung arm64 Images are little-endian, so the
expected first eight bytes are:

    9f eb 01 00 18 00 00 00

The four-byte prefix alone is not accepted. The complete header, the section
bounds and the required NUL byte at the start of the string section are all
validated, and the Image is rejected when the match is ambiguous.
"""

import struct
import sys
from pathlib import Path

PREFIX = b"\x9f\xeb\x01\x00"


def find_btf(image: bytes):
    candidates = []
    cursor = 0
    while True:
        start = image.find(PREFIX, cursor)
        if start < 0:
            break
        cursor = start + 1

        if start + 24 > len(image):
            continue

        header = struct.unpack_from("<HBBIIIII", image, start)
        magic, version, flags, header_len, type_off, type_len, str_off, str_len = header
        if magic != 0xEB9F or version != 1 or flags != 0 or header_len < 24:
            continue

        payload_len = max(type_off + type_len, str_off + str_len)
        end = start + header_len + payload_len
        string_start = start + header_len + str_off
        if end > len(image) or string_start >= end or image[string_start] != 0:
            continue

        candidates.append((start, end))

    return candidates


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: extract_btf.py RAW_IMAGE OUTPUT_BTF", file=sys.stderr)
        return 2

    image_path = Path(sys.argv[1])
    out_path = Path(sys.argv[2])

    image = image_path.read_bytes()
    candidates = find_btf(image)

    if len(candidates) != 1:
        print(
            f"expected one raw BTF blob, found: "
            f"{[(hex(s), hex(e)) for s, e in candidates]}",
            file=sys.stderr,
        )
        return 1

    start, end = candidates[0]
    out_path.write_bytes(image[start:end])
    print(f"raw BTF: [0x{start:x}, 0x{end:x}) ({end - start} bytes) -> {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
