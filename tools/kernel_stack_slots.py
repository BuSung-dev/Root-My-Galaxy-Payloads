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
NAME = sys.argv[1]
start = byname[NAME]
ins = list(md.disasm(code_at(start, nxt[start] - start), start))

# entry_sp = 0. sp is tracked as a signed offset from entry sp.
sp = 0
events = []   # (addr, kind, resolved_off, text)
predec = re.compile(r"\[sp,\s*#(-?0x[0-9a-f]+|-?\d+)\]!\s*$")
postinc = re.compile(r"\[sp\],\s*#(-?0x[0-9a-f]+|-?\d+)\]\s*$")
plain = re.compile(r"\[sp,\s*#(-?0x[0-9a-f]+|-?\d+)\]")

for i in ins:
    a = i.address - start
    m, op = i.mnemonic, i.op_str
    # pre-index: stp x29,x30,[sp,#-0x60]!  -> sp -= 0x60 then access
    pm = re.search(r"\[sp,\s*#(-?0x[0-9a-f]+|-?\d+)\]!", op)
    if pm and m.startswith("st"):
        imm = int(pm.group(1), 0)
        sp += imm
        events.append((a, "store", sp, f"{m} {op}   [sp->{sp:+#x} after pre-index]"))
        continue
    if m in ("sub", "add") and op.startswith("sp,"):
        parts = op.split(",")
        try:
            imm = int(parts[2].strip().lstrip("#"), 0)
        except Exception:
            imm = 0
        sp = sp + imm if m == "add" else sp - imm
        events.append((a, "spadj", sp, f"{m} {op}   [sp->{sp:+#x}]"))
        continue
    if m == "ldp" or m == "stp":
        pm2 = re.search(r"\[sp\],\s*#(-?0x[0-9a-f]+|-?\d+)\]", op)
        if pm2:
            off = sp
            events.append((a, "postidx", off, f"{m} {op}   [sp->{sp:+#x}]"))
            sp = sp + int(pm2.group(1), 0)
            continue
    qm = plain.search(op)
    if qm:
        off = sp + int(qm.group(1), 0)
        kind = "load" if m.startswith("ld") else "store"
        events.append((a, kind, off, f"{m} {op}   [sp->{off:+#x}]"))

print(f"=== {NAME} @0x{start:x}, {len(ins)} insns ===")
print(f"--- sp adjustments ---")
for a, k, off, t in events:
    if k == "spadj":
        print(f"  +0x{a:04x}  {t}")
print(f"--- stores/loads near entry sp (saved regs / ret addr), off in [-0x40,0x80) ---")
for a, k, off, t in events:
    if k in ("load", "store", "postidx") and -0x300 <= off < 0x80:
        print(f"  +0x{a:04x}  {k:7s} off={off:+#06x}  {t}")
