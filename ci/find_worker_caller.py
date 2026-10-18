#!/usr/bin/env python3
"""Find the SLIDE_TRACEFS_WORKER_CALLER_OFF value in a recovered vmlinux.

The `sched:sched_blocked_reason` tracepoint records the `caller` field
produced by `__get_wchan(tsk)`. For an idle kworker blocked inside
`worker_thread`, the saved return PC is the instruction immediately after the
`bl schedule` in that function.

This script decodes the ARM64 `bl` instructions inside `worker_thread` and
reports the return address of the call that targets `schedule`, which is the
value that belongs in the target profile.
"""

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from derive_offsets import read_elf_symbols  # noqa: E402

# ARM64 BL: bits 31..26 == 0b100101, imm26 is a signed word offset.
BL_MASK = 0xFC000000
BL_OP = 0x94000000


def read_section(elf_data: bytes, section_name: str):
    """Return (address, bytes) of a section in a little-endian ELF64 file."""
    e_shoff = struct.unpack_from("<Q", elf_data, 0x28)[0]
    e_shentsize = struct.unpack_from("<H", elf_data, 0x3A)[0]
    e_shnum = struct.unpack_from("<H", elf_data, 0x3C)[0]
    e_shstrndx = struct.unpack_from("<H", elf_data, 0x3E)[0]

    sections = []
    for i in range(e_shnum):
        base = e_shoff + i * e_shentsize
        fields = struct.unpack_from("<IIQQQQIIQQ", elf_data, base)
        sections.append(
            {
                "name_off": fields[0],
                "type": fields[1],
                "addr": fields[3],
                "offset": fields[4],
                "size": fields[5],
                "link": fields[6],
                "entsize": fields[9],
            }
        )

    strtab = sections[e_shstrndx]
    shstr = elf_data[strtab["offset"] : strtab["offset"] + strtab["size"]]

    for sec in sections:
        end = shstr.find(b"\0", sec["name_off"])
        name = shstr[sec["name_off"] : end].decode("ascii", "replace")
        if name == section_name:
            return sec["addr"], elf_data[sec["offset"] : sec["offset"] + sec["size"]]
    raise KeyError(f"section {section_name} not found")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("vmlinux", type=Path)
    parser.add_argument("--worker", default="worker_thread")
    parser.add_argument("--schedule", default="schedule")
    args = parser.parse_args()

    data = args.vmlinux.read_bytes()
    symbols = read_elf_symbols(args.vmlinux)

    base = min(
        (addr for name, addr in symbols.items() if addr >= 0xFFFFFFC000000000),
        default=0,
    ) & ~0xFFF

    if args.worker not in symbols:
        print(f"symbol {args.worker} not found")
        return 1
    if args.schedule not in symbols:
        print(f"symbol {args.schedule} not found")
        return 1

    worker_addr = symbols[args.worker]
    schedule_addr = symbols[args.schedule]
    print(f"ELF base:            0x{base:x}")
    print(f"{args.worker}: 0x{worker_addr:x} (offset 0x{worker_addr - base:x})")
    print(f"{args.schedule}: 0x{schedule_addr:x} (offset 0x{schedule_addr - base:x})")

    # The .kernel section holds the whole raw Image at the ELF base address.
    section_addr, section = read_section(data, ".kernel")
    print(f".kernel section at 0x{section_addr:x}, {len(section)} bytes")

    def read_bytes_at(addr, size):
        start = addr - section_addr
        if start < 0 or start + size > len(section):
            raise ValueError(f"address 0x{addr:x} is outside .kernel")
        return section[start : start + size]

    # The function body extends to the next symbol, which bounds the scan.
    later = [addr for addr in symbols.values() if addr > worker_addr]
    body_end = min(later) if later else worker_addr + 0x2000
    body = read_bytes_at(worker_addr, body_end - worker_addr)

    print(f"scanning 0x{worker_addr:x}..0x{body_end:x} for bl {args.schedule}")

    found = []
    for offset in range(0, len(body) - 3, 4):
        insn = struct.unpack_from("<I", body, offset)[0]
        if insn & BL_MASK != BL_OP:
            continue
        imm26 = insn & 0x03FFFFFF
        if imm26 & 0x02000000:
            imm26 -= 0x04000000
        target = worker_addr + offset + (imm26 << 2)
        return_address = worker_addr + offset + 4
        if target == schedule_addr:
            found.append((worker_addr + offset, return_address))

    if not found:
        print(f"no 'bl {args.schedule}' found in {args.worker}")
        return 1

    for call_addr, ret_addr in found:
        print(f"  bl {args.schedule} at 0x{call_addr:x} "
              f"(offset 0x{call_addr - base:x}); "
              f"return address 0x{ret_addr:x} "
              f"(offset 0x{ret_addr - base:x})")

    print(f"SLIDE_TRACEFS_WORKER_CALLER_OFF = 0x{found[0][1] - base:x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
