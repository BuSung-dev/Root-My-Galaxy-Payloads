#!/usr/bin/env python3
"""Unit test for the BTF parser in ci/derive_offsets.py.

Builds a synthetic BTF blob with known structure layouts and verifies that
the parser reports the expected sizes and member offsets.
"""

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from derive_offsets import Btf, BTF_INFO_KIND, BTF_KIND_FLAG  # noqa: E402
from derive_offsets import BTF_KIND_STRUCT, BTF_KIND_INT, BTF_KIND_PTR  # noqa: E402
from derive_offsets import BTF_KIND_TYPEDEF  # noqa: E402


def build_btf():
    strings = bytearray(b"\x00")
    offsets = {}

    def add_str(name):
        offsets[name] = len(strings)
        strings.extend(name.encode() + b"\x00")
        return offsets[name]

    # Type ids: 1 = int, 2 = ptr, 3 = typedef, 4 = struct file_operations,
    # 5 = struct task_struct
    types = bytearray()

    def add_type(name_off, kind, size_or_type, vlen=0, kind_flag=False, extra=b""):
        info = (vlen & 0xFFFF) | (kind << 24)
        if kind_flag:
            info |= BTF_KIND_FLAG
        types.extend(struct.pack("<III", name_off, info, size_or_type))
        types.extend(extra)

    add_type(add_str("int"), BTF_KIND_INT, 4, vlen=1,
             extra=struct.pack("<III", 0x01000008, 0, 32))
    add_type(add_str("void"), BTF_KIND_PTR, 1)

    # typedef unsigned long size_t;
    add_type(add_str("size_t"), BTF_KIND_TYPEDEF, 1)

    # struct file_operations { ... } with known member offsets.
    # BTF stores member offsets in bits, so the byte offsets below are
    # multiplied by eight.
    fo_members = [
        ("owner", 2, 0x00 * 8),
        ("llseek", 2, 0x08 * 8),
        ("read", 2, 0x10 * 8),
        ("write", 2, 0x18 * 8),
        ("read_iter", 2, 0x20 * 8),
        ("write_iter", 2, 0x28 * 8),
        ("unlocked_ioctl", 2, 0x50 * 8),
        ("compat_ioctl", 2, 0x58 * 8),
        ("mmap", 2, 0x60 * 8),
        ("open", 2, 0x70 * 8),
        ("release", 2, 0x80 * 8),
        ("splice_read", 2, 0xC8 * 8),
        ("show_fdinfo", 2, 0xE0 * 8),
    ]
    extra = bytearray()
    for member, mtype, bit_off in fo_members:
        extra.extend(
            struct.pack("<III", add_str(member), mtype, bit_off)
        )
    add_type(add_str("file_operations"), BTF_KIND_STRUCT, 0x110,
             vlen=len(fo_members), extra=bytes(extra))

    # struct task_struct { ... }.
    ts_members = [
        ("usage", 1, 0x40 * 8),
        ("prio", 1, 0x84 * 8),
        ("normal_prio", 1, 0x8C * 8),
        ("sched_task_group", 2, 0x348 * 8),
        ("pi_lock", 2, 0x924 * 8),
        ("pi_waiters", 2, 0x938 * 8),
        ("pi_top_task", 2, 0x948 * 8),
        ("pi_blocked_on", 2, 0x950 * 8),
    ]
    extra = bytearray()
    for member, mtype, bit_off in ts_members:
        extra.extend(struct.pack("<III", add_str(member), mtype, bit_off))
    add_type(add_str("task_struct"), BTF_KIND_STRUCT, 0x960,
             vlen=len(ts_members), extra=bytes(extra))

    hdr_len = 24
    type_off = 0
    type_len = len(types)
    str_off = type_len
    str_len = len(strings)

    header = struct.pack(
        "<HBBIIIII",
        0xEB9F,
        1,
        0,
        hdr_len,
        type_off,
        type_len,
        str_off,
        str_len,
    )
    return header + bytes(types) + bytes(strings)


def main() -> int:
    blob = build_btf()
    btf = Btf(blob)

    failures = []

    def check(label, actual, expected):
        if actual != expected:
            failures.append(f"{label}: got {actual!r}, expected {expected!r}")
        else:
            print(f"ok   {label} = {actual!r}")

    check("sizeof(file_operations)", btf.struct_size("file_operations"), 0x110)
    check("sizeof(task_struct)", btf.struct_size("task_struct"), 0x960)
    check(
        "file_operations.unlocked_ioctl",
        btf.struct_member_offset("file_operations", "unlocked_ioctl"),
        0x50,
    )
    check(
        "file_operations.compat_ioctl",
        btf.struct_member_offset("file_operations", "compat_ioctl"),
        0x58,
    )
    check(
        "file_operations.splice_read",
        btf.struct_member_offset("file_operations", "splice_read"),
        0xC8,
    )
    check(
        "file_operations.show_fdinfo",
        btf.struct_member_offset("file_operations", "show_fdinfo"),
        0xE0,
    )
    check(
        "task_struct.usage",
        btf.struct_member_offset("task_struct", "usage"),
        0x40,
    )
    check(
        "task_struct.sched_task_group",
        btf.struct_member_offset("task_struct", "sched_task_group"),
        0x348,
    )
    check(
        "task_struct.pi_blocked_on",
        btf.struct_member_offset("task_struct", "pi_blocked_on"),
        0x950,
    )

    # A missing member must raise, not silently return 0.
    try:
        btf.struct_member_offset("file_operations", "no_such_member")
    except KeyError:
        print("ok   missing member raises KeyError")
    else:
        failures.append("missing member did not raise KeyError")

    # A non byte aligned member must be rejected.
    tid = btf.find("file_operations", BTF_KIND_STRUCT)
    btf.members[tid].append(("bitfield", 1, 0x101))
    try:
        btf.struct_member_offset("file_operations", "bitfield")
    except ValueError:
        print("ok   non byte aligned member raises ValueError")
    else:
        failures.append("non byte aligned member did not raise ValueError")

    if failures:
        print()
        for failure in failures:
            print("FAIL " + failure)
        return 1
    print("\nall BTF parser tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
