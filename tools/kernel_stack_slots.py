#!/usr/bin/env python3
"""SP-tracking analysis (entry-sp-relative; ignores the naive [sp,#imm] trap): resolve every [sp,#imm] to a true frame offset
relative to the function's ENTRY sp, so the copy range can be compared
against the saved-register/return-address slots correctly."""
import argparse
import os
import re
import struct
import sys
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

def parse_hex_or_int(val):
    try:
        return int(val, 0)
    except ValueError:
        raise argparse.ArgumentTypeError(f"Invalid integer/hex value: '{val}'")

parser = argparse.ArgumentParser(
    description="SP-tracking analysis: resolve every [sp,#imm] to a true frame offset relative to entry sp"
)
parser.add_argument("name", help="Kernel function symbol name")
parser.add_argument(
    "lo", nargs="?", type=parse_hex_or_int, default=-0x300,
    help="Low bound offset relative to entry sp (default: -0x300)"
)
parser.add_argument(
    "hi", nargs="?", type=parse_hex_or_int, default=0x80,
    help="High bound offset relative to entry sp (default: 0x80)"
)
parser.add_argument(
    "--vmlinux", "-k",
    default=os.environ.get("VMLINUX"),
    help="Path to uncompressed vmlinux ELF (or set VMLINUX env var)"
)
parser.add_argument(
    "--assume-sp", default=None, type=parse_hex_or_int,
    help="hex sp for CFG-unreachable blocks; results tagged ASSUMED, not proven"
)
args = parser.parse_args()

LO, HI = args.lo, args.hi
ASSUME = args.assume_sp
NAME = args.name

if LO >= HI:
    parser.error(f"lo ({LO:#x}) must be strictly less than hi ({HI:#x})")

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
    o = e_shoff + i * e_shentsize
    nm, typ, fl, addr, off, size, link, info, al, es = \
        struct.unpack_from("<IIQQQQIIQQ", data, o)
    secs.append(dict(name=nm, typ=typ, addr=addr, off=off, size=size, link=link))
sh = secs[e_shstrndx]
for s in secs:
    b = data[sh["off"] + s["name"]:]
    idx = b.find(b"\0")
    s["sname"] = (b[:idx] if idx >= 0 else b).decode(errors="replace")
st = next((s for s in secs if s["sname"] == ".symtab"), None)
if not st:
    sys.exit(f"Error: .symtab section not found in '{args.vmlinux}'")
strt = secs[st["link"]]
syms = []
for i in range(st["size"] // 24):
    o = st["off"] + i * 24
    no, info, oth, shx, val, sz = struct.unpack_from("<IBBHQQ", data, o)
    b = data[strt["off"] + no:]
    idx = b.find(b"\0")
    n = (b[:idx] if idx >= 0 else b).decode(errors="replace")
    if n and (info & 0xf) == 2 and val:
        syms.append((val, n))
syms.sort()
byname = {}
for v, n in syms:
    byname.setdefault(n, v)
uniq_addrs = sorted(set(v for v, n in syms))
nxt = {}
for i, v in enumerate(uniq_addrs):
    nxt[v] = uniq_addrs[i + 1] if i + 1 < len(uniq_addrs) else v + 0x4000

def code_at(va, n):
    for s in secs:
        if s["typ"] == 1 and s["addr"] <= va < s["addr"] + s["size"]:
            o = s["off"] + (va - s["addr"])
            return data[o:o + n]
    return b""

md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)

if NAME not in byname:
    sys.exit(f"Error: symbol '{NAME}' not found in symtab of '{args.vmlinux}'")

start = byname[NAME]
ins = list(md.disasm(code_at(start, nxt[start] - start), start))

def parse_sp_imm(op):
    parts = op.split(",")
    if len(parts) < 3:
        return 0
    try:
        imm = int(parts[2].strip().lstrip("#"), 0)
        if len(parts) > 3 and "lsl #12" in parts[3]:
            imm <<= 12
        return imm
    except Exception:
        return 0

# entry_sp = 0. sp is tracked as a signed offset from entry sp.
sp = 0
events = []   # (addr, kind, resolved_off, text)
predec = re.compile(r"\[sp,\s*#(-?0x[0-9a-f]+|-?\d+)\]!")
postidx = re.compile(r"\[sp\],\s*#(-?0x[0-9a-f]+|-?\d+)\]")
plain = re.compile(r"\[sp(?:,\s*#(-?0x[0-9a-f]+|-?\d+))?\]")

# --- basic-block CFG so sp is propagated per block, not linearly ---
# (Linear scan is WRONG for code after a ret: those are alternate case bodies
# reached via jumps with the pre-epilogue sp. This bit us on
# do_ipv6_setsockopt +0x0ce8. Blocks split at branches/rets; sp flows along
# edges. Indirect branches (br/blr) have unknown successors and are flagged.)
addr2idx = {i.address: n for n, i in enumerate(ins)}

def branch_target(i):
    # returns absolute target address or None
    parts = [p.strip() for p in i.op_str.split(",")]
    for p in reversed(parts):
        if p.startswith("#0x") or p.startswith("#-0x") or \
                (p.startswith("#") and p[1:].lstrip("-").isdigit()):
            try:
                return int(p.lstrip("#"), 0)
            except ValueError:
                pass
    return None

COND = {"cbz", "cbnz", "tbz", "tbnz"}


def is_cond_branch(m):
    # capstone renders B.NE/B.LO/... as mnemonic "b.ne"/"b.lo"/...
    return m in COND or (m.startswith("b.") and m != "br" and m != "blr")
RET = {"ret"}
# block boundaries
bounds = {0}
for n, i in enumerate(ins):
    if (is_cond_branch(i.mnemonic) or i.mnemonic in RET
            or i.mnemonic in ("b", "br")):
        if n + 1 < len(ins):
            bounds.add(n + 1)
        t = branch_target(i)
        if t is not None and t in addr2idx:
            bounds.add(addr2idx[t])
blocks = sorted(bounds)
blk_of = {}
for b, s in enumerate(blocks):
    e = blocks[b + 1] if b + 1 < len(blocks) else len(ins)
    for n in range(s, e):
        blk_of[n] = b

sp_in = {}   # block index -> sp at block entry (None = unknown/conflict)
sp_in[0] = 0
work = [0]
while work:
    b = work.pop()
    cur = sp_in[b]
    s = blocks[b]
    e = blocks[b + 1] if b + 1 < len(blocks) else len(ins)
    for n in range(s, e):
        i = ins[n]
        m, op = i.mnemonic, i.op_str
        pm = predec.search(op)
        if pm and m.startswith("st") and cur is not None:
            cur += int(pm.group(1), 0)
        if m in ("sub", "add") and op.startswith("sp,") and cur is not None:
            imm = parse_sp_imm(op)
            cur = cur + imm if m == "add" else cur - imm
    last = ins[e - 1]
    succs = []
    if last.mnemonic in RET:
        pass
    elif last.mnemonic == "b":
        t = branch_target(last)
        if t is not None and t in addr2idx:
            succs = [blk_of[addr2idx[t]]]
    elif is_cond_branch(last.mnemonic):
        t = branch_target(last)
        if t is not None and t in addr2idx:
            succs = [blk_of[addr2idx[t]]]
        if e < len(ins):
            succs.append(blk_of[e])
    elif last.mnemonic == "br":
        pass  # unknown successors; flagged below
    else:
        if e < len(ins):
            succs = [blk_of[e]]
    for sb in succs:
        if sb not in sp_in:
            sp_in[sb] = cur
            work.append(sb)
        elif sp_in[sb] != cur:
            sp_in[sb] = None  # conflicting paths; mark unknown
            work.append(sb)

# per-instruction sp: re-simulate inside each block from its entry sp
sp_at = {}
for b, s in enumerate(blocks):
    e = blocks[b + 1] if b + 1 < len(blocks) else len(ins)
    cur = sp_in.get(b)
    for n in range(s, e):
        sp_at[n] = cur
        if cur is None:
            continue
        i = ins[n]
        m, op = i.mnemonic, i.op_str
        pm = predec.search(op)
        if pm and m.startswith("st"):
            cur += int(pm.group(1), 0)
            continue
        if m in ("sub", "add") and op.startswith("sp,"):
            imm = parse_sp_imm(op)
            cur = cur + imm if m == "add" else cur - imm

for n, i in enumerate(ins):
    a = i.address - start
    m, op = i.mnemonic, i.op_str
    sp = sp_at[n]
    assumed = False
    if sp is None and ASSUME is not None:
        sp = ASSUME
        assumed = True
    if sp is None:
        events.append((a, "unknown-sp", None,
                       f"{m} {op}   [sp UNKNOWN: conflicting/indirect preds]"))
        continue
    # pre-index: stp x29,x30,[sp,#-0x60]!  -> sp -= 0x60 then access
    pm = predec.search(op)
    if pm and m.startswith("st"):
        imm = int(pm.group(1), 0)
        sp += imm
        tag = " [ASSUMED]" if assumed else ""
        events.append((a, "store", sp, f"{m} {op}   [sp->{sp:+#x} after pre-index]{tag}"))
        continue
    if m in ("sub", "add") and op.startswith("sp,"):
        imm = parse_sp_imm(op)
        newsp = sp + imm if m == "add" else sp - imm
        tag = " [ASSUMED]" if assumed else ""
        events.append((a, "spadj", newsp, f"{m} {op}   [sp->{newsp:+#x}]{tag}"))
        continue
    if m == "ldp" or m == "stp":
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

print(f"=== {NAME} @0x{start:x}, {len(ins)} insns ===")
print("--- sp adjustments ---")
for a, k, off, t in events:
    if k == "spadj":
        print(f"  +0x{a:04x}  {t}")
print("--- all [sp,#imm] accesses, resolved to entry-sp-relative offsets ---")
for a, k, off, t in events:
    if k == "unknown-sp":
        print(f"  +0x{a:04x}  unknown-sp  {t}")
    elif k in ("load", "store", "postidx") and off is not None \
            and LO <= off < HI:
        print(f"  +0x{a:04x}  {k:7s} off={off:+#06x}  {t}")
