#!/usr/bin/env python3
"""Independent ELF+disasm harness for verifying kernel function disassembly."""

import argparse
import os
import struct
import sys
from pathlib import Path

from capstone import CS_ARCH_ARM64, CS_MODE_ARM, Cs


def parse_nonnegative_int(val: str) -> int:
    try:
        v = int(val, 0)
    except ValueError as err:
        raise argparse.ArgumentTypeError(f"Invalid integer/hex value: '{val}'") from err
    if v < 0:
        raise argparse.ArgumentTypeError(f"Limit must be non-negative: {v}")
    return v


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Disassemble a kernel function from an uncompressed vmlinux ELF"
    )
    parser.add_argument(
        "symbol",
        nargs="?",
        default="do_ipv6_setsockopt",
        help="Target kernel function symbol name (default: do_ipv6_setsockopt)",
    )
    parser.add_argument(
        "--limit",
        "-n",
        dest="opt_limit",
        type=parse_nonnegative_int,
        default=None,
        help="Limit number of instructions disassembled (0 = all)",
    )
    parser.add_argument(
        "--vmlinux",
        "-k",
        default=os.environ.get("VMLINUX"),
        help="Path to uncompressed vmlinux ELF (or set VMLINUX env var)",
    )
    return parser


def main(argv=None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    if not args.vmlinux:
        parser.error(
            "vmlinux path required: pass --vmlinux <path> "
            "or set VMLINUX environment variable"
        )
    vmlinux = Path(args.vmlinux)
    if not vmlinux.is_file():
        sys.exit(f"Error: vmlinux file not found at '{vmlinux}'")
    try:
        data = vmlinux.read_bytes()
    except OSError as err:
        sys.exit(f"Error reading {vmlinux}: {err}")

    if len(data) < 0x40 or data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        sys.exit(f"Error: '{vmlinux}' is not a valid 64-bit Little-Endian ELF file")
    try:
        (e_machine,) = struct.unpack_from("<H", data, 0x12)
    except struct.error as err:
        sys.exit(f"Error: truncated ELF header: {err}")
    if e_machine != 0xB7:
        sys.exit(
            f"Error: '{vmlinux}' is not an AArch64 ELF "
            f"(e_machine={e_machine:#x}, expected 0xb7)"
        )

    try:
        (e_shoff,) = struct.unpack_from("<Q", data, 0x28)
        (e_shentsize,) = struct.unpack_from("<H", data, 0x3A)
        (e_shnum,) = struct.unpack_from("<H", data, 0x3C)
        (e_shstrndx,) = struct.unpack_from("<H", data, 0x3E)
    except struct.error as err:
        sys.exit(f"Error: truncated ELF header: {err}")
    if e_shstrndx >= e_shnum:
        sys.exit("Error: corrupt ELF: e_shstrndx out of range")
    if e_shoff + e_shnum * e_shentsize > len(data):
        sys.exit("Error: corrupt ELF: section headers extend past EOF")

    secs = []
    try:
        for i in range(e_shnum):
            off = e_shoff + i * e_shentsize
            name, typ, _flags, addr, offset, size, link, _info, _al, _es = (
                struct.unpack_from("<IIQQQQIIQQ", data, off)
            )
            secs.append(
                {
                    "name": name,
                    "typ": typ,
                    "addr": addr,
                    "off": offset,
                    "size": size,
                    "link": link,
                }
            )
    except struct.error as err:
        sys.exit(f"Error: truncated section header: {err}")

    shstr = secs[e_shstrndx]

    def section_name(s) -> str:
        b = data[shstr["off"] + s["name"] :]
        idx = b.find(b"\0")
        return (b[:idx] if idx >= 0 else b).decode(errors="replace")

    for s in secs:
        s["sname"] = section_name(s)

    symtab = next((s for s in secs if s["sname"] == ".symtab"), None)
    if symtab is None:
        sys.exit(f"Error: .symtab section not found in '{vmlinux}'")
    if symtab["link"] >= len(secs):
        sys.exit("Error: corrupt ELF: symtab link out of range")
    strtab = secs[symtab["link"]]
    syms: list[tuple[int, int, str]] = []
    try:
        cnt = symtab["size"] // 24
        for i in range(cnt):
            off = symtab["off"] + i * 24
            nameoff, info, _other, _shndx, value, size = struct.unpack_from(
                "<IBBHQQ", data, off
            )
            b = data[strtab["off"] + nameoff :]
            idx = b.find(b"\0")
            nm = (b[:idx] if idx >= 0 else b).decode(errors="replace")
            if nm and (info & 0xF) == 2 and value:  # STT_FUNC
                syms.append((value, size, nm))
    except struct.error as err:
        sys.exit(f"Error: truncated symtab: {err}")
    syms.sort()
    by_name: dict[str, tuple[int, int]] = {}
    for v, sz, nm in syms:
        by_name.setdefault(nm, (v, sz))

    def code_at(vaddr: int, nbytes: int) -> bytes | None:
        for s in secs:
            if s["typ"] == 1 and s["addr"] <= vaddr < s["addr"] + s["size"]:
                o = s["off"] + (vaddr - s["addr"])
                return data[o : o + nbytes]
        return None

    def fn_bounds(name: str):
        v, st_size = by_name[name]
        nxt = next((sv for sv, _, _ in syms if sv > v), None)
        if st_size:
            size = st_size
            end = v + size
        elif nxt is not None:
            size = nxt - v
            end = nxt
        else:
            size = 0x2000
            end = v + size
        return v, size, end

    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    md.detail = False

    target = args.symbol
    limit = args.opt_limit if args.opt_limit is not None else 0

    if target not in by_name:
        sys.exit(f"Error: symbol '{target}' not found in symtab of '{vmlinux}'")

    start, size, end = fn_bounds(target)
    print(f"=== {target} @ 0x{start:x}  size=0x{size:x}  end=0x{end:x} ===")
    blob = code_at(start, size)
    if blob is None:
        sys.exit(f"Error: symbol '{target}' is not in a PROGBITS section")
    if not blob:
        sys.exit(f"Error: empty code range for symbol '{target}'")
    ins = list(md.disasm(blob, start, count=limit if limit else 0))
    if not ins:
        sys.exit(
            f"Error: no instructions disassembled for '{target}' "
            "(wrong symbol or bad function bounds)"
        )

    for insn in ins:
        print(
            f"+0x{insn.address - start:04x}  0x{insn.address:x}  "
            f"{insn.mnemonic:8s} {insn.op_str}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
