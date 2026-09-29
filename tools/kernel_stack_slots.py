#!/usr/bin/env python3
"""SP-tracking analysis (entry-sp-relative; ignores the naive [sp,#imm] trap): resolve every [sp,#imm] to a true frame offset
relative to the function's ENTRY sp, so the copy range can be compared
against the saved-register/return-address slots correctly."""
import struct, sys, re
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

import os
PATH = os.environ.get("VMLINUX", "/mnt/240G_SSD/s9180-fzg1/vmlinux.elf")
data = open(PATH, "rb").read()
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
    s["sname"] = b[:b.index(b"\0")].decode()
st = next(s for s in secs if s["sname"] == ".symtab")
strt = secs[st["link"]]
syms = []
for i in range(st["size"] // 24):
    o = st["off"] + i * 24
    no, info, oth, shx, val, sz = struct.unpack_from("<IBBHQQ", data, o)
    b = data[strt["off"] + no:]
    n = b[:b.index(b"\0")].decode(errors="replace")
    if n and (info & 0xf) == 2 and val:
        syms.append((val, n))
syms.sort()
byname = {}
for v, n in syms:
    byname.setdefault(n, v)
nxt = {}
for i, (v, n) in enumerate(syms):
    nxt.setdefault(v, syms[i + 1][0] if i + 1 < len(syms) else v + 0x4000)

def code_at(va, n):
    for s in secs:
        if s["typ"] == 1 and s["addr"] <= va < s["addr"] + s["size"]:
            o = s["off"] + (va - s["addr"])
            return data[o:o + n]
    return b""

md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
import argparse as _ap
_ap_p = _ap.ArgumentParser()
_ap_p.add_argument("name")
_ap_p.add_argument("lo", nargs="?", default="0x0")
_ap_p.add_argument("hi", nargs="?", default="0x0")
_ap_p.add_argument("--assume-sp", default=None,
                   help="hex sp for CFG-unreachable blocks (e.g. switch cases "
                        "after br); results tagged ASSUMED, not proven")
_ap_a = _ap_p.parse_args()
NAME, LO, HI = _ap_a.name, int(_ap_a.lo, 0), int(_ap_a.hi, 0)
ASSUME = int(_ap_a.assume_sp, 0) if _ap_a.assume_sp is not None else None
start = byname[NAME]
ins = list(md.disasm(code_at(start, nxt[start] - start), start))

# entry_sp = 0. sp is tracked as a signed offset from entry sp.
sp = 0
events = []   # (addr, kind, resolved_off, text)
predec = re.compile(r"\[sp,\s*#(-?0x[0-9a-f]+|-?\d+)\]!\s*$")
postinc = re.compile(r"\[sp\],\s*#(-?0x[0-9a-f]+|-?\d+)\]\s*$")
plain = re.compile(r"\[sp,\s*#(-?0x[0-9a-f]+|-?\d+)\]")

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
        pm = re.search(r"\[sp,\s*#(-?0x[0-9a-f]+|-?\d+)\]!", op)
        if pm and m.startswith("st") and cur is not None:
            cur += int(pm.group(1), 0)
        if m in ("sub", "add") and op.startswith("sp,") and cur is not None:
            parts = op.split(",")
            try:
                imm = int(parts[2].strip().lstrip("#"), 0)
            except Exception:
                imm = 0
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
    elif last.mnemonic in ("br", "blr"):
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
        pm = re.search(r"\[sp,\s*#(-?0x[0-9a-f]+|-?\d+)\]!", op)
        if pm and m.startswith("st"):
            cur += int(pm.group(1), 0)
            continue
        if m in ("sub", "add") and op.startswith("sp,"):
            parts = op.split(",")
            try:
                imm = int(parts[2].strip().lstrip("#"), 0)
            except Exception:
                imm = 0
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
    pm = re.search(r"\[sp,\s*#(-?0x[0-9a-f]+|-?\d+)\]!", op)
    if pm and m.startswith("st"):
        imm = int(pm.group(1), 0)
        sp += imm
        tag = " [ASSUMED]" if assumed else ""
        events.append((a, "store", sp, f"{m} {op}   [sp->{sp:+#x} after pre-index]{tag}"))
        continue
    if m in ("sub", "add") and op.startswith("sp,"):
        parts = op.split(",")
        try:
            imm = int(parts[2].strip().lstrip("#"), 0)
        except Exception:
            imm = 0
        newsp = sp + imm if m == "add" else sp - imm
        tag = " [ASSUMED]" if assumed else ""
        events.append((a, "spadj", newsp, f"{m} {op}   [sp->{newsp:+#x}]{tag}"))
        continue
    if m == "ldp" or m == "stp":
        pm2 = re.search(r"\[sp\],\s*#(-?0x[0-9a-f]+|-?\d+)\]", op)
        if pm2:
            tag = " [ASSUMED]" if assumed else ""
            events.append((a, "postidx", sp, f"{m} {op}   [sp->{sp:+#x}]{tag}"))
            continue
    qm = plain.search(op)
    if qm:
        off = sp + int(qm.group(1), 0)
        kind = "load" if m.startswith("ld") else "store"
        tag = " [ASSUMED]" if assumed else ""
        events.append((a, kind, off, f"{m} {op}   [sp->{off:+#x}]{tag}"))

print(f"=== {NAME} @0x{start:x}, {len(ins)} insns ===")
print(f"--- sp adjustments ---")
for a, k, off, t in events:
    if k == "spadj":
        print(f"  +0x{a:04x}  {t}")
print(f"--- all [sp,#imm] accesses, resolved to entry-sp-relative offsets ---")
for a, k, off, t in events:
    if k == "unknown-sp":
        print(f"  +0x{a:04x}  unknown-sp  {t}")
    elif k in ("load", "store", "postidx") and off is not None \
            and -0x300 <= off < 0x80:
        print(f"  +0x{a:04x}  {k:7s} off={off:+#06x}  {t}")
