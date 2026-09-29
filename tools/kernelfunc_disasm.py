#!/usr/bin/env python3
"""Independent ELF+disasm harness for verifying kernel function disassembly."""
import argparse
import os
import struct
import sys
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

def parse_nonnegative_int(val):
    try:
        v = int(val, 0)
    except ValueError:
        raise argparse.ArgumentTypeError(f"Invalid integer/hex value: '{val}'")
    if v < 0:
        raise argparse.ArgumentTypeError(f"Limit must be non-negative: {v}")
    return v

parser = argparse.ArgumentParser(
    description="Disassemble a kernel function from an uncompressed vmlinux ELF"
)
parser.add_argument(
    "symbol", nargs="?", default="do_ipv6_setsockopt",
    help="Target kernel function symbol name (default: do_ipv6_setsockopt)"
)
parser.add_argument(
    "pos_limit", nargs="?", default=None,
    help="Optional positional limit (or use -n/--limit)"
)
parser.add_argument(
    "--limit", "-n", dest="opt_limit", type=parse_nonnegative_int, default=None,
    help="Limit number of instructions disassembled (0 = all)"
)
parser.add_argument(
    "--vmlinux", "-k",
    default=os.environ.get("VMLINUX"),
    help="Path to uncompressed vmlinux ELF (or set VMLINUX env var)"
)
args = parser.parse_args()

if not args.vmlinux:
    parser.error("vmlinux path required: pass --vmlinux <path> or set VMLINUX environment variable")

if not os.path.isfile(args.vmlinux):
    sys.exit(f"Error: vmlinux file not found at '{args.vmlinux}'")

try:
    with open(args.vmlinux, "rb") as f:
        data = f.read()
except OSError as e:
    sys.exit(f"Error reading {args.vmlinux}: {e}")

if len(data) < 0x40 or data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
    sys.exit(f"Error: '{args.vmlinux}' is not a valid 64-bit Little-Endian ELF file")

e_machine, = struct.unpack_from("<H", data, 0x12)
if e_machine != 0xb7:
    sys.exit(f"Error: '{args.vmlinux}' is not an AArch64 ELF (e_machine={e_machine:#x}, expected 0xb7)")

e_shoff, = struct.unpack_from("<Q", data, 0x28)
e_shentsize, = struct.unpack_from("<H", data, 0x3a)
e_shnum, = struct.unpack_from("<H", data, 0x3c)
e_shstrndx, = struct.unpack_from("<H", data, 0x3e)

secs = []
for i in range(e_shnum):
    off = e_shoff + i * e_shentsize
    name, typ, flags, addr, offset, size, link, info, align, entsize = \
        struct.unpack_from("<IIQQQQIIQQ", data, off)
    secs.append(dict(name=name, typ=typ, addr=addr, off=offset, size=size,
                     link=link, entsize=entsize))

shstr = secs[e_shstrndx]
def sname(s):
    b = data[shstr["off"] + s["name"]:]
    idx = b.find(b"\0")
    return (b[:idx] if idx >= 0 else b).decode(errors="replace")

for s in secs:
    s["sname"] = sname(s)

# --- symbols ---
symtab = next((s for s in secs if s["sname"] == ".symtab"), None)
if not symtab:
    sys.exit(f"Error: .symtab section not found in '{args.vmlinux}'")
strtab = secs[symtab["link"]]
syms = []
cnt = symtab["size"] // 24
for i in range(cnt):
    off = symtab["off"] + i * 24
    nameoff, info, other, shndx, value, size = \
        struct.unpack_from("<IBBHQQ", data, off)
    b = data[strtab["off"] + nameoff:]
    idx = b.find(b"\0")
    nm = (b[:idx] if idx >= 0 else b).decode(errors="replace")
    if nm and (info & 0xf) == 2 and value:  # STT_FUNC
        syms.append((value, size, nm, nm))
syms.sort()
by_name = {}
for v, sz, nm, _ in syms:
    by_name.setdefault(nm, v)

def code_at(vaddr, nbytes):
    for s in secs:
        if s["typ"] == 1 and s["addr"] <= vaddr < s["addr"] + s["size"]:
            o = s["off"] + (vaddr - s["addr"])
            return data[o:o + nbytes]
    return b""

def fn_bounds(name):
    if name not in by_name:
        return None
    v = by_name[name]
    nxt = None
    for (sv, ssz, snm, _) in syms:
        if sv > v:
            nxt = sv
            break
    size = nxt - v if nxt else 0x2000
    end = nxt if nxt is not None else v + size
    return v, size, end

md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
md.detail = False

TARGET = args.symbol
if args.opt_limit is not None:
    LIMIT = args.opt_limit
elif args.pos_limit is not None:
    LIMIT = parse_nonnegative_int(args.pos_limit)
elif args.symbol.isdigit() or (args.symbol.startswith("0x") and len(args.symbol) <= 6):
    LIMIT = parse_nonnegative_int(args.symbol)
    TARGET = "do_ipv6_setsockopt"
else:
    LIMIT = 0

if TARGET not in by_name:
    sys.exit(f"Error: symbol '{TARGET}' not found in symtab of '{args.vmlinux}'")

start, size, end = fn_bounds(TARGET)
print(f"=== {TARGET} @ 0x{start:x}  size=0x{size:x}  end=0x{end:x} ===")
blob = code_at(start, size)
ins = list(md.disasm(blob, start, count=LIMIT))

for i in ins:
    print(f"+0x{i.address-start:04x}  0x{i.address:x}  {i.mnemonic:8s} {i.op_str}")
