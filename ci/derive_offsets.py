#!/usr/bin/env python3
"""Derive the S731NKSS7BZF1 target offsets from a recovered vmlinux and BTF.

This reads:
  * vmlinux.elf  - symbolized ELF produced by vmlinux-to-elf
  * vmlinux.btf   - raw BTF blob extracted from the kernel Image

and prints every offset that src/targets/<target>/target.h needs, together
with the symbol or BTF member it was derived from, exactly as
docs/PORTING.md requires.

The base address is the ELF load base of the recovered vmlinux
(0xffffffc008000000 for the 6.1 images used here).
"""

import argparse
import json
import struct
import sys
from pathlib import Path

# ---------------------------------------------------------------- BTF parser

BTF_KIND_INT = 1
BTF_KIND_PTR = 2
BTF_KIND_ARRAY = 3
BTF_KIND_STRUCT = 4
BTF_KIND_UNION = 5
BTF_KIND_ENUM = 6
BTF_KIND_FWD = 7
BTF_KIND_TYPEDEF = 8
BTF_KIND_VOLATILE = 9
BTF_KIND_CONST = 10
BTF_KIND_RESTRICT = 11
BTF_KIND_FUNC = 12
BTF_KIND_FUNC_PROTO = 13
BTF_KIND_VAR = 14
BTF_KIND_DATASEC = 15
BTF_KIND_FLOAT = 16
BTF_KIND_DECL_TAG = 17
BTF_KIND_TYPE_TAG = 18
BTF_KIND_ENUM64 = 19

BTF_INFO_KIND = 0x1F
BTF_INFO_VLEN = 0xFFFF
BTF_KIND_FLAG = 0x80000000


class Btf:
    def __init__(self, blob: bytes):
        magic, version, flags, hdr_len = struct.unpack_from("<HBBi", blob, 0)
        if magic != 0xEB9F:
            raise ValueError(f"bad BTF magic 0x{magic:x}")
        if version != 1:
            raise ValueError(f"bad BTF version {version}")
        _, _, _, _hdr_len, type_off, type_len, str_off, str_len = struct.unpack_from(
            "<HBBIIIII", blob, 0
        )
        self.strings = blob[hdr_len + str_off : hdr_len + str_off + str_len]
        types = blob[hdr_len + type_off : hdr_len + type_off + type_len]

        self.types = [0]
        self.names = [""]
        self.size = [0]
        self.kind = [0]
        self.vlen = [0]
        self.kind_flag = [False]
        self.members = [[]]  # list of list of (name, type_id, bit_offset)
        self.array = [None]  # (elem_type, index_type, nelems)
        self.enum_values = [[]]  # list of (name, value)

        offset = 0
        while offset < len(types):
            name_off, info, size_or_type = struct.unpack_from("<III", types, offset)
            offset += 12
            kind = (info >> 24) & BTF_INFO_KIND
            vlen = info & BTF_INFO_VLEN
            kind_flag = bool(info & BTF_KIND_FLAG)

            self.names.append(self._str(name_off))
            self.kind.append(kind)
            self.vlen.append(vlen)
            self.kind_flag.append(kind_flag)
            self.types.append(size_or_type)
            self.size.append(size_or_type)
            self.members.append([])
            self.array.append(None)
            self.enum_values.append([])

            if kind in (BTF_KIND_STRUCT, BTF_KIND_UNION):
                members = []
                for _ in range(vlen):
                    m_name_off, m_type, m_off = struct.unpack_from("<III", types, offset)
                    offset += 12
                    members.append((self._str(m_name_off), m_type, m_off))
                self.members[-1] = members
            elif kind == BTF_KIND_ARRAY:
                elem, index_type, nelems = struct.unpack_from("<III", types, offset)
                offset += 12
                self.array[-1] = (elem, index_type, nelems)
            elif kind == BTF_KIND_INT:
                # struct btf_int { __u32 encoding; __u32 offset; __u32 bits; }
                offset += 12
            elif kind == BTF_KIND_ENUM:
                values = []
                for _ in range(vlen):
                    e_name_off, e_val = struct.unpack_from("<Ii", types, offset)
                    offset += 8
                    values.append((self._str(e_name_off), e_val))
                self.enum_values[-1] = values
            elif kind == BTF_KIND_ENUM64:
                values = []
                for _ in range(vlen):
                    e_name_off, e_lo, e_hi = struct.unpack_from("<III", types, offset)
                    offset += 12
                    values.append((self._str(e_name_off), e_lo | (e_hi << 32)))
                self.enum_values[-1] = values
            elif kind == BTF_KIND_FUNC_PROTO:
                offset += 8 * vlen
            elif kind == BTF_KIND_VAR:
                # struct btf_var { __u32 linkage; }
                offset += 4
            elif kind == BTF_KIND_DATASEC:
                offset += 12 * vlen
            elif kind == BTF_KIND_DECL_TAG:
                # struct btf_decl_tag { __s32 component_idx; }
                offset += 4

        if offset != len(types):
            raise ValueError(
                f"BTF type section parse mismatch: consumed {offset} of {len(types)} bytes"
            )

    def _str(self, offset: int) -> str:
        end = self.strings.find(b"\0", offset)
        if end < 0:
            end = len(self.strings)
        return self.strings[offset:end].decode("utf-8", "replace")

    def find(self, name: str, kind: int | None = None):
        """Return the type id of the first type with this name."""
        for i, type_name in enumerate(self.names):
            if type_name != name:
                continue
            if kind is not None and self.kind[i] != kind:
                continue
            return i
        return None

    def struct_member_offset(self, struct_name: str, member: str):
        """Byte offset of a member inside a struct, requiring byte alignment."""
        tid = self.find(struct_name, BTF_KIND_STRUCT)
        if tid is None:
            raise KeyError(f"struct {struct_name} not found in BTF")
        for m_name, _m_type, bit_off in self.members[tid]:
            if m_name != member:
                continue
            if bit_off % 8:
                raise ValueError(
                    f"{struct_name}.{member} is not byte aligned (bit offset {bit_off})"
                )
            return bit_off // 8
        raise KeyError(f"{struct_name}.{member} not found in BTF")

    def struct_size(self, struct_name: str) -> int:
        tid = self.find(struct_name, BTF_KIND_STRUCT)
        if tid is None:
            raise KeyError(f"struct {struct_name} not found in BTF")
        return self.size[tid]

    def type_name(self, tid: int) -> str:
        if tid is None or tid <= 0 or tid >= len(self.types):
            return "?"
        return self.names[tid] or "void"


# --------------------------------------------------------------- ELF symbols


def read_elf_symbols(path: Path):
    """Return {name: address} from an ELF64 little-endian symtab."""
    data = path.read_bytes()
    if data[:4] != b"\x7fELF":
        raise ValueError("not an ELF file")
    if data[4] != 2:
        raise ValueError("not ELF64")
    if data[5] != 1:
        raise ValueError("not little endian")

    e_shoff = struct.unpack_from("<Q", data, 0x28)[0]
    e_shentsize = struct.unpack_from("<H", data, 0x3A)[0]
    e_shnum = struct.unpack_from("<H", data, 0x3C)[0]
    e_shstrndx = struct.unpack_from("<H", data, 0x3E)[0]

    sections = []
    for i in range(e_shnum):
        base = e_shoff + i * e_shentsize
        name_off, sh_type, _flags, _addr, sh_offset, sh_size, sh_link, _info, _align, sh_entsize = struct.unpack_from(
            "<IIQQQQIIQQ", data, base
        )
        sections.append(
            {
                "name_off": name_off,
                "type": sh_type,
                "offset": sh_offset,
                "size": sh_size,
                "link": sh_link,
                "entsize": sh_entsize,
            }
        )

    strtab = sections[e_shstrndx]
    shstr = data[strtab["offset"] : strtab["offset"] + strtab["size"]]

    def sec_name(sec):
        end = shstr.find(b"\0", sec["name_off"])
        return shstr[sec["name_off"] : end].decode("ascii", "replace")

    symbols = {}
    for sec in sections:
        if sec["type"] != 2:  # SHT_SYMTAB
            continue
        str_sec = sections[sec["link"]]
        strdata = data[str_sec["offset"] : str_sec["offset"] + str_sec["size"]]
        count = sec["size"] // sec["entsize"]
        for i in range(count):
            base = sec["offset"] + i * sec["entsize"]
            st_name, st_info, _st_other, st_shndx, st_value, _st_size = struct.unpack_from(
                "<IBBHQQ", data, base
            )
            if st_name == 0:
                continue
            end = strdata.find(b"\0", st_name)
            name = strdata[st_name:end].decode("utf-8", "replace")
            if not name:
                continue
            if name in symbols and symbols[name] != st_value:
                # Prefer the definition over an alias with a different value.
                continue
            symbols[name] = st_value
    return symbols


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("vmlinux", type=Path)
    parser.add_argument("btf", type=Path)
    parser.add_argument("--json", type=Path)
    parser.add_argument(
        "--trace-last-type",
        type=int,
        default=20,
        help=(
            "__TRACE_LAST_TYPE of the target kernel: the first dynamically "
            "assigned trace event id. This is a compile-time constant of "
            "kernel/trace/trace.h, not a symbol."
        ),
    )
    args = parser.parse_args()

    symbols = read_elf_symbols(args.vmlinux)
    btf = Btf(args.btf.read_bytes())

    # The recovered ELF base: the lowest text symbol is page aligned, and the
    # ELF base for these 6.1 images is 0xffffffc008000000.
    base = min(
        (addr for name, addr in symbols.items() if addr >= 0xFFFFFFC000000000),
        default=0,
    )
    base &= ~0xFFF
    print(f"ELF base: 0x{base:x}")

    def off(name):
        if name not in symbols:
            raise KeyError(f"symbol {name} not found in target vmlinux")
        return symbols[name] - base

    report = {}

    # Simple symbol offsets.
    symbol_map = {
        "CALL_USERMODEHELPER_EXEC_WORK_OFF": "call_usermodehelper_exec_work",
        "NOOP_LLSEEK_OFF": "noop_llseek",
        "COPY_SPLICE_READ_OFF": "generic_file_splice_read",
        "CONFIGFS_READ_ITER_OFF": "configfs_read_iter",
        "CONFIGFS_BIN_WRITE_ITER_OFF": "configfs_bin_write_iter",
        "ASHMEM_IOCTL_OFF": "ashmem_ioctl",
        "ASHMEM_COMPAT_IOCTL_OFF": "compat_ashmem_ioctl",
        "ASHMEM_MMAP_OFF": "ashmem_mmap",
        "ASHMEM_OPEN_OFF": "ashmem_open",
        "ASHMEM_RELEASE_OFF": "ashmem_release",
        "ASHMEM_SHOW_FDINFO_OFF": "ashmem_show_fdinfo",
        "ANON_PIPE_BUF_OPS_OFF": "anon_pipe_buf_ops",
        "ASHMEM_FOPS_OFF": "ashmem_fops",
        "KMALLOC_CACHES_OFF": "kmalloc_caches",
        "SYSTEM_UNBOUND_WQ_OFF": "system_unbound_wq",
        "INIT_TASK_OFF": "init_task",
        "ROOT_TASK_GROUP_OFF": "root_task_group",
        "SYSCTL_BOOTID_OFF": "sysctl_bootid",
        # nfulnl_logger is the complete struct nf_logger object; its first
        # qword is the "name" pointer.
        "SLIDE_NFULNL_LOGGER_OBJECT_OFF": "nfulnl_logger",
        # random_table[] holds the per-entry sysctl table of random.c.
        "RANDOM_TABLE_OFF": "random_table",
        # Trace event registration bookkeeping.
        "START_FTRACE_EVENTS_OFF": "__start_ftrace_events",
        "EVENT_SCHED_BLOCKED_REASON_OFF": "__event_sched_blocked_reason",
        "WORKER_THREAD_OFF": "worker_thread",
    }
    print("\n== symbol offsets ==")
    for macro, symbol in symbol_map.items():
        try:
            value = off(symbol)
        except KeyError as exc:
            print(f"{macro}: MISSING ({exc})")
            continue
        report[macro] = value
        print(f"{macro}: 0x{value:08x}  ({symbol})")

    # ASHMEM_MISC_FOPS_OFF is the miscdevice fops pointer slot of the ashmem
    # misc device. The device is a static object and may be absent from
    # kallsyms, so it is resolved from the target's own data when possible.
    misc_fops = btf.struct_member_offset("miscdevice", "fops")
    report["MISCDEVICE_FOPS_OFF"] = misc_fops
    print(f"\noffsetof(struct miscdevice, fops) = 0x{misc_fops:x}")

    # selinux_state.enforcing
    enforcing = btf.struct_member_offset("selinux_state", "enforcing")
    selinux_state = off("selinux_state")
    report["SELINUX_ENFORCING_OFF"] = selinux_state + enforcing
    print(
        f"SELINUX_ENFORCING_OFF: 0x{report['SELINUX_ENFORCING_OFF']:08x}  "
        f"(selinux_state + 0x{enforcing:x})"
    )

    # struct file_operations layout
    print("\n== struct file_operations ==")
    report["SIZEOF_FILE_OPERATIONS"] = btf.struct_size("file_operations")
    print(f"SIZEOF_FILE_OPERATIONS: 0x{report['SIZEOF_FILE_OPERATIONS']:x}")
    fops_members = {
        "FOPS_OWNER_OFF": "owner",
        "FOPS_LLSEEK_OFF": "llseek",
        "FOPS_READ_OFF": "read",
        "FOPS_WRITE_OFF": "write",
        "FOPS_READ_ITER_OFF": "read_iter",
        "FOPS_WRITE_ITER_OFF": "write_iter",
        "FOPS_UNLOCKED_IOCTL_OFF": "unlocked_ioctl",
        "FOPS_COMPAT_IOCTL_OFF": "compat_ioctl",
        "FOPS_MMAP_OFF": "mmap",
        "FOPS_OPEN_OFF": "open",
        "FOPS_RELEASE_OFF": "release",
        "FOPS_SPLICE_READ_OFF": "splice_read",
        "FOPS_SHOW_FDINFO_OFF": "show_fdinfo",
    }
    for macro, member in fops_members.items():
        try:
            value = btf.struct_member_offset("file_operations", member)
        except (KeyError, ValueError) as exc:
            print(f"{macro}: MISSING ({exc})")
            continue
        report[macro] = value
        print(f"{macro}: 0x{value:x}  (file_operations.{member})")

    # struct task_struct layout
    print("\n== struct task_struct ==")
    task_members = {
        "TASK_USAGE_OFF": "usage",
        "TASK_PRIO_OFF": "prio",
        "TASK_NORMAL_PRIO_OFF": "normal_prio",
        "TASK_SCHED_TASK_GROUP_OFF": "sched_task_group",
        "TASK_PI_LOCK_OFF": "pi_lock",
        "TASK_PI_WAITERS_OFF": "pi_waiters",
        "TASK_PI_TOP_TASK_OFF": "pi_top_task",
        "TASK_PI_BLOCKED_ON_OFF": "pi_blocked_on",
    }
    for macro, member in task_members.items():
        try:
            value = btf.struct_member_offset("task_struct", member)
        except (KeyError, ValueError) as exc:
            print(f"{macro}: MISSING ({exc})")
            continue
        report[macro] = value
        print(f"{macro}: 0x{value:x}  (task_struct.{member})")

    # struct page layout
    print("\n== struct page ==")
    report["SIZEOF_PAGE"] = btf.struct_size("page")
    print(f"SIZEOF_PAGE: 0x{report['SIZEOF_PAGE']:x}")
    page_members = {
        "PAGE_COMPOUND_HEAD_OFF": "compound_head",
        "PAGE_SLAB_CACHE_OFF": "slab_cache",
        "PAGE_PAGE_TYPE_OFF": "page_type",
    }
    for macro, member in page_members.items():
        try:
            value = btf.struct_member_offset("page", member)
        except (KeyError, ValueError) as exc:
            print(f"{macro}: MISSING ({exc})")
            continue
        report[macro] = value
        print(f"{macro}: 0x{value:x}  (page.{member})")

    # struct rt_mutex_waiter layout
    print("\n== struct rt_mutex_waiter ==")
    waiter_members = [
        "tree_entry",
        "tree_priority",
        "tree_deadline",
        "pi_tree_entry",
        "pi_tree_priority",
        "pi_tree_deadline",
        "task",
        "lock",
        "wake_state",
        "ww_ctx",
    ]
    waiter = {}
    for member in waiter_members:
        try:
            value = btf.struct_member_offset("rt_mutex_waiter", member)
        except (KeyError, ValueError):
            continue
        waiter[member] = value
        print(f"rt_mutex_waiter.{member}: 0x{value:x}")
    report["RT_MUTEX_WAITER"] = waiter
    if waiter:
        report["FAKE_WAITER_LAYOUT_SIZE"] = btf.struct_size("rt_mutex_waiter")
        print(f"sizeof(rt_mutex_waiter): 0x{report['FAKE_WAITER_LAYOUT_SIZE']:x}")

    # Trace event id: __TRACE_LAST_TYPE + (__event_sched_blocked_reason -
    # __start_ftrace_events) / 8
    print("\n== tracefs event id ==")
    # __TRACE_LAST_TYPE is the first dynamically assigned event id. It is a
    # compile-time constant of the target's kernel/trace/trace.h, not a
    # symbol, so it is supplied on the command line.
    trace_last_type = args.trace_last_type
    try:
        start = symbols["__start_ftrace_events"]
        event = symbols["__event_sched_blocked_reason"]
        event_index = (event - start) // 8
        print(f"__start_ftrace_events: 0x{start - base:x}")
        print(f"__event_sched_blocked_reason: 0x{event - base:x}")
        print(f"event_index (zero based): {event_index}")
        print(f"event id = {trace_last_type} + {event_index} = {trace_last_type + event_index}")
        report["TRACE_EVENT_INDEX"] = event_index
        report["SLIDE_TRACEFS_EVENT_ID"] = trace_last_type + event_index
    except KeyError as exc:
        print(f"tracefs symbols missing: {exc}")

    # nfulnl_logger object and its name string
    print("\n== nfnetlink_log ==")
    for symbol in ("nfulnl_logger", "loggers"):
        if symbol in symbols:
            value = symbols[symbol] - base
            report[symbol.upper() + "_OFF"] = value
            print(f"{symbol}: 0x{value:08x}")

    # random_table boot_id entry and sysctl_bootid
    print("\n== random / boot_id ==")
    for symbol in ("random_table", "sysctl_bootid", "boot_id"):
        if symbol in symbols:
            value = symbols[symbol] - base
            report[symbol.upper() + "_OFF"] = value
            print(f"{symbol}: 0x{value:08x}")

    if args.json:
        args.json.write_text(json.dumps(report, indent=2, sort_keys=True))
        print(f"\nwrote {args.json}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
