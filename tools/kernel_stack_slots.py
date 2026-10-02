#!/usr/bin/env python3
"""SP-tracking analysis (entry-sp-relative; ignores the naive [sp,#imm] trap).

Resolve every [sp,#imm] to a true frame offset relative to the function's
ENTRY sp, so the copy range can be compared against the saved-register /
return-address slots correctly.
"""

import argparse
import os
import re
import struct
import sys
from pathlib import Path

from capstone import CS_ARCH_ARM64, CS_MODE_ARM, Cs


def parse_hex_or_int(val: str) -> int:
    try:
        return int(val, 0)
    except ValueError as err:
        raise argparse.ArgumentTypeError(f"Invalid integer/hex value: '{val}'") from err


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="SP-tracking analysis: resolve every [sp,#imm] "
        "to a true frame offset relative to entry sp"
    )
    parser.add_argument("name", help="Kernel function symbol name")
    parser.add_argument(
        "lo",
        nargs="?",
        type=parse_hex_or_int,
        default=-0x300,
        help="Low bound offset relative to entry sp (default: -0x300)",
    )
    parser.add_argument(
        "hi",
        nargs="?",
        type=parse_hex_or_int,
        default=0x80,
        help="High bound offset relative to entry sp (default: 0x80)",
    )
    parser.add_argument(
        "--vmlinux",
        "-k",
        default=os.environ.get("VMLINUX"),
        help="Path to uncompressed vmlinux ELF (or set VMLINUX env var)",
    )
    parser.add_argument(
        "--assume-sp",
        default=None,
        type=parse_hex_or_int,
        help="hex sp for CFG-unreachable blocks; results tagged ASSUMED, not proven",
    )
    return parser


def load_elf(data: bytes):
    """Parse ELF section headers with bounds checks."""
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
            o = e_shoff + i * e_shentsize
            nm, typ, _fl, addr, off, size, link, _info, _al, _es = struct.unpack_from(
                "<IIQQQQIIQQ", data, o
            )
            secs.append(
                {
                    "name": nm,
                    "typ": typ,
                    "addr": addr,
                    "off": off,
                    "size": size,
                    "link": link,
                }
            )
    except struct.error as err:
        sys.exit(f"Error: truncated section header: {err}")
    if e_shstrndx >= len(secs) or secs[e_shstrndx]["off"] >= len(data):
        sys.exit("Error: corrupt ELF: shstrtab offset out of range")
    return secs, e_shstrndx


def parse_sp_imm(op: str) -> int | None:
    """Parse the immediate of add/sub sp. Returns None on failure."""
    parts = op.split(",")
    if len(parts) < 3:
        return None
    try:
        imm = int(parts[2].strip().removeprefix("#"), 0)
        if len(parts) > 3 and "lsl #12" in parts[3]:
            imm <<= 12
        return imm
    except ValueError:
        return None


def branch_target(op_str: str) -> int | None:
    """Return absolute branch target address or None."""
    for part in reversed([p.strip() for p in op_str.split(",")]):
        token = part.removeprefix("#")
        if token.startswith(("0x", "-0x")) or token.lstrip("-").isdigit():
            try:
                return int(token, 0)
            except ValueError:
                continue
    return None


COND_BRANCHES = {"cbz", "cbnz", "tbz", "tbnz"}
RET_INSNS = {"ret", "retab", "eret"}


def is_cond_branch(mnemonic: str) -> bool:
    # capstone renders B.NE/B.LO/... as mnemonic "b.ne"/"b.lo"/...
    return mnemonic in COND_BRANCHES or (
        mnemonic.startswith("b.") and mnemonic not in ("br", "blr")
    )


def main(argv=None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    lo, hi, assume, name = args.lo, args.hi, args.assume_sp, args.name

    if lo >= hi:
        parser.error(f"lo ({lo:#x}) must be strictly less than hi ({hi:#x})")
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
    (e_machine,) = struct.unpack_from("<H", data, 0x12)
    if e_machine != 0xB7:
        sys.exit(
            f"Error: '{vmlinux}' is not an AArch64 ELF "
            f"(e_machine={e_machine:#x}, expected 0xb7)"
        )

    secs, e_shstrndx = load_elf(data)
    sh = secs[e_shstrndx]

    def section_name(s) -> str:
        b = data[sh["off"] + s["name"] :]
        idx = b.find(b"\0")
        return (b[:idx] if idx >= 0 else b).decode(errors="replace")

    for s in secs:
        s["sname"] = section_name(s)
    symtab = next((s for s in secs if s["sname"] == ".symtab"), None)
    if symtab is None:
        sys.exit(f"Error: .symtab section not found in '{vmlinux}'")
    if symtab["link"] >= len(secs):
        sys.exit("Error: corrupt ELF: symtab link out of range")
    strt = secs[symtab["link"]]
    syms: list[tuple[int, int, str]] = []  # (value, size, name)
    try:
        for i in range(symtab["size"] // 24):
            o = symtab["off"] + i * 24
            no, info, _oth, _shx, val, sz = struct.unpack_from("<IBBHQQ", data, o)
            b = data[strt["off"] + no :]
            idx = b.find(b"\0")
            n = (b[:idx] if idx >= 0 else b).decode(errors="replace")
            if n and (info & 0xF) == 2 and val:
                syms.append((val, sz, n))
    except struct.error as err:
        sys.exit(f"Error: truncated symtab: {err}")
    syms.sort()
    byname: dict[str, tuple[int, int]] = {}
    for val, sz, n in syms:
        byname.setdefault(n, (val, sz))
    if name not in byname:
        sys.exit(f"Error: symbol '{name}' not found in symtab of '{vmlinux}'")

    # Prefer ELF st_size; fall back to next-symbol distance.
    uniq_addrs = sorted({v for v, _, _ in syms})

    def code_at(va: int, n: int) -> bytes | None:
        """Return n bytes at VA, or None if unmapped/non-PROGBITS."""
        for s in secs:
            if s["typ"] == 1 and s["addr"] <= va < s["addr"] + s["size"]:
                o = s["off"] + (va - s["addr"])
                return data[o : o + n]
        return None

    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    start, st_size = byname[name]
    nxt_addr = next((v for v in uniq_addrs if v > start), None)
    gap = (nxt_addr - start) if nxt_addr is not None else 0x4000
    func_len = st_size if st_size else gap
    blob = code_at(start, func_len)
    if blob is None:
        sys.exit(f"Error: symbol '{name}' @ {start:#x} is not in a PROGBITS section")
    if not blob:
        sys.exit(f"Error: empty code range for symbol '{name}'")
    ins = list(md.disasm(blob, start))
    if not ins:
        sys.exit(
            f"Error: no instructions disassembled for '{name}' "
            "(wrong symbol or bad function bounds)"
        )

    # entry_sp = 0. sp is tracked as a signed offset from entry sp.
    events = []  # (addr, kind, resolved_off, text)
    predec = re.compile(r"\[sp,\s*#(-?0x[0-9a-f]+|-?\d+)\]!")
    postidx = re.compile(r"\[sp\],\s*#(-?0x[0-9a-f]+|-?\d+)\]")
    plain = re.compile(r"\[sp(?:,\s*#(-?0x[0-9a-f]+|-?\d+))?\]")

    # --- basic-block CFG so sp is propagated per block, not linearly ---
    addr2idx = {insn.address: n for n, insn in enumerate(ins)}

    bounds = {0}
    for n, insn in enumerate(ins):
        if (
            is_cond_branch(insn.mnemonic)
            or insn.mnemonic in RET_INSNS
            or insn.mnemonic in ("b", "br", "blr")
        ):
            if n + 1 < len(ins):
                bounds.add(n + 1)
            t = branch_target(insn.op_str)
            if t is not None and t in addr2idx:
                bounds.add(addr2idx[t])
    blocks = sorted(bounds)
    blk_of = {}
    for b, s in enumerate(blocks):
        e = blocks[b + 1] if b + 1 < len(blocks) else len(ins)
        for n in range(s, e):
            blk_of[n] = b

    sp_in: dict[int, int | None] = {0: 0}
    work = [0]
    while work:
        b = work.pop()
        cur = sp_in[b]
        s = blocks[b]
        e = blocks[b + 1] if b + 1 < len(blocks) else len(ins)
        for n in range(s, e):
            insn = ins[n]
            m, op = insn.mnemonic, insn.op_str
            pm = predec.search(op)
            if pm and (m.startswith("st") or m.startswith("ld")) and cur is not None:
                cur += int(pm.group(1), 0)
            if m in ("sub", "add") and op.startswith("sp,") and cur is not None:
                imm = parse_sp_imm(op)
                if imm is None:
                    cur = None
                else:
                    cur = cur + imm if m == "add" else cur - imm
        if e <= s:
            continue
        last = ins[e - 1]
        succs: list[int] = []
        if last.mnemonic in RET_INSNS:
            pass
        elif last.mnemonic == "b":
            t = branch_target(last.op_str)
            if t is not None and t in addr2idx:
                succs = [blk_of[addr2idx[t]]]
        elif is_cond_branch(last.mnemonic):
            t = branch_target(last.op_str)
            if t is not None and t in addr2idx:
                succs = [blk_of[addr2idx[t]]]
            if e < len(ins):
                succs.append(blk_of[e])
        elif last.mnemonic in ("br", "blr"):
            pass  # indirect: unknown successors
        elif e < len(ins):
            succs = [blk_of[e]]
        for sb in succs:
            if sb not in sp_in:
                sp_in[sb] = cur
                work.append(sb)
            elif sp_in[sb] != cur and sp_in[sb] is not None:
                sp_in[sb] = None  # conflicting paths; mark unknown
                work.append(sb)

    # per-instruction sp: re-simulate inside each block from its entry sp
    sp_at: dict[int, int | None] = {}
    for b, s in enumerate(blocks):
        e = blocks[b + 1] if b + 1 < len(blocks) else len(ins)
        cur = sp_in.get(b)
        for n in range(s, e):
            sp_at[n] = cur
            if cur is None:
                continue
            insn = ins[n]
            m, op = insn.mnemonic, insn.op_str
            pm = predec.search(op)
            if pm and (m.startswith("st") or m.startswith("ld")):
                cur += int(pm.group(1), 0)
                continue
            if m in ("sub", "add") and op.startswith("sp,"):
                imm = parse_sp_imm(op)
                if imm is None:
                    cur = None
                else:
                    cur = cur + imm if m == "add" else cur - imm

    for n, insn in enumerate(ins):
        a = insn.address - start
        m, op = insn.mnemonic, insn.op_str
        sp = sp_at[n]
        assumed = False
        if sp is None and assume is not None:
            sp = assume
            assumed = True
        if sp is None:
            events.append(
                (
                    a,
                    "unknown-sp",
                    None,
                    f"{m} {op}   [sp UNKNOWN: conflicting/indirect preds]",
                )
            )
            continue
        pm = predec.search(op)
        if pm and (m.startswith("st") or m.startswith("ld")):
            imm = int(pm.group(1), 0)
            sp += imm
            tag = " [ASSUMED]" if assumed else ""
            kind = "load" if m.startswith("ld") else "store"
            events.append(
                (a, kind, sp, f"{m} {op}   [sp->{sp:+#x} after pre-index]{tag}")
            )
            continue
        if m in ("sub", "add") and op.startswith("sp,"):
            imm = parse_sp_imm(op)
            if imm is None:
                events.append(
                    (a, "unknown-sp", None, f"{m} {op}   [sp UNKNOWN: bad imm]")
                )
                continue
            newsp = sp + imm if m == "add" else sp - imm
            tag = " [ASSUMED]" if assumed else ""
            events.append((a, "spadj", newsp, f"{m} {op}   [sp->{newsp:+#x}]{tag}"))
            continue
        if m in ("ldp", "stp", "ldr", "str", "ldur", "stur"):
            pm2 = postidx.search(op)
            if pm2:
                tag = " [ASSUMED]" if assumed else ""
                events.append((a, "postidx", sp, f"{m} {op}   [sp->{sp:+#x}]{tag}"))
                continue
        qm = plain.search(op)
        if qm:
            raw_imm = qm.group(1)
            imm = int(raw_imm, 0) if raw_imm is not None else 0
            off = sp + imm
            kind = "load" if m.startswith("ld") else "store"
            tag = " [ASSUMED]" if assumed else ""
            events.append((a, kind, off, f"{m} {op}   [sp->{off:+#x}]{tag}"))

    print(f"=== {name} @0x{start:x}, {len(ins)} insns ===")
    print("--- sp adjustments ---")
    for a, k, _off, t in events:
        if k == "spadj":
            print(f"  +0x{a:04x}  {t}")
    print("--- all [sp,#imm] accesses, resolved to entry-sp-relative offsets ---")
    for a, k, off, t in events:
        if k == "unknown-sp":
            print(f"  +0x{a:04x}  unknown-sp  {t}")
        elif k in ("load", "store", "postidx") and off is not None and lo <= off < hi:
            print(f"  +0x{a:04x}  {k:7s} off={off:+#06x}  {t}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
