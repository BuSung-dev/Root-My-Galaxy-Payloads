#!/usr/bin/env python3
"""Independent ELF+disasm harness for verifying FZG1 mcast copy semantics."""
import struct, sys
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

import os
PATH = os.environ.get("VMLINUX", "/mnt/240G_SSD/s9180-fzg1/vmlinux.elf")
data = open(PATH, "rb").read()

# --- minimal ELF64 parse ---
assert data[:4] == b"\x7fELF"
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
    return b[:b.index(b"\0")].decode()

for s in secs:
    s["sname"] = sname(s)

# --- symbols ---
symtab = next(s for s in secs if s["sname"] == ".symtab")
strtab = secs[symtab["link"]]
syms = []
cnt = symtab["size"] // 24
for i in range(cnt):
    off = symtab["off"] + i * 24
    nameoff, info, other, shndx, value, size = \
        struct.unpack_from("<IBBHQQ", data, off)
    b = data[strtab["off"] + nameoff:]
    nm = b[:b.index(b"\0")].decode(errors="replace")
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
    return v, (nxt - v if nxt else 0x2000), nxt

md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
md.detail = False

TARGET = sys.argv[1] if len(sys.argv) > 1 else "do_ipv6_setsockopt"
LIMIT = int(sys.argv[2]) if len(sys.argv) > 2 else 0

if TARGET not in by_name:
    print(f"symbol {TARGET} NOT FOUND")
    sys.exit(1)

start, size, end = fn_bounds(TARGET)
print(f"=== {TARGET} @ 0x{start:x}  size=0x{size:x}  end=0x{end:x} ===")
blob = code_at(start, size)
ins = list(md.disasm(blob, start))
if LIMIT:
    ins = ins[:LIMIT]

for i in ins:
    print(f"+0x{i.address-start:04x}  0x{i.address:x}  {i.mnemonic:8s} {i.op_str}")
